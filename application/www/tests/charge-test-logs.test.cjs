const test = require('node:test');
const assert = require('node:assert/strict');
const { chargeSessions, chargeMilestone, chargeInterval, chargeTrend, decodeChargeRecord } = require('../lib/charge-test-logs.ts');
const { batterySessions, decodeBatteryRecord } = require('../lib/battery-test-logs.ts');
const { mockChargeRecords, mockFaultLogs } = require('../lib/mock-fault-logs.ts');
const { collectFaultLogs, logExport, logExportText } = require('../lib/device-fault-logs.ts');
const session = (scenario = 'charge-complete', coverage) => chargeSessions(mockChargeRecords(scenario), coverage)[0];

test('complete charge provides observed milestone times, means and independent Full', () => {
  const s = session();
  assert.equal(s.incomplete, false); assert.equal(s.cycles.length, 1);
  const c = s.cycles[0]; assert.equal(c.status, 'complete');
  assert.equal(c.elapsedMs, 14430000); assert.equal(c.startSoc, 10); assert.equal(c.endSoc, 100);
  assert.equal(chargeMilestone(c, 1000).ms, 14400000);
  assert.ok(chargeInterval(c, 200, 800) > 0);
  assert.ok(chargeInterval(c, 800, 'full') > 0);
  const points = chargeTrend(s); assert.equal(points.length, 48);
  assert.equal(points[0].inputMean, 9000); assert.equal(points.at(-1).lastSoc, 100);
  assert.ok(points.every(p => p.valid));
  assert.equal(batterySessions(mockChargeRecords('charge-complete')).length, 0);
});
test('unplugging creates independent segments and excludes waiting from totals', () => {
  const s = session('charge-interrupted');
  assert.equal(s.cycles.length, 2); assert.equal(s.cycles[0].status, 'interrupted');
  assert.equal(s.cycles[1].status, 'complete');
  assert.equal(s.cycles[0].elapsedMs + s.cycles[1].elapsedMs, s.inputMs);
  assert.ok(s.inputMs < s.end.data[3]);
  assert.equal(chargeMilestone(s.cycles[1], 200).status, 'already');
  assert.equal(chargeInterval(s.cycles[1], 200, 800), null);
});
test('gap preserves record completeness but invalidates charge result and breaks curves', () => {
  const s = session('charge-gap');
  assert.equal(s.incomplete, false); assert.equal(s.cycles[0].status, 'unverified');
  assert.ok(s.cycles[0].issues.includes('sample_gap'));
  assert.equal(chargeMilestone(s.cycles[0], 800).status, 'unverified');
  assert.equal(chargeInterval(s.cycles[0], 200, 800), null);
  assert.equal(chargeTrend(s).filter(p => !p.valid).length, 1);
  assert.equal(s.cycles[0].ratePerHour, null);
});
test('reboot endpoint and cumulative time are unknown, not zero or restarted', () => {
  const s = session('charge-reboot');
  assert.equal(s.inputMs, null); assert.equal(s.chargingMs, null);
  assert.equal(s.cycles[0].elapsedMs, null); assert.equal(s.cycles[0].endSoc, null);
  assert.ok(s.cycles[0].issues.includes('reboot')); assert.equal(s.incomplete, true);
});
test('missing summary buckets break the plot even if adjacent buckets have valid samples', () => {
  const records = mockChargeRecords('charge-complete');
  records.splice(records.findIndex(r => r.event === 122 && r.uptimeMs === 7200000), 1);
  const s = chargeSessions(records)[0];
  assert.equal(s.cycles[0].status, 'unverified');
  assert.ok(chargeTrend(s).some(p => !p.valid));
});
test('partial, pending, unknown versions and missing records never give a verified result', () => {
  assert.equal(session('charge-complete', { more: true }).cycles[0].status, 'unverified');
  for (const mutate of [
    records => { records.splice(10, 1); },
    records => { records[10].pending = true; records[10].sequence = 0; },
    records => { records[10].data[0] = 99; },
  ]) {
    const records = mockChargeRecords('charge-complete'); mutate(records);
    const s = chargeSessions(records)[0]; assert.equal(s.incomplete, true);
    assert.ok(s.cycles.every(c => c.status === 'unverified'));
  }
  assert.equal(decodeChargeRecord({ event: 122, data: [99, 8] }).unsupported, true);
  assert.equal(decodeBatteryRecord({ event: 122, data: [2, 8] }).unsupported, true);
});
test('load or supply change is visible and disqualifies a fixed-condition result', () => {
  for (const event of [103, 123]) {
    const records = mockChargeRecords('charge-complete'); const record = records.find(r => r.event === 124);
    record.event = event;
    if (event === 123) record.data[6] = 5000;
    const c = chargeSessions(records)[0].cycles[0];
    assert.equal(c.status, 'unverified');
    assert.ok(c.issues.includes(event === 103 ? 'configuration_changed' : 'supply_changed'));
  }
});
test('all pages and both export formats use the same charge analysis', async () => {
  const page = await collectFaultLogs(query => Promise.resolve(mockFaultLogs(query, 'charge-complete')), { source: 'battery' });
  assert.equal(page.more, false); assert.ok(page.items.length > 48);
  const output = logExport([page], { source: 'battery', category: 0, severity: 0 });
  assert.equal(output.chargeTests[0].cycles[0].elapsedMs, session().cycles[0].elapsedMs);
  assert.match(logExportText(output), /Charge session=8/);
  assert.match(logExportText(output), /CHG_MILESTONE/);
  assert.equal(output.batteryTests.length, 0);
});
