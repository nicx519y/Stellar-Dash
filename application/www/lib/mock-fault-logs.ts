import { FaultLogPage, FaultLogQuery, FaultRecord } from './device-fault-logs';

export type MockLogScenario = 'normal' | 'empty' | 'unsupported' | 'tx-unavailable' | 'corrupt' | 'overflow' | 'battery-interrupted' | 'full' | 'charge-complete' | 'charge-interrupted' | 'charge-gap' | 'charge-reboot';
export function mockFaultLogs(query: FaultLogQuery, scenario: MockLogScenario = 'normal'): FaultLogPage {
  const source = query.source;
  const count = scenario === 'empty' ? 0 : scenario === 'full' && source !== 'battery' ? source === 'stm32' ? 512 : 160 : 19;
  let records: FaultRecord[] = Array.from({ length: count }, (_, i) => ({
    sequence: count - i, boot: 3, uptimeMs: 2000 + (count - i) * 1000, firmware: 0,
    event: i % 2 ? 4 : 7, severity: i % 2 ? 3 : 2, category: i % 2 ? 2 : 3,
    repeat: 1, pending: false, data: [i % 2 ? 9 : 4],
  }));
  if (source === 'battery') records = records.map((r, i) => ({
    ...r, category: 4, severity: 1, event: i === 0 ? 114 : i === 18 ? 100 : 102,
    uptimeMs: (19 - i) * 300000, data: i === 0 ? [1, 7, 1, 5400000, 5200000, 0, 190, 0, 0] :
      i === 18 ? [1, 7, 1, 0, 8000, 50, 30, 1, 0, 300000, 0, 0, 0, 7] :
      [1, 7, (18 - i) * 300000, (19 - i) * 300000, 3200 + i * 50, 3210 + i * 50, i * 50, i * 50 - 5, 300, 0, (18 - i) * 300000, 0, 190, 0],
  }));
  if (source === 'battery' && scenario === 'battery-interrupted') {
    records = [];
    const add = (event: number, uptimeMs: number, detail = 0, soc = 1000, level = 0) => records.push({
      sequence: records.length + 1, boot: 3, uptimeMs, firmware: 0, event, severity: 1, category: 4,
      repeat: 1, pending: false, data: [1, 7, detail, 0, 3300 + soc, soc, 1, level, 0, uptimeMs],
    });
    add(100, 0); records[0].data = [1, 7, 1, 0, 8000, 50, 30, 1, 0, 300000, 0, 0, 0, 7];
    add(110, 0, 257); add(104, 1000, 257); add(110, 1000, 0); add(101, 1000);
    const summary = (from: number, to: number, first: number, last: number, batteryMs: number) => {
      add(102, to);
      records[records.length - 1].data = [1, 7, from, to, 3300 + last, 3300 + first, first, last, 300, 0, batteryMs, 0, 0, 0];
    };
    summary(1000, 7201000, 1000, 500, 7200000);
    add(110, 7201000, 257, 500); add(111, 7201000, 1, 500);
    add(110, 9001000, 0, 750); add(111, 9001000, 256, 750);
    summary(9001000, 18001000, 750, 100, 16200000);
    add(106, 18001000, 1, 100);
    summary(18001000, 19801000, 100, 40, 18000000);
    add(108, 19801000, 1, 40, 2);
    add(110, 19901000, 257, 40, 2); add(111, 19901000, 1, 40, 2);
    add(106, 20001000, 3, 80, 3); add(106, 20101000, 0, 80);
    add(114, 20101000);
    records[records.length - 1].data = [1, 7, 1, 20101000, 18000000, 0, 0, 0, 0];
    records.reverse();
  }
  if (source === 'battery' && scenario.startsWith('charge-')) records = mockChargeRecords(scenario);
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

export function mockChargeRecords(scenario: string): FaultRecord[] {
  const records: FaultRecord[] = [];
  let segment = 0, inputMs = 0, chargingMs = 0, state = 1;
  const add = (event: number, time: number, soc: number, detail = 0) => {
    const record: FaultRecord = { sequence: records.length + 1, boot: 3, uptimeMs: time, firmware: 0x010203,
      event, category: 4, severity: 1, repeat: 1, pending: false,
      data: [2, 8, segment, detail, 3300 + Math.round(soc * .9), soc, 9000, Math.max(0, 2400 - Math.max(0, soc - 800) * 12), state | 768, 0, 0, time, inputMs, chargingMs] };
    records.push(record); return record;
  };
  const pair = (a: number, b: number) => ((b << 16) | a) >>> 0;
  add(120, 0, 100); add(100, 0, 100).data = [2, 8, 1, 0, 8000, 50, 30, 1, 0, 300000, 0, 0, 0, 7];
  add(127, 0, 100, 1); segment = 1; add(121, 0, 100); add(123, 0, 100);
  let priorSoc = 100;
  const levels = [200, 500, 800, 900, 1000];
  const marked = new Set<number>();
  for (let minute = 5; minute <= 240; minute += 5) {
    const time = minute * 60000;
    if (scenario === 'charge-interrupted' && minute === 65) { state = 0; add(125, time - 300000, priorSoc, 6).data[8] = 512; continue; }
    if (scenario === 'charge-interrupted' && minute === 70) { segment = 2; state = 1; marked.clear(); add(121, time - 300000, priorSoc); add(123, time - 300000, priorSoc); }
    const nextSoc = Math.min(1000, 100 + Math.round(minute * 900 / 240));
    const gap = scenario === 'charge-gap' && minute === 120;
    inputMs += gap ? 0 : 300000; chargingMs += gap ? 0 : 300000;
    if (gap) add(127, time - 120000, priorSoc, 2).data[8] = 256;
    const r = add(122, time, nextSoc);
    const cell = 3300 + Math.round(nextSoc * .9), ma = r.data[7];
    r.data = [2, 8, time - 300000, time, pair(cell - 15, cell + 15), pair(8900, 9100), pair(Math.max(0, ma - 50), ma + 50),
      pair(priorSoc, nextSoc), pair(cell, 9000), ma, gap ? 180 : 300, gap ? 120 : 0, inputMs, chargingMs];
    if (gap) add(127, time, nextSoc, 1);
    for (const level of levels) if (priorSoc <= level && nextSoc >= level && !marked.has(level)) {
      marked.add(level); add(124, time, nextSoc, level);
    }
    priorSoc = nextSoc;
    if (scenario === 'charge-reboot' && minute === 120) {
      const end = add(126, 100, nextSoc); end.boot = 4; end.data = [2, 8, 4, 0, 0, 0, 0, 0, 1]; break;
    }
  }
  if (scenario !== 'charge-reboot') {
    state = 2; add(123, 14400000, 1000);
    inputMs += 30000;
    add(125, 14430000, 1000, 1);
    add(126, 14430000, 1000).data = [2, 8, 1, 14430000, inputMs, chargingMs, 120, 0, 0];
  }
  return records.reverse();
}
