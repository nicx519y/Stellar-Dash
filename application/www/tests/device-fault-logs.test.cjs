const test = require('node:test');
const assert = require('node:assert/strict');
const {
  parseFaultLogPage, collectFaultLogs, logExport, logExportText, failedLogPage,
} = require('../lib/device-fault-logs.ts');
const { mockFaultLogs } = require('../lib/mock-fault-logs.ts');

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
