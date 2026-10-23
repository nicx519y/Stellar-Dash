const test = require('node:test');
const assert = require('node:assert/strict');
const { batterySessions } = require('../lib/battery-test-logs.ts');
const { collectFaultLogs, logExport, logExportText } = require('../lib/device-fault-logs.ts');
const { mockFaultLogs } = require('../lib/mock-fault-logs.ts');

function run() {
  const records = [];
  const add = (event, time, detail = 0, options = {}) => {
    const data = event === 100 || event === 103 ? [1, 7, 1, 0, 8000, 50, 30, 1, 0, 300000, 0, 0, 0, 7] :
      event === 114 ? [1, 7, detail, time, time, 0, 0, 0, 0] :
        [1, 7, detail, 0, 4000, options.soc ?? 1000, options.valid === false ? 0 : 1, options.level ?? 0, 0, time];
    const r = { sequence: records.length + 1, boot: 3, uptimeMs: time, firmware: 0, event,
      severity: 1, category: 4, repeat: 1, pending: false, data, ...options.record };
    records.push(r); return r;
  };
  add(100, 0);
  return { records, add, sessions: coverage => batterySessions([...records].reverse(), coverage) };
}

test('unplug, protection and charging recovery form one frozen discharge cycle', () => {
  const r = run();
  r.add(110, 0, 257); // plugged
  r.add(104, 1000, 257); // valid fresh
  r.add(110, 1000, 0);
  r.add(101, 1000);
  r.add(106, 3601000, 1, { soc: 100 });
  r.add(108, 7201000, 1, { soc: 40, level: 2 });
  r.add(110, 7300000, 257, { soc: 40, level: 2 });
  r.add(111, 7300000, 1, { soc: 40, level: 2 });
  r.add(106, 7400000, 3, { soc: 80, level: 3 });
  r.add(103, 7450000); // recovery changes do not invalidate discharge
  r.add(106, 7500000, 0);
  r.add(114, 7500000, 1);
  const s = r.sessions()[0];
  assert.equal(s.cycles.length, 1);
  const c = s.cycles[0];
  assert.equal(c.status, 'valid');
  assert.equal(c.enduranceMs, 7200000);
  assert.equal(c.start.event, 101);
  assert.equal(c.endSocPercent, 4);
  assert.equal(c.milestones.toLowMs, 3600000);
  assert.equal(c.milestones.afterWarningMs, 3600000);
  assert.equal(s.milestones.chargingToReadyMs, 100000);
});

test('legacy single-start sessions split on every plug/unplug, never sum or span charging', () => {
  const r = run();
  r.add(101, 1000);
  r.add(110, 7201000, 257, { soc: 500 });
  r.add(111, 7201000, 1, { soc: 500 });
  r.add(110, 9001000, 0, { soc: 750 }); // legacy firmware has no second event 101
  r.add(111, 9001000, 256);
  r.add(108, 19801000, 1, { soc: 40, level: 2 });
  r.add(114, 20000000, 2);
  const s = r.sessions()[0];
  assert.equal(s.cycles.length, 2);
  assert.equal(s.cycles[0].elapsedMs, 7200000);
  assert.equal(s.cycles[0].endReason, 'external_power');
  assert.equal(s.cycles[0].status, 'interrupted');
  assert.equal(s.cycles[0].enduranceMs, null);
  assert.equal(s.cycles[1].enduranceMs, 10800000);
  assert.equal(s.cycles[1].startSocPercent, 75);
  assert.equal(s.milestones.toProtectionMs, null); // no ambiguous session-wide duration
});

test('configuration changes and bad samples invalidate only the affected discharge', () => {
  for (const [event, detail, issue] of [[103, 0, 'configuration_changed'], [104, 769, 'sample_gap'], [104, 0, 'invalid_sample']]) {
    const r = run(); r.add(101, 1000); r.add(event, 2000, detail); r.add(108, 3000, 1); r.add(114, 4000, 2);
    const c = r.sessions()[0].cycles[0];
    assert.equal(c.status, 'unverified');
    assert.equal(c.enduranceMs, null);
    assert.ok(c.issues.includes(issue));
  }
  const r = run(); r.add(101, 1000); r.add(103, 2000); r.add(110, 3000, 257);
  r.add(110, 4000, 0); r.add(108, 5000, 1); r.add(114, 6000, 2);
  assert.equal(r.sessions()[0].cycles[1].status, 'valid');
});

test('partial pages, dropped/missing records, pending endpoints and reboot cannot establish endurance', () => {
  const r = run(); r.add(101, 1000); r.add(108, 2000, 1); r.add(114, 3000, 2);
  for (const coverage of [{ more: true }, { status: 'unavailable' }, { overwritten: true }, { dropped: 1 }])
    assert.equal(r.sessions(coverage)[0].cycles[0].enduranceMs, null);
  r.records[2].sequence = 8;
  assert.ok(r.sessions()[0].cycles[0].issues.includes('missing_records'));
  const reboot = run(); reboot.add(101, 1000); reboot.add(114, 10, 4, { record: { boot: 4 } });
  assert.equal(reboot.sessions()[0].cycles[0].elapsedMs, null);
  assert.equal(reboot.sessions()[0].cycles[0].endReason, 'reboot');
  const pending = run(); pending.add(101, 1000);
  pending.add(108, 2000, 1, { record: { pending: true, sequence: 0 } });
  pending.add(114, 3000, 2, { record: { pending: true, sequence: 0 } });
  // RPC returns pending entries first in queue order, followed by persisted records.
  const pageOrder = [pending.records[2], pending.records[3], pending.records[1], pending.records[0]];
  const s = batterySessions(pageOrder)[0];
  assert.equal(s.cycles[0].endReason, 'protection');
  assert.equal(s.cycles[0].enduranceMs, null);
  assert.ok(s.cycles[0].issues.includes('pending_records'));
  assert.equal(s.incomplete, true);
});

test('manual, timeout, active and charging-only endpoints never become a completed discharge', () => {
  for (const reason of [1, 2, 3, 5]) {
    const r = run(); r.add(101, 1000); r.add(114, 2000, reason);
    assert.equal(r.sessions()[0].cycles[0].enduranceMs, null);
    assert.equal(r.sessions()[0].cycles[0].status, 'interrupted');
  }
  const active = run(); active.add(101, 1000); active.add(107, 3000);
  assert.equal(active.sessions()[0].cycles[0].status, 'recording');
  assert.equal(active.sessions()[0].cycles[0].elapsedMs, 2000);
  active.add(111, 4000, 1);
  assert.equal(active.sessions()[0].cycles[0].endReason, 'charging');
  const noStart = run(); noStart.add(108, 1000, 1); noStart.add(114, 2000, 1);
  assert.equal(noStart.sessions()[0].cycles.length, 0);
});

test('unsigned clock wrap, repeated power snapshots and protection-before-input are bounded', () => {
  const r = run(); r.add(101, 0xfffffff0); r.add(106, 20, 1); r.add(108, 100, 1); r.add(110, 110, 257); r.add(114, 120, 2);
  assert.equal(r.sessions()[0].cycles[0].enduranceMs, 116);
  assert.equal(r.sessions()[0].cycles[0].milestones.toLowMs, 36);
  const p = run(); p.add(110, 1000, 0); p.add(101, 1000); p.add(110, 2000, 0);
  p.add(106, 3000, 2); p.add(108, 3000, 1); p.add(110, 4000, 257); p.add(114, 5000, 1);
  assert.equal(p.sessions()[0].cycles.length, 1);
  assert.equal(p.sessions()[0].cycles[0].enduranceMs, 2000);
});

test('five-minute summaries with overlapping sample gaps conservatively invalidate a cycle', () => {
  const r = run(); r.add(101, 1000); r.add(108, 3000, 1);
  r.add(102, 5000, 0, { record: { data: [1, 7, 0, 5000, 3300, 4000, 1000, 40, 3, 1, 2000, 0, 0, 0] } });
  r.add(114, 6000, 2);
  assert.ok(r.sessions()[0].cycles[0].issues.includes('sample_gap'));
  assert.equal(r.sessions()[0].cycles[0].enduranceMs, null);
});

test('JSON and TXT exports share discharge segments and page coverage validation', () => {
  const r = run(); r.add(101, 1000); r.add(110, 2000, 257); r.add(110, 3000, 0); r.add(108, 5000, 1); r.add(114, 6000, 2);
  const page = { source: 'battery', status: 'ok', more: false, overwritten: false, dropped: 0, items: r.records };
  const exported = logExport([page], { source: 'battery', category: 0, severity: 0 });
  assert.equal(exported.batteryTests[0].cycles[1].enduranceMs, 2000);
  assert.match(logExportText(exported), /"endReason":"external_power"/);
  assert.match(logExportText(exported), /"enduranceMs":2000/);
  assert.equal(logExport([{ ...page, more: true }], {}).batteryTests[0].cycles[1].enduranceMs, null);
});

test('paginated Mock power interruptions export two segments, with only three hours verified', async () => {
  const first = mockFaultLogs({ source: 'battery' }, 'battery-interrupted');
  assert.equal(batterySessions(first.items, first)[0].cycles.every(c => c.enduranceMs === null), true);
  const page = await collectFaultLogs(q => mockFaultLogs(q, 'battery-interrupted'), { source: 'battery' });
  const s = logExport([page], { source: 'battery', category: 0, severity: 0 }).batteryTests[0];
  assert.equal(s.incomplete, false);
  assert.equal(s.cycles.length, 2);
  assert.deepEqual(s.cycles.map(c => c.status), ['interrupted', 'valid']);
  assert.deepEqual(s.cycles.map(c => c.enduranceMs), [null, 10800000]);
});

test('stale quality at unplug, terminal data loss and missing session start remain unverified', () => {
  const r = run(); r.add(104, 500, 258); r.add(110, 1000, 0); r.add(108, 2000, 1); r.add(114, 3000, 2);
  assert.ok(r.sessions()[0].cycles[0].issues.includes('invalid_sample'));
  const dropped = run(); dropped.add(101, 1000); dropped.add(108, 2000, 1);
  const end = dropped.add(114, 3000, 2); end.data[7] = 1;
  assert.equal(dropped.sessions()[0].incomplete, true);
  assert.equal(dropped.sessions()[0].milestones.toProtectionMs, null);
  dropped.records.shift();
  assert.ok(dropped.sessions()[0].cycles[0].issues.includes('unknown_start'));
});
