// Match Next.js CommonJS default import interop in the Sucrase host harness.
require('jszip').default = require('jszip');
const { capability } = require('./webhid-v2-fixture.cjs');
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
const { MockDeviceTransport } = require('../lib/device-transport/mock-device-transport.ts');
const { DeviceCommandClient } = require('../lib/device-transport/device-command-client.ts');
const { ImageTransferError } = require('../lib/device-transport/image-transfer-error.ts');
const { galleryErrorMessage } = require('../lib/gallery-error-message.ts');
const { GalleryApiError } = require('../lib/image-gallery.ts');
const { crc32 } = require('../lib/crc32.ts');
const { gifFrameTimelineUs, selectGifFrameIndices } = require('../lib/screen-control-image.ts');
const {
  clearDeviceImagePreviewMemory,
  loadDeviceImagePreview,
  saveDeviceImagePreview,
} = require('../lib/device-image-preview-cache.ts');
const {
  DEFAULT_DEVICE_SCOPES,
  DeviceTransportError,
} = require('../lib/device-transport/types.ts');
const {
  RecoverableBootstrapResponseTimeoutError,
  WebHidTransport,
} = require('../lib/device-transport/webhid-transport.ts');
const {
  DeviceConnectionPhase,
  reconnectRequiresPermission,
} = require('../lib/device-transport/device-command-types.ts');
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

  removeItem(key) {
    this.values.delete(key);
  }
}

const delay = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));

test('device preview keeps server source Blob URLs in memory and never stores image bytes in localStorage', () => {
  const previousStorage = globalThis.localStorage;
  const storage = new MemoryStorage();
  globalThis.localStorage = storage;
  try {
    const preview = {
      fingerprint: '320:172:110080:1:0:1234',
      previewUrl: URL.createObjectURL(new Blob(['preview'], { type: 'image/png' })),
      galleryImageId: 'gallery-a',
    };
    saveDeviceImagePreview({ deviceId: 'device-a', sessionId: 'session-a' }, preview);
    assert.deepEqual(
      loadDeviceImagePreview({ deviceId: 'device-a', sessionId: 'new-session' }, preview.fingerprint),
      preview,
    );
    assert.equal(
      loadDeviceImagePreview({ deviceId: 'device-b', sessionId: 'session-b' }, preview.fingerprint),
      null,
    );
    assert.equal(
      loadDeviceImagePreview({ deviceId: 'device-a', sessionId: 'new-session' }, 'changed-crc'),
      null,
    );
    assert.equal(storage.values.size, 0, 'server source bytes must remain memory-only');

    const reloadedPreview = {
      ...preview,
      previewUrl: URL.createObjectURL(new Blob(['reload'], { type: 'image/png' })),
    };
    saveDeviceImagePreview({ deviceId: 'device-a', sessionId: 'new-session' }, reloadedPreview);
    clearDeviceImagePreviewMemory();
    assert.equal(
      loadDeviceImagePreview({ deviceId: 'device-a', sessionId: 'after-reload' }, preview.fingerprint),
      null,
      'a document reload must refetch the authenticated source from the server',
    );

    saveDeviceImagePreview(
      { deviceId: null, sessionId: 'volatile-session' },
      {
        ...preview,
        previewUrl: URL.createObjectURL(new Blob(['volatile'], { type: 'image/png' })),
        galleryImageId: null,
      },
    );
    assert.equal(storage.values.size, 0, 'unstable device identities must not persist previews');
  } finally {
    clearDeviceImagePreviewMemory();
    if (previousStorage === undefined) delete globalThis.localStorage;
    else globalThis.localStorage = previousStorage;
  }
});

test('reconnect chooser is reachable only for explicit permission failures', () => {
  const base = { type: 'connection', message: 'fixture', timestamp: new Date() };
  assert.equal(reconnectRequiresPermission({ ...base, transportCode: 'permission-required' }), true);
  assert.equal(reconnectRequiresPermission({ ...base, transportCode: 'permission-denied' }), true);
  assert.equal(reconnectRequiresPermission({ ...base, transportCode: 'disconnected' }), false);
  assert.equal(reconnectRequiresPermission(null), false);
});

test('one startup deadline spans connect through markReady and reports the active initialization stage', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(
    transport,
    null,
    DEFAULT_DEVICE_SCOPES,
    25,
  );
  const errors = [];
  adapter.onError((error) => errors.push(error));

  await adapter.connect();
  const deadline = adapter.getStartupDeadlineMs();
  assert.ok(deadline > Date.now());
  adapter.setInitializationStage('hotkeys');
  await delay(40);

  assert.equal(errors.length, 1);
  assert.equal(errors[0].transportCode, 'timeout');
  assert.equal(errors[0].phase, DeviceConnectionPhase.INITIALIZING);
  assert.match(errors[0].message, /initializing\/hotkeys/);
  assert.equal(adapter.getPhase(), DeviceConnectionPhase.ERROR);
  assert.equal(adapter.getStartupDeadlineMs(), null);
  adapter.dispose();
});

test('markReady cancels the shared startup deadline', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(
    transport,
    null,
    DEFAULT_DEVICE_SCOPES,
    20,
  );
  let errors = 0;
  adapter.onError(() => { errors += 1; });
  await adapter.connect();
  assert.equal(adapter.markReady(), true);
  await delay(35);
  assert.equal(adapter.getPhase(), DeviceConnectionPhase.READY);
  assert.equal(errors, 0);
  adapter.dispose();
});

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

test('new devices use the factory hotkeys shown on the latest PCB', async () => {
  const transport = await createTransport();
  const result = await transport.request('get_hotkeys_config');
  assert.deepEqual(result.data.hotkeysConfig, [
    { key: 20, action: 'WebConfigMode', isHold: true, isLocked: true },
    { key: 19, action: 'CalibrationMode', isHold: true, isLocked: true },
    { key: 13, action: 'LedsEffectStyleNext', isHold: false, isLocked: false },
    { key: 12, action: 'LedsEffectStylePrev', isHold: false, isLocked: false },
    { key: 10, action: 'LedsBrightnessUp', isHold: false, isLocked: false },
    { key: 9, action: 'LedsBrightnessDown', isHold: false, isLocked: false },
    { key: 17, action: 'AmbientLightEffectStyleNext', isHold: false, isLocked: false },
    { key: 16, action: 'AmbientLightEffectStylePrev', isHold: false, isLocked: false },
    { key: 15, action: 'AmbientLightBrightnessUp', isHold: false, isLocked: false },
    { key: 14, action: 'AmbientLightBrightnessDown', isHold: false, isLocked: false },
    { key: 11, action: 'LedsEnableSwitch', isHold: true, isLocked: false },
  ]);
  await transport.close();
});

test('factory profiles use the latest PCB key mapping in every slot and preserve custom bindings', async () => {
  const storage = new MemoryStorage();
  const expected = {
    DPAD_UP: [1, 8], DPAD_LEFT: [5], DPAD_RIGHT: [7], DPAD_DOWN: [6],
    B4: [13], B3: [10], B2: [12], B1: [9],
    L3: [0], R3: [2], L2: [16], R2: [14], L1: [17], R1: [15],
    S1: [19], S2: [18], A1: [20], A2: [],
  };
  const first = await createTransport({ storage });
  const list = (await first.request('get_profile_list')).data.profileList;
  assert.equal(list.items.length, 16);
  const checkDefaults = async (transport) => {
    for (const { id } of list.items) {
      const profile = (await transport.request('get_profile_details', { profileId: id })).data.profileDetails;
      assert.equal(profile.isCompetitionProfile, false);
      assert.deepEqual(profile.keysConfig.keyMapping, expected, id);
      assert.deepEqual(profile.keysConfig.keyCombinations, []);
      const macros = await transport.request('get_profile_macros', { pid: id });
      assert.deepEqual(macros.data.m, [null, null, null, null, null]);
    }
  };
  await checkDefaults(first);
  await first.close();
  const reopened = await createTransport({ storage });
  await checkDefaults(reopened);
  await reopened.request('update_profile', {
    profileId: list.items[7].id,
    profileDetails: { keysConfig: { keyMapping: { ...expected, B1: [3] } } },
  });
  await reopened.close();
  const custom = await createTransport({ storage });
  const profile = (await custom.request('get_profile_details', { profileId: list.items[7].id })).data.profileDetails;
  assert.deepEqual(profile.keysConfig.keyMapping, { ...expected, B1: [3] });
  await custom.close();
});

test('adapter connect failure reports once and always settles disconnected', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  transport.connect = async () => {
    throw new DeviceTransportError('authentication-failed', 'fixture authentication failed');
  };
  const adapter = new DeviceCommandClient(transport);
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
  const adapter = new DeviceCommandClient(transport);

  const connecting = adapter.connect();
  const cancelled = assert.rejects(connecting, /断开或重连/);
  adapter.disconnect();
  releaseConnect();
  await cancelled;

  assert.equal(adapter.getState(), 'disconnected');
  assert.equal(transport.session, null);
  adapter.dispose();
});

test('a typed request failure is surfaced to its caller without creating hidden pending work', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const auth = {
    clearCalls: 0,
    clear() { this.clearCalls += 1; },
  };
  const adapter = new DeviceCommandClient(transport, auth);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);
  transport.request = async () => {
    throw new DeviceTransportError('protocol', 'fixture async failure');
  };

  await assert.rejects(adapter.request('fixture_failure'), /fixture async failure/);

  assert.equal(adapter.getState(), 'connected');
  assert.notEqual(transport.session, null);
  assert.equal(auth.clearCalls, 0);
  adapter.dispose();
});

test('an old typed request rejection cannot close a newer device session', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);
  let rejectOldRequest;
  transport.request = () => new Promise((resolve, reject) => {
    rejectOldRequest = reject;
  });
  let errorCount = 0;
  adapter.onError(() => { errorCount += 1; });

  const oldRequest = adapter.request('old_session_request');
  const oldRequestRejection = assert.rejects(oldRequest, /late old-session failure/);
  await delay(0);
  assert.equal(typeof rejectOldRequest, 'function');
  adapter.disconnect();
  await delay(0);
  await adapter.connect();
  rejectOldRequest(new DeviceTransportError('protocol', 'late old-session failure'));
  await oldRequestRejection;

  assert.equal(adapter.getState(), 'connected');
  assert.notEqual(transport.session, null);
  assert.equal(errorCount, 0);
  adapter.dispose();
});

test('a reconnect waits for the previous asynchronous HID close barrier', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
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
    receiveFeatureReport: capability,
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
  const adapter = new DeviceCommandClient(transport, auth);
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

test('a stale encrypted device session retries on the same open HID handle', async () => {
  let openCalls = 0;
  let closeCalls = 0;
  let getDevicesCalls = 0;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    receiveFeatureReport: capability,
    productId: 0x4021,
    productName: 'HBox stale-session fixture',
    collections: [],
    async open() {
      openCalls += 1;
      this.opened = true;
    },
    async close() {
      closeCalls += 1;
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
  let authenticateCalls = 0;
  const auth = {
    clear() {},
    async authenticate(currentTransport, scopes) {
      authenticateCalls += 1;
      if (authenticateCalls === 1) {
        throw new RecoverableBootstrapResponseTimeoutError(
          'attestation.create',
          '命令 attestation.create 响应超时',
        );
      }
      currentTransport.session = {
        transport: 'webhid',
        authenticated: true,
        scopes: [...scopes],
        sessionId: 'resynchronized-session',
      };
      return currentTransport.session;
    },
  };
  const adapter = new DeviceCommandClient(transport, auth);
  let errorCount = 0;
  adapter.onError(() => { errorCount += 1; });

  await adapter.connect();

  assert.equal(authenticateCalls, 2);
  assert.equal(getDevicesCalls, 1);
  assert.equal(openCalls, 1);
  assert.equal(closeCalls, 0);
  assert.equal(device.opened, true);
  assert.equal(adapter.getState(), 'connected');
  assert.equal(errorCount, 0);
  adapter.dispose();
});

test('disconnect aborts scope reauthorization and reconnect waits for it to settle', async () => {
  let getDevicesCalls = 0;
  let openCalls = 0;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    receiveFeatureReport: capability,
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
  const adapter = new DeviceCommandClient(transport, auth);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);
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

  const oldRequest = adapter.request('reboot');
  const oldRequestRejection = assert.rejects(oldRequest, /scope upgrade cancelled/);
  await delay(0);
  assert.equal(upgradeSignal.aborted, false);
  const queuedReadRejection = assert.rejects(
    adapter.request('get_global_config'),
    /scope upgrade cancelled/,
  );
  const queuedFetchRejection = assert.rejects(
    adapter.authorizedFetch('/api/queued-during-scope-upgrade'),
    /scope upgrade cancelled/,
  );
  const binaryRejection = assert.rejects(
    adapter.getImageCatalog(),
    /scope upgrade cancelled/,
  );
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
  await binaryRejection;
  await reconnect;
  assert.equal(getDevicesCalls, 2);
  assert.equal(openCalls, 2);
  assert.equal(adapter.getState(), 'connected');
  assert.deepEqual(transport.session.scopes, DEFAULT_DEVICE_SCOPES);
  adapter.dispose();
});

test('scope upgrade drains active HID RPCs without aborting or physically closing them', async () => {
  let closeCalls = 0;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    receiveFeatureReport: capability,
    productId: 0x4021,
    productName: 'HBox scope serialization fixture',
    collections: [],
    async open() { this.opened = true; },
    async close() { closeCalls += 1; this.opened = false; },
    async sendReport() {},
    addEventListener() {},
    removeEventListener() {},
  };
  const hid = {
    async getDevices() { return [device]; },
    async requestDevice() { return [device]; },
    addEventListener() {},
    removeEventListener() {},
  };
  const transport = new WebHidTransport({ navigator: hid });
  const auth = {
    grantedScopes: [],
    clear() { this.grantedScopes = []; },
    hasScopes(scopes) {
      return scopes.every((scope) => this.grantedScopes.includes(scope));
    },
    async authenticate(currentTransport, scopes) {
      this.grantedScopes = [...scopes];
      currentTransport.session = {
        transport: 'webhid',
        authenticated: true,
        scopes: [...scopes],
        sessionId: 'serialized-base',
      };
      return currentTransport.session;
    },
    async reauthorize(currentTransport, scopes, signal) {
      assert.equal(signal.aborted, false);
      reauthorizeCalls += 1;
      this.grantedScopes = [...scopes];
      currentTransport.session = {
        transport: 'webhid',
        authenticated: true,
        scopes: [...scopes],
        sessionId: 'serialized-elevated',
      };
      return currentTransport.session;
    },
  };
  let resolveRead;
  let activeReadSignal;
  let reauthorizeCalls = 0;
  const calls = [];
  transport.request = (command, _params, options = {}) => {
    calls.push(command);
    if (command === 'get_global_config') {
      activeReadSignal = options.signal;
      return new Promise((resolve) => { resolveRead = resolve; });
    }
    return Promise.resolve({ transactionId: 2, data: { accepted: true } });
  };

  const adapter = new DeviceCommandClient(transport, auth);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);
  const read = adapter.request('get_global_config');
  await delay(0);
  const elevated = adapter.request('reboot');
  await delay(0);

  assert.equal(reauthorizeCalls, 0, 'session.end must wait for the active RPC');
  assert.equal(activeReadSignal.aborted, false, 'scope upgrade must not abort HID writes');
  assert.equal(closeCalls, 0);

  resolveRead({ transactionId: 1, data: { ready: true } });
  await read;
  await elevated;
  assert.equal(reauthorizeCalls, 1);
  assert.deepEqual(calls, ['get_global_config', 'reboot']);
  assert.equal(closeCalls, 0);
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
    receiveFeatureReport: capability,
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
  const adapter = new DeviceCommandClient(transport, auth);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);
  const oldRequest = adapter.request('reboot');
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

test('an old HID export cannot continue in a reconnected session', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  // Exercise the WebHID-only sequential export path without coupling this
  // lifecycle test to the cryptographic authentication fixture.
  transport.kind = 'webhid';
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);
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
  let errorCount = 0;
  adapter.onError(() => { errorCount += 1; });

  const oldExport = adapter.exportConfig();
  const oldExportRejection = assert.rejects(oldExport, /断开或重连会话替代/);
  await delay(0);
  assert.equal(typeof resolveGlobal, 'function');
  adapter.disconnect();
  await adapter.connect();
  resolveGlobal({
    transactionId: 1,
    data: { globalConfig: { inputMode: 'XINPUT' } },
  });
  await oldExportRejection;

  assert.deepEqual(calls, ['get_global_config']);
  assert.equal(errorCount, 0);
  assert.equal(adapter.getState(), 'connected');
  assert.notEqual(transport.session, null);
  adapter.dispose();
});

test('an authorized HTTP response from an old session is rejected after reconnect', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);
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
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);
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

async function sendBinary(transport, bytes, responseCommand) {
  const envelope = await transport.request('binary.exchange', {
    encoding: 'base64',
    data: Buffer.from(bytes).toString('base64'),
  });
  const responseBytes = Buffer.from(envelope.data.data, 'base64');
  const response = responseBytes.buffer.slice(
    responseBytes.byteOffset,
    responseBytes.byteOffset + responseBytes.byteLength,
  );
  assert.equal(new DataView(response).getUint8(0), responseCommand);
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
  assert.equal(profiles.data.profileList.items.length, 16);
  assert.equal(profiles.data.profileList.maxNumProfiles, 16);
  assert.deepEqual(profiles.data.profileList.items.map((profile) => profile.slotIndex), Array.from({ length: 16 }, (_, i) => i));
  assert.deepEqual(profiles.data.profileList.items.map((profile) => profile.name),
    Array.from({ length: 16 }, (_, i) => `Profile-${String(i + 1).padStart(2, '0')}`));
  assert.equal(profiles.data.defaultProfileDetails.name, 'Profile-01');

  const layout = await transport.request('get_hitbox_layout');
  assert.equal(layout.data.length, 22);
  await transport.close();
});

test('fixed slot compatibility rejects legacy, incomplete, and duplicate slot identities', () => {
  const { profileSlots } = require('../lib/profile-slots.ts');
  const fixed = { defaultId: 'a', maxNumProfiles: 2, items: [{ id: 'b', slotIndex: 1 }, { id: 'a', slotIndex: 0 }] };
  assert.equal(profileSlots(fixed).compatible, true);
  assert.deepEqual(profileSlots(fixed).slots.map((item) => item.id), ['a', 'b']);
  assert.equal(profileSlots({ ...fixed, items: [{ id: 'a' }] }).compatible, false);
  assert.equal(profileSlots({ ...fixed, items: [fixed.items[0]] }).compatible, false);
  assert.equal(profileSlots({ ...fixed, items: [{ id: 'a', slotIndex: 0 }, { id: 'b', slotIndex: 0 }] }).compatible, false);
  assert.equal(profileSlots({ ...fixed, items: [{ id: 'a', slotIndex: 0 }, { id: 'a', slotIndex: 1 }] }).compatible, false);
  assert.equal(profileSlots({ ...fixed, maxNumProfiles: 0 }).compatible, false);
});

test('legacy mock state gains permanent slots and non-current rename preserves selection', async () => {
  const storage = new MemoryStorage();
  const storageKey = 'fixed-slots-test';
  const first = await createTransport({ storage, storageKey });
  await first.request('update_profile', { profileId: 'profile-tournament', profileDetails: { name: 'KeepMe' } });
  await first.close();
  const old = JSON.parse(storage.getItem(storageKey));
  old.profiles = old.profiles.slice(0, 2).map(({ slotIndex, ...profile }) => profile);
  storage.setItem(storageKey, JSON.stringify(old));
  const reopened = await createTransport({ storage, storageKey });
  const response = await reopened.request('get_profile_list');
  const before = response.data.profileList;
  assert.equal(before.items.length, 16);
  assert.equal(before.items[1].name, 'KeepMe');
  await reopened.request('update_profile', { profileId: before.items[15].id, profileDetails: { name: 'SlotSixteen', slotIndex: 0 } });
  const renamed = (await reopened.request('get_profile_list')).data.profileList;
  assert.equal(renamed.defaultId, before.defaultId);
  assert.equal(renamed.items[15].name, 'SlotSixteen');
  assert.equal(renamed.items[15].slotIndex, 15);
  await reopened.request('import_config_begin', { replaceProfiles: true, strict: false });
  await reopened.request('import_config_part', { section: 'profile', data: { ...before.items[1], name: 'Imported' } });
  await reopened.request('import_config_finish');
  const imported = (await reopened.request('get_profile_list')).data.profileList;
  assert.deepEqual(imported.items.map((item) => item.id), before.items.map((item) => item.id));
  assert.equal(imported.items[1].name, 'Imported');
  assert.equal(imported.items[15].name, 'SlotSixteen');
  await reopened.close();
  const final = await createTransport({ storage, storageKey });
  assert.deepEqual((await final.request('get_profile_list')).data.profileList, imported);
  for (const profile of imported.items) {
    await final.request('switch_default_profile', { profileId: profile.id });
    assert.equal((await final.request('get_default_profile')).data.defaultProfileDetails.id, profile.id);
  }
  await final.close();
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
  await first.request('preview_screen_brightness', { brightness: 31 });
  const previewedScreen = await first.request('get_screen_control_config');
  assert.equal(previewedScreen.data.screenControl.brightness, 31);
  await first.close();

  const refreshed = await createTransport({ storage });
  const global = await refreshed.request('get_global_config');
  const profile = await refreshed.request('get_default_profile');
  const screen = await refreshed.request('get_screen_control_config');
  assert.equal(global.data.globalConfig.wirelessReportRate, '4K');
  assert.equal(global.data.globalConfig.power.autoStandbyMs, 10000);
  assert.equal(profile.data.defaultProfileDetails.name, 'Edited Offline');
  assert.equal(screen.data.screenControl.brightness, 72);

  const otherTab = await createTransport({ storage: new MemoryStorage() });
  const otherProfile = await otherTab.request('get_default_profile');
  assert.equal(otherProfile.data.defaultProfileDetails.name, 'Profile-01');
  await refreshed.close();
  await otherTab.close();
});

test('supports fixed profile selection and rename, hotkeys, screen settings and device logs', async () => {
  const transport = await createTransport();

  const initialProfiles = await transport.request('get_profile_list');
  const lastProfile = initialProfiles.data.profileList.items[15];
  assert.ok(lastProfile);

  await transport.request('switch_default_profile', {
    profileId: lastProfile.id,
  });
  await transport.request('update_profile', {
    profileId: lastProfile.id,
    profileDetails: { name: 'Offline QA Edited' },
  });
  let profiles = await transport.request('get_profile_list');
  assert.equal(profiles.data.profileList.defaultId, lastProfile.id);
  assert.equal(
    profiles.data.profileList.items.find((profile) => profile.id === lastProfile.id).name,
    'Offline QA Edited',
  );

  const initialHotkeys = await transport.request('get_hotkeys_config');
  const nextHotkeys = [
    { key: 0, action: 'None', isHold: false, isLocked: false },
    { key: 1, action: 'None', isHold: false, isLocked: false },
    { key: 18, action: 'LedsBrightnessDown', isHold: true, isLocked: true },
  ];
  await transport.request('update_hotkeys_config', {
    hotkeysConfig: nextHotkeys,
  });
  const hotkeys = await transport.request('get_hotkeys_config');
  assert.equal(hotkeys.data.hotkeysConfig.length, 11);
  assert.deepEqual(
    hotkeys.data.hotkeysConfig.slice(0, 2),
    initialHotkeys.data.hotkeysConfig.slice(0, 2),
  );
  assert.deepEqual(hotkeys.data.hotkeysConfig[2], {
    key: 18,
    action: 'LedsBrightnessDown',
    isHold: true,
    isLocked: false,
  });

  await transport.request('update_screen_control_config', {
    screenControl: { brightness: 33, currentPageId: 7, standbyTimeoutSeconds: 120 },
  });
  const screen = await transport.request('get_screen_control_config');
  assert.equal(screen.data.screenControl.brightness, 33);
  assert.equal(screen.data.screenControl.currentPageId, 13);
  assert.equal(screen.data.screenControl.standbyTimeoutSeconds, 120);
  await assert.rejects(
    transport.request('update_screen_control_config', { screenControl: { standbyTimeoutSeconds: 15 } }),
    /Invalid standby timeout/,
  );

  const logs = await transport.request('get_device_logs_list');
  assert.ok(logs.data.items.length > 0);
  assert.match(logs.data.items[0], /\[MOCK\]/);

  await assert.rejects(transport.request('create_profile', { profileName: 'Extra' }), /Fixed profile slots/);
  await assert.rejects(transport.request('delete_profile', { profileId: lastProfile.id }), /Fixed profile slots/);
  profiles = await transport.request('get_profile_list');
  assert.equal(
    profiles.data.profileList.items.some((profile) => profile.id === lastProfile.id),
    true,
  );
  assert.equal(profiles.data.profileList.defaultId, lastProfile.id);
  assert.deepEqual(profiles.data.profileList.items.map((profile) => profile.id), initialProfiles.data.profileList.items.map((profile) => profile.id));
  await transport.close();
});

test('uses {k,s} profile macro slots and Base64 for the single-macro API', async () => {
  const transport = await createTransport();
  // Seed the macro used by this API test; factory profiles have no macros.
  await transport.request('update_profile_macros', {
    pid: 'profile-arcade',
    m: [{ k: [18, 19], s: [[0, 1 << 10, 0], [80, 0, 0]] }, null, null, null, null],
  });
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

test('emits typed button state and performance sample events', async () => {
  const transport = await createTransport();
  const buttonStates = [];
  let performanceSamples = 0;
  const unsubscribeButtons = transport.subscribe('button.state', (event) => {
    buttonStates.push(event.data);
  });
  const unsubscribePerformance = transport.subscribe('performance.sample', (event) => {
    assert.equal(event.data.byteLength, 44);
    performanceSamples += 1;
  });

  await transport.request('start_button_monitoring');
  await delay(0);
  assert.deepEqual(buttonStates.at(-1), {
    isActive: true,
    triggerMask: 0,
    totalButtons: 22,
    eventSequence: 1,
    droppedSnapshots: 0,
  });
  const states = await transport.request('get_button_states');
  assert.equal(states.data.triggerMask, 0);

  await transport.request('start_button_performance_monitoring');
  await delay(140);
  await transport.request('stop_button_performance_monitoring');
  assert.ok(performanceSamples > 0);
  unsubscribeButtons();
  unsubscribePerformance();
  await transport.close();
});

test('ordinary monitoring allows autosave while performance monitoring remains exclusive', async () => {
  const transport = await createTransport();

  await transport.request('start_button_monitoring');
  await transport.request('update_profile', {
      profileId: 'profile-arcade',
      profileDetails: { name: 'Saved While Monitoring' },
    });
  assert.equal((await transport.request('get_button_states')).data.isActive, true);
  await assert.rejects(transport.request('import_all_config', {}), /monitor-active/);
  await transport.request('push_leds_config', { ledBrightness: 50 });
  await transport.request('stop_button_monitoring');
  await transport.request('update_profile', {
    profileId: 'profile-arcade',
    profileDetails: { name: 'Saved After Stop' },
  });
  await transport.request('exit_webconfig');

  await transport.request('start_button_performance_monitoring');
  await assert.rejects(
    transport.request('update_global_config', {
      globalConfig: { wirelessReportRate: '2K' },
    }),
    /monitor-active/,
  );
  // Finish is a self-quiescing boundary: it must stop even an active
  // performance worker before doing its final persistent write.
  await transport.request('exit_webconfig');
  await transport.request('update_global_config', {
    globalConfig: { wirelessReportRate: '2K' },
  });

  const profile = await transport.request('get_default_profile');
  assert.equal(profile.data.defaultProfileDetails.name, 'Saved After Stop');
  await transport.close();
});

test('LED preview and sampling remain active across profile saves and switches', async () => {
  const transport = await createTransport();
  try {
    await transport.request('push_leds_config', { ledBrightness: 50 });
    const switched = await transport.request('switch_default_profile', { profileId: 'profile-tournament' });
    assert.equal(switched.data.profileList.defaultId, 'profile-tournament');
    assert.equal((await transport.request('get_button_states')).data.isActive, true);
  } finally {
    await transport.close();
  }
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
  const current = await transport.request('get_calibration_status');
  assert.equal(current.data.calibrationStatus.isActive, true);
  await Promise.race([
    complete,
    delay(1000).then(() => { throw new Error('calibration events timed out'); }),
  ]);
  assert.deepEqual(phases, ['TOP_SAMPLING', 'BOTTOM_SAMPLING', 'COMPLETED']);
  const completed = await transport.request('check_is_manual_calibration_completed');
  assert.equal(completed.data.isCompleted, true);
  await transport.close();
});

test('samples ADC mapping without shifting the first point and writes it back', async () => {
  const transport = await createTransport();
  const started = await transport.request('ms_mark_mapping_start', { id: 'mapping-default' });
  const length = started.data.status.length;
  let result;
  for (let index = 0; index <= length; index += 1) {
    result = await transport.request('ms_mark_mapping_step');
  }

  assert.equal(result.data.status.is_completed, true);
  assert.equal(result.data.status.values.length, length);
  assert.equal(result.data.status.values[0], 4000);
  assert.equal(result.data.status.values.at(-1), 800);
  const mapping = await transport.request('ms_get_mapping', { id: 'mapping-default' });
  assert.deepEqual(mapping.data.mapping.originalValues, result.data.status.values);
  assert.equal(mapping.data.mapping.calibratedValues.at(-1), (length - 1) * 0.1);
  await transport.close();
});

test('exposes one singleton mapping and rejects legacy multi-mapping mutations', async () => {
  const transport = await createTransport();

  await assert.rejects(
    transport.request('ms_create_mapping', { name: 'Too Short', length: 1, step: 0.1 }),
    /Invalid switch mapping parameters/,
  );
  await assert.rejects(
    transport.request('ms_create_mapping', { name: 'Too Long', length: 41, step: 0.1 }),
    /Invalid switch mapping parameters/,
  );
  await assert.rejects(
    transport.request('ms_create_mapping', { name: '六六六六六六', length: 2, step: 0.1 }),
    /Invalid switch mapping parameters/,
  );
  await assert.rejects(
    transport.request('ms_delete_mapping', { id: 'mapping-default' }),
    /Select another default mapping/,
  );

  await assert.rejects(
    transport.request('ms_create_mapping', { name: 'Travel 2', length: 2, step: 0.1 }),
    /Invalid switch mapping parameters/,
  );
  const list = await transport.request('ms_get_list');
  assert.equal(list.data.storageMode, 'shared-singleton');
  assert.equal(list.data.installSchemaVersion, 1);
  assert.equal(list.data.source, 'factory-fallback');
  assert.equal(list.data.mappingList.length, 1);
  await transport.close();
});

test('mock server curve editor accepts a shorter length and only persists active columns', async () => {
  const transport = await createTransport();
  const detailResponse = await transport.authorizedFetch('/api/switch-mappings/mock-axis');
  const detail = (await detailResponse.json()).data;
  const mapping = {
    ...detail.revision.mapping,
    length: 4,
    originalValues: [4050, 3200, 2100, 900],
  };
  const updateResponse = await transport.authorizedFetch(
    '/api/admin/switch-mappings/mock-axis/mapping',
    {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ mapping }),
    },
  );
  assert.equal(updateResponse.status, 200);
  const updated = (await updateResponse.json()).data.revision.mapping;
  assert.equal(updated.length, 4);
  assert.deepEqual(updated.originalValues, mapping.originalValues);
  await transport.close();
});

test('creates a RAM draft and installs a verified server revision as the singleton', async () => {
  const transport = await createTransport();
  await transport.request('ms_mapping_draft_begin', { name: 'Draft Axis', length: 2, step: 0.5 });
  await transport.request('ms_mark_mapping_step');
  await transport.request('ms_mark_mapping_step');
  await transport.request('ms_mark_mapping_step');
  const draft = await transport.request('ms_mapping_draft_get');
  assert.equal(draft.data.mapping.name, 'Draft Axis');
  assert.deepEqual(draft.data.mapping.originalValues, [4000, 800]);

  const detailResponse = await transport.authorizedFetch('/api/switch-mappings/mock-axis');
  const detail = (await detailResponse.json()).data;
  const installed = await transport.request('ms_install_mapping', {
    mapping: detail.revision.mapping,
    sha256: detail.revision.sha256,
  });
  assert.equal(installed.data.calibrationCleared, true);
  const list = await transport.request('ms_get_list');
  assert.equal(list.data.source, 'server-installed');
  assert.deepEqual(list.data.mappingList, [{ id: detail.revision.revisionId, name: 'Mock Axis' }]);
  const cleared = await transport.request('ms_clear_installed_mapping', {
    id: detail.revision.revisionId,
  });
  assert.equal(cleared.data.source, 'factory-fallback');
  assert.deepEqual(cleared.data.mappingList, [{ id: 'mapping-default', name: 'Factory Hall Curve' }]);
  await transport.close();
});

test('uploads, reports, reads and deletes an in-memory background image', async () => {
  const transport = await createTransport();
  const cid = 0x10203040;
  const pixels = Uint8Array.from([1, 2, 3, 4, 5, 6, 7, 8]);

  const begin = new Uint8Array(22);
  let view = new DataView(begin.buffer);
  view.setUint8(0, 0x30);
  view.setUint32(2, cid, true);
  view.setUint16(6, 2, true);
  view.setUint16(8, 2, true);
  view.setUint32(10, pixels.length, true);
  view.setUint8(14, 1);
  view.setUint8(16, 3);
  view.setUint32(18, crc32(pixels), true);
  const beginResponse = new DataView(await sendBinary(transport, begin, 0xb0));
  assert.equal(beginResponse.getUint8(1), 1);

  await transport.uploadImagePayload(pixels);

  const commit = new Uint8Array(6);
  view = new DataView(commit.buffer);
  view.setUint8(0, 0x32);
  view.setUint32(2, cid, true);
  const commitResponse = new DataView(await sendBinary(transport, commit, 0xb2));
  assert.equal(commitResponse.getUint8(1), 1);

  const info = new Uint8Array(6);
  view = new DataView(info.buffer);
  view.setUint8(0, 0x34);
  view.setUint8(1, 2);
  view.setUint32(2, cid, true);
  const infoResponse = new DataView(await sendBinary(transport, info, 0xb4));
  assert.equal(infoResponse.byteLength, 82);
  assert.equal(infoResponse.getUint8(6), 1);
  assert.equal(infoResponse.getUint8(7), 0);
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
  assert.equal(readView.getUint16(8, true), 2);
  assert.equal(readView.getUint16(10, true), 2);
  assert.equal(readView.getUint32(12, true), pixels.length);
  assert.equal(readView.getUint32(16, true), 0);
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
  const exitWebConfig = await transport.request('exit_webconfig');
  assert.equal(exitWebConfig.data.success, true);
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
    ...Array(16).fill('profile'),
    'end',
  ]);
  await transport.close();
});

test('typed image client covers upload, catalog, read and delete without publishing raw events', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);

  let jsonMessages = 0;
  const unsubscribeMessage = adapter.onMessage(() => {
    jsonMessages += 1;
  });

  const pixels = Uint8Array.from([1, 2, 3, 4, 5, 6, 7, 8]);
  await adapter.uploadImage({
    width: 2,
    height: 2,
    data: pixels,
    frameCount: 1,
    fps: 0,
  });
  const catalog = await adapter.getImageCatalog();
  assert.equal(catalog.protocolVersion, 5);
  assert.equal(catalog.maxUserFrames, 12);
  assert.equal(catalog.maxSystemFrames, 0);
  assert.equal(catalog.imageTransferVersion, 3);
  assert.equal(catalog.imageDataBytesPerReport, 996);
  assert.equal(catalog.imageTransferFlags & 3, 3);
  assert.equal(catalog.user.valid, true);
  assert.equal(catalog.user.size, pixels.byteLength);
  assert.equal(catalog.user.crc32, crc32(pixels));
  const downloaded = await adapter.readImage('user', catalog.user.size);
  assert.deepEqual(Array.from(downloaded), Array.from(pixels));
  await adapter.deleteImage();
  assert.equal((await adapter.getImageCatalog()).user.valid, false);
  await Promise.resolve();
  unsubscribeMessage();
  adapter.dispose();

  assert.equal(jsonMessages, 0);
});

test('two consecutive image uploads release both the image lane and device queue', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);

  const first = Uint8Array.from([1, 2, 3, 4, 5, 6, 7, 8]);
  const second = Uint8Array.from([8, 7, 6, 5, 4, 3, 2, 1]);
  assert.equal((await adapter.uploadImage({
    width: 2, height: 2, data: first, frameCount: 1, fps: 0,
  })).success, true);
  assert.equal((await adapter.uploadImage({
    width: 2, height: 2, data: second, frameCount: 1, fps: 0,
  })).success, true);

  const catalog = await adapter.getImageCatalog();
  assert.equal(catalog.user.valid, true);
  assert.deepEqual(
    Array.from(await adapter.readImage('user', second.byteLength)),
    Array.from(second),
  );
  adapter.dispose();
});

test('twelve full RGB565 frames upload and report the committed catalog CRC', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);
  const frameBytes = 320 * 172 * 2;
  const pixels = new Uint8Array(frameBytes * 12);
  for (let frame = 0; frame < 12; frame += 1) pixels.fill(frame + 1, frame * frameBytes, (frame + 1) * frameBytes);
  assert.equal((await adapter.uploadImage({
    width: 320, height: 172, data: pixels, frameCount: 12, fps: 6,
  })).success, true);
  const catalog = await adapter.getImageCatalog();
  assert.equal(catalog.maxUserFrames, 12);
  assert.equal(catalog.user.size, pixels.byteLength);
  assert.equal(catalog.user.frameCount, 12);
  assert.equal(catalog.user.fps, 6);
  assert.equal(catalog.user.crc32, crc32(pixels));
  adapter.dispose();
});

test('a complete image read and upload cannot interleave their HID exchanges', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);

  const previous = new Uint8Array(8192).fill(0x11);
  const replacement = new Uint8Array(8192).fill(0x22);
  await adapter.uploadImage({ width: 64, height: 64, data: previous, frameCount: 1, fps: 0 });

  const request = transport.request.bind(transport);
  const opcodes = [];
  let releaseFirstRead;
  let announceFirstRead;
  const firstReadStarted = new Promise(resolve => { announceFirstRead = resolve; });
  const firstReadGate = new Promise(resolve => { releaseFirstRead = resolve; });
  let delayed = false;
  transport.request = async (command, params, options) => {
    if (command === 'binary.exchange') {
      const opcode = Buffer.from(params.data, 'base64')[0];
      opcodes.push(opcode);
      if (opcode === 0x35 && !delayed) {
        delayed = true;
        const response = await request(command, params, options);
        announceFirstRead();
        await firstReadGate;
        return response;
      }
    }
    return request(command, params, options);
  };

  const reading = adapter.readImage('user', previous.byteLength);
  await firstReadStarted;
  const uploading = adapter.uploadImage({ width: 64, height: 64, data: replacement, frameCount: 1, fps: 0 });
  await Promise.resolve();
  assert.equal(opcodes.includes(0x30), false, 'BEGIN must wait for the complete preview read');
  releaseFirstRead();
  assert.deepEqual(Array.from(await reading), Array.from(previous));
  assert.equal((await uploading).success, true);
  assert.ok(opcodes.indexOf(0x30) > opcodes.lastIndexOf(0x35));
  adapter.dispose();
});

test('typed image catalog remains compatible with the legacy 64-byte response', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);
  const request = transport.request.bind(transport);
  transport.request = async (command, params, options) => {
    const envelope = await request(command, params, options);
    if (command !== 'binary.exchange') return envelope;
    const requestBytes = Buffer.from(params.data, 'base64');
    if (requestBytes[0] !== 0x34) return envelope;
    const legacy = Buffer.from(envelope.data.data, 'base64').subarray(0, 64);
    return {
      ...envelope,
      data: { ...envelope.data, data: legacy.toString('base64') },
    };
  };

  const catalog = await adapter.getImageCatalog();
  assert.equal(catalog.protocolVersion, 1);
  assert.equal(catalog.maxUserFrames, 6);
  assert.equal(catalog.maxSystemFrames, 8);
  assert.equal(catalog.user.crc32, undefined);
  assert.equal(catalog.system.crc32, undefined);
  adapter.dispose();
});

test('gallery image errors use the selected language without losing their transport reason', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);
  const request = transport.request.bind(transport);
  transport.request = async (command, params, options) => {
    if (command === 'binary.exchange' && Buffer.from(params.data, 'base64')[0] === 0x34) {
      throw new DeviceTransportError('protocol', 'Original catalog failure');
    }
    return request(command, params, options);
  };
  await assert.rejects(adapter.getImageCatalog(), error => {
    assert.ok(error instanceof ImageTransferError);
    assert.equal(error.reason, 'catalog-request-failed');
    assert.equal(error.cause.message, 'Original catalog failure');
    assert.match(galleryErrorMessage(error, 'en'), /Could not read image information/);
    assert.match(galleryErrorMessage(error, 'zh'), /无法读取设备图片信息/);
    assert.doesNotMatch(galleryErrorMessage(error, 'en'), /[\u3400-\u9fff]/);
    return true;
  });
  assert.match(galleryErrorMessage(new ImageTransferError('frame-limit', 'raw', 6), 'en'), /at most 6 image frames/);
  assert.match(galleryErrorMessage(new ImageTransferError('frame-limit', 'raw', 6), 'zh'), /最多支持 6 帧/);
  assert.equal(galleryErrorMessage(new Error('设备固件不支持'), 'en'), 'The operation failed. Please try again.');
  assert.match(galleryErrorMessage(new GalleryApiError('GALLERY_LIMIT_REACHED', 409, 'Personal gallery is full'), 'zh'), /数量上限/);
  assert.match(galleryErrorMessage(new GalleryApiError('GALLERY_LIMIT_REACHED', 409, 'Personal gallery is full'), 'en'), /image limit/);
  adapter.dispose();
});

test('image upload progress reaches total only after the commit ACK', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);
  const pixels = new Uint8Array(64 * 64 * 2);
  pixels.forEach((_, index) => { pixels[index] = index & 0xff; });
  const progress = [];
  const result = await adapter.uploadImage({
    width: 64,
    height: 64,
    data: pixels,
    frameCount: 1,
    fps: 0,
    onProgress: (received, total) => progress.push([received, total]),
  });
  assert.equal(result.success, true);
  assert.equal(progress.at(-1)[0], pixels.byteLength);
  assert.equal(progress.at(-1)[1], pixels.byteLength);
  assert.ok(progress.slice(0, -1).every(([sent, total], index, values) =>
    total === pixels.byteLength && sent < total && (index === 0 || sent > values[index - 1][0])));
  assert.ok(progress.slice(0, -1).every(([sent]) => sent % 996 === 0));
  adapter.dispose();
});

test('image client rejects a thirteenth frame and GIF sampling always spans the animation', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);
  await assert.rejects(
    adapter.uploadImage({
      width: 1,
      height: 1,
      data: new Uint8Array(26),
      frameCount: 13,
      fps: 3,
    }),
    /supported range/,
  );

  assert.deepEqual(selectGifFrameIndices([0], 100_000, 3, 6), [0]);
  const twelve = selectGifFrameIndices(
    Array.from({ length: 12 }, (_, index) => index * 1_000_000),
    12_000_000,
    3,
    12,
  );
  assert.equal(twelve.length, 12);
  assert.equal(twelve.at(-1), 3);
  for (const count of [13, 20]) {
    const selected = selectGifFrameIndices(
      Array.from({ length: count }, (_, index) => index * 1_000_000),
      count * 1_000_000,
      3,
      12,
    );
    assert.equal(selected.length, 12);
    assert.equal(selected[0], 0);
    assert.equal(selected.at(-1), 3);
  }
  adapter.dispose();
});

test('GIF frame delays are milliseconds and preserve a one-second loop at three FPS', () => {
  const { frameTimesUs, totalUs } = gifFrameTimelineUs(Array.from({ length: 25 }, () => ({ delay: 40 })));
  assert.equal(totalUs, 1_000_000);
  assert.equal(frameTimesUs[1], 40_000);
  const selected = selectGifFrameIndices(frameTimesUs, totalUs, 3, 12);
  assert.deepEqual(selected, [0, 8, 16]);
  assert.equal(selected.length / 3, 1);
  assert.deepEqual(gifFrameTimelineUs([{ delay: 0 }, {}]), {
    frameTimesUs: [0, 100_000], totalUs: 200_000,
  });
});

test('image client respects an older device catalog capped at six frames before BEGIN', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);
  const request = transport.request.bind(transport);
  let begins = 0;
  transport.request = async (command, params, options) => {
    if (command !== 'binary.exchange') return request(command, params, options);
    const opcode = Buffer.from(params.data, 'base64')[0];
    if (opcode === 0x30) begins += 1;
    const response = await request(command, params, options);
    if (opcode !== 0x34) return response;
    const bytes = Buffer.from(response.data.data, 'base64');
    bytes[65] = 6;
    return { ...response, data: { ...response.data, data: bytes.toString('base64') } };
  };
  await assert.rejects(adapter.uploadImage({
    width: 1, height: 1, data: new Uint8Array(14), frameCount: 7, fps: 3,
  }), /最多支持 6 帧/);
  assert.equal(begins, 0);
  adapter.dispose();
});

test('typed image reads fail closed on a short chunk or mismatched image total', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);

  const pixels = Uint8Array.from([1, 2, 3, 4, 5, 6, 7, 8]);
  await adapter.uploadImage({
    width: 2,
    height: 2,
    data: pixels,
    frameCount: 1,
    fps: 0,
  });
  const request = transport.request.bind(transport);
  let corruption = 'short';
  transport.request = async (command, params, options) => {
    const envelope = await request(command, params, options);
    if (command !== 'binary.exchange') return envelope;
    const requestBytes = Buffer.from(params.data, 'base64');
    if (requestBytes[0] !== 0x35) {
      return envelope;
    }
    const response = Buffer.from(envelope.data.data, 'base64');
    const view = new DataView(response.buffer, response.byteOffset, response.byteLength);
    if (corruption === 'short') {
      view.setUint16(20, view.getUint16(20, true) - 1, true);
    } else {
      view.setUint32(12, pixels.byteLength + 1, true);
    }
    return {
      ...envelope,
      data: { ...envelope.data, data: response.toString('base64') },
    };
  };

  await assert.rejects(adapter.readImage('user', pixels.byteLength), /invalid length/);
  corruption = 'total';
  await assert.rejects(adapter.readImage('user', pixels.byteLength), /invalid length/);
  adapter.dispose();
});

test('typed image catalog requires an exact successful response', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);

  const request = transport.request.bind(transport);
  let corruption = 'trailing';
  transport.request = async (command, params, options) => {
    const envelope = await request(command, params, options);
    if (command !== 'binary.exchange') return envelope;
    const requestBytes = Buffer.from(params.data, 'base64');
    if (requestBytes[0] !== 0x34) return envelope;
    let response = Buffer.from(envelope.data.data, 'base64');
    if (corruption === 'trailing') {
      response = Buffer.concat([response, Buffer.of(0)]);
    } else if (corruption === 'rejected') {
      response[1] = 0;
    } else {
      response[6] = 2;
    }
    return {
      ...envelope,
      data: { ...envelope.data, data: response.toString('base64') },
    };
  };

  await assert.rejects(adapter.getImageCatalog(), /invalid response/);
  corruption = 'rejected';
  await assert.rejects(adapter.getImageCatalog(), /was rejected/);
  corruption = 'invalid-valid-flag';
  await assert.rejects(adapter.getImageCatalog(), /invalid response/);
  adapter.dispose();
});

test('stream firmware ACK is returned verbatim and rejects a mismatched chunk index', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);
  const request = {
    sessionId: 'mock-session',
    componentName: 'application',
    chunkIndex: 7,
    totalChunks: 10,
    chunkOffset: 0,
    targetAddress: 0x90000000,
    checksumSha256: '00'.repeat(32),
    data: new Uint8Array(),
  };

  const makeCompletion = (chunkIndex) => {
    const raw = new Uint8Array(75);
    const view = new DataView(raw.buffer);
    raw[0] = 0x81;
    raw[1] = 1;
    view.setUint32(2, chunkIndex, true);
    view.setUint32(6, 80, true);
    return {
      complete: true,
      encoding: 'base64',
      data: Buffer.from(raw).toString('base64'),
      ack: {
        requestOpcode: 0x01,
        opcode: 0x81,
        success: true,
        kind: 'firmware.chunk',
        chunkIndex,
        progress: 80,
      },
    };
  };
  let forwardedTimeout = null;
  transport.upload = async (_stream, _data, options) => {
    forwardedTimeout = options.timeoutMs;
    return makeCompletion(7);
  };
  const accepted = await adapter.uploadFirmwareChunk(request, { timeoutMs: 4321 });
  assert.deepEqual(accepted, {
    success: true,
    chunkIndex: 7,
    progress: 80,
    error: null,
  });
  assert.equal(forwardedTimeout, 4321);

  const rejectedRaw = new Uint8Array(75);
  const rejectedView = new DataView(rejectedRaw.buffer);
  rejectedRaw[0] = 0x81;
  rejectedRaw[1] = 0;
  rejectedView.setUint32(2, 7, true);
  rejectedView.setUint32(6, 70, true);
  const rejection = Buffer.from('flash rejected');
  rejectedRaw[10] = rejection.byteLength;
  rejectedRaw.set(rejection, 11);
  transport.upload = async () => ({
    complete: true,
    encoding: 'base64',
    data: Buffer.from(rejectedRaw).toString('base64'),
    ack: {
      requestOpcode: 0x01,
      opcode: 0x81,
      success: false,
      kind: 'firmware.chunk',
      chunkIndex: 7,
      progress: 70,
    },
  });
  assert.deepEqual(await adapter.uploadFirmwareChunk(request), {
    success: false,
    chunkIndex: 7,
    progress: 70,
    error: 'flash rejected',
  });

  transport.upload = async () => ({
    ...makeCompletion(7),
    ack: { ...makeCompletion(7).ack, success: false },
  });
  await assert.rejects(
    adapter.uploadFirmwareChunk(request),
    /does not match the uploaded chunk/,
  );

  transport.upload = async () => makeCompletion(8);
  await assert.rejects(
    adapter.uploadFirmwareChunk(request),
    /mismatch|does not match the uploaded chunk/,
  );
  adapter.dispose();
});

test('continuous image write failure stops before COMMIT and never uses generic stream upload', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);

  let genericUploads = 0;
  let imageUploads = 0;
  const opcodes = [];
  const request = transport.request.bind(transport);
  transport.request = async (command, params, options) => {
    if (command === 'binary.exchange') opcodes.push(Buffer.from(params.data, 'base64')[0]);
    return request(command, params, options);
  };
  transport.upload = async () => { genericUploads += 1; throw new Error('generic upload must not run'); };
  transport.uploadImagePayload = async () => {
    imageUploads += 1;
    throw new DeviceTransportError('disconnected', 'image write failed');
  };
  await assert.rejects(adapter.uploadImage({
    width: 1, height: 1, data: Uint8Array.of(0xaa, 0xbb), frameCount: 1, fps: 0,
  }), /image write failed/);
  assert.equal(genericUploads, 0);
  assert.equal(imageUploads, 1);
  assert.equal(opcodes.filter(opcode => opcode === 0x30).length, 1);
  assert.equal(opcodes.includes(0x32), false);
  adapter.dispose();
});

test('image upload refuses legacy catalog capability without falling back to stream credit', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);

  const request = transport.request.bind(transport);
  let genericUploads = 0;
  let imageUploads = 0;
  transport.request = async (command, params, options) => {
    const envelope = await request(command, params, options);
    if (command !== 'binary.exchange' || Buffer.from(params.data, 'base64')[0] !== 0x34) return envelope;
    const legacy = Buffer.from(envelope.data.data, 'base64').subarray(0, 76);
    legacy[64] = 2;
    return { ...envelope, data: { ...envelope.data, data: legacy.toString('base64') } };
  };
  transport.upload = async () => { genericUploads += 1; return {}; };
  transport.uploadImagePayload = async () => { imageUploads += 1; };
  await assert.rejects(
    adapter.uploadImage({
      width: 1,
      height: 1,
      data: Uint8Array.of(0xaa, 0xbb),
      frameCount: 1,
      fps: 0,
    }),
    /升级设备固件/,
  );
  assert.equal(genericUploads, 0);
  assert.equal(imageUploads, 0);
  adapter.dispose();
});

test('versioned backup v3 restores profiles and user image without ADC data', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);

  await adapter.request('switch_default_profile', { profileId: 'profile-tournament' });
  const pixels = Uint8Array.from({ length: 2 * 2 * 2 * 12 }, (_, index) => index);
  assert.equal((await adapter.uploadImage({
    width: 2,
    height: 2,
    data: pixels,
    frameCount: 12,
    fps: 6,
  })).success, true);

  const backup = await adapter.exportConfig();
  assert.equal(backup.backupFormat, 'hbox-webconfig-backup');
  assert.equal(backup.backupVersion, 3);
  assert.equal(backup.globalConfig.defaultProfileId, 'profile-tournament');
  assert.equal(Object.hasOwn(backup, 'adcConfig'), false);
  assert.equal(backup.userImage.size, pixels.length);
  assert.equal(backup.userImage.frameCount, 12);
  assert.equal(backup.userImage.fps, 6);

  await adapter.request('switch_default_profile', { profileId: 'profile-arcade' });
  await adapter.request('update_profile', { profileId: 'profile-arcade', profileDetails: { name: 'Changed' } });
  assert.equal((await adapter.deleteImage()).success, true);

  await adapter.importConfig(backup);
  const profiles = await adapter.request('get_profile_list');
  assert.equal(profiles.defaultProfileDetails.id, 'profile-tournament');
  assert.deepEqual(
    profiles.profileList.items.map((profile) => profile.id).sort(),
    backup.profiles.map((profile) => profile.id).sort(),
  );
  assert.equal(profiles.profileList.items.length, 16);
  assert.equal(profiles.profileList.items[0].name, 'Profile-01');
  const restored = await adapter.readImage('user', pixels.length);
  assert.deepEqual([...restored], [...pixels]);
  adapter.dispose();
});

test('legacy backup imports other settings but warns and ignores ADC data', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);
  const backup = await adapter.exportConfig();
  backup.backupVersion = 2;
  backup.adcConfig = {
    version: 1,
    defaultMappingId: 'legacy-map',
    calibratedMappingId: 'legacy-map',
    mappings: [{
      id: 'legacy-map', name: 'Legacy', length: 2, step: 0.1,
      samplingFrequency: 1000, samplingNoise: 1, originalValues: [4000, 800],
    }],
  };
  const result = await adapter.importConfig(backup);
  assert.deepEqual(result.warnings, ['旧备份中的 ADC 映射与校准未导入']);
  const list = await adapter.request('ms_get_list');
  assert.equal(list.mappingList[0].id, 'mapping-default');
  adapter.dispose();
});

test('failed versioned import aborts staged config and restores the previous user image', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  assert.equal(adapter.markReady(), true);

  const previousPixels = Uint8Array.from([10, 20, 30, 40]);
  assert.equal((await adapter.uploadImage({
    width: 1,
    height: 2,
    data: previousPixels,
    frameCount: 1,
    fps: 0,
  })).success, true);
  const backup = await adapter.exportConfig();
  const replacementPixels = Uint8Array.from([1, 3, 5, 7]);
  backup.userImage = {
    ...backup.userImage,
    size: replacementPixels.length,
    data: Buffer.from(replacementPixels).toString('base64'),
  };

  const originalRequest = transport.request.bind(transport);
  transport.request = async (command, params) => {
    if (command === 'import_config_finish') {
      throw new DeviceTransportError('timeout', 'simulated finish failure');
    }
    return originalRequest(command, params);
  };

  await assert.rejects(adapter.importConfig(backup), /simulated finish failure/);
  const restored = await adapter.readImage('user', previousPixels.length);
  assert.deepEqual([...restored], [...previousPixels]);
  adapter.dispose();
});


test('auto sleep defaults off, persists, merges and rejects invalid switches atomically', async () => {
  const storage = new MemoryStorage();
  const transport = new MockDeviceTransport({ storage });
  const client = new DeviceCommandClient(transport);
  await client.connect();
  client.markReady();
  const get = async () => (await client.request('get_global_config')).globalConfig;
  const update = (globalConfig) => client.request('update_global_config', { globalConfig });
  // Use the public RPC surface, including persisted mock state.
  let value = await get();
  assert.equal(value.power.autoSleepEnabled, false);
  assert.equal(value.power.autoSleepSupported, true);
  await update({ ...value, power: { ...value.power, autoSleepEnabled: true, autoStandbyMs: 10000 } });
  value = await get();
  assert.equal(value.power.autoSleepEnabled, true);
  await update({ ...value, power: { wakeHoldMs: 3000, autoStandbyMs: 30000 } });
  value = await get();
  assert.equal(value.power.autoSleepEnabled, true);
  for (const invalid of [1, 'false', null]) {
    await assert.rejects(update({ ...value, power: { ...value.power, autoSleepEnabled: invalid } }), /Invalid power/);
    assert.deepEqual(await get(), value);
  }
  await update({ ...value, power: { ...value.power, autoSleepEnabled: false } });
  assert.equal((await get()).power.autoStandbyMs, 30000);
  const second = new DeviceCommandClient(new MockDeviceTransport({ storage }));
  await second.connect(); second.markReady();
  assert.equal((await second.request('get_global_config')).globalConfig.power.autoSleepEnabled, false);
  assert.equal((await second.request('get_global_config')).globalConfig.power.autoStandbyMs, 30000);
  const backup = await client.exportConfig();
  await update({ ...value, power: { ...value.power, autoSleepEnabled: true } });
  delete backup.globalConfig.power.autoSleepEnabled;
  await client.importConfig(backup);
  assert.equal((await get()).power.autoSleepEnabled, false);
  backup.globalConfig.power.autoSleepEnabled = 'true';
  await assert.rejects(client.importConfig(backup), /Invalid power/);
  assert.equal((await get()).power.autoSleepEnabled, false);
  second.dispose(); client.dispose();
});


test('whole-release Mock stages with backup, reinstalls and reports automatic recovery after reload', async () => {
  const { downloadRelease, installRelease } = require('../lib/device-transport/release-install-client.ts');
  const previous = globalThis.sessionStorage, previousLocal = globalThis.localStorage;
  globalThis.sessionStorage = new MemoryStorage(); globalThis.localStorage = new MemoryStorage();
  const client = new DeviceCommandClient(new MockDeviceTransport({ storage: null }));
  const finish = async adapter => {
    const i=JSON.parse(sessionStorage.getItem('xora-mock-install'));i.offlineStarted=Date.now()-7000;
    sessionStorage.setItem('xora-mock-install',JSON.stringify(i));return adapter.request('get_release_install_status');
  };
  try {
    await client.connect(); client.markReady();
    for (const targetSlot of ['B', 'A']) {
      const before = await client.request('get_firmware_inventory');
      const pkg = await downloadRelease(client, 'preview-2.0.0', before);
      const stages = [];
      await installRelease(client, pkg, p => stages.push(p.stage));
      const result = await finish(client);
      assert.equal(result.phase, 'completed'); assert.equal(result.currentSlot, targetSlot);
      assert.equal(result.confirmedDigest, pkg.digest); assert.equal(result.installationState, 'installed');
      assert.equal(result.txInstallMode,'dma');assert.equal(result.txRecoveryMode,'unknown');
      for(const phase of ['backing-up-tx','staging-controller','staging-tx','prepared','activating','waiting-device'])assert.ok(stages.includes(phase),phase);
    }
    const original=await client.request('get_firmware_inventory');
    sessionStorage.setItem('xora-mock-iap-mode','small-packet');
    sessionStorage.setItem('xora-mock-install-failure', 'tx');
    const pkg = await downloadRelease(client, 'preview-2.0.0', original);
    await installRelease(client, pkg, () => {});client.dispose();
    const reloaded = new DeviceCommandClient(new MockDeviceTransport({ storage: null }));
    try {
      await reloaded.connect(); reloaded.markReady();
      const recovering = JSON.parse(sessionStorage.getItem('xora-mock-install'));
      recovering.offlineStarted = Date.now() - 5000;
      sessionStorage.setItem('xora-mock-install', JSON.stringify(recovering));
      assert.equal((await reloaded.request('get_release_install_status')).phase, 'rollback-verifying');
      let result=await finish(reloaded);
      assert.equal(result.phase,'restored');assert.equal(result.recoveryResult,'restored');
      assert.equal(result.currentSlot,original.currentSlot);assert.deepEqual(result.stm32,original.stm32);assert.deepEqual(result.tx,original.tx);
      assert.equal(result.restoreAttempts,1);assert.equal(result.canAbort,false);assert.equal(result.canRetry,false);
      assert.equal(result.txInstallMode,'small-packet');assert.equal(result.txRecoveryMode,'small-packet');
      await assert.rejects(reloaded.request('retry_release_install',{session_id:result.sessionId}));
      sessionStorage.setItem('xora-mock-install-failure','restore');
      const next=await downloadRelease(reloaded,'preview-2.0.0',result);await installRelease(reloaded,next,()=>{});
      result=await finish(reloaded);assert.equal(result.phase,'restore-failed');assert.equal(result.restoreAttempts,2);
      assert.equal(result.errorCode,'TX_RESTORE_FAILED');assert.equal(result.currentSlot,original.currentSlot);
      assert.equal(result.txInstallMode,'small-packet');assert.equal(result.txRecoveryMode,'small-packet');
    } finally { reloaded.dispose(); }
  } finally {
    client.dispose();
    if (previous === undefined) delete globalThis.sessionStorage; else globalThis.sessionStorage = previous;
    if (previousLocal === undefined) delete globalThis.localStorage; else globalThis.localStorage = previousLocal;
  }
});


test('GIF sampling defaults to twelve FPS across a one-second loop', () => {
  const { frameTimesUs, totalUs } = gifFrameTimelineUs(Array.from({ length: 25 }, () => ({ delay: 40 })));
  const selected = selectGifFrameIndices(frameTimesUs, totalUs);
  assert.equal(selected.length, 12);
  assert.equal(selected[0], 0);
  assert.equal(selected.at(-1), 22);
});

test('six FPS support and image metadata are checked before destructive BEGIN', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
  adapter.markReady();
  const request = transport.request.bind(transport);
  let begins = 0;
  transport.request = async (command, params, options) => {
    if (command !== 'binary.exchange') return request(command, params, options);
    const opcode = Buffer.from(params.data, 'base64')[0];
    if (opcode === 0x30) begins += 1;
    const response = await request(command, params, options);
    if (opcode !== 0x34) return response;
    const bytes = Buffer.from(response.data.data, 'base64');
    bytes.writeUInt16LE(3, 80); // Older firmware has no 6 FPS capability.
    return { ...response, data: { ...response.data, data: bytes.toString('base64') } };
  };
  await assert.rejects(adapter.uploadImage({width:1,height:1,data:new Uint8Array(4),frameCount:2,fps:6}),
    error => error.reason === 'animation-rate');
  await assert.rejects(adapter.uploadImage({width:1,height:1,data:new Uint8Array(2),frameCount:2,fps:3}), /metadata/);
  await assert.rejects(adapter.uploadImage({width:1,height:1,data:new Uint8Array(4),frameCount:2,fps:5}), /metadata/);
  assert.equal(begins, 0);
  adapter.dispose();
});


test('JPEG upload, catalog, readback and backup restore preserve compressed 18-frame payload', async () => {
  const { parseJpegUimg } = require('../../../common/uimg-jpeg.cjs');
  const parsed = parseJpegUimg(new Uint8Array(fs.readFileSync(path.resolve(__dirname, '../../../common/test_vectors/uimg-jpeg/sequence.uimg'))));
  const transport = new MockDeviceTransport({ storage: null });
  const client = new DeviceCommandClient(transport);
  try {
    await client.connect(); client.markReady();
    assert.equal((await client.uploadImage({ ...parsed, data: parsed.payload })).success, true);
    const catalog = await client.getImageCatalog();
    assert.equal(catalog.user.format, 3); assert.equal(catalog.user.frameCount, 18);
    assert.equal(catalog.user.size, parsed.payload.length);
    assert.deepEqual(await client.readImage('user', parsed.payload.length), parsed.payload);
    const backup = await client.exportConfig();
    await client.deleteImage(); await client.importConfig(backup);
    assert.deepEqual(await client.readImage('user', parsed.payload.length), parsed.payload);
    const request = transport.request.bind(transport); let begins = 0;
    transport.request = async (command, params, options) => {
      const opcode = command === 'binary.exchange' ? Buffer.from(params.data, 'base64')[0] : 0;
      if (opcode === 0x30) begins++;
      const envelope = await request(command, params, options);
      if (opcode !== 0x34) return envelope;
      const bytes = Buffer.from(envelope.data.data, 'base64'); bytes.writeUInt16LE(7, 80);
      return { ...envelope, data: { ...envelope.data, data: bytes.toString('base64') } };
    };
    await assert.rejects(client.uploadImage({ ...parsed, data: parsed.payload }), error => error.reason === 'jpeg-required');
    assert.equal(begins, 0);
  } finally { client.dispose(); }
});


test('12 FPS installation uses each device capacity and fails before BEGIN without altering installed data', async () => {
  const { parseJpegUimg } = require('../../../common/uimg-jpeg.cjs');
  const parsed = parseJpegUimg(new Uint8Array(fs.readFileSync(path.resolve(__dirname, '../../../common/test_vectors/uimg-jpeg/sequence-12fps.uimg'))));
  assert.equal(parsed.fps, 12);
  for (const capacity of [parsed.payload.length - 1, parsed.payload.length, parsed.payload.length + 100]) {
    const transport = new MockDeviceTransport({ storage: null, imageCapacityBytes: capacity });
    const client = new DeviceCommandClient(transport);
    try {
      await client.connect();client.markReady();
      await client.uploadImage({width:1,height:1,data:Uint8Array.of(1,2),frameCount:1,fps:0});
      const original = await client.getImageCatalog();
      assert.equal(original.maxImagePayloadBytes, capacity); assert.equal(original.maxAnimationFps, 12);
      const request=transport.request.bind(transport);let begins=0;
      transport.request=async(command,params,options)=>{
        if(command==='binary.exchange' && Buffer.from(params.data,'base64')[0]===0x30)begins++;
        return request(command,params,options);
      };
      if(capacity < parsed.payload.length){
        await assert.rejects(client.uploadImage({...parsed,data:parsed.payload}),/Image capacity exceeded/);
        assert.equal(begins,0);assert.deepEqual((await client.getImageCatalog()).user,original.user);
      } else {
        await client.uploadImage({...parsed,data:parsed.payload}); assert.equal(begins,1);
        assert.equal((await client.getImageCatalog()).user.fps,12);
        assert.deepEqual(await client.readImage('user',parsed.payload.length),parsed.payload);
      }
      // Capacity must be fetched again; an older response cannot inherit a cached limit.
      transport.request=async(command,params,options)=>{
        const opcode=command==='binary.exchange'?Buffer.from(params.data,'base64')[0]:0;
        if(opcode===0x30)begins++;
        const response=await request(command,params,options);
        if(opcode!==0x34)return response;
        const bytes=Buffer.from(response.data.data,'base64').subarray(0,82);bytes[64]=4;
        return {...response,data:{...response.data,data:bytes.toString('base64')}};
      };
      const before=begins;
      await assert.rejects(client.uploadImage({...parsed,data:parsed.payload}),/Image capacity unavailable/);
      assert.equal(begins,before);
    } finally {client.dispose();}
  }
});


test('screen standby switch and grouped menus persist through Mock save, reload and import', async () => {
  const storage = new MemoryStorage();
  const client = new DeviceCommandClient(new MockDeviceTransport({ storage }));
  await client.connect(); client.markReady();
  const get = async () => (await client.request('get_screen_control_config')).screenControl;
  const update = screenControl => client.request('update_screen_control_config', { screenControl });
  const initial = await get();
  assert.equal(initial.standbyEnabled, false);
  assert.equal(initial.standbyDisplay, 'screenOff');
  await update({ standbyEnabled: true, standbyDisplay: 'screenOff', standbyTimeoutSeconds: 300,
    currentPageId: 14, featuresOrder: ['power', 'ledSetting'] });
  const value = await get();
  assert.equal(value.currentPageId, 14);
  assert.equal(value.featuresOrder.length, 10);
  assert.equal(value.features.ledBrightnessAdjust, undefined);
  for (const invalid of [1, 'false', null]) {
    await assert.rejects(update({ standbyEnabled: invalid }), /Invalid standbyEnabled/);
    assert.deepEqual(await get(), value);
  }
  const second = new DeviceCommandClient(new MockDeviceTransport({ storage }));
  await second.connect(); second.markReady();
  assert.deepEqual((await second.request('get_screen_control_config')).screenControl, { ...value, standbySupported: true });
  const backup = await client.exportConfig();
  await update({ standbyEnabled: false, standbyDisplay: 'buttonLayout' });
  await client.importConfig(backup);
  assert.equal((await get()).standbyEnabled, true);
  assert.equal((await get()).standbyDisplay, 'screenOff');
  backup.screenControl.standbyDisplay = 'none'; delete backup.screenControl.standbyEnabled;
  await client.importConfig(backup);
  assert.equal((await get()).standbyEnabled, false);
  assert.equal((await get()).standbyTimeoutSeconds, 300);
  second.dispose(); client.dispose();
});
