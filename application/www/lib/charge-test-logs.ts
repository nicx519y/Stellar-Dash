import type { FaultRecord } from './device-fault-logs';

export const chargeEvents = [
  ['CHG_TEST_START', '充电测试开始', 'Charge test started'],
  ['CHG_SEGMENT_START', '充电段开始', 'Charge segment started'],
  ['CHG_SAMPLE_SUMMARY', '充电趋势摘要', 'Charge trend'],
  ['CHG_STATE_CHANGED', '充电状态或供电变化', 'Charging state or supply changed'],
  ['CHG_MILESTONE', '到达电量节点', 'SOC milestone reached'],
  ['CHG_SEGMENT_END', '充电段结束', 'Charge segment ended'],
  ['CHG_TEST_END', '充电测试结束', 'Charge test ended'],
  ['CHG_QUALITY_CHANGED', '充电采样质量变化', 'Charge sample quality changed'],
] as const;
const lo = (v: number) => v & 65535;
const hi = (v: number) => v >>> 16;
const delta = (a: FaultRecord, b: FaultRecord) => a.boot === b.boot ? (b.uptimeMs - a.uptimeMs) >>> 0 : null;
export function chargeSummary(r: FaultRecord) {
  const d = r.data;
  return { from: d[2], to: d[3], cellMin: lo(d[4]), cellMax: hi(d[4]), inputMin: lo(d[5]), inputMax: hi(d[5]),
    currentMin: lo(d[6]), currentMax: hi(d[6]), firstSoc: lo(d[7]) / 10, lastSoc: hi(d[7]) / 10,
    cellMean: lo(d[8]), inputMean: hi(d[8]), currentMean: lo(d[9]), validSamples: d[10], missingSamples: d[11],
    inputMs: d[12], chargingMs: d[13] };
}
export function decodeChargeRecord(r: FaultRecord): Record<string, number | string | boolean | null> {
  const d = r.data;
  if (d[0] !== 2 || r.event < 120 || r.event > 127 || d.length !== (r.event === 126 ? 9 : 14)) return { unsupported: true };
  const base = { type: chargeEvents[r.event - 120][0], schemaVersion: 2, session: d[1], testType: 'charge' };
  if (r.event === 122) return { ...base, ...chargeSummary(r) };
  if (r.event === 126) return { ...base, reason: d[2], elapsedMs: d[2] === 4 ? null : d[3],
    inputMs: d[2] === 4 ? null : d[4], chargingMs: d[2] === 4 ? null : d[5], storagePauseMs: d[6], dropped: d[7], incomplete: !!d[8] };
  return { ...base, segment: d[2], detail: d[3], cellMv: d[4], socPercent: d[5] / 10,
    inputMv: d[6], chargeMa: d[7], chargeState: d[8] & 255, inputPresent: !!(d[8] & 256), valid: !!(d[8] & 512),
    thermalRegulation: !!(d[8] & 1024), inputCurrentRegulation: !!(d[8] & 2048), inputVoltageRegulation: !!(d[8] & 4096),
    chargeBlock: d[9], faults: d[10], sampledAtMs: d[11], inputMs: d[12], chargingMs: d[13] };
}
export type ChargeIssue = 'partial_log' | 'missing_records' | 'pending_records' | 'configuration_changed' |
  'sample_gap' | 'invalid_sample' | 'reboot' | 'unknown_start' | 'supply_changed' | 'unsupported';
export interface ChargeCycle {
  id: number; start: FaultRecord; end?: FaultRecord; last: FaultRecord; reason: number;
  midCharge: boolean; issues: ChargeIssue[]; milestones: Partial<Record<number, FaultRecord>>;
  elapsedMs: number | null; chargingMs: number | null; startSoc: number; endSoc: number | null;
  startCellMv: number; endCellMv: number | null; inputMv: number;
  status: 'complete' | 'interrupted' | 'recording' | 'unverified';
  ratePerHour: number | null;
}
type Coverage = { more?: boolean; status?: string; overwritten?: boolean; dropped?: number };
export const chargeMilestone = (cycle: ChargeCycle, level: number) => {
  if (cycle.startSoc > level / 10) return { status: 'already' as const, ms: null };
  const record = cycle.milestones[level];
  if (!record) return { status: 'unreached' as const, ms: null };
  if (cycle.issues.length) return { status: 'unverified' as const, ms: null };
  return { status: 'observed' as const, ms: delta(cycle.start, record) };
};
export function chargeInterval(cycle: ChargeCycle, from: number, to: number | 'full') {
  if (cycle.issues.length) return null;
  const a = cycle.milestones[from];
  const b = to === 'full' ? cycle.reason === 1 ? cycle.end : undefined : cycle.milestones[to];
  return a && b ? delta(a, b) : null;
}
export function chargeSessions(records: FaultRecord[], coverage: Coverage = {}) {
  const chargeIds = [...new Set(records.filter(r => r.data[0] === 2 && r.data.length >= 2).map(r => r.data[1]))];
  return chargeIds.map(id => {
    const items = records.filter(r => r.data[1] === id).sort((a, b) => Number(a.pending) - Number(b.pending) || (a.pending ? 0 : a.sequence - b.sequence));
    const start = items.find(r => r.event === 120 && r.data.length === 14);
    const end = items.find(r => r.event === 126 && r.data.length === 9);
    const persisted = items.filter(r => !r.pending);
    const commonIssues: ChargeIssue[] = [];
    if (!start) commonIssues.push('unknown_start');
    if (coverage.more || (coverage.status && coverage.status !== 'ok')) commonIssues.push('partial_log');
    if (coverage.overwritten || coverage.dropped || end?.data[7] || end?.data[8] ||
      persisted.some((r, i) => i > 0 && r.sequence !== persisted[i - 1].sequence + 1)) commonIssues.push('missing_records');
    if (items.some(r => r.pending)) commonIssues.push('pending_records');
    if (items.some(r => r.data[0] !== 2 || (r.event >= 120 ? decodeChargeRecord(r).unsupported : r.event < 100 || r.event > 114))) commonIssues.push('unsupported');
    const cycles: ChargeCycle[] = [];
    let active: ChargeCycle | undefined;
    let qualityGood = true;
    const issue = (c: ChargeCycle, value: ChargeIssue) => { if (!c.issues.includes(value)) c.issues.push(value); };
    for (const r of items) {
      if (active && r.boot !== active.start.boot) { issue(active, 'reboot'); active.reason = 4; active = undefined; }
      if (r.event === 127 && r.data.length === 14) {
        qualityGood = r.data[3] === 1;
        if (active && !qualityGood) issue(active, r.data[3] & 2 ? 'sample_gap' : 'invalid_sample');
      }
      if (r.event === 121 && r.data.length === 14) {
        if (active) { issue(active, 'missing_records'); active.reason = 0; }
        active = { id: r.data[2], start: r, last: r, reason: -1, midCharge: !!r.data[3], issues: [...commonIssues],
          milestones: {}, elapsedMs: 0, chargingMs: 0, startSoc: r.data[5] / 10, endSoc: r.data[5] / 10,
          startCellMv: r.data[4], endCellMv: r.data[4], inputMv: r.data[6], status: 'recording', ratePerHour: null };
        if (!qualityGood || !(r.data[8] & 512)) issue(active, 'invalid_sample');
        cycles.push(active);
      }
      if (active) {
        if (r.event === 103) issue(active, 'configuration_changed');
        if ([121, 123, 124, 125, 127].includes(r.event) && r.data.length === 14 && r.data[2] === active.id) {
          active.last = r;
          if (!(r.data[8] & 512)) issue(active, 'invalid_sample');
          if (r.data[8] & 256 && ((r.data[6] >= 7000) !== (active.inputMv >= 7000))) issue(active, 'supply_changed');
          if (r.event === 124 && !active.milestones[r.data[3]]) active.milestones[r.data[3]] = r;
          if (r.event === 125) { active.end = r; active.reason = r.data[3]; active = undefined; }
        }
        if (active && r.event === 122 && r.data.length === 14) {
          const s = chargeSummary(r);
          active.endSoc = s.validSamples ? s.lastSoc : null;
          active.endCellMv = s.validSamples ? s.cellMean : null;
          active.elapsedMs = delta(active.start, r);
          active.chargingMs = (s.chargingMs - active.start.data[13]) >>> 0;
        }
        if (active && r.event === 126) { active.reason = r.data[2]; issue(active, r.data[2] === 4 ? 'reboot' : 'missing_records'); active = undefined; }
      }
    }
    const trend = items.filter(r => r.event === 122 && r.data[0] === 2 && r.data.length === 14);
    for (const c of cycles) {
      const endTime = c.end ? (c.end.uptimeMs - c.start.uptimeMs) >>> 0 : Infinity;
      // A bucket can overlap an unplug boundary; a gap contaminates every overlapping segment.
      for (const r of trend) {
        const s = chargeSummary(r);
        const from = (s.from - c.start.uptimeMs) | 0, to = (s.to - c.start.uptimeMs) | 0;
        if (r.boot === c.start.boot && to >= 0 && from <= endTime && (!s.validSamples || s.missingSamples)) issue(c, 'sample_gap');
      }
      if (c.end) {
        c.elapsedMs = delta(c.start, c.end); c.endSoc = c.end.data[8] & 512 ? c.end.data[5] / 10 : null;
        c.endCellMv = c.end.data[8] & 512 ? c.end.data[4] : null;
        c.chargingMs = (c.end.data[13] - c.start.data[13]) >>> 0;
      } else if (c.reason === 4) { c.elapsedMs = null; c.chargingMs = null; c.endSoc = null; c.endCellMv = null; }
      else if (!c.elapsedMs) c.elapsedMs = delta(c.start, c.last);
      c.status = c.issues.length ? 'unverified' : c.reason === 1 ? 'complete' : c.reason === -1 ? 'recording' : 'interrupted';
      c.ratePerHour = !c.issues.length && c.elapsedMs && c.endSoc !== null ? (c.endSoc - c.startSoc) * 3600000 / c.elapsedMs : null;
    }
    const latestSummary = trend.at(-1);
    const totals = latestSummary ? chargeSummary(latestSummary) : undefined;
    const knownEnd = end && end.data[2] !== 4;
    return { id, testType: 'charge' as const, start, end, trend, cycles, items,
      incomplete: !end || !!commonIssues.length,
      inputMs: knownEnd ? end.data[4] : end ? null : totals?.inputMs ?? null,
      chargingMs: knownEnd ? end.data[5] : end ? null : totals?.chargingMs ?? null,
      records: items.map(r => ({ sequence: r.sequence, uptimeMs: r.uptimeMs, pending: r.pending,
        ...(r.event >= 120 ? decodeChargeRecord(r) : { type: 'BATTERY_CONTEXT', schemaVersion: r.data[0], event: r.event, data: r.data }) })) };
  });
}
export type ChargeSession = ReturnType<typeof chargeSessions>[number];

export function chargeTrend(session: ChargeSession) {
  const origin = session.start?.uptimeMs ?? session.trend[0]?.data[2] ?? 0;
  return session.trend.map((r, index) => {
    const s = chargeSummary(r);
    const previous = session.trend[index - 1];
    return { ...s, x: ((s.to - origin) >>> 0) / 3600000,
      valid: s.validSamples > 0 && s.missingSamples === 0 && (!previous || previous.boot === r.boot && previous.data[3] === s.from) };
  });
}
