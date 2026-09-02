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
}

const delay = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));

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

test('rejects persistent configuration during ordinary and performance monitoring', async () => {
  const transport = await createTransport();

  await transport.request('start_button_monitoring');
  await assert.rejects(
    transport.request('update_profile', {
      profileId: 'profile-arcade',
      profileDetails: { name: 'Must Not Persist' },
    }),
    /monitor-active/,
  );
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
    'profile',
    'profile',
    'end',
  ]);
  await transport.close();
});

test('typed image client covers upload, catalog, read and delete without publishing raw events', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();

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
  assert.equal(adapter.imageTransferTotals.size, 0);
  const catalog = await adapter.getImageCatalog();
  assert.equal(catalog.user.valid, true);
  assert.equal(catalog.user.size, pixels.byteLength);
  const downloaded = await adapter.readImage('user', catalog.user.size);
  assert.deepEqual(Array.from(downloaded), Array.from(pixels));
  await adapter.deleteImage();
  assert.equal((await adapter.getImageCatalog()).user.valid, false);
  await Promise.resolve();
  unsubscribeMessage();
  adapter.dispose();

  assert.equal(jsonMessages, 0);
});

test('typed image reads fail closed on a short chunk or mismatched image total', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();

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

test('stream image rejection is correlated and returned without sending commit', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();

  let streamCalls = 0;
  let forwardedTimeout = null;
  transport.upload = async (_stream, data, options) => {
    streamCalls += 1;
    forwardedTimeout = options.timeoutMs;
    const request = data instanceof Uint8Array ? data : new Uint8Array(data);
    const view = new DataView(request.buffer, request.byteOffset, request.byteLength);
    const cid = view.getUint32(2, true);
    const offset = view.getUint32(6, true);
    const chunkSize = view.getUint16(10, true);
    const raw = new Uint8Array(79);
    const rawView = new DataView(raw.buffer);
    raw[0] = 0xb1;
    raw[1] = 0;
    rawView.setUint32(2, cid, true);
    rawView.setUint32(6, 0, true);
    rawView.setUint32(10, 1, true);
    const error = Buffer.from('image rejected');
    raw[14] = error.byteLength;
    raw.set(error, 15);
    return {
      complete: true,
      encoding: 'base64',
      data: Buffer.from(raw).toString('base64'),
      ack: {
        requestOpcode: 0x31,
        opcode: 0xb1,
        success: false,
        kind: 'image.chunk',
        cid,
        offset,
        chunkSize,
        received: 0,
        total: 1,
      },
    };
  };
  assert.deepEqual(await adapter.uploadImage({
    width: 1,
    height: 1,
    data: Uint8Array.of(0xaa),
    frameCount: 1,
    fps: 0,
    timeoutMs: 7654,
  }), {
    success: false,
    received: 0,
    total: 1,
    error: 'image rejected',
  });
  assert.equal(streamCalls, 1, 'a rejected chunk must stop before COMMIT');
  assert.equal(forwardedTimeout, 7654);
  assert.equal(adapter.imageTransferTotals.size, 0);
  adapter.dispose();
});

test('stream image ACK rejects mismatched cid and offset metadata', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();

  const raw = new Uint8Array(79);
  const rawView = new DataView(raw.buffer);
  raw[0] = 0xb1;
  raw[1] = 1;
  rawView.setUint32(2, 0x11223344, true);
  rawView.setUint32(6, 17, true);
  rawView.setUint32(10, 32, true);
  transport.upload = async () => ({
    complete: true,
    encoding: 'base64',
    data: Buffer.from(raw).toString('base64'),
    ack: {
      requestOpcode: 0x31,
      opcode: 0xb1,
      success: true,
      kind: 'image.chunk',
      cid: 0x11223344,
      offset: 15,
      chunkSize: 1,
      received: 17,
      total: 32,
    },
  });
  await assert.rejects(
    adapter.uploadImage({
      width: 1,
      height: 1,
      data: Uint8Array.of(0xaa),
      frameCount: 1,
      fps: 0,
    }),
    /unrelated ACK|does not match the uploaded chunk/,
  );
  assert.equal(adapter.imageTransferTotals.size, 0);
  adapter.dispose();
});

test('versioned backup v3 restores profiles and user image without ADC data', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();

  await adapter.request('switch_default_profile', { profileId: 'profile-tournament' });
  const pixels = Uint8Array.from([1, 2, 3, 4, 5, 6, 7, 8]);
  assert.equal((await adapter.uploadImage({
    width: 2,
    height: 2,
    data: pixels,
    frameCount: 1,
    fps: 0,
  })).success, true);

  const backup = await adapter.exportConfig();
  assert.equal(backup.backupFormat, 'hbox-webconfig-backup');
  assert.equal(backup.backupVersion, 3);
  assert.equal(backup.globalConfig.defaultProfileId, 'profile-tournament');
  assert.equal(Object.hasOwn(backup, 'adcConfig'), false);
  assert.equal(backup.userImage.size, pixels.length);

  await adapter.request('switch_default_profile', { profileId: 'profile-arcade' });
  await adapter.request('create_profile', { profileName: 'Must Be Removed' });
  assert.equal((await adapter.deleteImage()).success, true);

  await adapter.importConfig(backup);
  const profiles = await adapter.request('get_profile_list');
  assert.equal(profiles.defaultProfileDetails.id, 'profile-tournament');
  assert.deepEqual(
    profiles.profileList.items.map((profile) => profile.id).sort(),
    ['profile-arcade', 'profile-tournament'],
  );
  const restored = await adapter.readImage('user', pixels.length);
  assert.deepEqual([...restored], [...pixels]);
  adapter.dispose();
});

test('legacy backup imports other settings but warns and ignores ADC data', async () => {
  const transport = new MockDeviceTransport({ storage: null });
  const adapter = new DeviceCommandClient(transport);
  await adapter.connect();
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
