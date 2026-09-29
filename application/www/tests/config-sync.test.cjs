const test = require('node:test');
const assert = require('node:assert/strict');
const { readDeviceConfigSnapshot } = require('../lib/device-transport/config-sync.ts');
const { writeConfigResource } = require('../lib/device-transport/config-snapshot.ts');
const { MockDeviceTransport } = require('../lib/device-transport/mock-device-transport.ts');
const { DeviceCommandClient } = require('../lib/device-transport/device-command-client.ts');
const { DeviceTransportError } = require('../lib/device-transport/types.ts');
const { connectionPresentation } = require('../lib/connection-presentation.ts');
const { DeviceConnectionPhase } = require('../lib/device-transport/device-command-types.ts');

async function fixture(options = {}) {
  const mock = new MockDeviceTransport({ storage: null, ...options });
  await mock.connect();
  const calls = [];
  const request = async (command, params) => {
    calls.push(command);
    return (await mock.request(command, params)).data;
  };
  const sync = async (reader = request, current = () => true) => {
    calls.length = 0;
    return readDeviceConfigSnapshot(reader, x => x, () => {}, current);
  };
  return { mock, calls, request, sync };
}

test('failed configuration read identifies resource and completed progress without retry', async () => {
  const f = await fixture();
  let completed = 0, total = 0, attempts = 0;
  await assert.rejects(readDeviceConfigSnapshot(async (command, params) => {
    if (command === 'get_profile_details') {
      ++attempts;
      throw new DeviceTransportError('timeout', 'read timed out', { transactionId: 42, nativeWriteComplete: true });
    }
    return f.request(command, params);
  }, x => x, value => { completed = value.completed; total = value.total; }), error => {
    assert.equal(error.code, 'timeout');
    assert.match(error.cause.resource, /^profile:/);
    assert.equal(error.cause.command, 'get_profile_details');
    assert.equal(error.cause.completed, completed);
    assert.equal(error.cause.total, total);
    assert.equal(error.cause.transactionId, 42);
    assert.equal(error.cause.nativeWriteComplete, true);
    return true;
  });
  assert.equal(attempts, 1);
});

test('every connection reads all 36 modules even when device versions are unchanged', async () => {
  const f = await fixture();
  const first = await f.sync();
  assert.equal(f.calls.length, 38);
  const expectedCalls = [...f.calls];
  for (let attempt = 0; attempt < 2; attempt++) {
    const next = await f.sync();
    assert.deepEqual(next.resources, first.resources);
    assert.deepEqual(f.calls, expectedCalls);
  }
  await f.mock.request('update_screen_control_config', { screenControl: { brightness: 37 } });
  const changed = await f.sync();
  assert.equal(changed.resources['screen-control'].brightness, 37);
  assert.deepEqual(f.calls, expectedCalls);
});

test('lost save ACK and mutable old snapshots cannot hide remote changes on reconnect', async () => {
  const f = await fixture();
  const old = await f.sync();
  const id = old.resources['selected-profile'];
  await f.mock.request('update_profile', { profileDetails: { id, name: 'Saved without ACK' } });
  old.resources[`profile:${id}`].name = 'Local draft';
  const result = await f.sync();
  assert.equal(result.resources[`profile:${id}`].name, 'Saved without ACK');
  assert.equal(f.calls.length, 38);
});

test('client only deletes the retired config database; connection and save never open storage', async () => {
  const previous = global.indexedDB;
  const deleted = []; let opens = 0;
  global.indexedDB = {
    deleteDatabase: name => { deleted.push(name); return {}; },
    open: () => { opens++; throw new Error('Configuration storage must not be opened'); },
  };
  const mock = new MockDeviceTransport({ storage: null });
  const client = new DeviceCommandClient(mock);
  try {
    await client.connect();
    await readDeviceConfigSnapshot((cmd, params) => client.requestInitialization(cmd, params), x => x);
    client.markReady();
    await client.request('update_screen_control_config', { screenControl: { brightness: 41 } });
    assert.deepEqual(deleted, ['xora-config-cache']);
    assert.equal(opens, 0);
    assert.equal(client.configCache, undefined);
  } finally {
    await client.disconnect();
    if (previous === undefined) delete global.indexedDB; else global.indexedDB = previous;
  }
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
});

test('changed device identity during a full read fails', async () => {
  const f = await fixture(); let count = 0;
  await assert.rejects(f.sync(async (cmd, params) => {
    const value = await f.request(cmd, params);
    if (cmd === 'get_config_manifest' && ++count > 1) value.deviceCacheKey = 'a'.repeat(64);
    return value;
  }), /device changed/);
});

test('changes while synchronizing are reread; continuously changing versions fail after two extra rounds', async () => {
  const f = await fixture();
  let manifests = 0;
  const result = await f.sync(async (cmd, params) => {
    if (cmd === 'get_config_manifest' && ++manifests === 2) await f.mock.request('update_screen_control_config', { screenControl: { brightness: 39 } });
    return f.request(cmd, params);
  });
  assert.equal(result.resources['screen-control'].brightness, 39);
  assert.equal(f.calls.filter(c => c === 'get_screen_control_config').length, 2);
  manifests = 0;
  await assert.rejects(f.sync(async (cmd, params) => {
    if (cmd === 'get_config_manifest') await f.mock.request('update_screen_control_config', { screenControl: { brightness: ++manifests } });
    return f.request(cmd, params);
  }), /kept changing/);
  assert.equal(manifests, 4);
});

test('disconnection and stale generations never publish or replace the previous snapshot', async () => {
  const f = await fixture();
  let current = true;
  await assert.rejects(f.sync(async (cmd, params) => { const value = await f.request(cmd, params); current = false; return value; }, () => current), /cancelled/);
  await assert.rejects(f.sync(async (cmd, params) => { await f.mock.disconnect(); return f.request(cmd, params); }));
});

test('DeviceCommandClient loads all resources on reconnect after acknowledged saves', async () => {
  const mock = new MockDeviceTransport({ storage: null });
  const client = new DeviceCommandClient(mock);
  await client.connect();
  const result = await readDeviceConfigSnapshot((cmd, params) => client.requestInitialization(cmd, params), x => x);
  assert.equal(client.markReady(), true);
  await client.request('update_screen_control_config', { screenControl: { brightness: 40 } });
  for (const item of result.resources['profile-list'].items.slice(0, 2)) {
    const profile = structuredClone(result.resources[`profile:${item.id}`]);
    profile.ledsConfigs.ledBrightness = 42;
    await writeConfigResource((cmd, params) => client.request(cmd, params), `profile:${item.id}`, profile, x => x);
    profile.ledsConfigs.ledBrightness = 99; // Later edits must remain drafts.
  }
  await client.disconnect();
  await client.connect();
  const calls = [];
  const warm = await readDeviceConfigSnapshot((cmd, params) => {
    calls.push(cmd);
    return client.requestInitialization(cmd, params);
  }, x => x);
  assert.equal(calls.length, 38);
  assert.equal(warm.resources['screen-control'].brightness, 40);
  for (const item of result.resources['profile-list'].items.slice(0, 2)) {
    assert.equal(warm.resources[`profile:${item.id}`].ledsConfigs.ledBrightness, 42);
  }
  await client.disconnect();
});

test('overlay continues checking even with no stale modules or all reads complete', () => {
  for (const count of [0, 3]) {
    const state = connectionPresentation(DeviceConnectionPhase.INITIALIZING, { completed: count, total: count, phase: 'checking' });
    assert.equal(state.stage, 1); assert.equal(state.detail, 'checking');
  }
  assert.equal(connectionPresentation(DeviceConnectionPhase.INITIALIZING, { completed: 0, total: 0, phase: 'complete' }).stage, 2);
});
