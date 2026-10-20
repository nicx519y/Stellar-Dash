export type LogSource = 'stm32' | 'tx';
export type LogStatus = 'ok' | 'unsupported' | 'unavailable' | 'storage_error';
export interface FaultRecord {
  sequence: number;
  boot: number;
  uptimeMs: number;
  firmware: number;
  event: number;
  severity: number;
  category: number;
  repeat: number;
  pending: boolean;
  data: number[];
}
export interface FaultLogPage {
  version: 1;
  source: LogSource;
  status: LogStatus;
  upper: number;
  oldest: number;
  nextBefore: number;
  more: boolean;
  overwritten: boolean;
  pending: number;
  dropped: number;
  firmware: number;
  restricted: boolean;
  items: FaultRecord[];
  errorMessage?: string;
}
export interface FaultLogQuery {
  source: LogSource;
  category?: number;
  severity?: number;
  upper?: number;
  before?: number;
  oldest?: number;
  limit?: number;
}
export type FaultLogFetcher = (query: FaultLogQuery) => Promise<FaultLogPage>;
const uint = (value: unknown): value is number =>
  typeof value === 'number' && Number.isInteger(value) && value >= 0 && value <= 0xffffffff;

export function parseFaultLogPage(value: unknown, source: LogSource): FaultLogPage {
  if (!value || typeof value !== 'object') throw new Error('Invalid device log response');
  const p = value as FaultLogPage;
  if (p.version !== 1 || p.source !== source ||
      !['ok', 'unsupported', 'unavailable', 'storage_error'].includes(p.status) ||
      !['upper', 'oldest', 'nextBefore', 'pending', 'dropped', 'firmware'].every(key => uint(p[key as keyof FaultLogPage])) ||
      !['more', 'overwritten', 'restricted'].every(key => typeof p[key as keyof FaultLogPage] === 'boolean') ||
      !Array.isArray(p.items) || p.items.length > 11 ||
      (p.more && (!p.nextBefore || p.nextBefore > p.upper))) throw new Error('Invalid device log page');
  for (const r of p.items) {
    if (!r || !['sequence', 'boot', 'uptimeMs', 'firmware', 'event', 'severity', 'category', 'repeat']
      .every(key => uint(r[key as keyof FaultRecord])) ||
      typeof r.pending !== 'boolean' || !Array.isArray(r.data) || r.data.length > 14 ||
      !r.data.every(uint) || (!r.pending && (!r.sequence || r.sequence > p.upper)))
      throw new Error('Invalid device log record');
  }
  return p;
}

export function failedLogPage(source: LogSource, error: unknown): FaultLogPage {
  const message = error instanceof Error ? error.message : String(error);
  return {
    version: 1, source, status: /unknown|unsupported|not supported|不支持/i.test(message) ? 'unsupported' : 'unavailable',
    upper: 0, oldest: 0, nextBefore: 0, more: false, overwritten: false,
    pending: 0, dropped: 0, firmware: 0, restricted: false, items: [], errorMessage: message,
  };
}

export function mergeLogPages(first: FaultLogPage, next: FaultLogPage): FaultLogPage {
  if (first.source !== next.source || first.upper !== next.upper ||
      (next.more && next.nextBefore >= first.nextBefore)) throw new Error('Device log cursor did not advance');
  const existing = new Set(first.items.filter(r => !r.pending).map(r => r.sequence));
  if (next.items.some(r => r.pending || existing.has(r.sequence) ||
      (first.nextBefore && r.sequence >= first.nextBefore))) throw new Error('Device log snapshot changed');
  return { ...next, oldest: first.oldest, pending: Math.max(first.pending, next.pending),
    dropped: Math.max(first.dropped, next.dropped), overwritten: first.overwritten || next.overwritten,
    items: [...first.items, ...next.items] };
}

export async function collectFaultLogs(fetchPage: FaultLogFetcher, query: FaultLogQuery,
  cancelled: () => boolean = () => false): Promise<FaultLogPage> {
  let result: FaultLogPage;
  try { result = await fetchPage(query); } catch (error) { return failedLogPage(query.source, error); }
  for (let page = 0; result.more && page < 256; page++) {
    if (cancelled()) return { ...result, status: 'unavailable' };
    if (result.status !== 'ok' && result.status !== 'storage_error') break;
    try {
      const next = await fetchPage({ ...query, upper: result.upper, oldest: result.oldest, before: result.nextBefore });
      result = mergeLogPages(result, next);
    } catch {
      return { ...result, status: 'unavailable' };
    }
  }
  if (result.more) result = { ...result, status: 'unavailable' };
  return result;
}

export function logExport(pages: FaultLogPage[], filters: { source: string; category: number; severity: number }) {
  return {
    product: 'XORA', schemaVersion: 1, exportedAt: new Date().toISOString(), filters,
    complete: pages.every(p => p.status === 'ok' && !p.more && !p.overwritten && !p.dropped),
    timeBasis: 'Per-device boot number and uptime; export time is not event time.',
    volatileWarning: 'Pending records may be lost on power removal.',
    sources: pages,
  };
}
export function logExportText(value: ReturnType<typeof logExport>): string {
  const lines = ['XORA device logs', `Export: ${value.exportedAt}`, `Complete: ${value.complete}`,
    `Filters: ${JSON.stringify(value.filters)}`, value.timeBasis, value.volatileWarning];
  for (const p of value.sources) {
    lines.push(`\n${p.source.toUpperCase()} status=${p.status} firmware=${p.firmware} pending=${p.pending} dropped=${p.dropped} overwritten=${p.overwritten} restricted=${p.restricted}`);
    for (const r of p.items) lines.push(`boot=${r.boot} +${r.uptimeMs}ms seq=${r.sequence} event=${r.event} severity=${r.severity} category=${r.category} repeat=${r.repeat} firmware=${r.firmware} pending=${r.pending} data=${r.data.map(v => `0x${v.toString(16).padStart(8, '0')}`).join(',')}`);
  }
  return lines.join('\n');
}
