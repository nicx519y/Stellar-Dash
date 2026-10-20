import { FaultLogPage, FaultLogQuery, FaultRecord } from './device-fault-logs';

export type MockLogScenario = 'normal' | 'empty' | 'unsupported' | 'tx-unavailable' | 'corrupt' | 'overflow';
export function mockFaultLogs(query: FaultLogQuery, scenario: MockLogScenario = 'normal'): FaultLogPage {
  const source = query.source;
  const records: FaultRecord[] = Array.from({ length: scenario === 'empty' ? 0 : 19 }, (_, i) => ({
    sequence: 19 - i, boot: 3, uptimeMs: 2000 + (19 - i) * 1000, firmware: 0,
    event: i % 2 ? 4 : 7, severity: i % 2 ? 3 : 2, category: i % 2 ? 2 : 3,
    repeat: 1, pending: false, data: [i % 2 ? 9 : 4],
  }));
  const upper = query.upper ?? records[0]?.sequence ?? 0;
  const matches = records.filter(r => r.sequence <= upper && (!query.before || r.sequence < query.before) &&
    (!query.category || r.category === query.category) && (!query.severity || r.severity === query.severity));
  const limit = query.limit ?? 8;
  const items = matches.slice(0, limit);
  const status = scenario === 'unsupported' ? 'unsupported' :
    scenario === 'tx-unavailable' && source === 'tx' ? 'unavailable' :
    scenario === 'corrupt' ? 'storage_error' : 'ok';
  const readable = status === 'ok' || status === 'storage_error';
  const more = readable && matches.length > limit;
  if (readable && scenario === 'overflow' && query.upper === undefined &&
      (!query.category || query.category === 2) && (!query.severity || query.severity === 3)) {
    items.unshift({ sequence: 0, boot: 4, uptimeMs: 100, firmware: 0, event: 4,
      category: 2, severity: 3, repeat: 5, pending: true, data: [9] });
  }
  return { version: 1, source, status, upper, oldest: records.length ? 1 : 0,
    nextBefore: more ? items[items.length - 1].sequence : 0, more, overwritten: false,
    pending: scenario === 'overflow' ? 1 : 0, dropped: scenario === 'overflow' ? 12 : 0,
    firmware: 0, restricted: false, items: readable ? items : [] };
}
