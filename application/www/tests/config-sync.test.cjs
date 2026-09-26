const test = require('node:test');
const assert = require('node:assert/strict');
const { readIncrementalConfigSnapshot } = require('../lib/device-transport/config-sync.ts');
const { ConfigSyncCache, configCacheKey, IndexedDbConfigCache } = require('../lib/device-transport/config-cache.ts');
const { contentChecksum } = require('../lib/device-transport/config-modules.ts');
const { writeConfigResource } = require('../lib/device-transport/config-snapshot.ts');
const { MockDeviceTransport } = require('../lib/device-transport/mock-device-transport.ts');
const { DeviceCommandClient } = require('../lib/device-transport/device-command-client.ts');
const { DeviceTransportError } = require('../lib/device-transport/types.ts');
const { connectionPresentation } = require('../lib/connection-presentation.ts');
const { DeviceConnectionPhase } = require('../lib/device-transport/device-command-types.ts');

class MemoryStorage {
  values = new Map();
  async read(key) { return structuredClone(this.values.get(key)); }
  async write(key, value, signal) { if (!signal?.aborted) this.values.set(key, structuredClone(value)); }
}
async function fixture(options = {}) {
  const mock = new MockDeviceTransport({ storage: null, ...options });
  await mock.connect();
  const storage = new MemoryStorage();
  const cache = new ConfigSyncCache('mock', storage);
  const calls = [];
  let bytes = 0;
  const request = async (command, params) => {
    calls.push(command);
    const result = (await mock.request(command, params)).data;
    bytes += Buffer.byteLength(JSON.stringify(result));
    return result;
  };
  const sync = async (reader = request, current = () => true) => {
    calls.length = 0; bytes = 0;
    return readIncrementalConfigSnapshot(reader, x => x, cache, () => {}, current);
  };
  const activate = async result => { cache.activate(result.manifest, result.modules); await cache.settled(); };
  return { mock, cache, storage, calls, request, sync, activate, bytes: () => bytes };
}

test('cold reads 36 modules, warm only two manifests, one stale module only one GET', async t => {
  const f = await fixture();
  const cold = await f.sync();
  assert.equal(Object.keys(cold.modules).length, 36);
  assert.equal(f.calls.length, 38);
  const coldBytes = f.bytes();
  await f.activate(cold);
  const warm = await f.sync();
  assert.deepEqual(warm.resources, cold.resources);
  assert.deepEqual(f.calls, ['get_config_manifest', 'get_config_manifest']);
  const warmBytes = f.bytes();
  await f.mock.request('update_screen_control_config', { screenControl: { brightness: 37 } });
  const changed = await f.sync();
  assert.equal(changed.resources['screen-control'].brightness, 37);
  assert.deepEqual(f.calls, ['get_config_manifest', 'get_screen_control_config', 'get_config_manifest']);
  t.diagnostic(JSON.stringify({ cold: { requests: 38, responseJsonBytes: coldBytes }, warm: { requests: 2, responseJsonBytes: warmBytes }, oneModule: { requests: 3, responseJsonBytes: f.bytes() } }));
});

test('macros change independently; switching slots refreshes global and list', async () => {
  const f = await fixture(); const cold = await f.sync(); await f.activate(cold);
  const id = cold.resources['profile-list'].items[1].id;
  await f.mock.request('update_profile_macros', { pid: id, m: [{ k: [1], s: [[20, 1, 0]] }, null, null, null, null] });
  await f.sync();
  assert.deepEqual(f.calls, ['get_config_manifest', 'get_profile_macros', 'get_config_manifest']);
  await f.activate(await f.sync());
  await f.mock.request('switch_default_profile', { profileId: id });
  const changed = await f.sync();
  assert.equal(changed.resources['selected-profile'], id);
  assert.deepEqual(f.calls, ['get_config_manifest', 'get_profile_list', 'get_global_config', 'get_config_manifest']);
});

test('multiple profiles, names and macros update exactly their affected resources', async () => {
  const f = await fixture(); const cold = await f.sync(); await f.activate(cold);
  const [a, b] = cold.resources['profile-list'].items;
  await f.mock.request('update_profile', { profileId: a.id, profileDetails: { name: 'Changed' } });
  await f.mock.request('update_profile', { profileId: b.id, profileDetails: { ledsConfigs: { ledBrightness: 38 } } });
  const changed = await f.sync();
  assert.equal(changed.resources[`profile:${a.id}`].name, 'Changed');
  assert.equal(changed.resources[`profile:${b.id}`].ledsConfigs.ledBrightness, 38);
  assert.equal(f.calls.filter(x => x === 'get_profile_details').length, 2);
  assert.equal(f.calls.filter(x => x === 'get_profile_list').length, 1);
  assert.equal(f.calls.filter(x => x === 'get_profile_macros').length, 0);
});

test('corruption, valid-checksum malformed data and missing entries are cache misses', async () => {
  const f = await fixture(); const cold = await f.sync(); await f.activate(cold);
  const key = configCacheKey('mock', cold.manifest);
  const raw = f.storage.values.get(key);
  raw.global.data.inputMode = 'CORRUPT';
  raw.hotkeys.data = {}; raw.hotkeys.checksum = await contentChecksum({});
  delete raw['screen-control'];
  await f.sync();
  assert.deepEqual(new Set(f.calls), new Set(['get_config_manifest', 'get_global_config', 'get_hotkeys_config', 'get_screen_control_config']));
});

test('device and transport namespaces never reuse each other; cache survives a new client', async () => {
  const f = await fixture(); const cold = await f.sync(); await f.activate(cold);
  const newCache = new ConfigSyncCache('mock', f.storage);
  f.calls.length = 0;
  await readIncrementalConfigSnapshot(f.request, x => x, newCache);
  assert.equal(f.calls.length, 2);
  const secondDevice = await fixture({ storageKey: 'other-device' });
  await readIncrementalConfigSnapshot(secondDevice.request, x => x, newCache);
  assert.equal(secondDevice.calls.length, 38);
  f.calls.length = 0;
  await readIncrementalConfigSnapshot(f.request, x => x, new ConfigSyncCache('webhid', f.storage));
  assert.equal(f.calls.length, 38);
});

test('explicit unsupported command falls back; timeouts, malformed manifests and permissions do not', async () => {
  const f = await fixture();
  const fallback = await f.sync((cmd, params) => cmd === 'get_config_manifest'
    ? Promise.reject(new DeviceTransportError('protocol', 'Unknown command', { command: cmd, errNo: 404 })) : f.request(cmd, params));
  assert.equal(fallback.manifest, undefined);
  assert.equal(f.calls.length, 36);
  for (const error of [new DeviceTransportError('timeout', 'Timeout'), new DeviceTransportError('protocol', 'Denied', { command: 'get_config_manifest', errNo: 403 }), new DeviceTransportError('protocol', 'Unknown command')]) {
    await assert.rejects(f.sync(() => Promise.reject(error)), e => e === error);
    assert.equal(f.calls.length, 0);
  }
  await assert.rejects(f.sync(async () => ({})), /manifest/);
});

test('missing response versions and profile-list/manifest disagreement fail closed', async () => {
  const f = await fixture();
  await assert.rejects(f.sync(async (cmd, params) => {
    const value = await f.request(cmd, params);
    if (cmd === 'get_screen_control_config') delete value.configVersions;
    return value;
  }), /Missing configuration response version/);
  await assert.rejects(f.sync(async (cmd, params) => {
    const value = await f.request(cmd, params);
    if (cmd === 'get_config_manifest') delete value.modules[Object.keys(value.modules).find(key => key.startsWith('macros:'))];
    return value;
  }), /does not match profile slots/);
  assert.equal(f.storage.values.size, 0);
});

test('hardware and cache-format partitions miss; changed device identity during sync fails', async () => {
  const f = await fixture(); const cold = await f.sync(); await f.activate(cold);
  assert.deepEqual(await f.cache.load({ ...cold.manifest, hardwareVersion: 'other-hardware' }), {});
  const key = configCacheKey('mock', cold.manifest);
  const oldKey = JSON.parse(key); oldKey[4] = 0;
  f.storage.values.set(JSON.stringify(oldKey), f.storage.values.get(key)); f.storage.values.delete(key);
  await f.sync(); assert.equal(f.calls.length, 38);
  let count = 0;
  await assert.rejects(f.sync(async (cmd, params) => {
    const value = await f.request(cmd, params);
    if (cmd === 'get_config_manifest' && ++count > 1) value.deviceCacheKey = 'a'.repeat(64);
    return value;
  }), /device changed/);
});

test('changes while synchronizing are reread; continuously changing versions fail after two extra rounds', async () => {
  const f = await fixture(); const cold = await f.sync(); await f.activate(cold);
  let manifests = 0;
  const result = await f.sync(async (cmd, params) => {
    if (cmd === 'get_config_manifest' && ++manifests === 2) await f.mock.request('update_screen_control_config', { screenControl: { brightness: 39 } });
    return f.request(cmd, params);
  });
  assert.equal(result.resources['screen-control'].brightness, 39);
  assert.equal(f.calls.filter(c => c === 'get_screen_control_config').length, 1);
  manifests = 0;
  await assert.rejects(f.sync(async (cmd, params) => {
    if (cmd === 'get_config_manifest') await f.mock.request('update_screen_control_config', { screenControl: { brightness: ++manifests } });
    return f.request(cmd, params);
  }), /kept changing/);
  assert.equal(manifests, 4);
});

test('disconnection and stale generations never publish or replace the previous snapshot', async () => {
  const f = await fixture(); const cold = await f.sync(); await f.activate(cold);
  const before = structuredClone(f.storage.values);
  let current = true;
  await assert.rejects(f.sync(async (cmd, params) => { const value = await f.request(cmd, params); current = false; return value; }, () => current), /cancelled/);
  await assert.rejects(f.sync(async (cmd, params) => { await f.mock.disconnect(); return f.request(cmd, params); }));
  assert.deepEqual(f.storage.values, before);
});

test('unavailable storage is harmless and partial sync is not persisted', async () => {
  const f = await fixture();
  const storage = { read: async () => { throw Error('disabled'); }, write: async () => { throw Error('quota'); } };
  const cache = new ConfigSyncCache('mock', storage);
  const result = await readIncrementalConfigSnapshot(f.request, x => x, cache);
  cache.activate(result.manifest, result.modules); await cache.settled();
  assert.equal(Object.keys(result.modules).length, 36);
  assert.equal(await new IndexedDbConfigCache().read('absent'), undefined);
  await assert.rejects(f.sync((cmd, params) => cmd === 'get_profile_macros' ? Promise.reject(Error('interrupted')) : f.request(cmd, params)), /interrupted/);
  assert.equal(f.storage.values.size, 0);
});

test('cache updates from device ACKs, not mutable drafts; uncertain writes invalidate', async () => {
  const f = await fixture(); const cold = await f.sync(); await f.activate(cold);
  const params = { screenControl: { brightness: 45 } };
  f.cache.invalidateForCommand('update_screen_control_config', params);
  const ack = (await f.mock.request('update_screen_control_config', params)).data;
  const observing = f.cache.observe(ack, params);
  ack.screenControl.brightness = 99; // mutation after callback must not change stored body
  await observing; await f.cache.settled();
  const warm = await f.sync();
  assert.equal(warm.resources['screen-control'].brightness, 45);
  assert.equal(f.calls.length, 2);
  f.cache.invalidateForCommand('update_screen_control_config', params);
  await f.cache.settled();
  await f.sync();
  assert.equal(f.calls.length, 3);
  f.cache.invalidateForCommand('import_config_finish', {}); await f.cache.settled();
  await f.sync(); assert.equal(f.calls.length, 38);
});

test('saving profile settings preserves unchanged list and macros for a warm reconnect', async () => {
  const f = await fixture(); const cold = await f.sync(); await f.activate(cold);
  const id = cold.resources['selected-profile'];
  const params = { profileDetails: { id, ledsConfigs: { ledBrightness: 38 } } };
  f.cache.invalidateForCommand('update_profile', params);
  const ack = (await f.mock.request('update_profile', params)).data;
  await f.cache.observe(ack, params); await f.cache.settled();
  const warm = await f.sync();
  assert.equal(warm.resources[`profile:${id}`].ledsConfigs.ledBrightness, 38);
  assert.deepEqual(f.calls, ['get_config_manifest', 'get_config_manifest']);
});

test('profile save side effects retain old fingerprints and reread only changed dependencies', async () => {
  for (const dependency of ['profile-list', 'macros']) {
    const f = await fixture(); const cold = await f.sync(); await f.activate(cold);
    const id = cold.resources['selected-profile'];
    const key = dependency === 'macros' ? `macros:${id}` : dependency;
    const patch = dependency === 'macros'
      ? { keysConfig: { macros: [{ index: 0, triggerKeys: [1], steps: [{ timeMs: 20, buttonMask: 1, dynamicMask: 0 }] }] } }
      : { name: 'Renamed profile' };
    const params = { profileDetails: { id, ...patch } };
    f.cache.invalidateForCommand('update_profile', params);
    await f.cache.observe((await f.mock.request('update_profile', params)).data, params);
    await f.cache.settled();
    const stored = await f.cache.load(cold.manifest);
    assert.deepEqual(stored[key], cold.modules[key]); // Never invent a version for a missing ACK body.
    const result = await f.sync();
    assert.notEqual(result.modules[key].version, cold.modules[key].version);
    assert.deepEqual(f.calls, ['get_config_manifest', dependency === 'macros' ? 'get_profile_macros' : 'get_profile_list', 'get_config_manifest']);
  }
});

test('lost profile save ACK leaves details invalid and detects changed list on reconnect', async () => {
  const f = await fixture(); const cold = await f.sync(); await f.activate(cold);
  const id = cold.resources['selected-profile'];
  const params = { profileDetails: { id, name: 'Saved without ACK' } };
  f.cache.invalidateForCommand('update_profile', params);
  await f.mock.request('update_profile', params); // Device commits, but the reply never reaches the cache.
  await f.cache.settled();
  assert.equal((await f.cache.load(cold.manifest))[`profile:${id}`], undefined);
  const result = await f.sync();
  assert.equal(result.resources[`profile:${id}`].name, 'Saved without ACK');
  assert.deepEqual(f.calls, ['get_config_manifest', 'get_profile_list', 'get_profile_details', 'get_config_manifest']);
});

test('late acknowledgements cannot modify a newly activated device cache', async () => {
  const f = await fixture(); const cold = await f.sync(); await f.activate(cold);
  const ack = (await f.mock.request('get_global_config')).data;
  const pending = f.cache.observe(ack, {});
  f.cache.endSession();
  const other = { ...cold.manifest, deviceCacheKey: 'f'.repeat(64) };
  f.cache.activate(other, {});
  await pending; await f.cache.settled();
  assert.deepEqual(await f.cache.load(other), {});
});

test('DeviceCommandClient allows manifest during initialization and observes normalized save replies', async () => {
  const mock = new MockDeviceTransport({ storage: null });
  const client = new DeviceCommandClient(mock);
  client.configCache.storage = new MemoryStorage();
  await client.connect();
  const result = await readIncrementalConfigSnapshot((cmd, params) => client.requestInitialization(cmd, params), x => x, client.configCache);
  assert.equal(client.markReady(), true);
  client.configCache.activate(result.manifest, result.modules);
  await client.request('update_screen_control_config', { screenControl: { brightness: 40 } });
  await client.configCache.settled();
  const loaded = await client.configCache.load(result.manifest);
  assert.equal(loaded['screen-control'].data.brightness, 40);
  for (const item of result.resources['profile-list'].items.slice(0, 2)) {
    const profile = structuredClone(result.resources[`profile:${item.id}`]);
    profile.ledsConfigs.ledBrightness = 42;
    await writeConfigResource((cmd, params) => client.request(cmd, params), `profile:${item.id}`, profile, x => x);
    profile.ledsConfigs.ledBrightness = 99; // Later edits must remain drafts.
  }
  await client.configCache.settled();
  client.disconnect();
  await client.connect();
  const calls = [];
  const warm = await readIncrementalConfigSnapshot((cmd, params) => {
    calls.push(cmd);
    return client.requestInitialization(cmd, params);
  }, x => x, client.configCache);
  assert.deepEqual(calls, ['get_config_manifest', 'get_config_manifest']);
  for (const item of result.resources['profile-list'].items.slice(0, 2)) {
    assert.equal(warm.resources[`profile:${item.id}`].ledsConfigs.ledBrightness, 42);
  }
  client.disconnect();
});

test('overlay continues checking even with no stale modules or all reads complete', () => {
  for (const count of [0, 3]) {
    const state = connectionPresentation(DeviceConnectionPhase.INITIALIZING, { completed: count, total: count, phase: 'checking' });
    assert.equal(state.stage, 1); assert.equal(state.detail, 'checking');
  }
  assert.equal(connectionPresentation(DeviceConnectionPhase.INITIALIZING, { completed: 0, total: 0, phase: 'complete' }).stage, 2);
});
