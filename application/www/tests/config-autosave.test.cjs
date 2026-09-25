const test = require('node:test');
const assert = require('node:assert/strict');
const { SessionConfigStore, rebaseConfig } = require('../lib/session-config-store.ts');
const { DeferredConfigCoordinator } = require('../lib/deferred-config-coordinator.ts');
const { readConfigSnapshot, writeConfigResource } = require('../lib/device-transport/config-snapshot.ts');
const { MockDeviceTransport } = require('../lib/device-transport/mock-device-transport.ts');
const { DeviceCommandClient } = require('../lib/device-transport/device-command-client.ts');
const tick = () => new Promise(resolve => setImmediate(resolve));
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
function gate() { let resolve; const promise = new Promise(r => resolve = r); return { promise, resolve }; }

test('provider autosave keeps live feedback running and does not replay LEDs after ordinary saves', async () => {
  const fs = require('node:fs');
  const { transform } = require('sucrase');
  const source = fs.readFileSync(require('node:path').join(__dirname, '../contexts/gamepad-config-context.tsx'), 'utf8');
  const start = source.indexOf('    const performDeferredConfigFlush = async');
  const end = source.indexOf('    const queueConfigTransaction =', start);
  const compiled = transform(source.slice(start, end), { transforms: ['typescript'] }).code;
  const held = gate(); const statuses = []; const commands = [];
  const generation = { current: 1 };
  const store = { confirmed: { 'selected-profile': 'a' }, get: () => 'a' };
  const dependencies = {
    initializationGenerationRef: generation, configReadyRef: { current: true },
    configStoreRef: { current: store },
    deferredConfigRef: { current: { dirty: true, flush: () => held.promise } },
    setDeferredConfigSaving: value => statuses.push(value),
    ledPreviewRef: { current: { replay: () => { throw new Error('Unexpected animation restart'); } } },
    sendDeviceRequest: async command => commands.push(command),
  };
  const flush = Function(...Object.keys(dependencies), compiled + '\nreturn performDeferredConfigFlush;')(...Object.values(dependencies));
  const saving = flush(undefined, true, undefined, false);
  await tick();
  assert.deepEqual(statuses, [true]);
  assert.deepEqual(commands, []);
  held.resolve(); await saving;
  assert.deepEqual(statuses, [true, false]);
  assert.deepEqual(commands, []); // No stop/start monitoring or clear/push LEDs.
});

test('snapshot fetches every slot and its macros before publishing; incomplete reads reject', async () => {
  const mock = new MockDeviceTransport({ storage: null });
  const client = new DeviceCommandClient(mock);
  await client.connect();
  const commands = [];
  const progress = [];
  const snapshot = await readConfigSnapshot(async (cmd, params) => {
    commands.push(cmd); return client.requestInitialization(cmd, params);
  }, x => x, (completed, total) => progress.push({ completed, total }));
  const list = snapshot['profile-list'];
  const total = 4 + list.items.length * 2;
  assert.deepEqual(progress, [
    { completed: 0, total: 0 },
    ...Array.from({ length: total }, (_, i) => ({ completed: i + 1, total })),
  ]); // One stable denominator, advancing once per completed request.
  assert.equal(commands.filter(c => c === 'get_profile_details').length, list.items.length);
  for (const profile of list.items) assert.ok(Array.isArray(snapshot[`macros:${profile.id}`]));
  assert.equal(snapshot['selected-profile'], list.defaultId);
  await assert.rejects(readConfigSnapshot(async (cmd, params) => {
    if (cmd === 'get_profile_macros') return { m: null };
    return client.requestInitialization(cmd, params);
  }, x => x), /compact macro/);
  await client.disconnect();
});

test('nested patches preserve siblings, replace arrays and eliminate edits reverted before send', () => {
  const store = new SessionConfigStore();
  store.hydrate('A', { global: { power: { hold: 3, sleep: 5 }, items: [1, 2] } });
  store.patch('global', { power: { sleep: 10 }, items: [3] });
  assert.deepEqual(store.get('global'), { power: { hold: 3, sleep: 10 }, items: [3] });
  store.set('global', store.confirmed.global);
  assert.deepEqual(store.dirtyKeys, []);
});

test('old acknowledgement normalizes unchanged fields but never overwrites a newer edit', () => {
  const store = new SessionConfigStore();
  store.hydrate('A', { global: { a: 0, b: 0 } });
  store.patch('global', { a: 1, b: 1 });
  const ticket = store.begin('global');
  store.patch('global', { a: 2 });
  store.acknowledge(ticket, { a: 1, b: 10 });
  assert.deepEqual(store.get('global'), { a: 2, b: 10 });
  assert.equal(store.dirty('global'), true);
  const next = store.begin('global');
  store.acknowledge(next, next.sent);
  assert.equal(store.dirty('global'), false);
});

test('reverting an in-flight edit remains dirty until the corrective write is acknowledged', () => {
  const store = new SessionConfigStore();
  store.hydrate('A', { global: { value: 0 } });
  store.patch('global', { value: 1 });
  const ticket = store.begin('global');
  store.patch('global', { value: 0 });
  assert.equal(store.dirty('global'), true);
  store.acknowledge(ticket, ticket.sent);
  assert.equal(store.get('global').value, 0);
  assert.equal(store.dirty('global'), true);
});

test('unknown write completion preserves both new edits and reverted fields across reconnect', () => {
  const store = new SessionConfigStore();
  store.hydrate('A', { global: { reverted: 0, desired: 0, other: 0 } });
  store.patch('global', { reverted: 1, desired: 2 });
  const ticket = store.begin('global');
  store.patch('global', { reverted: 0 });
  store.fail(ticket);
  const backup = store.detach();
  store.hydrate('A', { global: { reverted: 1, desired: 2, other: 9 } });
  store.restore(backup);
  assert.deepEqual(store.get('global'), { reverted: 0, desired: 2, other: 9 });
  store.hydrate('B', { global: {} });
  assert.throws(() => store.restore(backup), /another device/);
  store.acknowledge(ticket, { unwanted: true });
  assert.deepEqual(store.get('global'), {});
});

test('arrays are restored atomically without discarding unrelated remote fields', () => {
  assert.deepEqual(rebaseConfig({ keys: [1], power: 2 }, { keys: [2], power: 2 }, { keys: [3], power: 4 }), { keys: [2], power: 4 });
});

test('finite round uses the latest not-yet-sent value, retains in-flight edits, and delays profile switching', async () => {
  const first = gate(); const calls = [];
  const queue = new DeferredConfigCoordinator(undefined, { cooldownMs: 0 });
  queue.stage('profile:A', async () => { calls.push('A1'); await first.promise; }, 10);
  queue.stage('profile:B', async () => calls.push('B1'), 10);
  queue.stage('selected-profile', async () => calls.push('switch'), 30);
  const saving = queue.flush(false);
  await tick();
  queue.stage('profile:A', async () => calls.push('A2'), 10);
  queue.stage('profile:B', async () => calls.push('B2'), 10);
  assert.deepEqual(calls, ['A1']);
  first.resolve(); await saving;
  assert.deepEqual(calls, ['A1', 'B2']);
  assert.equal(queue.dirty, true);
  await queue.flush();
  assert.deepEqual(calls, ['A1', 'B2', 'A2', 'switch']);
});

test('clear invalidates old completion and never commits its followers or clears the new generation', async () => {
  const first = gate(); const calls = [];
  const queue = new DeferredConfigCoordinator(undefined, { cooldownMs: 0 });
  queue.stage('global', async () => { await first.promise; });
  queue.stage('hotkeys', async () => calls.push('old'));
  const saving = queue.flush();
  queue.clear();
  queue.stage('global', async () => calls.push('new'));
  first.resolve(); await assert.rejects(saving, /session ended/);
  assert.equal(queue.dirty, true);
  await queue.flush();
  assert.deepEqual(calls, ['new']);
});

test('debounce coalesces edits, pause blocks automatic work, failure is retained until retry', async () => {
  let calls = 0; let reject = true;
  const queue = new DeferredConfigCoordinator(undefined, {
    debounceMs: 15, maxWaitMs: 50, cooldownMs: 0, autoFlush: () => queue.flush(false),
  });
  queue.pause(true);
  for (let i = 0; i < 20; i++) queue.stage('global', async () => { calls++; if (reject) throw new Error('device refused'); });
  await delay(25); assert.equal(calls, 0);
  queue.pause(false); await delay(35);
  assert.equal(calls, 1); assert.equal(queue.dirty, true); assert.match(queue.state.error, /refused/);
  await delay(35); assert.equal(calls, 1);
  reject = false; queue.retry(); await delay(35);
  assert.equal(calls, 2); assert.equal(queue.dirty, false); assert.equal(queue.state.error, null);
  queue.clear();
});

test('continuous edits still become eligible by max wait', async () => {
  let calls = 0;
  const queue = new DeferredConfigCoordinator(undefined, {
    debounceMs: 35, maxWaitMs: 55, cooldownMs: 0, autoFlush: () => queue.flush(false),
  });
  const edit = () => queue.stage('global', async () => { calls++; });
  edit();
  const timer = setInterval(edit, 8);
  await delay(80); clearInterval(timer);
  assert.ok(calls >= 1); queue.clear();
});

test('cooldown begins after acknowledgement and never releases a still-active write', async () => {
  const first = gate(); const times = [];
  const queue = new DeferredConfigCoordinator(undefined, { cooldownMs: 25 });
  queue.stage('a', async () => { times.push(Date.now()); await first.promise; });
  queue.stage('b', async () => times.push(Date.now()));
  const saving = queue.flush();
  await delay(40); assert.equal(times.length, 1);
  const acknowledgedAt = Date.now(); first.resolve(); await saving;
  assert.ok(times[1] - acknowledgedAt >= 23);
});

test('mock gate verifies queued device writes and exact latest data after slow acknowledgements', async () => {
  const held = gate(); const calls = [];
  const mock = new MockDeviceTransport({ storage: null, beforeRequest: async (cmd) => {
    calls.push(cmd); if (cmd === 'update_global_config') await held.promise;
  } });
  const client = new DeviceCommandClient(mock); await client.connect(); client.markReady();
  const request = (cmd, params) => client.enqueue(cmd, params, true);
  const snapshot = await readConfigSnapshot(request, x => x);
  const store = new SessionConfigStore(); store.hydrate('A', snapshot);
  const queue = new DeferredConfigCoordinator(undefined, { cooldownMs: 0 });
  const stage = key => queue.stage(key, async () => {
    const ticket = store.begin(key);
    store.acknowledge(ticket, await writeConfigResource(request, key, ticket.sent, x => x));
  });
  await request('start_button_monitoring', {});
  await request('push_leds_config', { ledBrightness: 50 });
  calls.length = 0;
  store.patch('global', { power: { autoStandbyMs: 60000 } }); stage('global');
  const saving = queue.flush(false); await delay(10);
  // Monitoring remains active while the durable request has no ACK.
  assert.equal((await mock.request('get_button_states')).data.isActive, true);
  store.patch('global', { power: { autoStandbyMs: 120000 } }); stage('global');
  const id = snapshot['profile-list'].items[1].id;
  store.patch(`profile:${id}`, { name: 'EditedSlot' }); stage(`profile:${id}`);
  assert.deepEqual(calls, ['update_global_config', 'get_button_states']);
  held.resolve(); await saving;
  assert.equal(store.get('global').power.autoStandbyMs, 120000);
  await queue.flush();
  const reread = await readConfigSnapshot(request, x => x);
  assert.equal(reread.global.power.autoStandbyMs, 120000);
  assert.equal(reread[`profile:${id}`].name, 'EditedSlot');
  assert.deepEqual(store.dirtyKeys, []);
  assert.ok(!calls.includes('stop_button_monitoring'));
  assert.ok(!calls.includes('clear_leds_preview'));
  await client.disconnect();
});
