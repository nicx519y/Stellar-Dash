const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const {
  FragmentAssembler,
  SecureHidFrameFlags,
  SecureHidFrameType,
  SecureHidReportCodec,
  SECURE_HID_REPORT_SIZE,
  SECURE_HID_REPORT_VERSION,
  fragmentPayload,
} = require('../lib/device-transport/secure-hid-frame.ts');
const {
  AesGcmHidSessionCipher,
} = require('../lib/device-transport/session-crypto.ts');
const {
  PerformanceCheckpointAssembler,
  PerformanceTelemetryCache,
  applyCheckpointPreservingEdges,
  parsePerformanceCheckpointChunk,
  parsePerformanceEdge,
  parsePerformanceSample,
} = require('../lib/device-transport/performance-codec.ts');
const {
  DEVICE_CLOCK_SYNC_INTERVAL_MS,
  DeviceClockSyncScheduler,
  DeviceClockSynchronizer,
} = require('../lib/device-transport/device-clock-sync.ts');
const {
  DeviceAuthClient,
} = require('../lib/device-transport/device-auth-client.ts');
const {
  DeviceCommandClient,
} = require('../lib/device-transport/device-command-client.ts');
const {
  binaryOpcodeScope,
  elevatedScopesForCommand,
} = require('../lib/device-transport/scope-policy.ts');
const {
  exportWebHidConfigSections,
} = require('../lib/device-transport/webhid-config-export.ts');
const {
  DEFAULT_DEVICE_SCOPES,
  DeviceTransportError,
  DeviceTransportState,
} = require('../lib/device-transport/types.ts');
const {
  FIRMWARE_BINARY_HEADER_SIZE,
  WEBHID_FIRMWARE_CHUNK_DATA_SIZE,
  WEBHID_MAX_FIRMWARE_PACKET_SIZE,
  WEBHID_MAX_LOGICAL_MESSAGE_SIZE,
  WEBHID_MAX_STREAM_SIZE,
  RecoverableBootstrapResponseTimeoutError,
  WebHidTransport,
} = require('../lib/device-transport/webhid-transport.ts');
const {
  FIRMWARE_RECONNECT_DELAY_MS,
  scheduleAuthorizedReconnect,
} = require('../lib/device-transport/authorized-reconnect.ts');
const {
  initializeDeviceSession,
} = require('../lib/device-transport/device-initialization.ts');
const {
  registerDevicePageLifecycle,
  scheduleInitialDeviceAutoConnect,
  WEBHID_CROSS_DOCUMENT_HANDOFF_GRACE_MS,
} = require('../lib/device-transport/initial-auto-connect.ts');
const {
  WebHidDeviceLease,
  WEBHID_DEVICE_LOCK_NAME,
  WEBHID_DEVICE_LOCK_TIMEOUT_MS,
} = require('../lib/device-transport/device-lease.ts');
const {
  resolveDefaultFirmwareServerHost,
} = require('../lib/device-transport/firmware-server-origin.ts');
const {
  resolveAuthenticatedWebConfigTarget,
} = require('../lib/device-transport/webconfig-target.ts');

test('the first WebHID auto-connect waits for cross-document close handoff and is StrictMode-cancellable', async () => {
  const scheduled = [];
  const cancelled = new Set();
  let connects = 0;
  const schedule = (callback, delayMs) => {
    const id = scheduled.length + 1;
    scheduled.push({ id, callback, delayMs });
    return id;
  };
  const cancel = (id) => cancelled.add(id);
  const connect = async () => { connects += 1; };

  const cleanupFirstMount = scheduleInitialDeviceAutoConnect(
    'webhid',
    2_000,
    connect,
    assert.fail,
    schedule,
    cancel,
  );
  assert.equal(WEBHID_CROSS_DOCUMENT_HANDOFF_GRACE_MS, 250);
  assert.equal(scheduled[0].delayMs, 2_000 + WEBHID_CROSS_DOCUMENT_HANDOFF_GRACE_MS);
  assert.equal(connects, 0);

  cleanupFirstMount();
  scheduled[0].callback();
  await Promise.resolve();
  assert.equal(cancelled.has(scheduled[0].id), true);
  assert.equal(connects, 0, 'StrictMode cleanup must suppress the disposed mount timer');

  scheduleInitialDeviceAutoConnect(
    'webhid',
    2_000,
    connect,
    assert.fail,
    schedule,
    cancel,
  );
  scheduled[1].callback();
  await Promise.resolve();
  assert.equal(connects, 1, 'the replacement mount receives exactly one automatic connect');
});

test('initial mock auto-connect remains immediate and ignores the WebHID handoff timeout', async () => {
  let connects = 0;
  let schedules = 0;
  scheduleInitialDeviceAutoConnect(
    'mock',
    Number.NaN,
    async () => { connects += 1; },
    assert.fail,
    () => {
      schedules += 1;
      return 1;
    },
    () => undefined,
  );
  await Promise.resolve();
  assert.equal(connects, 1);
  assert.equal(schedules, 0);
});

test('same-origin WebHID lease excludes a second page with a bounded device-busy result', async () => {
  assert.equal(WEBHID_DEVICE_LOCK_TIMEOUT_MS, 2_000);
  const locks = makeExclusiveLockManager();
  const first = new WebHidDeviceLease(locks, 100);
  const second = new WebHidDeviceLease(locks, 20);
  await first.acquire();
  await assert.rejects(
    second.acquire(),
    (error) => error instanceof DeviceTransportError && error.code === 'device-busy',
  );
  assert.deepEqual(locks.requestedNames, [
    WEBHID_DEVICE_LOCK_NAME,
    WEBHID_DEVICE_LOCK_NAME,
  ]);

  first.release();
  await waitFor(() => locks.activeCount === 0, 250);
  await second.acquire();
  assert.equal(locks.activeCount, 1);
  second.release();
  await waitFor(() => locks.activeCount === 0, 250);
});

test('both automatic and chooser connects acquire the lease before any navigator.hid call', async () => {
  let getDevicesCalls = 0;
  let requestDeviceCalls = 0;
  let acquireCalls = 0;
  const busyLease = {
    async acquire() {
      acquireCalls += 1;
      throw new DeviceTransportError('device-busy', 'fixture lease busy');
    },
    release() {},
  };
  const transport = new WebHidTransport({
    navigator: {
      async getDevices() { getDevicesCalls += 1; return []; },
      async requestDevice() { requestDeviceCalls += 1; return []; },
      addEventListener() {},
      removeEventListener() {},
    },
    connectionLease: busyLease,
  });
  const client = new DeviceCommandClient(
    transport,
    null,
    DEFAULT_DEVICE_SCOPES,
    30_000,
  );
  await assert.rejects(client.connect(false), /fixture lease busy/);
  await assert.rejects(client.connect(true), /fixture lease busy/);
  assert.equal(acquireCalls, 2);
  assert.equal(getDevicesCalls, 0);
  assert.equal(requestDeviceCalls, 0);
  client.dispose();
});

test('the WebHID transport boundary itself acquires a lease before direct navigator access', async () => {
  let getDevicesCalls = 0;
  let requestDeviceCalls = 0;
  let acquireCalls = 0;
  const busyLease = {
    async acquire() {
      acquireCalls += 1;
      throw new DeviceTransportError('device-busy', 'direct transport lease busy');
    },
    release() {},
  };
  const transport = new WebHidTransport({
    navigator: {
      async getDevices() { getDevicesCalls += 1; return []; },
      async requestDevice() { requestDeviceCalls += 1; return []; },
      addEventListener() {},
      removeEventListener() {},
    },
    connectionLease: busyLease,
  });
  await assert.rejects(transport.connect(), /direct transport lease busy/);
  await assert.rejects(transport.requestPermissionAndConnect(), /direct transport lease busy/);
  assert.equal(acquireCalls, 2);
  assert.equal(getDevicesCalls, 0);
  assert.equal(requestDeviceCalls, 0);
});

for (const directConnectKind of ['automatic', 'chooser']) {
  test(`transport close cancels a ${directConnectKind} connect still waiting for its lease`, async () => {
    let resolveAcquire;
    const acquireGate = new Promise((resolve) => { resolveAcquire = resolve; });
    let acquireCalls = 0;
    let releases = 0;
    let getDevicesCalls = 0;
    let requestDeviceCalls = 0;
    let openCalls = 0;
    const device = {
      opened: false,
      vendorId: 0xcafe,
      productId: 0x4021,
      productName: `HBox pending ${directConnectKind} lease`,
      collections: [],
      async open() { openCalls += 1; this.opened = true; },
      async close() { this.opened = false; },
      addEventListener() {},
      removeEventListener() {},
      async sendReport() {},
    };
    const lease = {
      async acquire() {
        acquireCalls += 1;
        await acquireGate;
      },
      release() { releases += 1; },
    };
    const transport = new WebHidTransport({
      navigator: {
        async getDevices() { getDevicesCalls += 1; return [device]; },
        async requestDevice() { requestDeviceCalls += 1; return [device]; },
        addEventListener() {},
        removeEventListener() {},
      },
      connectionLease: lease,
    });
    const connecting = (
      directConnectKind === 'chooser'
        ? transport.requestPermissionAndConnect()
        : transport.connect()
    ).catch((error) => error);
    await waitFor(() => acquireCalls === 1, 250);
    const closing = transport.close();
    resolveAcquire();
    const result = await connecting;
    await closing;
    await waitFor(() => releases === 1, 250);

    assert.ok(result instanceof DeviceTransportError);
    assert.equal(result.code, 'disconnected');
    assert.equal(getDevicesCalls, 0);
    assert.equal(requestDeviceCalls, 0);
    assert.equal(openCalls, 0);
    assert.equal(releases, 1);
  });
}

for (const firstConnectKind of ['automatic', 'chooser']) {
  const secondConnectKind = firstConnectKind === 'automatic' ? 'chooser' : 'automatic';
  test(`a direct ${secondConnectKind} connect cancels an older pending ${firstConnectKind} attempt`, async () => {
    let resolveFirstNative;
    const firstNative = new Promise((resolve) => { resolveFirstNative = resolve; });
    let getDevicesCalls = 0;
    let requestDeviceCalls = 0;
    let openCalls = 0;
    let releases = 0;
    const device = {
      opened: false,
      vendorId: 0xcafe,
      productId: 0x4021,
      productName: `HBox concurrent ${firstConnectKind}`,
      collections: [],
      async open() { openCalls += 1; this.opened = true; },
      async close() { this.opened = false; },
      addEventListener() {},
      removeEventListener() {},
      async sendReport() {},
    };
    const transport = new WebHidTransport({
      navigator: {
        getDevices() {
          getDevicesCalls += 1;
          return firstConnectKind === 'automatic' && getDevicesCalls === 1
            ? firstNative
            : Promise.resolve([device]);
        },
        requestDevice() {
          requestDeviceCalls += 1;
          return firstConnectKind === 'chooser' && requestDeviceCalls === 1
            ? firstNative
            : Promise.resolve([device]);
        },
        addEventListener() {},
        removeEventListener() {},
      },
      connectionLease: {
        async acquire() {},
        release() { releases += 1; },
      },
    });
    const invoke = (kind) => kind === 'chooser'
      ? transport.requestPermissionAndConnect()
      : transport.connect();
    const older = invoke(firstConnectKind).catch((error) => error);
    await waitFor(
      () => firstConnectKind === 'automatic' ? getDevicesCalls === 1 : requestDeviceCalls === 1,
      250,
    );
    const newer = invoke(secondConnectKind).catch((error) => error);
    const newerResult = await newer;
    assert.ok(newerResult instanceof DeviceTransportError);
    assert.equal(newerResult.code, 'device-busy');

    resolveFirstNative([device]);
    const olderResult = await older;
    assert.ok(olderResult instanceof DeviceTransportError);
    assert.equal(olderResult.code, 'disconnected');
    assert.equal(openCalls, 0, 'the superseded physical attempt must never late-open');
    await waitFor(() => releases === 1, 250);

    await invoke(secondConnectKind);
    assert.equal(openCalls, 1, 'a clean retry succeeds after the cancelled attempt is physically safe');
    await transport.close();
  });
}

test('the client delegates the single physical lease exclusively to its WebHID transport', async () => {
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox shared lease fixture',
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener() {},
    removeEventListener() {},
    async sendReport() {},
  };
  let physicallyHeld = false;
  let physicalAcquires = 0;
  let physicalReleases = 0;
  const sharedLease = {
    async acquire() {
      if (physicallyHeld) return;
      physicallyHeld = true;
      physicalAcquires += 1;
    },
    release() {
      if (!physicallyHeld) return;
      physicallyHeld = false;
      physicalReleases += 1;
    },
  };
  const transport = new WebHidTransport({
    navigator: makeHidNavigator(device),
    connectionLease: sharedLease,
  });
  const client = new DeviceCommandClient(
    transport,
    { async authenticate() {}, clear() {} },
    DEFAULT_DEVICE_SCOPES,
    30_000,
  );
  await client.connect(false);
  assert.equal(physicalAcquires, 1);
  client.dispose();
  await waitFor(() => physicalReleases === 1, 250);
  assert.equal(physicalAcquires, 1);
  assert.equal(physicalReleases, 1);
});

test('every pagehide releases HID while bfcache pages restore exactly once and StrictMode listeners stay isolated', () => {
  const listeners = new Map([
    ['pagehide', new Set()],
    ['pageshow', new Set()],
  ]);
  const target = {
    addEventListener(type, listener) {
      listeners.get(type).add(listener);
    },
    removeEventListener(type, listener) {
      listeners.get(type).delete(listener);
    },
  };
  const first = { suspend: 0, destroy: 0, restore: 0 };
  const replacement = { suspend: 0, destroy: 0, restore: 0 };
  const removeFirstMount = registerDevicePageLifecycle(
    {
      suspendForBfcache: () => { first.suspend += 1; },
      destroyDocument: () => { first.destroy += 1; },
      restoreFromBfcache: () => { first.restore += 1; },
    },
    target,
  );
  removeFirstMount();
  registerDevicePageLifecycle(
    {
      suspendForBfcache: () => { replacement.suspend += 1; },
      destroyDocument: () => { replacement.destroy += 1; },
      restoreFromBfcache: () => { replacement.restore += 1; },
    },
    target,
  );

  for (const listener of listeners.get('pagehide')) listener({ persisted: true });
  for (const listener of listeners.get('pagehide')) listener({ persisted: true });
  assert.deepEqual(first, { suspend: 0, destroy: 0, restore: 0 });
  assert.deepEqual(replacement, { suspend: 1, destroy: 0, restore: 0 });
  for (const listener of listeners.get('pageshow')) listener({ persisted: true });
  for (const listener of listeners.get('pageshow')) listener({ persisted: true });
  assert.deepEqual(replacement, { suspend: 1, destroy: 0, restore: 1 });

  for (const listener of listeners.get('pagehide')) listener({ persisted: false });
  for (const listener of listeners.get('pagehide')) listener({ persisted: false });
  for (const listener of listeners.get('pageshow')) listener({ persisted: true });
  assert.deepEqual(
    replacement,
    { suspend: 1, destroy: 1, restore: 1 },
    'terminal pagehide disposes once and cannot later restore',
  );
});

test('bounded close timeout retains the lease until native close settles and disposed timers stay rejected', async () => {
  let releaseClose;
  const nativeClose = new Promise((resolve) => { releaseClose = resolve; });
  let closeCalls = 0;
  let openCalls = 0;
  let reportCalls = 0;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox pagehide close fixture',
    collections: [],
    async open() { openCalls += 1; this.opened = true; },
    async close() {
      closeCalls += 1;
      await nativeClose;
      this.opened = false;
    },
    addEventListener() {},
    removeEventListener() {},
    async sendReport() { reportCalls += 1; },
  };
  let leaseReleases = 0;
  const transport = new WebHidTransport({
    navigator: makeHidNavigator(device),
    closeTimeoutMs: 20,
    connectionLease: {
      async acquire() {},
      release() { leaseReleases += 1; },
    },
  });
  const client = new DeviceCommandClient(
    transport,
    { async authenticate() {}, clear() {} },
    DEFAULT_DEVICE_SCOPES,
    30_000,
  );
  await client.connect(false);

  client.dispose();
  client.dispose();
  assert.equal(closeCalls, 1, 'HIDDevice.close must be invoked before dispose returns');
  assert.equal(reportCalls, 0, 'page teardown must not send session.end or any protocol report');
  await assert.rejects(client.connect(false), /已释放/);
  await assert.rejects(client.request('ping'), /已释放/);

  let staleTimer = null;
  const staleFailures = [];
  scheduleInitialDeviceAutoConnect(
    'webhid',
    2_000,
    () => client.connect(false),
    (error) => staleFailures.push(error),
    (callback) => {
      staleTimer = callback;
      return 1;
    },
    () => undefined,
  );
  staleTimer();
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(openCalls, 1, 'a disposed document timer cannot reopen a hidden HID handle');
  assert.equal(staleFailures.length, 1);
  assert.match(staleFailures[0].message, /已释放/);

  await new Promise((resolve) => setTimeout(resolve, 35));
  assert.equal(
    leaseReleases,
    0,
    'the bounded UI close timeout must not release cross-document ownership',
  );

  releaseClose();
  await waitFor(() => !device.opened, 250);
  await waitFor(() => leaseReleases === 1, 250);
  assert.equal(transport.state, DeviceTransportState.DISCONNECTED);
});

test('a physical disconnect releases the lease even when native HID close never settles', async () => {
  let disconnectListener = null;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox physical release fixture',
    collections: [],
    async open() { this.opened = true; },
    close() { return new Promise(() => {}); },
    addEventListener() {},
    removeEventListener() {},
    async sendReport() {},
  };
  let releases = 0;
  const transport = new WebHidTransport({
    navigator: {
      async getDevices() { return [device]; },
      async requestDevice() { return [device]; },
      addEventListener(type, listener) {
        if (type === 'disconnect') disconnectListener = listener;
      },
      removeEventListener() {},
    },
    closeTimeoutMs: 20,
    connectionLease: {
      async acquire() {},
      release() { releases += 1; },
    },
  });
  const client = new DeviceCommandClient(
    transport,
    { async authenticate() {}, clear() {} },
    DEFAULT_DEVICE_SCOPES,
    30_000,
  );
  await client.connect(false);
  client.dispose();
  await new Promise((resolve) => setTimeout(resolve, 35));
  assert.equal(releases, 0);
  disconnectListener({ device });
  await waitFor(() => releases === 1, 250);
});

for (const pendingStage of ['getDevices', 'requestDevice']) {
  test(`dispose retains the lease while navigator.hid.${pendingStage} is still pending`, async () => {
    let resolveNative;
    const native = new Promise((resolve) => { resolveNative = resolve; });
    let discoveryCalls = 0;
    let chooserCalls = 0;
    let openCalls = 0;
    let releases = 0;
    const device = {
      opened: false,
      vendorId: 0xcafe,
      productId: 0x4021,
      productName: `HBox pending ${pendingStage}`,
      collections: [],
      async open() { openCalls += 1; this.opened = true; },
      async close() { this.opened = false; },
      addEventListener() {},
      removeEventListener() {},
      async sendReport() {},
    };
    const transport = new WebHidTransport({
      navigator: {
        getDevices() {
          discoveryCalls += 1;
          return pendingStage === 'getDevices' ? native : Promise.resolve([device]);
        },
        requestDevice() {
          chooserCalls += 1;
          return pendingStage === 'requestDevice' ? native : Promise.resolve([device]);
        },
        addEventListener() {},
        removeEventListener() {},
      },
      openTimeoutMs: 1_000,
      closeTimeoutMs: 20,
      connectionLease: {
        async acquire() {},
        release() { releases += 1; },
      },
    });
    const client = new DeviceCommandClient(
      transport,
      null,
      DEFAULT_DEVICE_SCOPES,
      2_000,
    );
    const connecting = client.connect(pendingStage === 'requestDevice').catch((error) => error);
    await waitFor(
      () => pendingStage === 'getDevices' ? discoveryCalls === 1 : chooserCalls === 1,
      250,
    );

    client.dispose();
    await new Promise((resolve) => setTimeout(resolve, 30));
    assert.equal(releases, 0, 'a pending native discovery/chooser still owns the page lease');

    resolveNative([device]);
    await connecting;
    await waitFor(() => releases > 0, 250);
    assert.equal(releases, 1, 'one completed connect attempt releases its page lease once');
    assert.equal(openCalls, 0, 'a disposed discovery/chooser result must never proceed to device.open');
  });
}

test('a late device.open after dispose is closed before the lease is released', async () => {
  let resolveOpen;
  const nativeOpen = new Promise((resolve) => { resolveOpen = resolve; });
  let resolveClose;
  const nativeClose = new Promise((resolve) => { resolveClose = resolve; });
  let openCalls = 0;
  let closeCalls = 0;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox late open fixture',
    collections: [],
    open() {
      openCalls += 1;
      return nativeOpen.then(() => { this.opened = true; });
    },
    close() {
      closeCalls += 1;
      return nativeClose.then(() => { this.opened = false; });
    },
    addEventListener() {},
    removeEventListener() {},
    async sendReport() {},
  };
  let releases = 0;
  const transport = new WebHidTransport({
    navigator: makeHidNavigator(device),
    openTimeoutMs: 1_000,
    closeTimeoutMs: 20,
    connectionLease: {
      async acquire() {},
      release() { releases += 1; },
    },
  });
  const client = new DeviceCommandClient(
    transport,
    null,
    DEFAULT_DEVICE_SCOPES,
    2_000,
  );
  const connecting = client.connect(false).catch((error) => error);
  await waitFor(() => openCalls === 1, 250);
  client.dispose();
  await new Promise((resolve) => setTimeout(resolve, 30));
  assert.equal(releases, 0);

  resolveOpen();
  await waitFor(() => closeCalls === 1, 250);
  assert.equal(releases, 0, 'late-open cleanup must retain ownership through native close');
  resolveClose();
  await connecting;
  await waitFor(() => releases === 1, 250);
  assert.equal(device.opened, false);
});

test('an unrelated HID disconnect cannot settle or replace the active physical release barrier', async () => {
  let disconnectListener = null;
  const makeDevice = (name) => ({
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: name,
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener() {},
    removeEventListener() {},
    async sendReport() {},
  });
  const active = makeDevice('HBox active');
  const unrelated = makeDevice('HBox unrelated');
  const transport = new WebHidTransport({
    navigator: {
      async getDevices() { return [active]; },
      async requestDevice() { return [active]; },
      addEventListener(type, listener) {
        if (type === 'disconnect') disconnectListener = listener;
      },
      removeEventListener() {},
    },
  });
  await transport.connect();
  let released = false;
  void transport.waitForPhysicalRelease().then(() => { released = true; });
  disconnectListener({ device: unrelated });
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(released, false);
  assert.equal(transport.state, DeviceTransportState.AUTHENTICATING);
  await transport.close();
  await waitFor(() => released, 250);
});

test('a rejected native close keeps the lease until a real disconnect of that device', async () => {
  let disconnectListener = null;
  let releases = 0;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox rejected close fixture',
    collections: [],
    async open() { this.opened = true; },
    async close() { throw new Error('native close rejected'); },
    addEventListener() {},
    removeEventListener() {},
    async sendReport() {},
  };
  const transport = new WebHidTransport({
    navigator: {
      async getDevices() { return [device]; },
      async requestDevice() { return [device]; },
      addEventListener(type, listener) {
        if (type === 'disconnect') disconnectListener = listener;
      },
      removeEventListener() {},
    },
    closeTimeoutMs: 20,
    connectionLease: {
      async acquire() {},
      release() { releases += 1; },
    },
  });
  const client = new DeviceCommandClient(
    transport,
    { async authenticate() {}, clear() {} },
    DEFAULT_DEVICE_SCOPES,
    2_000,
  );
  await client.connect(false);
  client.dispose();
  await new Promise((resolve) => setTimeout(resolve, 30));
  assert.equal(releases, 0);
  device.opened = false;
  disconnectListener({ device });
  await waitFor(() => releases === 1, 250);
  await transport.connect();
  assert.equal(
    transport.state,
    DeviceTransportState.AUTHENTICATING,
    'an exact-device unplug clears a terminal close rejection quarantine',
  );
  await transport.close();
  device.opened = false;
  disconnectListener({ device });
});

test('production WebHID construction and navigator access remain centralized behind the leased transport', () => {
  const hostedFactory = fs.readFileSync(path.join(
    __dirname,
    '..',
    'lib',
    'device-transport',
    'factory-runtime-hosted.ts',
  ), 'utf8');
  const client = fs.readFileSync(path.join(
    __dirname,
    '..',
    'lib',
    'device-transport',
    'device-command-client.ts',
  ), 'utf8');
  const context = fs.readFileSync(path.join(
    __dirname,
    '..',
    'contexts',
    'gamepad-config-context.tsx',
  ), 'utf8');
  const layout = fs.readFileSync(path.join(
    __dirname,
    '..',
    'app',
    'layout.tsx',
  ), 'utf8');
  assert.match(hostedFactory, /const connectionLease = createBrowserWebHidDeviceLease\(\)/);
  assert.match(hostedFactory, /new WebHidTransport\(\{[\s\S]*?connectionLease,/);
  const productionClientConstruction = hostedFactory.slice(hostedFactory.lastIndexOf('new DeviceCommandClient('));
  assert.doesNotMatch(productionClientConstruction, /connectionLease/);
  const webhidTransport = fs.readFileSync(path.join(
    __dirname,
    '..',
    'lib',
    'device-transport',
    'webhid-transport.ts',
  ), 'utf8');
  assert.match(
    webhidTransport,
    /usesInjectedNavigator[\s\S]*?options\.connectionLease \?\? createBrowserWebHidDeviceLease\(\)/,
    'a real browser navigator must create a lease even when WebHidTransport is used directly',
  );
  assert.doesNotMatch(client, /connectionLease|DeviceConnectionLease/);
  assert.ok(client.indexOf('transport.connect()') >= 0);
  assert.ok(client.indexOf('transport.requestPermissionAndConnect()') >= 0);
  assert.doesNotMatch(context, /\.transport\.(?:connect|requestPermissionAndConnect)\(/);
  assert.match(layout, /deviceError\?\.transportCode === 'device-busy'[\s\S]*?deviceError\.message/);
  assert.match(layout, /error instanceof DeviceTransportError && error\.code === 'device-busy'[\s\S]*?error\.message/);
});

test('authenticated product and PCB identity selects only a local profile', () => {
  assert.deepEqual(
    resolveAuthenticatedWebConfigTarget({
      productId: 'HBOX',
      pcbRevision: '2.0.0',
      webConfigProfile: 'hbox-pcb-v2',
    }, '2.0.0'),
    {
      productId: 'HBOX',
      pcbRevision: '2.0.0',
      webConfigProfile: 'hbox-pcb-v2',
      basePath: '/webconfig/hbox-pcb-v2/',
    },
  );
  for (const target of [
    {
      productId: 'FAKE',
      pcbRevision: '2.0.0',
      webConfigProfile: 'hbox-pcb-v2',
    },
    {
      productId: 'HBOX',
      pcbRevision: '3.0.0',
      webConfigProfile: 'hbox-pcb-v3',
    },
    {
      productId: 'HBOX',
      pcbRevision: '2.0.0',
      webConfigProfile: 'https://evil.example',
    },
  ]) {
    assert.throws(
      () => resolveAuthenticatedWebConfigTarget(target, '2.0.0'),
      /不支持|不一致/,
    );
  }
});

test('WebHID report types and flags match common/webhid_protocol.h', () => {
  assert.deepEqual(
    {
      bootstrapRequest: SecureHidFrameType.BOOTSTRAP_REQUEST,
      bootstrapResponse: SecureHidFrameType.BOOTSTRAP_RESPONSE,
      secureRequest: SecureHidFrameType.RPC_REQUEST,
      secureResponse: SecureHidFrameType.RPC_RESPONSE,
      secureEvent: SecureHidFrameType.EVENT,
      perfSample: SecureHidFrameType.PERF_SAMPLE,
      perfEdge: SecureHidFrameType.PERF_EDGE,
      perfCheckpoint: SecureHidFrameType.PERF_CHECKPOINT,
      streamFragment: SecureHidFrameType.STREAM_CHUNK,
    },
    {
      bootstrapRequest: 0x01,
      bootstrapResponse: 0x02,
      secureRequest: 0x10,
      secureResponse: 0x11,
      secureEvent: 0x12,
      perfSample: 0x20,
      perfEdge: 0x21,
      perfCheckpoint: 0x22,
      streamFragment: 0x30,
    },
  );
  assert.equal(SecureHidFrameFlags.SECURE, 1);
  assert.equal(SecureHidFrameFlags.FRAGMENTED, 2);
  assert.equal(SecureHidFrameFlags.LAST, 4);
  assert.equal(SecureHidFrameFlags.ACK_REQUIRED, 8);
});

test('WebHID and mock firmware APIs remain same-origin', () => {
  assert.equal(resolveDefaultFirmwareServerHost('webhid'), '');
  assert.equal(resolveDefaultFirmwareServerHost('mock'), '');
  assert.equal(
    resolveDefaultFirmwareServerHost('webhid', ' https://config.example/ '),
    '',
  );
});

test('SecureHidReportV1 encodes the locked 64-byte bootstrap ABI', async () => {
  const codec = new SecureHidReportCodec();
  const payload = Uint8Array.from([1, 2, 3, 4]);
  const report = await codec.encode({
    type: SecureHidFrameType.BOOTSTRAP_REQUEST,
    flags: SecureHidFrameFlags.LAST,
    sequence: 0x12345678,
    payload,
    secure: false,
  });

  assert.equal(report.byteLength, 64);
  assert.deepEqual(Array.from(report.slice(0, 8)), [
    1,
    SecureHidFrameType.BOOTSTRAP_REQUEST,
    SecureHidFrameFlags.LAST,
    4,
    0x78,
    0x56,
    0x34,
    0x12,
  ]);
  assert.deepEqual((await codec.decode(report)).payload, payload);
});

test('protected cleartext reports are fail-closed', async () => {
  const codec = new SecureHidReportCodec();
  await assert.rejects(
    codec.encode({
      type: SecureHidFrameType.RPC_REQUEST,
      flags: SecureHidFrameFlags.LAST,
      sequence: 1,
      payload: new Uint8Array(),
      secure: false,
    }),
    /before authentication/,
  );
});

test('AES-GCM authenticates header, payload and 12-byte tag', async () => {
  const key = await crypto.subtle.generateKey(
    { name: 'AES-GCM', length: 256 },
    false,
    ['encrypt', 'decrypt'],
  );
  const prefix = Uint8Array.from([0, 1, 2, 3, 4, 5, 6, 7]);
  const codec = new SecureHidReportCodec(
    new AesGcmHidSessionCipher({
      txKey: key,
      rxKey: key,
      txNoncePrefix: prefix,
      rxNoncePrefix: prefix,
    }),
  );
  const payload = Uint8Array.from({ length: 44 }, (_, index) => index);
  const report = await codec.encode({
    type: SecureHidFrameType.PERF_SAMPLE,
    flags: SecureHidFrameFlags.LAST,
    sequence: 9,
    payload,
    secure: true,
  });
  assert.deepEqual((await codec.decode(report)).payload, payload);

  report[63] ^= 0x01;
  await assert.rejects(codec.decode(report), /authentication failed/);
});

test('logical payload fragmentation is bounded to 44 bytes and reassembles', () => {
  const source = Uint8Array.from({ length: 100 }, (_, index) => index);
  const fragments = fragmentPayload(source);
  assert.deepEqual(fragments.map((fragment) => fragment.byteLength), [44, 44, 12]);

  const assembler = new FragmentAssembler(128);
  let complete = null;
  fragments.forEach((payload, index) => {
    complete = assembler.push({
      version: 1,
      type: SecureHidFrameType.RPC_RESPONSE,
      flags:
        SecureHidFrameFlags.FRAGMENTED |
        (index === fragments.length - 1 ? SecureHidFrameFlags.LAST : 0),
      payloadLength: payload.byteLength,
      sequence: index + 1,
      payload,
      secure: true,
    });
  });
  assert.deepEqual(complete, source);
});

test('PERF_SAMPLE parses the fixed 44-byte 18-key payload', () => {
  const payload = new Uint8Array(44);
  const view = new DataView(payload.buffer);
  view.setUint32(0, 123456, true);
  payload[4] = 0x01;
  payload[5] = 0x02;
  payload[6] = 0x03;
  payload[7] = 4;
  for (let index = 0; index < 18; index += 1) {
    view.setUint16(8 + index * 2, index * 100, true);
  }
  const sample = parsePerformanceSample(payload);
  assert.equal(sample.deviceTimestampUs, 123456);
  assert.equal(sample.pressedMask, 0x030201);
  assert.equal(sample.droppedSamples, 4);
  assert.equal(sample.currentDistanceUm[17], 1700);
});

test('PERF_EDGE and checkpoint cache preserve existing UI field semantics', () => {
  const payload = new Uint8Array(22);
  const view = new DataView(payload.buffer);
  view.setUint32(0, 4000, true);
  view.setUint32(4, 7, true);
  payload[8] = 2;
  payload[9] = 1;
  view.setUint16(10, 2048, true);
  [1200, 1000, 800, 700, 500].forEach((value, index) => {
    view.setUint16(12 + index * 2, value, true);
  });
  const edge = parsePerformanceEdge(payload);
  const cache = new PerformanceTelemetryCache();
  cache.applyEdge(edge);
  const snapshot = cache.snapshot();
  assert.equal(snapshot.buttonData[2].isPressed, true);
  assert.equal(snapshot.buttonData[2].currentDistance, 1.2);
  assert.equal(snapshot.buttonData[2].releaseStartDistance, 0.5);
  assert.equal(snapshot.deviceTimestampUs, 4000);
});

test('WebHID page-load connect never opens the permission chooser', async () => {
  let chooserCalls = 0;
  const hid = {
    getDevices: async () => [],
    requestDevice: async () => {
      chooserCalls += 1;
      return [];
    },
    addEventListener() {},
    removeEventListener() {},
  };
  const transport = new WebHidTransport({ navigator: hid });
  await assert.rejects(transport.connect(), /已授权/);
  assert.equal(chooserCalls, 0);
  await assert.rejects(transport.requestPermissionAndConnect(), /未选择/);
  assert.equal(chooserCalls, 1);
});

test('WebHID page-load connect requires an explicit choice when multiple devices are granted', async () => {
  const makeDevice = (productName) => ({
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName,
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    async sendReport() {},
    addEventListener() {},
    removeEventListener() {},
  });
  const first = makeDevice('HBox A');
  const second = makeDevice('HBox B');
  let chooserCalls = 0;
  const hid = {
    getDevices: async () => [first, second],
    requestDevice: async () => {
      chooserCalls += 1;
      return [second];
    },
    addEventListener() {},
    removeEventListener() {},
  };
  const transport = new WebHidTransport({ navigator: hid });

  await assert.rejects(transport.connect(), /多台已授权/);
  assert.equal(first.opened, false);
  assert.equal(second.opened, false);
  assert.equal(chooserCalls, 0);

  const selected = await transport.requestPermissionAndConnect();
  assert.equal(selected.productName, 'HBox B');
  assert.equal(second.opened, true);
  assert.equal(chooserCalls, 1);
  await transport.close();
});

test('WebHID discovery and physical open cannot leave connection loading forever', async () => {
  const blockedDiscovery = new WebHidTransport({
    openTimeoutMs: 20,
    navigator: {
      getDevices: () => new Promise(() => {}),
      requestDevice: async () => [],
      addEventListener() {},
      removeEventListener() {},
    },
  });
  await assert.rejects(blockedDiscovery.connect(), /device discovery.*超时/i);
  assert.equal(blockedDiscovery.state, DeviceTransportState.ERROR);

  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox blocked open',
    collections: [],
    open: () => new Promise(() => {}),
    async close() {},
    async sendReport() {},
    addEventListener() {},
    removeEventListener() {},
  };
  const blockedOpen = new WebHidTransport({
    openTimeoutMs: 20,
    navigator: {
      getDevices: async () => [device],
      requestDevice: async () => [device],
      addEventListener() {},
      removeEventListener() {},
    },
  });
  await assert.rejects(blockedOpen.connect(), /device open.*超时/i);
  assert.equal(blockedOpen.state, DeviceTransportState.ERROR);
});

test('firmware reconnect waits three seconds, uses the authorized reconnect callback, and is cancellable', async () => {
  const scheduled = [];
  const cancelled = [];
  let reconnectCalls = 0;
  let failure = null;
  const cancel = scheduleAuthorizedReconnect(
    async () => { reconnectCalls += 1; },
    (error) => { failure = error; },
    (callback, delayMs) => {
      scheduled.push({ callback, delayMs });
      return 123;
    },
    (timer) => { cancelled.push(timer); },
  );

  assert.equal(FIRMWARE_RECONNECT_DELAY_MS, 3000);
  assert.equal(scheduled.length, 1);
  assert.equal(scheduled[0].delayMs, 3000);
  assert.equal(reconnectCalls, 0);
  scheduled[0].callback();
  await Promise.resolve();
  assert.equal(reconnectCalls, 1);
  assert.equal(failure, null);
  cancel();
  assert.deepEqual(cancelled, [123]);
});

test('device initialization runs six named stages in order and becomes ready only after all succeed', async () => {
  const completed = [];
  const stages = [];
  let releaseLayout;
  const layoutPending = new Promise((resolve) => { releaseLayout = resolve; });
  let readyLayout = null;
  let failure = null;
  const resultPromise = initializeDeviceSession({
    loaders: {
      globalConfig: async () => { completed.push('global'); },
      screenControl: async () => { completed.push('screen'); },
      profileList: async () => { completed.push('profiles'); },
      hotkeys: async () => { completed.push('hotkeys'); },
      firmwareMetadata: async () => { completed.push('firmware'); },
      hitboxLayout: () => layoutPending,
    },
    isCurrent: () => true,
    onStage: (stage, status) => stages.push(`${stage}:${status}`),
    onReady: (layout) => { readyLayout = layout; },
    onFailure: (error) => { failure = error; },
  });

  await waitFor(() => completed.length === 5);
  assert.equal(readyLayout, null);
  assert.deepEqual(completed, ['global', 'screen', 'profiles', 'hotkeys', 'firmware']);
  releaseLayout([{ x: 1, y: 2, r: 3 }]);
  assert.equal(await resultPromise, 'ready');
  assert.deepEqual(readyLayout, [{ x: 1, y: 2, r: 3 }]);
  assert.equal(failure, null);
  assert.deepEqual(stages, [
    'global-config:started',
    'global-config:completed',
    'screen-control:started',
    'screen-control:completed',
    'profile-list:started',
    'profile-list:completed',
    'hotkeys:started',
    'hotkeys:completed',
    'firmware-metadata:started',
    'firmware-metadata:completed',
    'hitbox-layout:started',
    'hitbox-layout:completed',
  ]);
});

test('device initialization reports a current failure and ignores stale completion', async () => {
  const failure = new Error('fixture loader failed');
  let reportedFailure = null;
  const failed = await initializeDeviceSession({
    loaders: {
      globalConfig: async () => { throw failure; },
      screenControl: async () => {},
      profileList: async () => {},
      hotkeys: async () => {},
      firmwareMetadata: async () => {},
      hitboxLayout: async () => [],
    },
    isCurrent: () => true,
    onReady: () => assert.fail('failed initialization must not become ready'),
    onFailure: (error) => { reportedFailure = error; },
  });
  assert.equal(failed, 'failed');
  assert.equal(reportedFailure.stage, 'global-config');
  assert.equal(reportedFailure.cause, failure);

  let staleCallbackCalled = false;
  const stale = await initializeDeviceSession({
    loaders: {
      globalConfig: async () => {},
      screenControl: async () => {},
      profileList: async () => {},
      hotkeys: async () => {},
      firmwareMetadata: async () => {},
      hitboxLayout: async () => [],
    },
    isCurrent: () => false,
    onReady: () => { staleCallbackCalled = true; },
    onFailure: () => { staleCallbackCalled = true; },
  });
  assert.equal(stale, 'stale');
  assert.equal(staleCallbackCalled, false);
});

test('device initialization has one total deadline and late loader rejection is observed', async () => {
  let rejectLate;
  const never = new Promise((_resolve, reject) => { rejectLate = reject; });
  let reportedFailure = null;
  let unhandled = null;
  const onUnhandled = (error) => { unhandled = error; };
  process.once('unhandledRejection', onUnhandled);
  try {
    const result = await initializeDeviceSession({
      loaders: {
        globalConfig: async () => {},
        screenControl: () => never,
        profileList: async () => assert.fail('later stage must not start'),
        hotkeys: async () => assert.fail('later stage must not start'),
        firmwareMetadata: async () => assert.fail('later stage must not start'),
        hitboxLayout: async () => assert.fail('later stage must not start'),
      },
      timeoutMs: 20,
      isCurrent: () => true,
      onReady: () => assert.fail('timed out initialization must not become ready'),
      onFailure: (error) => { reportedFailure = error; },
    });
    assert.equal(result, 'failed');
    assert.equal(reportedFailure.stage, 'screen-control');
    assert.match(reportedFailure.message, /timed out/);
    rejectLate(new Error('late loader failure'));
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(unhandled, null);
  } finally {
    process.removeListener('unhandledRejection', onUnhandled);
  }
});

test('WebHID bootstrap supports fragmented request/response but protected RPC stays closed', async () => {
  const deviceCodec = new SecureHidReportCodec();
  const requestAssembler = new FragmentAssembler();
  let inputListener = null;
  let deviceSequence = 1;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox WebConfig',
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener(type, listener) {
      if (type === 'inputreport') inputListener = listener;
    },
    removeEventListener() { inputListener = null; },
    async sendReport(_reportId, source) {
      const report = source instanceof Uint8Array
        ? source
        : new Uint8Array(source.buffer, source.byteOffset || 0, source.byteLength);
      const frame = await deviceCodec.decode(report);
      const complete = requestAssembler.push(frame);
      if (!complete) return;
      const request = JSON.parse(new TextDecoder().decode(complete));
      const responseBytes = new TextEncoder().encode(JSON.stringify({
        transactionId: request.transactionId,
        errNo: 0,
        data: { accepted: true, echo: request.params.large },
      }));
      const responseFragments = fragmentPayload(responseBytes);
      for (let index = 0; index < responseFragments.length; index += 1) {
        const response = await deviceCodec.encode({
          type: SecureHidFrameType.BOOTSTRAP_RESPONSE,
          flags:
            (responseFragments.length > 1 ? SecureHidFrameFlags.FRAGMENTED : 0) |
            (index === responseFragments.length - 1 ? SecureHidFrameFlags.LAST : 0),
          sequence: deviceSequence++,
          payload: responseFragments[index],
          secure: false,
        });
        queueMicrotask(() => inputListener({
          device,
          reportId: 0,
          data: new DataView(response.buffer),
        }));
      }
    },
  };
  const hid = {
    getDevices: async () => [device],
    requestDevice: async () => [device],
    addEventListener() {},
    removeEventListener() {},
  };
  const transport = new WebHidTransport({ navigator: hid, requestTimeoutMs: 1000 });
  const session = await transport.connect();
  assert.equal(session.authenticated, false);
  assert.equal(transport.state, DeviceTransportState.AUTHENTICATING);
  await assert.rejects(transport.request('get_global_config'), /尚未通过在线证明/);
  const result = await transport.bootstrapRequest('test.bootstrap', {
    large: 'x'.repeat(120),
  });
  assert.equal(result.accepted, true);
  assert.equal(result.echo.length, 120);
  await transport.close();
});

test('stale secure device output before session setup does not consume bootstrap sequence one', async () => {
  const deviceCodec = new SecureHidReportCodec();
  const requestAssembler = new FragmentAssembler();
  const responseSequences = [];
  let inputListener = null;
  let staleDelivered = false;
  let transport;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox stale secure queue fixture',
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener(type, listener) {
      if (type === 'inputreport') inputListener = listener;
    },
    removeEventListener() { inputListener = null; },
    async sendReport(_reportId, source) {
      const report = source instanceof Uint8Array
        ? source
        : new Uint8Array(source.buffer, source.byteOffset || 0, source.byteLength);
      const complete = requestAssembler.push(await deviceCodec.decode(report));
      if (!complete) return;
      const request = JSON.parse(new TextDecoder().decode(complete));

      if (!staleDelivered) {
        staleDelivered = true;
        const staleTypes = [
          SecureHidFrameType.RPC_RESPONSE,
          SecureHidFrameType.EVENT,
          SecureHidFrameType.PERF_SAMPLE,
          SecureHidFrameType.PERF_EDGE,
          SecureHidFrameType.PERF_CHECKPOINT,
          SecureHidFrameType.ERROR,
        ];
        staleTypes.forEach((type, index) => {
          const stale = new Uint8Array(SECURE_HID_REPORT_SIZE);
          stale[0] = SECURE_HID_REPORT_VERSION;
          stale[1] = type;
          stale[2] = SecureHidFrameFlags.SECURE | SecureHidFrameFlags.LAST;
          new DataView(stale.buffer).setUint32(4, 100 + index, true);
          stale.fill(index + 1, SECURE_HID_REPORT_SIZE - 12);
          inputListener({
            device,
            reportId: 0,
            data: new DataView(stale.buffer),
          });
        });
        await new Promise((resolve) => setImmediate(resolve));
        assert.equal(
          transport.lastRxSequence,
          0,
          'discarded reports must not advance the new bootstrap generation',
        );
      }

      const payload = new TextEncoder().encode(JSON.stringify({
        transactionId: request.transactionId,
        errNo: 0,
        data: { accepted: true },
      }));
      const fragments = fragmentPayload(payload);
      for (const [index, fragment] of fragments.entries()) {
        const sequence = index + 1;
        responseSequences.push(sequence);
        const response = await deviceCodec.encode({
          type: SecureHidFrameType.BOOTSTRAP_RESPONSE,
          flags:
            (fragments.length > 1 ? SecureHidFrameFlags.FRAGMENTED : 0) |
            (index === fragments.length - 1 ? SecureHidFrameFlags.LAST : 0),
          sequence,
          payload: fragment,
          secure: false,
        });
        inputListener({
          device,
          reportId: 0,
          data: new DataView(response.buffer),
        });
      }
    },
  };
  transport = new WebHidTransport({
    navigator: makeHidNavigator(device),
    requestTimeoutMs: 1000,
  });
  const errors = [];
  transport.onError((error) => errors.push(error));
  await transport.connect();

  const result = await transport.bootstrapRequest('device.public-info', {});
  assert.deepEqual(result, { accepted: true });
  assert.equal(responseSequences[0], 1);
  assert.equal(transport.state, DeviceTransportState.AUTHENTICATING);
  assert.equal(errors.length, 0);
  await transport.close();
});

test('a permanently pending permit sendReport stays fatal, isolates its generation, and settles once', async () => {
  let releaseWrite;
  const blockedWrite = new Promise((resolve) => { releaseWrite = resolve; });
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox WebConfig',
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener() {},
    removeEventListener() {},
    sendReport() { return blockedWrite; },
  };
  const transport = new WebHidTransport({
    navigator: makeHidNavigator(device),
    requestTimeoutMs: 1000,
    closeTimeoutMs: 25,
  });
  await transport.connect();

  let unhandled = null;
  const onUnhandled = (error) => { unhandled = error; };
  process.once('unhandledRejection', onUnhandled);
  try {
    const startedAt = Date.now();
    await assert.rejects(
      transport.bootstrapRequest('session.install-permit', {}, { timeoutMs: 20 }),
      (error) =>
        !(error instanceof RecoverableBootstrapResponseTimeoutError) &&
        /session\.install-permit.*超时/.test(error.message),
    );
    assert.ok(Date.now() - startedAt < 250, 'operation deadline must bound sendReport');
    assert.equal(transport.session, null);
    assert.equal(transport.pendingBootstrap.size, 0);
    const failedGeneration = transport.connectionGeneration;

    // A late physical write completion belongs to the invalidated generation
    // and must neither revive the request nor create an unhandled rejection.
    releaseWrite();
    await new Promise((resolve) => setImmediate(resolve));
    await waitFor(() => transport.state === DeviceTransportState.DISCONNECTED, 250);
    assert.equal(transport.connectionGeneration, failedGeneration);
    assert.equal(transport.pendingBootstrap.size, 0);
    assert.equal(unhandled, null);
    await assert.rejects(
      transport.connect(),
      /拔下设备 USB/,
      'a timed-out native writer must quarantine the old HIDDevice object',
    );
  } finally {
    process.removeListener('unhandledRejection', onUnhandled);
  }
});

test('the Nth fragment may hang forever without interleaving, leaking pending RPCs, or reviving its generation', async () => {
  let releaseThirdWrite;
  const thirdWrite = new Promise((resolve) => { releaseThirdWrite = resolve; });
  let writes = 0;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox fragmented blocked-write fixture',
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener() {},
    removeEventListener() {},
    sendReport() {
      writes += 1;
      return writes === 3 ? thirdWrite : Promise.resolve();
    },
  };
  const transport = new WebHidTransport({
    navigator: makeHidNavigator(device),
    requestTimeoutMs: 1000,
    closeTimeoutMs: 20,
  });
  await transport.connect();

  let unhandled = null;
  const onUnhandled = (error) => { unhandled = error; };
  process.once('unhandledRejection', onUnhandled);
  try {
    await assert.rejects(
      transport.bootstrapRequest(
        'blocked.fragment',
        { marker: 'F'.repeat(320) },
        { timeoutMs: 25 },
      ),
      /blocked\.fragment.*超时/,
    );
    assert.equal(writes, 3);
    assert.equal(transport.pendingBootstrap.size, 0);
    const failedGeneration = transport.connectionGeneration;
    await waitFor(() => transport.state === DeviceTransportState.DISCONNECTED, 250);

    releaseThirdWrite();
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(writes, 3, 'no later fragment may escape the invalidated writer');
    assert.equal(transport.connectionGeneration, failedGeneration);
    assert.equal(transport.state, DeviceTransportState.DISCONNECTED);
    assert.equal(transport.pendingBootstrap.size, 0);
    assert.equal(unhandled, null);
  } finally {
    process.removeListener('unhandledRejection', onUnhandled);
  }
});

test('a completed logical write with no response has one deadline and leaves no pending transaction', async () => {
  let writes = 0;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox missing-response fixture',
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener() {},
    removeEventListener() {},
    async sendReport() { writes += 1; },
  };
  const transport = new WebHidTransport({
    navigator: makeHidNavigator(device),
    requestTimeoutMs: 1000,
    closeTimeoutMs: 20,
  });
  await transport.connect();

  await assert.rejects(
    transport.bootstrapRequest(
      'missing.response',
      { marker: 'R'.repeat(180) },
      { timeoutMs: 25 },
    ),
    /missing\.response.*超时/,
  );
  assert.ok(writes >= 2, 'the fragmented logical write must finish before response timeout');
  assert.equal(transport.pendingBootstrap.size, 0);
  await waitFor(() => transport.state === DeviceTransportState.DISCONNECTED, 250);
  assert.equal(transport.session, null);
  assert.equal(transport.pendingBootstrap.size, 0);
});

test('screen background previews are loaded only by an explicit user action', () => {
  const source = fs.readFileSync(path.join(
    __dirname,
    '..',
    'components',
    'screen-control-setting-content.tsx',
  ), 'utf8');
  assert.doesNotMatch(
    source,
    /useEffect\(\(\) => \{\s*if \(!deviceConnected\) return;\s*void fetchBgImagesFromDeviceOnce\(\);/,
  );
  assert.match(
    source,
    /key="load-previews"[\s\S]*?onClick=\{\(\) => void fetchBgImagesFromDeviceOnce\(\)\}/,
  );
});

test('recoverable bootstrap response timeouts allow two bounded same-handle resynchronizations', async () => {
  const runScenario = async (timedOutCommand, failuresBeforeSuccess) => {
    let openCalls = 0;
    let closeCalls = 0;
    let authenticateCalls = 0;
    let resynchronizeCalls = 0;
    const device = {
      opened: false,
      vendorId: 0xcafe,
      productId: 0x4021,
      productName: 'HBox stale-attestation fixture',
      collections: [],
      async open() { openCalls += 1; this.opened = true; },
      async close() { closeCalls += 1; this.opened = false; },
      addEventListener() {},
      removeEventListener() {},
      async sendReport() {},
    };
    const transport = new WebHidTransport({
      navigator: makeHidNavigator(device),
      closeTimeoutMs: 20,
    });
    const originalResynchronize =
      transport.resynchronizeBootstrap.bind(transport);
    transport.resynchronizeBootstrap = () => {
      resynchronizeCalls += 1;
      originalResynchronize();
    };
    const auth = {
      clear() {},
      async authenticate(currentTransport, scopes) {
        authenticateCalls += 1;
        if (authenticateCalls <= failuresBeforeSuccess) {
          throw new RecoverableBootstrapResponseTimeoutError(
            timedOutCommand,
            `命令 ${timedOutCommand} 响应超时`,
          );
        }
        currentTransport.session = {
          transport: 'webhid',
          authenticated: true,
          scopes: [...scopes],
          sessionId: 'resynchronized-session',
        };
        currentTransport.state = DeviceTransportState.CONNECTED;
        return currentTransport.session;
      },
    };
    const client = new DeviceCommandClient(transport, auth);
    return {
      client,
      transport,
      device,
      counters: () => ({
        openCalls,
        closeCalls,
        authenticateCalls,
        resynchronizeCalls,
      }),
    };
  };

  for (const command of ['attestation.create', 'session.install-permit']) {
    const recovered = await runScenario(command, 1);
    await recovered.client.connect();
    assert.deepEqual(recovered.counters(), {
      openCalls: 1,
      closeCalls: 0,
      authenticateCalls: 2,
      resynchronizeCalls: 1,
    });
    assert.equal(recovered.client.getState(), DeviceTransportState.CONNECTED);
    recovered.client.dispose();
    await waitFor(() => !recovered.device.opened, 250);

    const recoveredAfterTwoLostAcks = await runScenario(command, 2);
    await recoveredAfterTwoLostAcks.client.connect();
    assert.deepEqual(recoveredAfterTwoLostAcks.counters(), {
      openCalls: 1,
      closeCalls: 0,
      authenticateCalls: 3,
      resynchronizeCalls: 2,
    });
    assert.equal(
      recoveredAfterTwoLostAcks.client.getState(),
      DeviceTransportState.CONNECTED,
    );
    recoveredAfterTwoLostAcks.client.dispose();
    await waitFor(() => !recoveredAfterTwoLostAcks.device.opened, 250);

    const failed = await runScenario(command, 3);
    await assert.rejects(
      failed.client.connect(),
      new RegExp(`${command.replace('.', '\\.')}.*超时`),
    );
    assert.deepEqual(failed.counters(), {
      openCalls: 1,
      closeCalls: 1,
      authenticateCalls: 3,
      resynchronizeCalls: 2,
    });
    assert.equal(failed.client.getState(), DeviceTransportState.DISCONNECTED);
    assert.equal(failed.transport.session, null);
    failed.client.dispose();
  }
});

test('bootstrap write-stage and ordinary RPC timeouts never enter bootstrap resynchronization', async () => {
  const makeClient = (authenticate) => {
    let resynchronizeCalls = 0;
    let closeCalls = 0;
    const device = {
      opened: false,
      vendorId: 0xcafe,
      productId: 0x4021,
      productName: 'HBox non-recoverable timeout fixture',
      collections: [],
      async open() { this.opened = true; },
      async close() { closeCalls += 1; this.opened = false; },
      addEventListener() {},
      removeEventListener() {},
      async sendReport() {},
    };
    const transport = new WebHidTransport({
      navigator: makeHidNavigator(device),
      closeTimeoutMs: 20,
    });
    const originalResynchronize = transport.resynchronizeBootstrap.bind(transport);
    transport.resynchronizeBootstrap = () => {
      resynchronizeCalls += 1;
      originalResynchronize();
    };
    return {
      client: new DeviceCommandClient(transport, { clear() {}, authenticate }),
      transport,
      counters: () => ({ resynchronizeCalls, closeCalls }),
    };
  };

  let writeStageAuthCalls = 0;
  const writeStage = makeClient(async () => {
    writeStageAuthCalls += 1;
    throw new DeviceTransportError(
      'timeout',
      '命令 session.install-permit 操作超时',
    );
  });
  await assert.rejects(
    writeStage.client.connect(),
    /session\.install-permit.*超时/,
  );
  assert.equal(writeStageAuthCalls, 1);
  assert.deepEqual(writeStage.counters(), {
    resynchronizeCalls: 0,
    closeCalls: 1,
  });

  const ordinaryRpc = makeClient(async (transport, scopes) => {
    transport.session = {
      transport: 'webhid',
      authenticated: true,
      scopes: [...scopes],
      sessionId: 'ordinary-rpc-session',
    };
    transport.state = DeviceTransportState.CONNECTED;
    return transport.session;
  });
  await ordinaryRpc.client.connect();
  let rpcCalls = 0;
  ordinaryRpc.transport.request = async () => {
    rpcCalls += 1;
    throw new DeviceTransportError('timeout', '命令 get_global_config 操作超时');
  };
  await assert.rejects(
    ordinaryRpc.client.request('get_global_config'),
    /get_global_config.*超时/,
  );
  assert.equal(rpcCalls, 1);
  assert.equal(ordinaryRpc.counters().resynchronizeCalls, 0);
  ordinaryRpc.client.dispose();
});

test('bootstrap retries share the original startup hard deadline', async () => {
  let authenticateCalls = 0;
  let resynchronizeCalls = 0;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox retry startup deadline fixture',
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener() {},
    removeEventListener() {},
    async sendReport() {},
  };
  const transport = new WebHidTransport({
    navigator: makeHidNavigator(device),
    closeTimeoutMs: 20,
  });
  const originalResynchronize = transport.resynchronizeBootstrap.bind(transport);
  transport.resynchronizeBootstrap = () => {
    resynchronizeCalls += 1;
    originalResynchronize();
  };
  const auth = {
    clear() {},
    async authenticate(_transport, _scopes, signal) {
      authenticateCalls += 1;
      if (authenticateCalls === 1) {
        throw new RecoverableBootstrapResponseTimeoutError(
          'session.install-permit',
          '命令 session.install-permit 响应超时',
        );
      }
      return await new Promise((_resolve, reject) => {
        const abort = () => reject(new DeviceTransportError(
          'disconnected',
          '设备认证流程已被启动总截止取消',
        ));
        if (signal.aborted) abort();
        else signal.addEventListener('abort', abort, { once: true });
      });
    },
  };
  const client = new DeviceCommandClient(
    transport,
    auth,
    DEFAULT_DEVICE_SCOPES,
    30,
  );
  const startedAt = Date.now();
  await assert.rejects(client.connect(), /启动总截止取消/);
  assert.ok(Date.now() - startedAt < 250);
  assert.equal(authenticateCalls, 2);
  assert.equal(resynchronizeCalls, 1);
  assert.equal(client.getState(), DeviceTransportState.DISCONNECTED);
  client.dispose();
});

for (const bootstrapCommand of ['attestation.create', 'session.install-permit']) {
for (const staleResponseSequence of [1, 7]) {
test(`a late settled ${bootstrapCommand} response at old sequence ${staleResponseSequence} is drained only in the immediately resynchronized bootstrap generation`, async () => {
  const codec = new SecureHidReportCodec();
  const requests = new FragmentAssembler();
  let inputListener = null;
  let firstTransactionId = 0;
  let requestCount = 0;
  let responseSequence = 1;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox late bootstrap fixture',
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener(type, listener) {
      if (type === 'inputreport') inputListener = listener;
    },
    removeEventListener() {},
    async sendReport(_reportId, source) {
      const report = source instanceof Uint8Array
        ? source
        : new Uint8Array(source.buffer, source.byteOffset || 0, source.byteLength);
      const complete = requests.push(await codec.decode(report));
      if (!complete) return;
      const request = JSON.parse(new TextDecoder().decode(complete));
      requestCount += 1;
      if (requestCount === 1) {
        firstTransactionId = request.transactionId;
        return;
      }
      const emitResponse = async (transactionId, attempt) => {
        const payload = new TextEncoder().encode(JSON.stringify({
          transactionId,
          errNo: 0,
          data: { attempt },
        }));
        for (const [index, fragment] of fragmentPayload(payload).entries()) {
          const fragments = fragmentPayload(payload);
          const encoded = await codec.encode({
            type: SecureHidFrameType.BOOTSTRAP_RESPONSE,
            flags:
              (fragments.length > 1 ? SecureHidFrameFlags.FRAGMENTED : 0) |
              (index === fragments.length - 1 ? SecureHidFrameFlags.LAST : 0),
            sequence: responseSequence++,
            payload: fragment,
            secure: false,
          });
          queueMicrotask(() => inputListener?.({
            device,
            reportId: 0,
            data: new DataView(encoded.buffer),
          }));
        }
      };
      responseSequence = staleResponseSequence;
      await emitResponse(firstTransactionId, 1);
      // The retry request is a cleartext sequence-one takeover.  Firmware
      // resets its logical output generation even if a previous response was
      // already queued in the physical USB IN path.
      responseSequence = 1;
      await emitResponse(request.transactionId, 2);
    },
  };
  const transport = new WebHidTransport({
    navigator: makeHidNavigator(device),
    requestTimeoutMs: 1000,
  });
  await transport.connect();
  await assert.rejects(
    transport.bootstrapRequest(bootstrapCommand, {}, { timeoutMs: 20 }),
    new RegExp(`${bootstrapCommand.replace('.', '\\.')}.*超时`),
  );

  transport.resynchronizeBootstrap();
  responseSequence = 1;
  const retry = await transport.bootstrapRequest(
    bootstrapCommand,
    {},
    { timeoutMs: 200 },
  );
  assert.deepEqual(retry, { attempt: 2 });

  const unknownRpcPayload = new TextEncoder().encode(JSON.stringify({
    transactionId: 0x7fffffff,
    errNo: 0,
    data: {},
  }));
  assert.throws(
    () => transport.handleLogicalResponse({
      type: SecureHidFrameType.RPC_RESPONSE,
      flags: SecureHidFrameFlags.LAST,
      sequence: 99,
      payload: unknownRpcPayload,
      secure: false,
    }, transport.pendingRpc),
    /Unknown HID transaction/,
    'unknown protected RPC transactions must remain fail-closed',
  );
  await transport.close();
});
}
}

test('a new bootstrap sequence one ends a truncated old-response drain', async () => {
  const codec = new SecureHidReportCodec();
  const requests = new FragmentAssembler();
  let inputListener = null;
  let requestCount = 0;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox truncated late permit ACK fixture',
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener(type, listener) {
      if (type === 'inputreport') inputListener = listener;
    },
    removeEventListener() {},
    async sendReport(_reportId, source) {
      const report = source instanceof Uint8Array
        ? source
        : new Uint8Array(source.buffer, source.byteOffset || 0, source.byteLength);
      const complete = requests.push(await codec.decode(report));
      if (!complete) return;
      const request = JSON.parse(new TextDecoder().decode(complete));
      requestCount += 1;
      if (requestCount === 1) return;

      const stalePayload = new TextEncoder().encode(JSON.stringify({
        transactionId: request.transactionId - 1,
        errNo: 0,
        data: { accepted: true, padding: 's'.repeat(80) },
      }));
      const staleFragment = fragmentPayload(stalePayload)[0];
      const staleReport = await codec.encode({
        type: SecureHidFrameType.BOOTSTRAP_RESPONSE,
        flags: SecureHidFrameFlags.FRAGMENTED,
        sequence: 7,
        payload: staleFragment,
        secure: false,
      });
      queueMicrotask(() => inputListener?.({
        device,
        reportId: 0,
        data: new DataView(staleReport.buffer),
      }));

      const currentPayload = new TextEncoder().encode(JSON.stringify({
        transactionId: request.transactionId,
        errNo: 0,
        data: { accepted: true, generation: 'current' },
      }));
      const currentFragments = fragmentPayload(currentPayload);
      for (const [index, fragment] of currentFragments.entries()) {
        const currentReport = await codec.encode({
          type: SecureHidFrameType.BOOTSTRAP_RESPONSE,
          flags:
            (currentFragments.length > 1 ? SecureHidFrameFlags.FRAGMENTED : 0) |
            (index === currentFragments.length - 1 ? SecureHidFrameFlags.LAST : 0),
          sequence: index + 1,
          payload: fragment,
          secure: false,
        });
        queueMicrotask(() => inputListener?.({
          device,
          reportId: 0,
          data: new DataView(currentReport.buffer),
        }));
      }
    },
  };
  const transport = new WebHidTransport({
    navigator: makeHidNavigator(device),
    requestTimeoutMs: 1000,
  });
  await transport.connect();
  await assert.rejects(
    transport.bootstrapRequest('session.install-permit', {}, { timeoutMs: 20 }),
    /session\.install-permit.*超时/,
  );

  transport.resynchronizeBootstrap();
  const retry = await transport.bootstrapRequest(
    'session.install-permit',
    {},
    { timeoutMs: 200 },
  );
  assert.deepEqual(retry, { accepted: true, generation: 'current' });
  assert.equal(transport.state, DeviceTransportState.AUTHENTICATING);
  await transport.close();
});

test('WebHID close is bounded when the platform close promise never settles', async () => {
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox WebConfig',
    collections: [],
    async open() { this.opened = true; },
    close() { return new Promise(() => {}); },
    addEventListener() {},
    removeEventListener() {},
    async sendReport() {},
  };
  const transport = new WebHidTransport({
    navigator: makeHidNavigator(device),
    closeTimeoutMs: 20,
  });
  await transport.connect();
  const startedAt = Date.now();
  await transport.close();
  assert.ok(Date.now() - startedAt < 250, 'close must not inherit a permanently pending HID close');
  assert.equal(transport.state, DeviceTransportState.DISCONNECTED);
  assert.equal(transport.session, null);
});

test('a timed-out close quarantines the handle until disconnect and the old close both settle', async () => {
  let releaseClose;
  const nativeClose = new Promise((resolve) => { releaseClose = resolve; });
  let openCalls = 0;
  let disconnectListener = null;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox late-close fixture',
    collections: [],
    async open() { openCalls += 1; this.opened = true; },
    async close() { await nativeClose; this.opened = false; },
    addEventListener() {},
    removeEventListener() {},
    async sendReport() {},
  };
  const hid = {
    async getDevices() { return [device]; },
    async requestDevice() { return [device]; },
    addEventListener(type, listener) {
      if (type === 'disconnect') disconnectListener = listener;
    },
    removeEventListener() {},
  };
  const transport = new WebHidTransport({
    navigator: hid,
    closeTimeoutMs: 20,
  });
  await transport.connect();
  await transport.close();
  assert.equal(openCalls, 1);
  await assert.rejects(transport.connect(), /拔下设备 USB/);
  assert.equal(openCalls, 1);

  disconnectListener({ device });
  await assert.rejects(
    transport.connect(),
    /拔下设备 USB/,
    'disconnect alone cannot make a still-pending old close safe',
  );
  releaseClose();
  await waitFor(() => !device.opened, 250);
  await transport.connect();
  assert.equal(openCalls, 2, 'reopen is allowed only after the old close can no longer arrive late');
  await transport.close();
});

test('an abort signal bounds a queued WebHID operation and invalidates stale writers', async () => {
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox WebConfig',
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener() {},
    removeEventListener() {},
    sendReport() { return new Promise(() => {}); },
  };
  const transport = new WebHidTransport({
    navigator: makeHidNavigator(device),
    closeTimeoutMs: 20,
  });
  await transport.connect();
  const controller = new AbortController();
  const pending = transport.bootstrapRequest(
    'cancelled.write',
    {},
    { signal: controller.signal, timeoutMs: 1000 },
  );
  controller.abort(new Error('test cancellation'));
  await assert.rejects(pending, /已取消/);
  assert.equal(transport.session, null);
  await waitFor(() => transport.state === DeviceTransportState.DISCONNECTED, 250);
});

test('concurrent multi-fragment logical requests never interleave and sequence follows write order', async () => {
  const deviceCodec = new SecureHidReportCodec();
  const requestAssembler = new FragmentAssembler();
  const completedCommands = [];
  const writtenSequences = [];
  let inputListener = null;
  let deviceSequence = 1;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox WebConfig',
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener(type, listener) {
      if (type === 'inputreport') inputListener = listener;
    },
    removeEventListener() { inputListener = null; },
    async sendReport(_reportId, source) {
      // Yield on every physical write to make an operation-level locking bug
      // deterministically interleave the two callers.
      await new Promise((resolve) => setImmediate(resolve));
      const report = source instanceof Uint8Array
        ? source
        : new Uint8Array(source.buffer, source.byteOffset || 0, source.byteLength);
      const frame = await deviceCodec.decode(report);
      writtenSequences.push(frame.sequence);
      const complete = requestAssembler.push(frame);
      if (!complete) return;
      const request = JSON.parse(new TextDecoder().decode(complete));
      completedCommands.push(request.command);
      const responsePayload = new TextEncoder().encode(JSON.stringify({
        transactionId: request.transactionId,
        errNo: 0,
        data: { command: request.command },
      }));
      const fragments = fragmentPayload(responsePayload);
      for (let index = 0; index < fragments.length; index += 1) {
        const response = await deviceCodec.encode({
          type: SecureHidFrameType.BOOTSTRAP_RESPONSE,
          flags:
            (fragments.length > 1 ? SecureHidFrameFlags.FRAGMENTED : 0) |
            (index === fragments.length - 1 ? SecureHidFrameFlags.LAST : 0),
          sequence: deviceSequence++,
          payload: fragments[index],
          secure: false,
        });
        queueMicrotask(() => inputListener?.({
          device,
          reportId: 0,
          data: new DataView(response.buffer),
        }));
      }
    },
  };
  const hid = makeHidNavigator(device);
  const transport = new WebHidTransport({ navigator: hid, requestTimeoutMs: 1000 });
  await transport.connect();

  const first = transport.bootstrapRequest('first.request', {
    marker: 'A'.repeat(240),
  });
  const second = transport.bootstrapRequest('second.request', {
    marker: 'B'.repeat(240),
  });
  const [firstResult, secondResult] = await Promise.all([first, second]);

  assert.equal(firstResult.command, 'first.request');
  assert.equal(secondResult.command, 'second.request');
  assert.deepEqual(completedCommands, ['first.request', 'second.request']);
  assert.deepEqual(
    writtenSequences,
    Array.from({ length: writtenSequences.length }, (_, index) => index + 1),
  );
  await transport.close();
});

test('an authenticated input tag failure destroys the session and stale readers cannot revive ERROR', async () => {
  const key = await crypto.subtle.generateKey(
    { name: 'AES-GCM', length: 256 },
    false,
    ['encrypt', 'decrypt'],
  );
  const prefix = Uint8Array.from([0, 1, 2, 3, 4, 5, 6, 7]);
  const cipher = new AesGcmHidSessionCipher({
    txKey: key,
    rxKey: key,
    txNoncePrefix: prefix,
    rxNoncePrefix: prefix,
  });
  const incomingCodec = new SecureHidReportCodec(cipher);
  let inputListener = null;
  let closeCalls = 0;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox WebConfig',
    collections: [],
    async open() { this.opened = true; },
    async close() {
      closeCalls += 1;
      this.opened = false;
    },
    addEventListener(type, listener) {
      if (type === 'inputreport') inputListener = listener;
    },
    removeEventListener() {},
    async sendReport() {},
  };
  const transport = new WebHidTransport({
    navigator: makeHidNavigator(device),
    requestTimeoutMs: 1000,
  });
  await transport.connect();
  transport.establishSecureSession(cipher, {
    transport: 'webhid',
    authenticated: true,
    scopes: ['monitor.read'],
    sessionId: 'test-session',
    expiresAt: Date.now() + 60_000,
  });
  const errors = [];
  transport.onError((error) => errors.push(error));

  const pending = transport.request('performance.clock-sync', { sampleId: 1 });
  await new Promise((resolve) => setImmediate(resolve));
  const corrupted = await incomingCodec.encode({
    type: SecureHidFrameType.PERF_SAMPLE,
    flags: SecureHidFrameFlags.LAST,
    sequence: 1,
    payload: new Uint8Array(44),
    secure: true,
  });
  corrupted[63] ^= 0x80;
  const listener = inputListener;
  listener({
    device,
    reportId: 0,
    data: new DataView(corrupted.buffer),
  });
  // This callback was queued under the old generation. It must be ignored
  // after the first frame closes the connection.
  listener({
    device,
    reportId: 0,
    data: new DataView(corrupted.buffer),
  });

  await assert.rejects(pending, /authentication failed/);
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(closeCalls, 1);
  assert.equal(errors.length, 1);
  assert.equal(transport.session, null);
  assert.equal(transport.state, DeviceTransportState.DISCONNECTED);
});

test('authenticated RX gaps fail closed because V1 cannot prove missing reports were telemetry', async () => {
  const key = await crypto.subtle.generateKey(
    { name: 'AES-GCM', length: 256 },
    false,
    ['encrypt', 'decrypt'],
  );
  const prefix = Uint8Array.from([2, 4, 6, 8, 10, 12, 14, 16]);
  const cipher = new AesGcmHidSessionCipher({
    txKey: key,
    rxKey: key,
    txNoncePrefix: prefix,
    rxNoncePrefix: prefix,
  });
  const incomingCodec = new SecureHidReportCodec(cipher);
  let inputListener = null;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox WebConfig',
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener(type, listener) {
      if (type === 'inputreport') inputListener = listener;
    },
    removeEventListener() {},
    async sendReport() {},
  };
  const transport = new WebHidTransport({ navigator: makeHidNavigator(device) });
  await transport.connect();
  transport.establishSecureSession(cipher, {
    transport: 'webhid',
    authenticated: true,
    scopes: ['monitor.read'],
    sessionId: 'sequence-test',
    expiresAt: Date.now() + 60_000,
  });
  let samples = 0;
  transport.subscribe('performance.sample', () => { samples += 1; });
  const makeSample = (sequence) => incomingCodec.encode({
    type: SecureHidFrameType.PERF_SAMPLE,
    flags: SecureHidFrameFlags.LAST,
    sequence,
    payload: new Uint8Array(44),
    secure: true,
  });
  const first = await makeSample(1);
  const third = await makeSample(3);
  inputListener({ device, reportId: 0, data: new DataView(first.buffer) });
  inputListener({ device, reportId: 0, data: new DataView(third.buffer) });
  await waitFor(() => transport.state === DeviceTransportState.DISCONNECTED);
  assert.equal(samples, 1);
  assert.equal(transport.state, DeviceTransportState.DISCONNECTED);
  assert.equal(transport.session, null);
});

test('a gap inside a fragmented authenticated RPC rejects pending work and never reassembles across it', async () => {
  const key = await crypto.subtle.generateKey(
    { name: 'AES-GCM', length: 256 },
    false,
    ['encrypt', 'decrypt'],
  );
  const prefix = Uint8Array.from([16, 15, 14, 13, 12, 11, 10, 9]);
  const cipher = new AesGcmHidSessionCipher({
    txKey: key,
    rxKey: key,
    txNoncePrefix: prefix,
    rxNoncePrefix: prefix,
  });
  const codec = new SecureHidReportCodec(cipher);
  let inputListener = null;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox WebConfig',
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener(type, listener) {
      if (type === 'inputreport') inputListener = listener;
    },
    removeEventListener() {},
    async sendReport() {},
  };
  const transport = new WebHidTransport({
    navigator: makeHidNavigator(device),
    requestTimeoutMs: 1000,
  });
  await transport.connect();
  transport.establishSecureSession(cipher, {
    transport: 'webhid',
    authenticated: true,
    scopes: ['monitor.read'],
    sessionId: 'fragment-gap-test',
    expiresAt: Date.now() + 60_000,
  });

  const pending = transport.request('performance.clock-sync', { sampleId: 7 });
  await new Promise((resolve) => setImmediate(resolve));
  const payload = new TextEncoder().encode(JSON.stringify({
    transactionId: 1,
    errNo: 0,
    data: { marker: 'R'.repeat(120) },
  }));
  const fragments = fragmentPayload(payload);
  assert.ok(fragments.length >= 3);
  const first = await codec.encode({
    type: SecureHidFrameType.RPC_RESPONSE,
    flags: SecureHidFrameFlags.FRAGMENTED,
    sequence: 1,
    payload: fragments[0],
    secure: true,
  });
  const afterGap = await codec.encode({
    type: SecureHidFrameType.RPC_RESPONSE,
    flags: SecureHidFrameFlags.FRAGMENTED | SecureHidFrameFlags.LAST,
    sequence: 3,
    payload: fragments[2],
    secure: true,
  });
  inputListener({ device, reportId: 0, data: new DataView(first.buffer) });
  inputListener({ device, reportId: 0, data: new DataView(afterGap.buffer) });

  await assert.rejects(
    pending,
    /Authenticated HID sequence gap: expected 2, received 3.*reconnect required/,
  );
  await waitFor(() => transport.state === DeviceTransportState.DISCONNECTED);
  assert.equal(transport.session, null);
  assert.equal(transport.assemblers.size, 0);
  assert.equal(transport.pendingRpc.size, 0);
});

test('TX sequence exhaustion destroys the session and stale input cannot revive it', async () => {
  const codec = new SecureHidReportCodec();
  const writtenSequences = [];
  let inputListener = null;
  let closeCalls = 0;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox WebConfig',
    collections: [],
    async open() { this.opened = true; },
    async close() {
      closeCalls += 1;
      this.opened = false;
    },
    addEventListener(type, listener) {
      if (type === 'inputreport') inputListener = listener;
    },
    removeEventListener() {},
    async sendReport(_reportId, report) {
      writtenSequences.push((await codec.decode(report)).sequence);
    },
  };
  const transport = new WebHidTransport({
    navigator: makeHidNavigator(device),
  });
  await transport.connect();
  const errors = [];
  transport.onError((error) => errors.push(error));
  transport.nextSequence = 0xffffffff;

  await transport.sendFrame(
    SecureHidFrameType.BOOTSTRAP_REQUEST,
    new Uint8Array([1]),
    SecureHidFrameFlags.LAST,
    false,
  );
  assert.deepEqual(writtenSequences, [0xffffffff]);
  await assert.rejects(
    transport.sendFrame(
      SecureHidFrameType.BOOTSTRAP_REQUEST,
      new Uint8Array([2]),
      SecureHidFrameFlags.LAST,
      false,
    ),
    /sequence exhausted/,
  );
  assert.equal(closeCalls, 1);
  assert.equal(errors.length, 1);
  assert.equal(transport.session, null);
  assert.equal(transport.state, DeviceTransportState.DISCONNECTED);

  const stale = await codec.encode({
    type: SecureHidFrameType.BOOTSTRAP_RESPONSE,
    flags: SecureHidFrameFlags.LAST,
    sequence: 1,
    payload: new Uint8Array([0]),
    secure: false,
  });
  inputListener({
    device,
    reportId: 0,
    data: new DataView(stale.buffer),
  });
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(transport.session, null);
  assert.equal(transport.state, DeviceTransportState.DISCONNECTED);
});

test('a clean reconnect starts a new HID sequence generation at one', async () => {
  const codec = new SecureHidReportCodec();
  const sequences = [];
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox WebConfig',
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener() {},
    removeEventListener() {},
    async sendReport(_reportId, report) {
      sequences.push((await codec.decode(report)).sequence);
    },
  };
  const transport = new WebHidTransport({
    navigator: makeHidNavigator(device),
  });
  for (let generation = 0; generation < 2; generation += 1) {
    await transport.connect();
    await transport.sendFrame(
      SecureHidFrameType.BOOTSTRAP_REQUEST,
      new Uint8Array([generation]),
      SecureHidFrameFlags.LAST,
      false,
    );
    await transport.close();
  }
  assert.deepEqual(sequences, [1, 1]);
  assert.equal(transport.session, null);
  assert.equal(transport.state, DeviceTransportState.DISCONNECTED);
});

test('scope reauthorization ends the encrypted session only after its secure ACK', async () => {
  const key = await crypto.subtle.generateKey(
    { name: 'AES-GCM', length: 256 },
    false,
    ['encrypt', 'decrypt'],
  );
  const prefix = Uint8Array.from([9, 8, 7, 6, 5, 4, 3, 2]);
  const cipher = new AesGcmHidSessionCipher({
    txKey: key,
    rxKey: key,
    txNoncePrefix: prefix,
    rxNoncePrefix: prefix,
  });
  const deviceCodec = new SecureHidReportCodec(cipher);
  const assembler = new FragmentAssembler();
  let inputListener = null;
  let responseSequence = 1;
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox WebConfig',
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener(type, listener) {
      if (type === 'inputreport') inputListener = listener;
    },
    removeEventListener() {},
    async sendReport(_reportId, source) {
      const report = source instanceof Uint8Array
        ? source
        : new Uint8Array(source.buffer, source.byteOffset || 0, source.byteLength);
      const complete = assembler.push(await deviceCodec.decode(report));
      if (!complete) return;
      const request = JSON.parse(new TextDecoder().decode(complete));
      assert.equal(request.command, 'session.end');
      const payload = new TextEncoder().encode(JSON.stringify({
        transactionId: request.transactionId,
        errNo: 0,
        data: { ended: true },
      }));
      for (const [index, fragment] of fragmentPayload(payload).entries()) {
        const fragments = fragmentPayload(payload);
        const response = await deviceCodec.encode({
          type: SecureHidFrameType.RPC_RESPONSE,
          flags:
            (fragments.length > 1 ? SecureHidFrameFlags.FRAGMENTED : 0) |
            (index === fragments.length - 1 ? SecureHidFrameFlags.LAST : 0),
          sequence: responseSequence++,
          payload: fragment,
          secure: true,
        });
        queueMicrotask(() => inputListener?.({
          device,
          reportId: 0,
          data: new DataView(response.buffer),
        }));
      }
    },
  };
  const transport = new WebHidTransport({ navigator: makeHidNavigator(device) });
  await transport.connect();
  transport.establishSecureSession(cipher, {
    transport: 'webhid',
    authenticated: true,
    scopes: DEFAULT_DEVICE_SCOPES,
    sessionId: 'base-session',
    expiresAt: Date.now() + 60_000,
  });
  await transport.endSecureSessionForReauthorization();
  assert.equal(transport.state, DeviceTransportState.AUTHENTICATING);
  assert.equal(transport.session.authenticated, false);
  assert.deepEqual(transport.session.scopes, []);
  assert.equal(transport.nextSequence, 1);
  assert.equal(transport.lastRxSequence, 0);
  await transport.close();
});

test('fixed checkpoint chunks assemble exactly 18 buttons and reject order/id gaps', () => {
  const assembler = new PerformanceCheckpointAssembler();
  let checkpoint = null;
  for (let chunkIndex = 0; chunkIndex < 9; chunkIndex += 1) {
    checkpoint = assembler.push(
      parsePerformanceCheckpointChunk(
        makeCheckpointChunk(7, chunkIndex, 100, false),
      ),
    );
  }
  assert.ok(checkpoint);
  assert.equal(checkpoint.buttons.length, 18);
  assert.equal(checkpoint.edgeSequence, 100);
  assert.equal(checkpoint.buttons[17].buttonIndex, 17);
  assert.equal(checkpoint.buttons[17].virtualPin, 117);

  assert.throws(
    () => assembler.push(parsePerformanceCheckpointChunk(makeCheckpointChunk(9, 0, 101, false))),
    /id gap/,
  );
  assembler.reset(true);
  assembler.push(parsePerformanceCheckpointChunk(makeCheckpointChunk(20, 0, 200, false)));
  assert.throws(
    () => assembler.push(parsePerformanceCheckpointChunk(makeCheckpointChunk(20, 2, 200, false))),
    /order/,
  );
});

test('checkpoint id wraps from 255 to 1 and reserves zero', () => {
  const assembler = new PerformanceCheckpointAssembler();
  for (const checkpointId of [255, 1]) {
    let complete = null;
    for (let chunkIndex = 0; chunkIndex < 9; chunkIndex += 1) {
      complete = assembler.push(parsePerformanceCheckpointChunk(
        makeCheckpointChunk(checkpointId, chunkIndex, checkpointId, false),
      ));
    }
    assert.ok(complete);
  }
  assert.throws(
    () => parsePerformanceCheckpointChunk(makeCheckpointChunk(0, 0, 2, false)),
    /header/,
  );
});

test('checkpoint completion replays newer reliable edges instead of rolling cache backward', () => {
  const assembler = new PerformanceCheckpointAssembler();
  let checkpoint = null;
  for (let chunkIndex = 0; chunkIndex < 9; chunkIndex += 1) {
    checkpoint = assembler.push(
      parsePerformanceCheckpointChunk(
        makeCheckpointChunk(1, chunkIndex, 10, false),
      ),
    );
  }
  const edge = {
    deviceTimestampUs: 2000,
    edgeSequence: 11,
    buttonIndex: 0,
    pressed: true,
    rawAdc: 2222,
    currentDistanceUm: 1200,
    pressTriggerDistanceUm: 1000,
    pressStartDistanceUm: 800,
    releaseTriggerDistanceUm: 600,
    releaseStartDistanceUm: 400,
  };
  const cache = new PerformanceTelemetryCache();
  cache.applyEdge(edge);
  const lastSequence = applyCheckpointPreservingEdges(cache, checkpoint, [edge]);
  const snapshot = cache.snapshot();
  assert.equal(lastSequence, 11);
  assert.equal(snapshot.buttonData[0].isPressed, true);
  assert.equal(snapshot.buttonData[0].currentDistance, 1.2);
});

test('clock synchronization uses five STM32 timestamp samples and selects the lowest RTT', async () => {
  const times = [0, 10, 20, 22, 30, 38, 40, 45, 50, 54];
  const deviceTimes = [4000, 21000, 34000, 42000, 52000];
  let timeIndex = 0;
  let requestIndex = 0;
  const transport = {
    async request(command, params) {
      assert.equal(command, 'performance.clock-sync');
      const index = requestIndex++;
      return {
        transactionId: index + 1,
        data: {
          sampleId: params.sampleId,
          deviceTimestampUs: deviceTimes[index],
        },
      };
    },
  };
  const synchronizer = new DeviceClockSynchronizer(transport, {
    sampleCount: 5,
    now: () => times[timeIndex++],
  });
  const estimate = await synchronizer.synchronize();
  assert.equal(requestIndex, 5);
  assert.equal(estimate.roundTripUs, 2000);
  assert.equal(estimate.offsetUs, 0);
  assert.equal(synchronizer.deviceToBrowserTimeMs(22000), 22);
});

test('aborting a clock synchronization stops the sample loop before another request', async () => {
  const controller = new AbortController();
  let requests = 0;
  const requester = {
    request(_command, _params, options) {
      requests += 1;
      return new Promise((_resolve, reject) => {
        options.signal.addEventListener('abort', () => {
          reject(new Error('request aborted'));
        }, { once: true });
      });
    },
  };
  const synchronizer = new DeviceClockSynchronizer(requester, {
    sampleCount: 5,
    now: () => 0,
  });

  const pending = synchronizer.synchronize(controller.signal);
  await Promise.resolve();
  assert.equal(requests, 1);
  controller.abort(new Error('session ended'));
  await assert.rejects(pending, /cancelled/);
  await Promise.resolve();
  assert.equal(requests, 1);
});

test('clock synchronization runs initially and every 10 seconds only while connected', async () => {
  let state = DeviceTransportState.CONNECTED;
  let stateHandler = null;
  let intervalCallback = null;
  let intervalMs = null;
  let synchronizeCalls = 0;
  let cleared = false;
  const transport = {
    kind: 'webhid',
    get state() { return state; },
    onStateChange(handler) {
      stateHandler = handler;
      return () => { stateHandler = null; };
    },
  };
  const scheduler = new DeviceClockSyncScheduler(
    transport,
    {
      async synchronize() {
        synchronizeCalls += 1;
        return {};
      },
    },
    {
      setInterval(callback, milliseconds) {
        intervalCallback = callback;
        intervalMs = milliseconds;
        return 123;
      },
      clearInterval(handle) {
        assert.equal(handle, 123);
        cleared = true;
      },
    },
  );
  scheduler.start();
  await Promise.resolve();
  assert.equal(DEVICE_CLOCK_SYNC_INTERVAL_MS, 10_000);
  assert.equal(intervalMs, 10_000);
  assert.equal(synchronizeCalls, 1);
  intervalCallback();
  await Promise.resolve();
  assert.equal(synchronizeCalls, 2);
  state = DeviceTransportState.AUTHENTICATING;
  intervalCallback();
  await Promise.resolve();
  assert.equal(synchronizeCalls, 2);
  state = DeviceTransportState.CONNECTED;
  stateHandler(state);
  await Promise.resolve();
  assert.equal(synchronizeCalls, 3);
  scheduler.stop();
  assert.equal(cleared, true);
  assert.equal(stateHandler, null);
});

test('WebHID keeps JSON at 16 KiB while streams stay at 8 KiB and firmware chunks stay at 4096 bytes', async () => {
  assert.equal(WEBHID_MAX_LOGICAL_MESSAGE_SIZE, 16384);
  assert.equal(WEBHID_MAX_STREAM_SIZE, 8192);
  assert.equal(FIRMWARE_BINARY_HEADER_SIZE, 106);
  assert.equal(WEBHID_FIRMWARE_CHUNK_DATA_SIZE, 4096);
  assert.equal(WEBHID_MAX_FIRMWARE_PACKET_SIZE, 4202);
  assert.ok(WEBHID_MAX_FIRMWARE_PACKET_SIZE <= WEBHID_MAX_STREAM_SIZE);
  assert.ok(FIRMWARE_BINARY_HEADER_SIZE + 8192 > WEBHID_MAX_STREAM_SIZE);

  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox WebConfig',
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener() {},
    removeEventListener() {},
    async sendReport() {},
  };
  const transport = new WebHidTransport({ navigator: makeHidNavigator(device) });
  await transport.connect();
  transport.establishSecureSession({
    async seal(_header, _sequence, plaintext) {
      return { ciphertext: plaintext.slice(), tag: new Uint8Array(12) };
    },
    async open(_header, _sequence, ciphertext) {
      return ciphertext.slice();
    },
  }, {
    transport: 'webhid',
    authenticated: true,
    scopes: ['firmware.update'],
    sessionId: 'boundary-test',
    expiresAt: Date.now() + 60_000,
  });
  transport.request = async (command) => ({
    transactionId: 1,
    data: command === 'stream.begin' || command === 'stream.credit'
      ? { transferId: 1, credit: 255 }
      : {},
  });
  transport.sendFrame = async () => {};
  await transport.upload('firmware', new Uint8Array(WEBHID_MAX_STREAM_SIZE));
  await assert.rejects(
    transport.upload('firmware', new Uint8Array(WEBHID_MAX_STREAM_SIZE + 1)),
    /1..8192 bytes/,
  );
  await transport.close();
});

test('one stream deadline expires during a middle fragment instead of resetting per write', async () => {
  const { transport, device } = await makeUploadDeadlineTransport();
  let frameCalls = 0;
  let completeCalls = 0;
  const observedTimeouts = [];
  transport.request = async (command, _params, options = {}) => {
    observedTimeouts.push({ command, timeoutMs: options.timeoutMs });
    if (command === 'stream.complete') completeCalls += 1;
    await boundedFixtureDelay(15, options.timeoutMs, command);
    return {
      transactionId: 1,
      data: command === 'stream.begin'
        ? { transferId: 1, credit: 255 }
        : { complete: true },
    };
  };
  transport.sendFrame = async (_type, _payload, _flags, _secure, options = {}) => {
    frameCalls += 1;
    observedTimeouts.push({ command: `frame-${frameCalls}`, timeoutMs: options.timeoutMs });
    await boundedFixtureDelay(15, options.timeoutMs, `frame-${frameCalls}`);
  };

  await assert.rejects(
    transport.upload('firmware', new Uint8Array(90), { timeoutMs: 50 }),
    /timeout|超时/i,
  );
  assert.ok(frameCalls >= 2 && frameCalls <= 3);
  assert.equal(completeCalls, 0);
  for (let index = 1; index < observedTimeouts.length; index += 1) {
    assert.ok(
      observedTimeouts[index].timeoutMs < observedTimeouts[index - 1].timeoutMs,
      'every stream stage must receive only the remaining absolute deadline',
    );
  }
  await transport.close();
  assert.equal(device.opened, false);
});

test('stream.complete shares the original upload deadline', async () => {
  const { transport } = await makeUploadDeadlineTransport();
  let completeTimeout = null;
  transport.request = async (command, _params, options = {}) => {
    const delayMs = command === 'stream.complete' ? 20 : 12;
    if (command === 'stream.complete') completeTimeout = options.timeoutMs;
    await boundedFixtureDelay(delayMs, options.timeoutMs, command);
    return {
      transactionId: 1,
      data: command === 'stream.begin'
        ? { transferId: 1, credit: 255 }
        : { complete: true },
    };
  };
  transport.sendFrame = async (_type, _payload, _flags, _secure, options = {}) => {
    await boundedFixtureDelay(12, options.timeoutMs, 'frame');
  };

  await assert.rejects(
    transport.upload('firmware', new Uint8Array(30), { timeoutMs: 38 }),
    /timeout|超时/i,
  );
  assert.ok(completeTimeout > 0 && completeTimeout < 20);
  await transport.close();
});

test('an already-aborted upload returns immediately while stream.abort remains bounded best-effort', async () => {
  const { transport } = await makeUploadDeadlineTransport();
  const controller = new AbortController();
  let abortCalls = 0;
  let abortOptions = null;
  let frameCalls = 0;

  transport.request = async (command, _params, options = {}) => {
    if (command === 'stream.begin') {
      controller.abort();
      return {
        transactionId: 1,
        data: { transferId: 17, credit: 1 },
      };
    }
    if (command === 'stream.abort') {
      abortCalls += 1;
      abortOptions = options;
      return new Promise(() => {});
    }
    throw new Error(`unexpected request ${command}`);
  };
  transport.sendFrame = async () => {
    frameCalls += 1;
  };

  const startedAt = Date.now();
  await assert.rejects(
    transport.upload('firmware', new Uint8Array(30), {
      timeoutMs: 5000,
      signal: controller.signal,
    }),
    (error) => error?.name === 'AbortError',
  );
  const elapsedMs = Date.now() - startedAt;

  assert.ok(elapsedMs < 250, `AbortError should not wait for stream.abort (${elapsedMs}ms)`);
  assert.equal(abortCalls, 1);
  assert.equal(frameCalls, 0);
  assert.equal(abortOptions.signal, undefined);
  assert.ok(abortOptions.timeoutMs > 0 && abortOptions.timeoutMs <= 1000);
  await transport.close();
});

test('WebHID typed image reads reject malformed success, error and length fields', async () => {
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox image-read fixture',
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener() {},
    removeEventListener() {},
    async sendReport() {},
  };
  const transport = new WebHidTransport({
    navigator: makeHidNavigator(device),
    closeTimeoutMs: 20,
  });
  const expectedTotal = 4097;
  let corruption = 'short-final';
  transport.request = async (command, params) => {
    assert.equal(command, 'binary.exchange');
    const request = Buffer.from(params.data, 'base64');
    const requestView = new DataView(request.buffer, request.byteOffset, request.byteLength);
    assert.equal(requestView.getUint8(0), 0x35);
    const offset = requestView.getUint32(6, true);
    const wanted = requestView.getUint16(10, true);
    const rejected = corruption === 'oversized-error';
    const length = rejected
      ? 0
      : corruption === 'short-final' && offset + wanted === expectedTotal
        ? wanted - 1
        : wanted;
    const response = new Uint8Array(55 + length);
    const responseView = new DataView(response.buffer);
    responseView.setUint8(0, 0xb5);
    responseView.setUint8(
      1,
      corruption === 'invalid-success' ? 2 : rejected ? 0 : 1,
    );
    responseView.setUint8(2, requestView.getUint8(1));
    responseView.setUint32(4, requestView.getUint32(2, true), true);
    responseView.setUint32(
      12,
      corruption === 'wrong-total' ? expectedTotal + 1 : expectedTotal,
      true,
    );
    responseView.setUint32(16, offset, true);
    responseView.setUint16(20, length, true);
    responseView.setUint8(
      22,
      corruption === 'accepted-error' ? 1 : rejected ? 33 : 0,
    );
    return {
      errNo: 0,
      data: { data: Buffer.from(response).toString('base64') },
    };
  };
  const auth = {
    clear() {},
    hasScopes() { return true; },
    async authenticate(currentTransport, scopes) {
      currentTransport.session = {
        transport: 'webhid',
        authenticated: true,
        scopes: [...scopes],
        sessionId: 'image-read-session',
      };
      currentTransport.state = DeviceTransportState.CONNECTED;
      return currentTransport.session;
    },
  };
  const client = new DeviceCommandClient(transport, auth);
  await client.connect();

  await assert.rejects(client.readImage('user', expectedTotal), /invalid length/);
  corruption = 'wrong-total';
  await assert.rejects(client.readImage('user', expectedTotal), /invalid length/);
  corruption = 'invalid-success';
  await assert.rejects(client.readImage('user', expectedTotal), /does not match/);
  corruption = 'accepted-error';
  await assert.rejects(client.readImage('user', expectedTotal), /invalid length/);
  corruption = 'oversized-error';
  await assert.rejects(client.readImage('user', expectedTotal), /does not match/);
  client.dispose();
  await waitFor(() => !device.opened, 250);
});

test('authentication defaults are least-privilege and bearer tokens are origin/scope pinned', async () => {
  assert.deepEqual(DEFAULT_DEVICE_SCOPES, [
    'config.read',
    'config.write',
    'monitor.read',
  ]);
  const fetchCalls = [];
  const auth = new DeviceAuthClient({
    serverOrigin: 'https://config.example',
    challengeEndpoint: '/api/v2/device-auth/challenges',
    verifyEndpoint: '/api/v2/device-auth/verify',
    fetch: async (input, init) => {
      fetchCalls.push({ input, init });
      return { ok: true };
    },
  });
  auth.apiToken = 'memory-only-token';
  auth.apiTokenExpiresAt = Date.now() + 60_000;
  auth.grantedScopes = ['config.read'];

  await auth.authorizedFetch(
    'https://config.example/api/firmware-check-update',
    { method: 'POST' },
    ['config.read'],
  );
  assert.equal(fetchCalls.length, 1);
  assert.equal(
    fetchCalls[0].init.headers.get('Authorization'),
    'Bearer memory-only-token',
  );
  assert.equal(fetchCalls[0].init.redirect, 'error');
  await assert.rejects(
    auth.authorizedFetch(
      'https://evil.example/download',
      {},
      ['config.read'],
    ),
    /非认证服务器 origin/,
  );
  await assert.rejects(
    auth.authorizedFetch(
      'https://config.example/download',
      {},
      ['firmware.update'],
    ),
    /缺少此操作所需/,
  );
  assert.equal(fetchCalls.length, 1);
  auth.clear();
  await assert.rejects(
    auth.authorizedFetch('https://config.example/api', {}, []),
    /会话已过期/,
  );
});

test('both cleartext authentication bootstrap steps use the dedicated ten-second deadline', async () => {
  const deviceEphemeral = await crypto.subtle.generateKey(
    { name: 'ECDH', namedCurve: 'P-256' },
    true,
    ['deriveBits'],
  );
  const devicePublicKey = Buffer.from(await crypto.subtle.exportKey(
    'raw',
    deviceEphemeral.publicKey,
  )).toString('base64');
  const scopes = [...DEFAULT_DEVICE_SCOPES];
  const sessionId = 'session-bootstrap-timeout-fixture';
  let fetchCall = 0;
  const auth = new DeviceAuthClient({
    serverOrigin: 'https://config.example',
    fetch: async () => ({
      ok: true,
      status: 200,
      async json() {
        fetchCall += 1;
        if (fetchCall === 1) {
          return {
            challengeId: 'challenge-bootstrap-timeout-fixture',
            nonce: Buffer.alloc(32, 0x31).toString('base64'),
            expiresAt: Date.now() + 60_000,
          };
        }
        return {
          apiToken: 'bootstrap-timeout-token',
          expiresInMs: 60_000,
          sessionId,
          deviceSessionPermit: 'bootstrap-timeout-permit',
          sessionSalt: Buffer.alloc(16, 0x42).toString('base64'),
          scopes,
          productId: 'HBOX',
          pcbRevision: '2.0.0',
          webConfigProfile: 'hbox-pcb-v2',
        };
      },
    }),
  });
  const bootstrapCalls = [];
  const transport = {
    session: {
      transport: 'webhid',
      productName: 'HBox bootstrap timeout fixture',
      authenticated: false,
      scopes: [],
    },
    setAuthenticating() {},
    async bootstrapRequest(command, _params, options) {
      bootstrapCalls.push({ command, timeoutMs: options.timeoutMs });
      if (command === 'attestation.create') {
        return {
          deviceId: 'device-bootstrap-timeout-fixture',
          certificate: 'fixture-certificate',
          bootAttestation: 'fixture-boot-attestation',
          bootNonce: 'fixture-boot-nonce',
          deviceEphemeralPublicKey: devicePublicKey,
          firmwareMeasurement: 'fixture-measurement',
          hardwareVersion: '2.0.0',
          firmwareVersion: '2.0.0',
          signature: 'fixture-signature',
        };
      }
      assert.equal(command, 'session.install-permit');
      return { accepted: true, sessionId };
    },
    establishSecureSession(_cipher, session) { this.session = session; },
  };

  await auth.authenticate(transport, scopes);
  assert.deepEqual(bootstrapCalls, [
    { command: 'attestation.create', timeoutMs: 10_000 },
    { command: 'session.install-permit', timeoutMs: 10_000 },
  ]);
});

test('authentication HTTP write and response parsing have one bounded deadline', async () => {
  const blockedFetch = new DeviceAuthClient({
    serverOrigin: 'https://config.example',
    httpTimeoutMs: 20,
    fetch: () => new Promise(() => {}),
  });
  await assert.rejects(
    blockedFetch.postJson('https://config.example/challenge', {}, undefined),
    /请求超时/,
  );

  const blockedJson = new DeviceAuthClient({
    serverOrigin: 'https://config.example',
    httpTimeoutMs: 20,
    fetch: async () => ({
      ok: true,
      status: 200,
      json: () => new Promise(() => {}),
    }),
  });
  await assert.rejects(
    blockedJson.postJson('https://config.example/verify', {}, undefined),
    /响应超时/,
  );
});

test('aborted reauthorization cannot install a permit, token, or close a later session', async () => {
  const requestedScopes = [...DEFAULT_DEVICE_SCOPES, 'device.control'];
  const challenge = {
    challengeId: 'challenge-cancel-fixture',
    nonce: Buffer.alloc(32, 0x11).toString('base64'),
    expiresAt: Date.now() + 60_000,
  };
  const attestation = {
    deviceId: 'device-cancel-fixture',
    certificate: 'fixture-certificate',
    bootAttestation: 'fixture-boot-attestation',
    bootNonce: 'fixture-boot-nonce',
    deviceEphemeralPublicKey: Buffer.alloc(65, 0x04).toString('base64'),
    firmwareMeasurement: 'fixture-measurement',
    hardwareVersion: '2.0.0',
    firmwareVersion: '2.0.0',
    signature: 'fixture-signature',
  };
  const authorization = {
    apiToken: 'stale-api-token',
    expiresInMs: 60_000,
    sessionId: 'session-cancel-fixture',
    deviceSessionPermit: 'fixture-permit',
    sessionSalt: Buffer.alloc(16, 0x22).toString('base64'),
    scopes: requestedScopes,
  };
  const fetchSignals = [];
  let fetchIndex = 0;
  let resolveAuthorization;
  let authorizationReadStarted;
  const authorizationReadStartedPromise = new Promise((resolve) => {
    authorizationReadStarted = resolve;
  });
  const auth = new DeviceAuthClient({
    serverOrigin: 'https://config.example',
    fetch: async (_input, init) => {
      fetchSignals.push(init.signal);
      const isChallenge = fetchIndex++ === 0;
      return {
        ok: true,
        status: 200,
        async json() {
          if (isChallenge) {
            return challenge;
          }
          authorizationReadStarted();
          return new Promise((resolve) => {
            resolveAuthorization = resolve;
          });
        },
      };
    },
  });
  let permitCalls = 0;
  let establishCalls = 0;
  let closeCalls = 0;
  const transport = {
    session: {
      transport: 'webhid',
      authenticated: false,
      scopes: [],
    },
    setAuthenticating() {},
    async bootstrapRequest(command) {
      if (command === 'attestation.create') {
        return attestation;
      }
      assert.equal(command, 'session.install-permit');
      permitCalls += 1;
      return { accepted: true, sessionId: authorization.sessionId };
    },
    establishSecureSession() { establishCalls += 1; },
    async close() { closeCalls += 1; },
  };
  const controller = new AbortController();
  const reauthorization = auth.reauthorize(
    transport,
    requestedScopes,
    controller.signal,
  );
  const cancellation = assert.rejects(reauthorization, /认证流程.*取消/);
  await authorizationReadStartedPromise;
  controller.abort();
  resolveAuthorization(authorization);
  await cancellation;

  assert.equal(fetchSignals.length, 2);
  assert.ok(fetchSignals.every((signal) => signal !== controller.signal));
  assert.equal(fetchSignals[0].aborted, false);
  assert.equal(fetchSignals[1].aborted, true);
  assert.equal(permitCalls, 0);
  assert.equal(establishCalls, 0);
  assert.equal(closeCalls, 0);
  assert.equal(auth.apiToken, null);
  assert.deepEqual(auth.grantedScopes, []);
});

test('binary and RPC scope policy is explicit and unknown opcodes fail closed', () => {
  assert.equal(binaryOpcodeScope(0x01), 'firmware.update');
  for (const opcode of [0x30, 0x31, 0x32, 0x33]) {
    assert.equal(binaryOpcodeScope(opcode), 'asset.write');
  }
  for (const opcode of [0x34, 0x35]) {
    assert.equal(binaryOpcodeScope(opcode), 'config.read');
  }
  for (const opcode of [0x00, 0x02, 0x2f, 0x36, 0xff]) {
    assert.throws(() => binaryOpcodeScope(opcode), /Unsupported/);
  }
  assert.deepEqual(elevatedScopesForCommand('reboot'), ['device.control']);
  assert.deepEqual(
    elevatedScopesForCommand('create_firmware_upgrade_session'),
    ['firmware.update'],
  );
  assert.deepEqual(elevatedScopesForCommand('get_global_config'), []);
});

test('WebHID config export uses bounded sequential RPCs and preserves profile macros', async () => {
  const calls = [];
  const sections = [];
  const responses = {
    get_global_config: { globalConfig: { inputMode: 'XINPUT' } },
    get_hotkeys_config: { hotkeysConfig: [{ action: 'None' }] },
    get_screen_control_config: { screenControl: { brightness: 80 } },
    get_profile_list: {
      profileList: {
        defaultId: 'p2',
        items: [{ id: 'p1' }, { id: 'p2' }],
      },
    },
  };
  await exportWebHidConfigSections(async (command, params = {}) => {
    calls.push([command, params]);
    if (command === 'get_profile_details') {
      return {
        profileDetails: {
          id: params.profileId,
          name: `Profile ${params.profileId}`,
          keysConfig: { inputMode: 'XINPUT' },
        },
      };
    }
    if (command === 'get_profile_macros') {
      return {
        m: [
          null,
          {
            k: [1, 2],
            s: [[15, 0x1234, 0x40]],
          },
          null,
          null,
          null,
        ],
      };
    }
    return responses[command];
  }, (section) => sections.push(section));

  assert.deepEqual(calls.map(([command]) => command), [
    'get_global_config',
    'get_profile_list',
    'get_hotkeys_config',
    'get_screen_control_config',
    'get_profile_details',
    'get_profile_macros',
    'get_profile_details',
    'get_profile_macros',
  ]);
  assert.equal(sections[0].data.defaultProfileId, 'p2');
  assert.deepEqual(sections.map(({ section }) => section), [
    'global',
    'hotkeys',
    'screenControl',
    'profile',
    'profile',
    'end',
  ]);
  const firstProfile = sections[3].data;
  assert.deepEqual(firstProfile.keysConfig.macros, [{
    index: 1,
    triggerKeys: [1, 2],
    steps: [{ timeMs: 15, buttonMask: 0x1234, dynamicMask: 0x40 }],
  }]);
});

async function makeUploadDeadlineTransport() {
  const device = {
    opened: false,
    vendorId: 0xcafe,
    productId: 0x4021,
    productName: 'HBox stream deadline fixture',
    collections: [],
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener() {},
    removeEventListener() {},
    async sendReport() {},
  };
  const transport = new WebHidTransport({ navigator: makeHidNavigator(device) });
  await transport.connect();
  transport.establishSecureSession({
    async seal(_header, _sequence, plaintext) {
      return { ciphertext: plaintext.slice(), tag: new Uint8Array(12) };
    },
    async open(_header, _sequence, ciphertext) {
      return ciphertext.slice();
    },
  }, {
    transport: 'webhid',
    authenticated: true,
    scopes: ['firmware.update'],
    sessionId: 'stream-deadline-session',
    expiresAt: Date.now() + 60_000,
  });
  return { transport, device };
}

async function boundedFixtureDelay(delayMs, timeoutMs, label) {
  assert.equal(typeof timeoutMs, 'number', `${label} must receive a remaining timeout`);
  if (timeoutMs < delayMs) {
    await new Promise((resolve) => setTimeout(resolve, Math.max(0, timeoutMs)));
    throw new DeviceTransportError('timeout', `${label} timeout`);
  }
  await new Promise((resolve) => setTimeout(resolve, delayMs));
}

function makeExclusiveLockManager() {
  let active = false;
  const waiters = [];
  const requestedNames = [];
  const drain = () => {
    if (active) return;
    while (waiters.length > 0) {
      const waiter = waiters.shift();
      if (waiter.signal.aborted) continue;
      active = true;
      waiter.signal.removeEventListener('abort', waiter.abort);
      waiter.resolve();
      return;
    }
  };
  return {
    requestedNames,
    get activeCount() { return active ? 1 : 0; },
    request(name, options, callback) {
      requestedNames.push(name);
      const granted = new Promise((resolve, reject) => {
        const waiter = {
          signal: options.signal,
          resolve,
          reject,
          abort: null,
        };
        waiter.abort = () => {
          const index = waiters.indexOf(waiter);
          if (index >= 0) waiters.splice(index, 1);
          reject(new DOMException('Lock request aborted', 'AbortError'));
        };
        if (options.signal.aborted) waiter.abort();
        else {
          options.signal.addEventListener('abort', waiter.abort, { once: true });
          waiters.push(waiter);
          drain();
        }
      });
      return granted.then(async () => {
        try {
          return await callback({ name });
        } finally {
          active = false;
          drain();
        }
      });
    },
  };
}

function makeHidNavigator(device) {
  return {
    getDevices: async () => [device],
    requestDevice: async () => [device],
    addEventListener() {},
    removeEventListener() {},
  };
}

async function waitFor(predicate, timeoutMs = 1000) {
  const deadline = Date.now() + timeoutMs;
  while (!predicate()) {
    if (Date.now() >= deadline) {
      throw new Error(`Timed out after ${timeoutMs}ms waiting for condition`);
    }
    await new Promise((resolve) => setTimeout(resolve, 0));
  }
}

function makeCheckpointChunk(checkpointId, chunkIndex, edgeSequence, pressed) {
  const payload = new Uint8Array(44);
  const view = new DataView(payload.buffer);
  view.setUint32(0, 1000, true);
  view.setUint32(4, edgeSequence, true);
  view.setUint16(8, 4000, true);
  view.setUint16(10, 3, true);
  payload[12] = checkpointId;
  payload[13] = chunkIndex;
  payload[14] = 9;
  payload[15] = chunkIndex * 2;
  for (let record = 0; record < 2; record += 1) {
    const buttonIndex = chunkIndex * 2 + record;
    const offset = 16 + record * 14;
    payload[offset] = 100 + buttonIndex;
    payload[offset + 1] = pressed ? 1 : 0;
    view.setUint16(offset + 2, 2000 + buttonIndex, true);
    view.setUint16(offset + 4, 100 + buttonIndex, true);
    view.setUint16(offset + 6, 200 + buttonIndex, true);
    view.setUint16(offset + 8, 300 + buttonIndex, true);
    view.setUint16(offset + 10, 400 + buttonIndex, true);
    view.setUint16(offset + 12, 500 + buttonIndex, true);
  }
  return payload;
}
