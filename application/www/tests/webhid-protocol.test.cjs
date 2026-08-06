const test = require('node:test');
const assert = require('node:assert/strict');

const {
  FragmentAssembler,
  SecureHidFrameFlags,
  SecureHidFrameType,
  SecureHidReportCodec,
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
  binaryOpcodeScope,
  elevatedScopesForCommand,
} = require('../lib/device-transport/scope-policy.ts');
const {
  exportWebHidConfigSections,
} = require('../lib/device-transport/webhid-config-export.ts');
const {
  DEFAULT_DEVICE_SCOPES,
  DeviceTransportState,
} = require('../lib/device-transport/types.ts');
const {
  FIRMWARE_BINARY_HEADER_SIZE,
  WEBHID_FIRMWARE_CHUNK_DATA_SIZE,
  WEBHID_MAX_FIRMWARE_PACKET_SIZE,
  WEBHID_MAX_LOGICAL_MESSAGE_SIZE,
  WEBHID_MAX_STREAM_SIZE,
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
  resolveDefaultFirmwareServerHost,
} = require('../lib/device-transport/firmware-server-origin.ts');
const {
  resolveAuthenticatedWebConfigTarget,
} = require('../lib/device-transport/webconfig-target.ts');

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

test('hosted V2 firmware APIs default to same-origin while legacy keeps its remote host', () => {
  assert.equal(resolveDefaultFirmwareServerHost('webhid'), '');
  assert.equal(resolveDefaultFirmwareServerHost('mock'), '');
  assert.equal(
    resolveDefaultFirmwareServerHost('legacy-websocket'),
    'https://firmware.st-dash.com',
  );
  assert.equal(
    resolveDefaultFirmwareServerHost('webhid', ' https://config.example/ '),
    '',
  );
  assert.equal(
    resolveDefaultFirmwareServerHost('legacy-websocket', ' https://config.example/ '),
    'https://config.example',
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

test('device initialization becomes ready only after all six loaders succeed', async () => {
  const completed = [];
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
    onReady: (layout) => { readyLayout = layout; },
    onFailure: (error) => { failure = error; },
  });

  await Promise.resolve();
  assert.equal(readyLayout, null);
  assert.deepEqual(completed.sort(), ['firmware', 'global', 'hotkeys', 'profiles', 'screen']);
  releaseLayout([{ x: 1, y: 2, r: 3 }]);
  assert.equal(await resultPromise, 'ready');
  assert.deepEqual(readyLayout, [{ x: 1, y: 2, r: 3 }]);
  assert.equal(failure, null);
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
  assert.equal(reportedFailure, failure);

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
    /sequence gap cannot be proven telemetry-only/,
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

test('WebHID 8 KiB boundaries reserve the firmware header and keep data chunks at 4096 bytes', async () => {
  assert.equal(WEBHID_MAX_LOGICAL_MESSAGE_SIZE, 8192);
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
  assert.ok(fetchSignals.every((signal) => signal === controller.signal));
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
    'get_hotkeys_config',
    'get_screen_control_config',
    'get_profile_list',
    'get_profile_details',
    'get_profile_macros',
    'get_profile_details',
    'get_profile_macros',
  ]);
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
