const test = require('node:test');
const assert = require('node:assert/strict');
const { createDeviceConnectionWatchdog } = require('../lib/device-transport/device-connection-watchdog.ts');
function harness(probe) {
  let timer, enabled = true, generation = 1, calls = 0, failures = 0;
  const stop = createDeviceConnectionWatchdog({
    enabled: () => enabled, generation: () => generation,
    probe: async () => { calls++; return probe(); }, unavailable: () => { failures++; enabled = false; },
    schedule: (fn, delay) => { assert.equal(delay, 3000); timer = fn; return 1; }, cancel: () => { timer = undefined; },
  });
  return { stop, tick() { const fn = timer; timer = undefined; fn?.(); },
    pause() { enabled = false; }, resume() { enabled = true; }, replace() { generation++; },
    get calls() { return calls; }, get failures() { return failures; }, get scheduled() { return !!timer; } };
}
const flush = () => new Promise(resolve => setImmediate(resolve));
test('ready session uses one periodic read; unavailable controller disconnects once', async () => {
  let failed = false;
  const h = harness(async () => { if (failed) throw Error('WebConfig exited'); });
  h.tick(); await flush(); assert.equal(h.calls, 1); assert.equal(h.failures, 0);
  failed = true; h.tick(); await flush(); assert.equal(h.failures, 1);
  h.tick(); await flush(); assert.equal(h.calls, 2); h.stop(); assert.equal(h.scheduled, false);
});
test('firmware installation and exclusive operations suppress connection probes', async () => {
  const h = harness(async () => { throw Error('TX offline'); }); h.pause();
  h.tick(); await flush(); assert.equal(h.calls, 0); assert.equal(h.failures, 0);
  h.resume(); h.tick(); await flush(); assert.equal(h.failures, 1); h.stop();
});
for (const action of ['pause', 'replace', 'stop']) test(`late failure after ${action} cannot disconnect the current session`, async () => {
  let reject; const pending = new Promise((_, r) => { reject = r; });
  const h = harness(() => pending); h.tick(); h.tick(); assert.equal(h.calls, 1);
  h[action](); reject(Error('old timeout')); await flush(); assert.equal(h.failures, 0); h.stop();
});
