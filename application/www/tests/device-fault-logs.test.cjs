const test = require('node:test');
const assert = require('node:assert/strict');
const {
  parseFaultLogPage, collectFaultLogs, logExport, logExportText, failedLogPage,
} = require('../lib/device-fault-logs.ts');
const { mockFaultLogs } = require('../lib/mock-fault-logs.ts');
const { batterySessions, decodeBatteryRecord, batteryEventName } = require('../lib/battery-test-logs.ts');

test('battery source exports a whole session, units and a terminal summary', async () => {
  const page = await collectFaultLogs(async q => parseFaultLogPage(mockFaultLogs(q), q.source), { source: 'battery', category: 4 });
  assert.equal(page.items.length, 19);
  const sessions = batterySessions(page.items);
  assert.equal(sessions.length, 1);
  assert.equal(sessions[0].ended, true);
  assert.equal(sessions[0].incomplete, false);
  assert.equal(sessions[0].trend.length, 17);
  assert.equal(decodeBatteryRecord(page.items[1]).type, 'BAT_SAMPLE_SUMMARY');
  assert.equal(decodeBatteryRecord(page.items[1]).lastSocPercent, 4.5);
  const output = logExport([page], { source: 'battery', category: 4, severity: 0 });
  assert.equal(output.batteryTests[0].id, 7);
  assert.match(logExportText(output), /dischargeMs/);
  assert.equal(batteryEventName(108, 'zh'), '进入电池保护');
  assert.deepEqual(decodeBatteryRecord({ ...page.items[0], data: [99, 1] }), { unsupported: true });
  const invalid = { ...page.items[1], data: [...page.items[1].data] }; invalid.data[8] = 0;
  assert.equal(decodeBatteryRecord(invalid).minMv, null);
  assert.equal(decodeBatteryRecord(invalid).lastSocPercent, null);
  const interrupted = { ...page.items[0], data: [...page.items[0].data] }; interrupted.data[2] = 4;
  assert.equal(decodeBatteryRecord(interrupted).dischargeMs, null);
});

test('unfinished or partial battery pages never imply a completed discharge test', async () => {
  const page = mockFaultLogs({ source: 'battery' });
  assert.equal(batterySessions(page.items)[0].incomplete, true); // start not loaded yet
  const noEnd = (await collectFaultLogs(async q => mockFaultLogs(q), { source: 'battery' })).items.slice(1);
  assert.equal(batterySessions(noEnd)[0].ended, false);
});

test('battery milestones handle tick wrap and separate ordinary/protection STOP statistics', () => {
  const template = mockFaultLogs({ source: 'battery' }).items[0];
  const record = (sequence, event, uptimeMs, detail = 0) => ({
    ...template, sequence, event, uptimeMs, data: [1, 7, detail, 0, 3500, 100, 1, 0, 0, uptimeMs],
  });
  const records = [record(1, 101, 0xfffffff0), record(2, 106, 20, 1), record(3, 108, 100)];
  const milestones = batterySessions(records)[0].cycles[0].milestones;
  assert.equal(milestones.toLowMs, 36);
  assert.equal(milestones.toProtectionMs, 116);
  records[2].boot++;
  assert.equal(batterySessions(records)[0].cycles[0].milestones.toProtectionMs, null);
  assert.equal(batterySessions(records)[0].milestones.toLowMs, null); // unverified session has no validated result
  const decoded = decodeBatteryRecord({ ...template, event: 109, data: [1, 7, 2000, 3, 1, 1500, 5, 256, 2, 1000, 1, 500, 0, 1] });
  assert.equal(decoded.ordinaryStops, 2);
  assert.equal(decoded.protectionStopMs, 500);
  assert.equal(decoded.protectionStopFailures, 1);
});

test('complete export traverses a stable snapshot without duplicating records', async () => {
  const calls = [];
  const page = await collectFaultLogs(async query => {
    calls.push(query);
    return parseFaultLogPage(mockFaultLogs(query), query.source);
  }, { source: 'stm32' });
  assert.equal(page.items.length, 19);
  assert.equal(new Set(page.items.map(r => r.sequence)).size, 19);
  assert.equal(calls.length, 3);
  assert.equal(calls[1].upper, 19);
  assert.equal(calls[1].oldest, 1);
  assert.equal(page.more, false);
  const result = logExport([page], { source: 'stm32', category: 0, severity: 0 });
  assert.equal(result.complete, true);
  assert.match(logExportText(result), /boot=3/);
});

test('one unavailable source and interrupted pagination produce marked partial exports', async () => {
  const good = await collectFaultLogs(async q => mockFaultLogs(q), { source: 'stm32' });
  const bad = await collectFaultLogs(async q => mockFaultLogs(q, 'tx-unavailable'), { source: 'tx' });
  assert.equal(good.items.length, 19);
  assert.equal(bad.status, 'unavailable');
  assert.equal(logExport([good, bad], { source: 'all', category: 0, severity: 0 }).complete, false);
  let calls = 0;
  const partial = await collectFaultLogs(async q => {
    if (++calls === 2) throw new Error('Disconnected');
    return mockFaultLogs(q);
  }, { source: 'stm32' });
  assert.equal(partial.items.length, 8);
  assert.equal(partial.status, 'unavailable');
});

test('filters, empty, corrupt, pending overflow and old firmware remain distinct', async () => {
  for (const scenario of ['empty', 'corrupt', 'overflow', 'unsupported']) {
    const result = await collectFaultLogs(async q => parseFaultLogPage(mockFaultLogs(q, scenario), q.source), { source: 'tx' });
    if (scenario === 'empty') assert.equal(result.items.length, 0);
    if (scenario === 'corrupt') assert.equal(result.status, 'storage_error');
    if (scenario === 'unsupported') assert.equal(result.status, 'unsupported');
    if (scenario === 'overflow') {
      assert.equal(result.items.filter(r => r.pending).length, 1);
      assert.equal(result.dropped, 12);
    }
  }
  const filtered = await collectFaultLogs(async q => mockFaultLogs(q), { source: 'stm32', category: 3, severity: 2 });
  assert.equal(filtered.items.length, 10);
  assert(filtered.items.every(r => r.category === 3 && r.severity === 2));
  assert.equal(failedLogPage('tx', new Error('Unknown command')).status, 'unsupported');
  assert.equal(failedLogPage('tx', new Error('Device timeout')).status, 'unavailable');
});

test('invalid or nonadvancing cursor never loops or claims a complete export', async () => {
  const first = mockFaultLogs({ source: 'stm32' });
  const result = await collectFaultLogs(async () => first, { source: 'stm32' });
  assert.equal(result.status, 'unavailable');
  assert.equal(result.items.length, 8);
  assert.throws(() => parseFaultLogPage({ ...first, source: 'tx' }, 'stm32'));
  assert.throws(() => parseFaultLogPage({ ...first, items: [{ ...first.items[0], data: [NaN] }] }, 'stm32'));
});

test('overwritten history is retained in the final export status', async () => {
  const result = await collectFaultLogs(async q => ({ ...mockFaultLogs(q), overwritten: !!q.before }), { source: 'stm32' });
  assert.equal(result.overwritten, true);
  assert.equal(logExport([result], { source: 'stm32', category: 0, severity: 0 }).complete, false);
});

test('automatic reads progressively render every page in snapshot order', async () => {
  const progress = [];
  const result = await collectFaultLogs(async q => mockFaultLogs(q), { source: 'tx' },
    () => false, page => progress.push(page));
  assert.deepEqual(progress.map(page => page.items.length), [8, 16, 19]);
  assert.deepEqual(progress.map(page => page.more), [true, true, false]);
  assert.deepEqual(result.items.map(record => record.sequence), Array.from({ length: 19 }, (_, i) => 19 - i));
  assert.equal(progress[0].items.length, 8); // later merges retain previous render snapshots
});

test('switching sources stops old reads without publishing an in-flight stale page', async () => {
  let cancelled = false;
  let calls = 0;
  const progress = [];
  const result = await collectFaultLogs(async q => {
    calls++;
    if (calls === 2) cancelled = true; // source changes while page two is in flight
    return mockFaultLogs(q);
  }, { source: 'stm32' }, () => cancelled, page => progress.push(page.items.length));
  assert.deepEqual(progress, [8]);
  assert.equal(calls, 2);
  assert.equal(result.items.length, 8);
  assert.equal(result.status, 'unavailable');
});


test('read progress follows the stable snapshot and never finishes early', async () => {
  const { logReadPercent } = require('../lib/device-fault-logs.ts');
  const values = [];
  const page = await collectFaultLogs(async q => mockFaultLogs(q, 'full'), { source: 'stm32' },
    () => false, value => values.push(logReadPercent(value)));
  assert.equal(page.items.length, 512);
  assert.equal(values.length, 64);
  assert.equal(values.at(-1), 100);
  assert(values.slice(0, -1).every(value => value >= 0 && value < 100));
  assert(values.every((value, i) => !i || value >= values[i - 1]));
});

test('virtual rows cover the viewport with overscan and account for expanded details', () => {
  const { logVirtualWindow } = require('../lib/log-virtual-window.ts');
  const heights = Array(512).fill(48);
  const top = logVirtualWindow(heights, 0, 600);
  assert.equal(top.total, 24576);
  assert(top.end < 25);
  heights[200] = 400;
  const middle = logVirtualWindow(heights, 200 * 48 + 200, 600);
  assert(middle.start <= 200 && middle.end > 200);
  const bottom = logVirtualWindow(heights, 24576 + 352 - 600, 600);
  assert.equal(bottom.end, 512);
  assert(bottom.start > 480);
});
