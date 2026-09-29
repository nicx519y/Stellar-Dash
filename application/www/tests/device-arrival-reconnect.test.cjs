const test = require('node:test');
const assert = require('node:assert/strict');
const { WebHidTransport } = require('../lib/device-transport/webhid-transport.ts');
const { registerDeviceArrivalReconnect } = require('../lib/device-transport/initial-auto-connect.ts');

function fixture() {
  const listeners = new Map();
  let chooserCalls = 0;
  const hid = {
    getDevices: async () => [],
    requestDevice: async () => { chooserCalls++; return []; },
    addEventListener(type, handler) { listeners.set(type, handler); },
    removeEventListener(type, handler) { if (listeners.get(type) === handler) listeners.delete(type); },
  };
  const transport = new WebHidTransport({ navigator: hid });
  const timers = new Map();
  let nextTimer = 0;
  let eligible = true;
  let connects = 0;
  let connect = async () => { connects++; };
  const failures = [];
  const remove = registerDeviceArrivalReconnect(
    handler => transport.onAvailable(handler), () => eligible, () => connect(),
    error => failures.push(error), 100,
    (callback, delay) => { assert.equal(delay, 350); timers.set(++nextTimer, callback); return nextTimer; },
    timer => timers.delete(timer),
  );
  const arrive = (device = { vendorId: 0xcafe, productId: 0x4021 }) => listeners.get('connect')?.({ device });
  const flush = async () => {
    const callbacks = [...timers.values()]; timers.clear();
    callbacks.forEach(callback => callback());
    await new Promise(resolve => setImmediate(resolve));
  };
  return { arrive, flush, remove, failures, timers, listeners,
    eligible: value => { eligible = value; }, connect: value => { connect = value; },
    connects: () => connects, chooserCalls: () => chooserCalls };
}

test('authorized USB arrival coalesces reconnects and ignores other devices', async () => {
  const f = fixture();
  f.arrive({ vendorId: 123, productId: 456 }); f.arrive(null);
  assert.equal(f.timers.size, 0);
  f.arrive(); f.arrive();
  assert.equal(f.timers.size, 1);
  await f.flush();
  assert.equal(f.connects(), 1);
  assert.equal(f.chooserCalls(), 0);
  f.remove();
  assert.equal(f.listeners.size, 0);
});

test('hidden, connecting and connected clients cannot start arrival reconnects', async () => {
  const f = fixture();
  f.eligible(false); f.arrive();
  assert.equal(f.timers.size, 0);
  f.eligible(true); f.arrive();
  f.eligible(false); // User started a connection or backgrounded the page during the delay.
  await f.flush();
  assert.equal(f.connects(), 0);
  f.eligible(true); f.arrive(); await f.flush();
  assert.equal(f.connects(), 1);
  f.remove();
});

test('unmount cancels pending reconnect and late failure notifications', async () => {
  const pending = fixture(); pending.arrive(); pending.remove();
  await pending.flush();
  assert.equal(pending.connects(), 0);
  const running = fixture();
  let reject;
  running.connect(() => new Promise((_, failure) => { reject = failure; }));
  running.arrive(); await running.flush(); running.remove(); reject(Error('disposed'));
  await running.flush();
  assert.deepEqual(running.failures, []);
});

test('arrival during reconnect does not duplicate it; failure waits for a new arrival', async () => {
  const f = fixture();
  let reject;
  f.connect(() => new Promise((_, failure) => { reject = failure; }));
  f.arrive(); await f.flush(); f.arrive();
  assert.equal(f.timers.size, 0);
  const error = Error('device unavailable'); reject(error); await f.flush();
  assert.deepEqual(f.failures, [error]);
  assert.equal(f.timers.size, 0);
  f.arrive(); assert.equal(f.timers.size, 1);
  assert.equal(f.chooserCalls(), 0);
  f.remove();
});
