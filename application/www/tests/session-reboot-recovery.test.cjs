const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { transform } = require('sucrase');
const { initializeDeviceSession } = require('../lib/device-transport/device-initialization.ts');

// Exercise the real lifecycle's initialization effect with host-only device
// fixtures. No HID connection or hardware operations are used.
function fixture({ fail = false, markReady = true } = {}) {
  const effects = [];
  const exports = {};
  let completion;
  let release;
  const layout = new Promise(resolve => { release = resolve; });
  const state = { rebooting: true, ready: false, disconnected: false, clears: 0 };
  const client = {
    getPhase: () => 'initializing', getStartupDeadlineMs: () => undefined,
    setInitializationStage() {}, markReady: () => markReady,
    transport: { kind: 'webhid', session: { deviceId: 'fixture' } },
    disconnect() { state.disconnected = true; },
  };
  const deps = {
    deviceClient: client, deviceConnected: true, deviceState: 'connected',
    devicePhase: 'initializing', dataIsReady: false,
    initializationGenerationRef: { current: 1 }, transportConfig: { startupTimeoutMs: 1000 },
    postReadyRequestSchedulerRef: { current: null }, buttonMonitorLeaseRef: { current: null },
    configStoreRef: { current: { hydrate() {} } }, detachedDraftsRef: { current: new Map() },
    configReadyRef: { current: false },
    fetchFirmwareMetadata: async () => {}, getHitboxLayout: () => layout,
    publishConfig() {}, updateSyncPause() {}, setHitboxLayout() {}, setShowReconnect() {}, setError() {},
    setDataIsReady(value) { state.ready = value; },
    setUserRebooting(value) { state.rebooting = value; state.clears++; },
  };
  const code = transform(fs.readFileSync(path.join(__dirname, '../contexts/gamepad-config/session-lifecycle.ts'), 'utf8'), {
    transforms: ['typescript', 'imports'],
  }).code;
  vm.runInNewContext(code, {
    exports, AbortController, console,
    require(id) {
      if (id === 'react') return { useEffect: (fn, keys) => effects.push({ fn, keys }) };
      if (id === '@/lib/device-transport') return {
        DeviceTransportState: { CONNECTED: 'connected' },
        DeviceConnectionPhase: { INITIALIZING: 'initializing' },
      };
      if (id.endsWith('/device-initialization')) return { initializeDeviceSession(options) {
        completion = initializeDeviceSession(options); return completion;
      } };
      if (id.endsWith('/config-sync')) return { readDeviceConfigSnapshot: async () => {
        if (fail) throw new Error('fixture read failed');
        return { resources: {} };
      } };
      return {};
    },
  });
  exports.useSessionLifecycle(deps, { resetDeviceSessionState() {}, cancelPendingAutomaticConnects() {} });
  const initialize = effects.find(({ keys }) => keys?.[0] === true && keys[1] === 'connected');
  assert.ok(initialize, 'initialization effect is registered');
  const cleanup = initialize.fn();
  return { state, deps, release, cleanup, completion };
}

test('successful automatic reconnect clears the previous reboot pause only after initialization', async () => {
  const f = fixture();
  assert.equal(f.state.rebooting, true);
  assert.equal(f.state.ready, false);
  f.release([]);
  assert.equal(await f.completion, 'ready');
  assert.equal(f.state.ready, true);
  assert.equal(f.state.rebooting, false);
  assert.equal(f.state.clears, 1);
});

test('failed initialization does not release the reboot pause or publish readiness', async () => {
  const f = fixture({ fail: true });
  assert.equal(await f.completion, 'failed');
  assert.equal(f.state.rebooting, true);
  assert.equal(f.state.ready, false);
  assert.equal(f.state.disconnected, true);
});

for (const obsolete of ['generation', 'unmount', 'markReady']) {
  test(`obsolete initialization (${obsolete}) cannot clear a later reboot pause`, async () => {
    const f = fixture({ markReady: obsolete !== 'markReady' });
    if (obsolete === 'generation') f.deps.initializationGenerationRef.current++;
    if (obsolete === 'unmount') f.cleanup();
    f.release([]);
    await f.completion;
    assert.equal(f.state.rebooting, true);
    assert.equal(f.state.ready, false);
    assert.equal(f.state.clears, 0);
  });
}
