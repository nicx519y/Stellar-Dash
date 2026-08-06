const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const Module = require('node:module');

// The production adapter uses the same @/ aliases as Next.js. Keep this
// contract test dependency-free by resolving those aliases inside this test
// process before loading the adapter.
const resolveFilename = Module._resolveFilename;
const loadModule = Module._load;
Module._resolveFilename = function resolveTestAlias(request, parent, isMain, options) {
  let resolvedRequest = request;
  if (request.startsWith('@/')) {
    const base = path.resolve(__dirname, '..', request.slice(2));
    resolvedRequest = [
      `${base}.ts`,
      `${base}.tsx`,
      `${base}.js`,
      path.join(base, 'index.ts'),
      path.join(base, 'index.tsx'),
      path.join(base, 'index.js'),
      base,
    ].find((candidate) => {
      try {
        return fs.statSync(candidate).isFile();
      } catch {
        return false;
      }
    }) ?? base;
  }
  return resolveFilename.call(this, resolvedRequest, parent, isMain, options);
};
Module._load = function loadTestDependency(request, parent, isMain) {
  if (request === '@/lib/websocket-queue-manager') {
    return {
      WebSocketQueueManager: class TestQueueManager {
        setSendFunction() {}
        clear() {}
      },
    };
  }
  return loadModule.call(this, request, parent, isMain);
};

const { MockDeviceTransport } = require('../lib/device-transport/mock-device-transport.ts');
const { DeviceTransportFrameworkAdapter } = require('../lib/device-transport/framework-adapter.ts');
const {
  DEFAULT_DEVICE_SCOPES,
  DeviceTransportError,
} = require('../lib/device-transport/types.ts');
const { WebHidTransport } = require('../lib/device-transport/webhid-transport.ts');
const {
  DEFAULT_SCREEN_CONTROL_CONFIG,
  withRequiredWebConfigEntry,
} = require('../types/gamepad-config.ts');
Module._resolveFilename = resolveFilename;
Module._load = loadModule;

class MemoryStorage {
  constructor() {
    this.values = new Map();
  }

  getItem(key) {
    return this.values.get(key) ?? null;
  }

  setItem(key, value) {
    this.values.set(key, value);
  }
}

const delay = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));

test('screen policy always restores the required WebConfig recovery entry', () => {
  const disabled = {
    ...DEFAULT_SCREEN_CONTROL_CONFIG.features,
    webConfigEntry: false,
  };
  const normalized = withRequiredWebConfigEntry(disabled);

  assert.equal(disabled.webConfigEntry, false);
  assert.equal(normalized.webConfigEntry, true);
  assert.equal(
    normalized.connectionModeSwitch,
    DEFAULT_SCREEN_CONTROL_CONFIG.features.connectionModeSwitch,
  );
});

async function createTransport(options = { storage: null }) {
  const transport = new MockDeviceTransport(options);
  await transport.connect();
  return transport;
}

test('adapter connect failure reports once and always settles disconnected', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  transport.connect = async () => {
    throw new DeviceTransportError('authentication-failed', 'fixture authentication failed');
  };
  const adapter = new DeviceTransportFrameworkAdapter(transport);
  let errorCount = 0;
  adapter.onError(() => { errorCount += 1; });

  await assert.rejects(adapter.connect(), /fixture authentication failed/);
  assert.equal(adapter.getState(), 'disconnected');
  assert.equal(transport.session, null);
  assert.equal(errorCount, 1);
  adapter.dispose();
});

test('disconnect immediately cancels a connect before it can publish a session', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  let releaseConnect;
  transport.connect = () => new Promise((resolve) => {
    releaseConnect = () => {
      transport.session = {
        transport: 'mock',
        authenticated: true,
        scopes: [...DEFAULT_DEVICE_SCOPES],
      };
      resolve(transport.session);
    };
  });
  const adapter = new DeviceTransportFrameworkAdapter(transport);

  const connecting = adapter.connect();
  const cancelled = assert.rejects(connecting, /断开或重连/);
  adapter.disconnect();
  releaseConnect();
  await cancelled;

  assert.equal(adapter.getState(), 'disconnected');
  assert.equal(transport.session, null);
  adapter.dispose();
});

test('adapter fire-and-forget failure destroys the transport instead of leaving ERROR half-connected', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const auth = {
    clearCalls: 0,
    clear() { this.clearCalls += 1; },
  };
  const adapter = new DeviceTransportFrameworkAdapter(transport, auth);
  await adapter.connect();
  let errorCount = 0;
  let disconnectCount = 0;
  adapter.onError(() => { errorCount += 1; });
  adapter.onDisconnect(() => { disconnectCount += 1; });
  transport.request = async () => {
    throw new DeviceTransportError('protocol', 'fixture async failure');
  };

  adapter.sendMessageNoResponse('fixture_failure');
  await delay(10);

  assert.equal(adapter.getState(), 'disconnected');
  assert.equal(transport.session, null);
  assert.equal(errorCount, 1);
  assert.equal(disconnectCount, 1);
  assert.ok(auth.clearCalls >= 1);
  adapter.dispose();
});

test('an old fire-and-forget rejection cannot close a newer device session', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceTransportFrameworkAdapter(transport);
  await adapter.connect();
  let rejectOldRequest;
  transport.request = () => new Promise((resolve, reject) => {
    rejectOldRequest = reject;
  });
  let errorCount = 0;
  adapter.onError(() => { errorCount += 1; });

  adapter.sendMessageNoResponse('old_session_request');
  await delay(0);
  assert.equal(typeof rejectOldRequest, 'function');
  adapter.disconnect();
  await delay(0);
  await adapter.connect();
  rejectOldRequest(new DeviceTransportError('protocol', 'late old-session failure'));
  await delay(10);

  assert.equal(adapter.getState(), 'connected');
  assert.notEqual(transport.session, null);
  assert.equal(errorCount, 0);
  adapter.dispose();
});

test('a reconnect waits for the previous asynchronous HID close barrier', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceTransportFrameworkAdapter(transport);
  await adapter.connect();
  let releaseOldClose;
  transport.close = () => new Promise((resolve) => {
    releaseOldClose = resolve;
  });

  adapter.disconnect();
  let reconnectSettled = false;
  const reconnect = adapter.connect().then(() => {
    reconnectSettled = true;
  });
  await delay(0);
  assert.equal(reconnectSettled, false);
  releaseOldClose();
  await reconnect;

  assert.equal(adapter.getState(), 'connected');
  // Restore a real close so dispose does not leave a pending fixture promise.
  transport.close = MockDeviceTransport.prototype.close.bind(transport);
  adapter.dispose();
});

test('the close barrier waits for the physical WebHID handle before reopening it', async () => {
  let releasePhysicalClose;
  const physicalClose = new Promise((resolve) => {
    releasePhysicalClose = resolve;
  });
  let openCalls = 0;
  let getDevicesCalls = 0;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox delayed-close fixture',
    collections: [],
    async open() {
      openCalls += 1;
      this.opened = true;
    },
    async close() {
      await physicalClose;
      this.opened = false;
    },
    async sendReport() {},
    addEventListener() {},
    removeEventListener() {},
  };
  const hid = {
    async getDevices() {
      getDevicesCalls += 1;
      return [device];
    },
    async requestDevice() { return [device]; },
    addEventListener() {},
    removeEventListener() {},
  };
  const transport = new WebHidTransport({ navigator: hid });
  const auth = { clear() {}, async authenticate() {} };
  const adapter = new DeviceTransportFrameworkAdapter(transport, auth);
  await adapter.connect();
  assert.equal(openCalls, 1);

  adapter.disconnect();
  const reconnect = adapter.connect();
  await delay(0);
  assert.equal(getDevicesCalls, 1);
  assert.equal(openCalls, 1);

  releasePhysicalClose();
  await reconnect;
  assert.equal(getDevicesCalls, 2);
  assert.equal(openCalls, 2);
  assert.equal(device.opened, true);
  adapter.dispose();
});

test('disconnect aborts scope reauthorization and reconnect waits for it to settle', async () => {
  let getDevicesCalls = 0;
  let openCalls = 0;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox scope-upgrade fixture',
    collections: [],
    async open() {
      openCalls += 1;
      this.opened = true;
    },
    async close() { this.opened = false; },
    async sendReport() {},
    addEventListener() {},
    removeEventListener() {},
  };
  const hid = {
    async getDevices() {
      getDevicesCalls += 1;
      return [device];
    },
    async requestDevice() { return [device]; },
    addEventListener() {},
    removeEventListener() {},
  };
  const transport = new WebHidTransport({ navigator: hid });
  let rejectUpgrade;
  let upgradeSignal;
  const auth = {
    grantedScopes: [],
    clear() { this.grantedScopes = []; },
    hasScopes(scopes) {
      return scopes.every((scope) => this.grantedScopes.includes(scope));
    },
    async authenticate(currentTransport, scopes, signal) {
      assert.equal(signal.aborted, false);
      this.grantedScopes = [...scopes];
      currentTransport.session = {
        transport: 'webhid',
        authenticated: true,
        scopes: [...scopes],
        sessionId: `session-${openCalls}`,
      };
      return currentTransport.session;
    },
    reauthorize(_currentTransport, _scopes, signal) {
      upgradeSignal = signal;
      return new Promise((_resolve, reject) => {
        rejectUpgrade = reject;
      });
    },
  };
  const adapter = new DeviceTransportFrameworkAdapter(transport, auth);
  await adapter.connect();
  assert.deepEqual(auth.grantedScopes, DEFAULT_DEVICE_SCOPES);
  let transportRequestCalls = 0;
  let authorizedFetchCalls = 0;
  transport.request = async () => {
    transportRequestCalls += 1;
    return { transactionId: 1, data: {} };
  };
  transport.authorizedFetch = async () => {
    authorizedFetchCalls += 1;
    return new Response('{}', { status: 200 });
  };

  const oldRequest = adapter.sendMessage('reboot');
  const oldRequestRejection = assert.rejects(oldRequest, /scope upgrade cancelled/);
  await delay(0);
  assert.equal(upgradeSignal.aborted, false);
  const queuedReadRejection = assert.rejects(
    adapter.sendMessage('get_global_config'),
    /scope upgrade cancelled/,
  );
  const queuedFetchRejection = assert.rejects(
    adapter.authorizedFetch('/api/queued-during-scope-upgrade'),
    /scope upgrade cancelled/,
  );
  adapter.sendBinaryMessage(new Uint8Array([0x34]));
  await delay(0);
  assert.equal(transportRequestCalls, 0);
  assert.equal(authorizedFetchCalls, 0);

  adapter.disconnect();
  assert.equal(upgradeSignal.aborted, true);
  let reconnectSettled = false;
  const reconnect = adapter.connect().then(() => {
    reconnectSettled = true;
  });
  await delay(0);
  assert.equal(reconnectSettled, false);
  assert.equal(getDevicesCalls, 1);
  assert.equal(openCalls, 1);

  rejectUpgrade(new DeviceTransportError('disconnected', 'scope upgrade cancelled'));
  await oldRequestRejection;
  await queuedReadRejection;
  await queuedFetchRejection;
  await reconnect;
  assert.equal(getDevicesCalls, 2);
  assert.equal(openCalls, 2);
  assert.equal(adapter.getState(), 'connected');
  assert.deepEqual(transport.session.scopes, DEFAULT_DEVICE_SCOPES);
  adapter.dispose();
});

test('a physical WebHID disconnect aborts scope upgrade before reconnecting', async () => {
  let navigatorDisconnect = null;
  let getDevicesCalls = 0;
  let openCalls = 0;
  let releasePhysicalClose;
  const physicalClose = new Promise((resolve) => {
    releasePhysicalClose = resolve;
  });
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox physical-disconnect fixture',
    collections: [],
    async open() {
      openCalls += 1;
      this.opened = true;
    },
    async close() {
      await physicalClose;
      this.opened = false;
    },
    async sendReport() {},
    addEventListener() {},
    removeEventListener() {},
  };
  const hid = {
    async getDevices() {
      getDevicesCalls += 1;
      return [device];
    },
    async requestDevice() { return [device]; },
    addEventListener(type, handler) {
      if (type === 'disconnect') navigatorDisconnect = handler;
    },
    removeEventListener(type, handler) {
      if (type === 'disconnect' && navigatorDisconnect === handler) {
        navigatorDisconnect = null;
      }
    },
  };
  const transport = new WebHidTransport({ navigator: hid });
  let rejectUpgrade;
  let upgradeSignal;
  const auth = {
    grantedScopes: [],
    clear() { this.grantedScopes = []; },
    hasScopes(scopes) {
      return scopes.every((scope) => this.grantedScopes.includes(scope));
    },
    async authenticate(currentTransport, scopes, signal) {
      assert.equal(signal.aborted, false);
      this.grantedScopes = [...scopes];
      currentTransport.session = {
        transport: 'webhid',
        authenticated: true,
        scopes: [...scopes],
        sessionId: `physical-session-${openCalls}`,
      };
      return currentTransport.session;
    },
    reauthorize(_currentTransport, _scopes, signal) {
      upgradeSignal = signal;
      return new Promise((_resolve, reject) => {
        rejectUpgrade = reject;
      });
    },
  };
  const adapter = new DeviceTransportFrameworkAdapter(transport, auth);
  await adapter.connect();
  const oldRequest = adapter.sendMessage('reboot');
  const oldRequestRejection = assert.rejects(oldRequest, /physical scope cancelled/);
  await delay(0);
  const fireDisconnect = navigatorDisconnect;
  assert.equal(typeof fireDisconnect, 'function');

  fireDisconnect({ device });
  assert.equal(upgradeSignal.aborted, true);
  let reconnectSettled = false;
  const reconnect = adapter.connect().then(() => {
    reconnectSettled = true;
  });
  releasePhysicalClose();
  await delay(0);
  assert.equal(reconnectSettled, false);
  assert.equal(getDevicesCalls, 1);

  rejectUpgrade(new DeviceTransportError('disconnected', 'physical scope cancelled'));
  await oldRequestRejection;
  await reconnect;
  assert.equal(getDevicesCalls, 2);
  assert.equal(openCalls, 2);
  assert.equal(adapter.getState(), 'connected');
  assert.deepEqual(transport.session.scopes, DEFAULT_DEVICE_SCOPES);
  adapter.dispose();
});

test('an old WebHID export cannot emit or continue in a reconnected session', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  // Exercise the WebHID-only sequential export path without coupling this
  // lifecycle test to the cryptographic authentication fixture.
  transport.kind = 'webhid';
  const adapter = new DeviceTransportFrameworkAdapter(transport);
  await adapter.connect();
  const calls = [];
  let resolveGlobal;
  transport.request = (command) => {
    calls.push(command);
    if (command !== 'get_global_config') {
      throw new Error(`old export continued with ${command}`);
    }
    return new Promise((resolve) => {
      resolveGlobal = resolve;
    });
  };
  const messages = [];
  let errorCount = 0;
  adapter.onMessage((message) => messages.push(message));
  adapter.onError(() => { errorCount += 1; });

  adapter.sendMessageNoResponse('export_all_config');
  await delay(0);
  assert.equal(typeof resolveGlobal, 'function');
  adapter.disconnect();
  await adapter.connect();
  resolveGlobal({
    transactionId: 1,
    data: { globalConfig: { inputMode: 'XINPUT' } },
  });
  await delay(10);

  assert.deepEqual(calls, ['get_global_config']);
  assert.deepEqual(messages, []);
  assert.equal(errorCount, 0);
  assert.equal(adapter.getState(), 'connected');
  assert.notEqual(transport.session, null);
  adapter.dispose();
});

test('an authorized HTTP response from an old session is rejected after reconnect', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceTransportFrameworkAdapter(transport);
  await adapter.connect();
  let resolveFetch;
  transport.authorizedFetch = () => new Promise((resolve) => {
    resolveFetch = resolve;
  });

  const oldFetch = adapter.authorizedFetch('/api/old-session');
  const oldFetchRejection = assert.rejects(oldFetch, /断开或重连会话替代/);
  await delay(0);
  assert.equal(typeof resolveFetch, 'function');
  adapter.disconnect();
  await adapter.connect();
  resolveFetch(new Response('{}', { status: 200 }));
  await oldFetchRejection;

  assert.equal(adapter.getState(), 'connected');
  assert.notEqual(transport.session, null);
  adapter.dispose();
});

test('disconnect aborts an authorized response body with the merged session signal', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceTransportFrameworkAdapter(transport);
  await adapter.connect();
  const callerController = new AbortController();
  let fetchSignal;
  transport.authorizedFetch = async (_input, init) => {
    fetchSignal = init.signal;
    const body = new ReadableStream({
      start(controller) {
        fetchSignal.addEventListener('abort', () => {
          controller.error(new DOMException('session body aborted', 'AbortError'));
        }, { once: true });
      },
    });
    return new Response(body, { status: 200 });
  };

  const response = await adapter.authorizedFetch(
    '/api/streaming-old-session',
    { signal: callerController.signal },
  );
  assert.notEqual(fetchSignal, callerController.signal);
  assert.equal(fetchSignal.aborted, false);
  const bodyRead = response.text();
  const bodyAborted = assert.rejects(bodyRead, (error) => error.name === 'AbortError');

  adapter.disconnect();
  assert.equal(fetchSignal.aborted, true);
  assert.equal(callerController.signal.aborted, false);
  await bodyAborted;
  adapter.dispose();
});

function waitForBinary(transport, command) {
  return new Promise((resolve) => {
    const unsubscribe = transport.subscribe('legacy.binary', (event) => {
      if (!event.binary || new DataView(event.binary).getUint8(0) !== command) return;
      unsubscribe();
      resolve(event.binary);
    });
  });
}

async function sendBinary(transport, bytes, responseCommand) {
  const response = waitForBinary(transport, responseCommand);
  await transport.upload('legacy-binary', bytes);
  return response;
}

function encodeMacro(triggerKeys, steps) {
  const bytes = new Uint8Array(1 + triggerKeys.length + 1 + steps.length * 10);
  const view = new DataView(bytes.buffer);
  let offset = 0;
  bytes[offset++] = triggerKeys.length;
  triggerKeys.forEach((key) => { bytes[offset++] = key; });
  bytes[offset++] = steps.length;
  steps.forEach(([timeMs, buttonMask, dynamicMask]) => {
    view.setUint16(offset, timeMs, true);
    offset += 2;
    view.setUint32(offset, buttonMask, true);
    offset += 4;
    view.setUint32(offset, dynamicMask, true);
    offset += 4;
  });
  return Buffer.from(bytes).toString('base64');
}

test('connects and serves a complete V2 fixture without auto calibration', async () => {
  const transport = await createTransport();

  assert.equal(transport.session.transport, 'mock');
  assert.equal(transport.session.authenticated, true);
  assert.equal(transport.state, 'connected');

  const global = await transport.request('get_global_config');
  assert.equal(global.data.globalConfig.hardware.hardwareVersion, '2.0.0');
  assert.equal(global.data.globalConfig.autoCalibrationEnabled, false);

  const profiles = await transport.request('get_profile_list');
  assert.equal(profiles.data.profileList.items.length, 2);
  assert.equal(profiles.data.defaultProfileDetails.name, 'Arcade');

  const layout = await transport.request('get_hitbox_layout');
  assert.equal(layout.data.length, 22);
  await transport.close();
});

test('persists configuration in the injected tab storage and isolates new tabs', async () => {
  const storage = new MemoryStorage();
  const first = await createTransport({ storage });
  await first.request('update_global_config', {
    globalConfig: {
      wirelessReportRate: '4K',
      power: { wakeHoldMs: 3000, autoStandbyMs: 10000 },
    },
  });
  await first.request('update_profile', {
    profileId: 'profile-arcade',
    profileDetails: { name: 'Edited Offline' },
  });
  await first.close();

  const refreshed = await createTransport({ storage });
  const global = await refreshed.request('get_global_config');
  const profile = await refreshed.request('get_default_profile');
  assert.equal(global.data.globalConfig.wirelessReportRate, '4K');
  assert.equal(global.data.globalConfig.power.autoStandbyMs, 10000);
  assert.equal(profile.data.defaultProfileDetails.name, 'Edited Offline');

  const otherTab = await createTransport({ storage: new MemoryStorage() });
  const otherProfile = await otherTab.request('get_default_profile');
  assert.equal(otherProfile.data.defaultProfileDetails.name, 'Arcade');
  await refreshed.close();
  await otherTab.close();
});

test('supports Profile CRUD, hotkeys, screen settings and device logs', async () => {
  const transport = await createTransport();

  const created = await transport.request('create_profile', {
    profileName: 'Offline QA',
  });
  const createdProfile = created.data.profileList.items.find(
    (profile) => profile.name === 'Offline QA',
  );
  assert.ok(createdProfile);

  await transport.request('switch_default_profile', {
    profileId: createdProfile.id,
  });
  await transport.request('update_profile', {
    profileId: createdProfile.id,
    profileDetails: { name: 'Offline QA Edited' },
  });
  let profiles = await transport.request('get_profile_list');
  assert.equal(profiles.data.profileList.defaultId, createdProfile.id);
  assert.equal(
    profiles.data.profileList.items.find((profile) => profile.id === createdProfile.id).name,
    'Offline QA Edited',
  );

  const initialHotkeys = await transport.request('get_hotkeys_config');
  const nextHotkeys = initialHotkeys.data.hotkeysConfig.slice(0, 2);
  await transport.request('update_hotkeys_config', {
    hotkeysConfig: nextHotkeys,
  });
  const hotkeys = await transport.request('get_hotkeys_config');
  assert.deepEqual(hotkeys.data.hotkeysConfig, nextHotkeys);

  await transport.request('update_screen_control_config', {
    screenControl: { brightness: 33, currentPageId: 7 },
  });
  const screen = await transport.request('get_screen_control_config');
  assert.equal(screen.data.screenControl.brightness, 33);
  assert.equal(screen.data.screenControl.currentPageId, 7);

  const logs = await transport.request('get_device_logs_list');
  assert.ok(logs.data.items.length > 0);
  assert.match(logs.data.items[0], /\[MOCK\]/);

  await transport.request('delete_profile', {
    profileId: createdProfile.id,
  });
  profiles = await transport.request('get_profile_list');
  assert.equal(
    profiles.data.profileList.items.some((profile) => profile.id === createdProfile.id),
    false,
  );
  assert.notEqual(profiles.data.profileList.defaultId, createdProfile.id);
  await transport.close();
});

test('uses {k,s} profile macro slots and Base64 for the single-macro API', async () => {
  const transport = await createTransport();
  const initial = await transport.request('get_profile_macros', { pid: 'profile-arcade' });
  assert.equal(initial.data.m.length, 5);
  assert.deepEqual(initial.data.m[0].k, [18, 19]);
  assert.equal('t' in initial.data.m[0], false);

  // This is the exact shape sent by updateProfileDetails. JSON transports omit
  // the undefined macros field, so the mock must preserve the existing macro.
  await transport.request('update_profile', {
    profileId: 'profile-arcade',
    profileDetails: {
      keysConfig: {
        invertXAxis: true,
        macros: undefined,
      },
    },
  });
  const afterProfileSave = await transport.request('get_profile_macros', {
    pid: 'profile-arcade',
  });
  assert.deepEqual(afterProfileSave.data.m[0], initial.data.m[0]);

  const wire = [
    null,
    { k: [4, 5], s: [[25, 0x1234, 0x80000000]] },
    null,
    null,
    null,
  ];
  const updated = await transport.request('update_profile_macros', {
    pid: 'profile-arcade',
    m: wire,
  });
  assert.deepEqual(updated.data.m, wire);

  const encoded = encodeMacro([7], [[40, 0x10, 0x20]]);
  const singleUpdate = await transport.request('update_macro', {
    profileId: 'profile-arcade',
    macro: { index: 2, data: encoded },
  });
  assert.deepEqual(singleUpdate.data.macro, { index: 2, data: encoded });
  const singleRead = await transport.request('get_macro', {
    profileId: 'profile-arcade',
    index: 2,
  });
  assert.deepEqual(singleRead.data.macro, { index: 2, data: encoded });
  await transport.close();
});

test('stages imports and applies them atomically only on finish', async () => {
  const transport = await createTransport();
  await transport.request('import_config_part', {
    section: 'global',
    data: { wirelessReportRate: '2K' },
  });
  let global = await transport.request('get_global_config');
  assert.equal(global.data.globalConfig.wirelessReportRate, '8K');

  await transport.request('import_config_finish');
  global = await transport.request('get_global_config');
  assert.equal(global.data.globalConfig.wirelessReportRate, '2K');

  await transport.request('import_config_part', {
    section: 'global',
    data: { wirelessReportRate: '1K' },
  });
  await transport.request('import_config_part', {
    section: 'hotkeys',
    data: { invalid: true },
  });
  await assert.rejects(() => transport.request('import_config_finish'), /hotkeys must be an array/);
  global = await transport.request('get_global_config');
  assert.equal(global.data.globalConfig.wirelessReportRate, '2K');
  await transport.close();
});

test('exports and atomically restores the default profile selection', async () => {
  const transport = await createTransport();
  await transport.request('switch_default_profile', {
    profileId: 'profile-tournament',
  });

  let exportedGlobal;
  const complete = new Promise((resolve) => {
    const unsubscribe = transport.subscribe('export_all_config', (event) => {
      if (event.data.section === 'global') exportedGlobal = event.data.data;
      if (event.data.section === 'end') {
        unsubscribe();
        resolve();
      }
    });
  });
  await transport.request('export_all_config');
  await complete;
  assert.equal(exportedGlobal.defaultProfileId, 'profile-tournament');

  await transport.request('switch_default_profile', {
    profileId: 'profile-arcade',
  });
  await transport.request('import_config_part', {
    section: 'global',
    data: exportedGlobal,
  });
  let profiles = await transport.request('get_profile_list');
  assert.equal(profiles.data.profileList.defaultId, 'profile-arcade');

  await transport.request('import_config_finish');
  profiles = await transport.request('get_profile_list');
  assert.equal(profiles.data.profileList.defaultId, 'profile-tournament');
  const global = await transport.request('get_global_config');
  assert.equal(global.data.globalConfig.defaultProfileId, 'profile-tournament');
  await transport.close();
});

test('keeps normal button monitoring silent and emits performance frames explicitly', async () => {
  const transport = await createTransport();
  const commands = [];
  const unsubscribe = transport.subscribe('legacy.binary', (event) => {
    commands.push(new DataView(event.binary).getUint8(0));
  });

  await transport.request('start_button_monitoring');
  await delay(220);
  assert.deepEqual(commands, []);
  const states = await transport.request('get_button_states');
  assert.equal(states.data.triggerMask, 0);

  await transport.request('start_button_performance_monitoring');
  await delay(140);
  await transport.request('stop_button_performance_monitoring');
  assert.ok(commands.includes(2));
  unsubscribe();
  await transport.close();
});

test('emits uncalibrated, top, bottom and completed calibration states', async () => {
  const transport = await createTransport();
  const phases = [];
  const complete = new Promise((resolve) => {
    const unsubscribe = transport.subscribe('calibration_update', (event) => {
      const status = event.data.calibrationStatus;
      phases.push(status.buttons[0].phase);
      if (status.allCalibrated) {
        unsubscribe();
        resolve();
      }
    });
  });

  const started = await transport.request('start_manual_calibration');
  assert.equal(started.data.calibrationStatus.allCalibrated, false);
  // Match React development Strict Mode's immediate effect cleanup. It must
  // not cancel the newly mounted calibration view's deterministic sequence.
  await transport.request('stop_manual_calibration');
  await Promise.race([
    complete,
    delay(1000).then(() => { throw new Error('calibration events timed out'); }),
  ]);
  assert.deepEqual(phases, ['IDLE', 'TOP_SAMPLING', 'BOTTOM_SAMPLING', 'COMPLETED']);
  const completed = await transport.request('check_is_manual_calibration_completed');
  assert.equal(completed.data.isCompleted, true);
  await transport.close();
});

test('samples ADC mapping without shifting the first point and writes it back', async () => {
  const transport = await createTransport();
  const started = await transport.request('ms_mark_mapping_start', { id: 'mapping-default' });
  const length = started.data.status.length;
  let result;
  for (let index = 0; index < length; index += 1) {
    result = await transport.request('ms_mark_mapping_step');
  }

  assert.equal(result.data.status.is_completed, true);
  assert.equal(result.data.status.values.length, length);
  assert.equal(result.data.status.values[0], 4000);
  assert.equal(result.data.status.values.at(-1), 800);
  const mapping = await transport.request('ms_get_mapping', { id: 'mapping-default' });
  assert.deepEqual(mapping.data.mapping.originalValues, result.data.status.values);
  assert.equal(mapping.data.mapping.calibratedValues.at(-1), (length - 1) * 100);
  await transport.close();
});

test('uploads, reports, reads and deletes an in-memory background image', async () => {
  const transport = await createTransport();
  const cid = 0x10203040;
  const pixels = Uint8Array.from([1, 2, 3, 4, 5, 6, 7, 8]);

  const begin = new Uint8Array(18);
  let view = new DataView(begin.buffer);
  view.setUint8(0, 0x30);
  view.setUint32(2, cid, true);
  view.setUint16(6, 2, true);
  view.setUint16(8, 2, true);
  view.setUint32(10, pixels.length, true);
  view.setUint8(14, 1);
  const beginResponse = new DataView(await sendBinary(transport, begin, 0xb0));
  assert.equal(beginResponse.getUint8(1), 1);

  const chunk = new Uint8Array(14 + pixels.length);
  view = new DataView(chunk.buffer);
  view.setUint8(0, 0x31);
  view.setUint32(2, cid, true);
  view.setUint16(10, pixels.length, true);
  chunk.set(pixels, 14);
  const chunkResponse = new DataView(await sendBinary(transport, chunk, 0xb1));
  assert.equal(chunkResponse.getUint32(6, true), pixels.length);

  const commit = new Uint8Array(6);
  view = new DataView(commit.buffer);
  view.setUint8(0, 0x32);
  view.setUint32(2, cid, true);
  const commitResponse = new DataView(await sendBinary(transport, commit, 0xb2));
  assert.equal(commitResponse.getUint8(1), 1);

  const info = new Uint8Array(6);
  view = new DataView(info.buffer);
  view.setUint8(0, 0x34);
  view.setUint32(2, cid, true);
  const infoResponse = new DataView(await sendBinary(transport, info, 0xb4));
  assert.equal(infoResponse.getUint8(6), 1);
  assert.equal(infoResponse.getUint8(7), 1);
  assert.equal(infoResponse.getUint32(12, true), pixels.length);

  const read = new Uint8Array(14);
  view = new DataView(read.buffer);
  view.setUint8(0, 0x35);
  view.setUint8(1, 0);
  view.setUint32(2, cid, true);
  view.setUint16(10, pixels.length, true);
  const readBuffer = await sendBinary(transport, read, 0xb5);
  const readView = new DataView(readBuffer);
  assert.equal(readView.getUint8(1), 1);
  assert.equal(readView.getUint32(4, true), cid);
  assert.equal(readView.getUint16(20, true), pixels.length);
  assert.deepEqual(
    Array.from(new Uint8Array(readBuffer, 55, pixels.length)),
    Array.from(pixels),
  );

  const remove = new Uint8Array(6);
  view = new DataView(remove.buffer);
  view.setUint8(0, 0x33);
  view.setUint32(2, cid, true);
  await sendBinary(transport, remove, 0xb3);
  const deletedInfo = new DataView(await sendBinary(transport, info, 0xb4));
  assert.equal(deletedInfo.getUint8(6), 0);
  await transport.close();
});

test('keeps firmware checks offline and returns a simulated reboot success', async () => {
  const transport = await createTransport();
  const response = await transport.authorizedFetch(
    'https://firmware.example/api/firmware-check-update',
  );
  const body = await response.json();
  assert.equal(response.status, 200);
  assert.equal(body.data.updateAvailable, false);
  assert.equal(body.data.currentVersion, '2.0.0-mock');

  const reboot = await transport.request('reboot');
  assert.equal(reboot.data.success, true);
  assert.equal(transport.state, 'connected');
  await transport.close();
});

test('serves firmware metadata, upgrade sessions and the binary chunk ACK', async () => {
  const transport = await createTransport();
  const metadata = await transport.request('get_firmware_metadata');
  assert.equal(metadata.data.version, '2.0.0-mock');
  assert.ok(metadata.data.components.some((component) => component.name === 'application'));

  const created = await transport.request('create_firmware_upgrade_session', {
    session_id: 'mock-upgrade-contract',
    manifest: {},
  });
  assert.equal(created.data.session_id, 'mock-upgrade-contract');
  const chunk = await transport.request('upload_firmware_chunk', {
    session_id: created.data.session_id,
    chunk_size: 4096,
  });
  assert.equal(chunk.data.success, true);
  const completed = await transport.request('complete_firmware_upgrade_session', {
    session_id: created.data.session_id,
  });
  assert.equal(completed.data.status, 'completed');

  await transport.request('create_firmware_upgrade_session', {
    session_id: 'mock-upgrade-abort',
    manifest: {},
  });
  const aborted = await transport.request('abort_firmware_upgrade_session', {
    session_id: 'mock-upgrade-abort',
  });
  assert.equal(aborted.data.status, 'aborted');

  const binaryChunk = new Uint8Array(62);
  const binaryView = new DataView(binaryChunk.buffer);
  binaryView.setUint8(0, 0x01);
  binaryView.setUint32(54, 2, true);
  binaryView.setUint32(58, 4, true);
  const ack = new DataView(await sendBinary(transport, binaryChunk, 0x81));
  assert.equal(ack.getUint8(1), 1);
  assert.equal(ack.getUint32(2, true), 2);
  assert.equal(ack.getUint32(6, true), 75);
  await transport.close();
});

test('emits a complete ordered configuration export stream', async () => {
  const transport = await createTransport();
  const sections = [];
  const complete = new Promise((resolve) => {
    const unsubscribe = transport.subscribe('export_all_config', (event) => {
      sections.push(event.data.section);
      if (event.data.section === 'end') {
        unsubscribe();
        resolve();
      }
    });
  });

  await transport.request('export_all_config');
  await complete;
  assert.deepEqual(sections, [
    'global',
    'hotkeys',
    'screenControl',
    'profile',
    'profile',
    'end',
  ]);
  await transport.close();
});

test('adapter delivers legacy binary once without duplicating it as JSON', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceTransportFrameworkAdapter(transport);
  await adapter.connect();

  let jsonMessages = 0;
  let binaryMessages = 0;
  const unsubscribeMessage = adapter.onMessage(() => {
    jsonMessages += 1;
  });
  const binaryReceived = new Promise((resolve) => {
    const unsubscribeBinary = adapter.onBinaryMessage(() => {
      binaryMessages += 1;
      unsubscribeBinary();
      resolve();
    });
  });

  const request = new ArrayBuffer(6);
  const requestView = new DataView(request);
  requestView.setUint8(0, 0x34);
  requestView.setUint32(2, 0x12345678, true);
  adapter.sendBinaryMessage(request);

  await binaryReceived;
  await Promise.resolve();
  unsubscribeMessage();
  adapter.dispose();

  assert.equal(binaryMessages, 1);
  assert.equal(jsonMessages, 0);
});
