const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const {
  isLocalWebHidTraceHostname,
  relatedWebHidTraceFrames,
  resolveWebHidNetworkTraceMode,
  upsertWebHidTraceRecord,
} = require('../lib/device-transport/webhid-network-trace.ts');

test('trace viewer replaces a pending request with its failed outcome', () => {
  const pending = {
    recordId: 'source:1',
    captureSequence: 1,
    sourceId: 'source',
    sourceUrl: 'http://localhost:3001/global/',
    name: 'TX-LOGICAL-config.write-TID-7',
    traceKind: 'logical',
    direction: 'tx',
    typeName: 'RPC_REQUEST',
    capturedAt: new Date(0).toISOString(),
    status: 'pending',
  };
  const failed = {
    ...pending,
    status: 'failed',
    errorCode: 'timeout',
    errorMessage: 'request timed out',
  };
  const records = upsertWebHidTraceRecord(
    upsertWebHidTraceRecord([], pending),
    failed,
  );
  assert.equal(records.length, 1);
  assert.equal(records[0].status, 'failed');
  assert.equal(records[0].errorCode, 'timeout');
});

test('trace viewer links request and response HID frames to one logical request', () => {
  const logical = {
    recordId: 'source:logical',
    captureSequence: 1,
    sourceId: 'source',
    sourceUrl: 'http://localhost:3001/global/',
    name: 'TX-LOGICAL-config.write-TID-7',
    traceKind: 'logical',
    direction: 'tx',
    typeName: 'RPC_REQUEST',
    capturedAt: new Date(0).toISOString(),
    frameRecordIds: [],
    responseFrameRecordIds: ['source:rx-frame'],
  };
  const txFrame = {
    recordId: 'source:tx-frame',
    captureSequence: 2,
    sourceId: 'source',
    sourceUrl: logical.sourceUrl,
    name: 'TX-FRAME-RPC_REQUEST-SEQ-1',
    traceKind: 'frame',
    direction: 'tx',
    typeName: 'RPC_REQUEST',
    capturedAt: logical.capturedAt,
    logicalRecordId: logical.recordId,
  };
  const rxFrame = {
    ...txFrame,
    recordId: 'source:rx-frame',
    captureSequence: 3,
    name: 'RX-FRAME-RPC_RESPONSE-SEQ-2',
    direction: 'rx',
    typeName: 'RPC_RESPONSE',
    logicalRecordId: undefined,
  };
  const unrelated = {
    ...txFrame,
    recordId: 'source:unrelated',
    captureSequence: 4,
    logicalRecordId: 'source:other-logical',
  };

  assert.deepEqual(
    relatedWebHidTraceFrames([unrelated, rxFrame, logical, txFrame], logical)
      .map((record) => record.recordId),
    ['source:tx-frame', 'source:rx-frame'],
  );
});

test('WebHID Network query trace is development or localhost only and explicitly selected', () => {
  assert.equal(resolveWebHidNetworkTraceMode('?webhidNetworkTrace=control', true), 'control');
  assert.equal(resolveWebHidNetworkTraceMode('?webhidNetworkTrace=all', true), 'all');
  assert.equal(resolveWebHidNetworkTraceMode('?webhidNetworkTrace=off', true), null);
  assert.equal(resolveWebHidNetworkTraceMode('?webhidNetworkTrace=all', false), null);
  assert.equal(resolveWebHidNetworkTraceMode('?webhidNetworkTrace=all', false, true), 'all');
});

test('dedicated WebHID trace receiver is restricted to exact loopback hosts', () => {
  assert.equal(isLocalWebHidTraceHostname('localhost'), true);
  assert.equal(isLocalWebHidTraceHostname('127.0.0.1'), true);
  assert.equal(isLocalWebHidTraceHostname('::1'), true);
  assert.equal(isLocalWebHidTraceHostname('[::1]'), true);
  assert.equal(isLocalWebHidTraceHostname('localhost.example.com'), false);
  assert.equal(isLocalWebHidTraceHostname('hbox.example.com'), false);
});

test('WebHID Network trace worker consumes records locally', () => {
  const source = fs.readFileSync(path.join(
    __dirname,
    '..',
    'public',
    'webhid-network-trace-sw.js',
  ), 'utf8');

  assert.match(source, /request\.method !== 'POST'/);
  assert.match(source, /url\.origin !== self\.location\.origin/);
  assert.match(source, /url\.pathname\.startsWith\(TRACE_ENDPOINT_PREFIX\)/);
  assert.match(source, /new Response\(body/);
  assert.doesNotMatch(source, /\bfetch\s*\(/);
});

test('WebHID Network trace captures the opt-in before the index redirect', () => {
  const source = fs.readFileSync(path.join(
    __dirname,
    '..',
    'lib',
    'device-transport',
    'webhid-network-trace.ts',
  ), 'utf8');

  const initialSearch = source.indexOf('const initialBrowserSearch');
  const configuredMode = source.indexOf('let configuredMode', initialSearch);
  const runtimeLookup = source.indexOf('function currentTraceMode');
  assert.ok(initialSearch >= 0 && configuredMode > initialSearch);
  assert.ok(runtimeLookup > configuredMode, 'initial query must be captured before runtime routing');
});

test('dedicated trace page receives broadcasts without mounting the HID provider', () => {
  const traceSource = fs.readFileSync(path.join(
    __dirname,
    '..',
    'lib',
    'device-transport',
    'webhid-network-trace.ts',
  ), 'utf8');
  const layoutSource = fs.readFileSync(path.join(
    __dirname,
    '..',
    'app',
    'layout.tsx',
  ), 'utf8');
  const viewerSource = fs.readFileSync(path.join(
    __dirname,
    '..',
    'app',
    'webhid-trace',
    'page.tsx',
  ), 'utf8');

  assert.match(traceSource, /new BroadcastChannel\(TRACE_CHANNEL_NAME\)/);
  assert.match(traceSource, /kind: 'trace-record'/);
  assert.match(traceSource, /TRACE_VIEWER_LEASE_MS/);
  assert.match(layoutSource, /if \(isTraceViewer\) \{\s*return <>\{children\}<\/>;/);
  assert.match(viewerSource, /openWebHidTraceViewer\('control'/);
  assert.doesNotMatch(viewerSource, /useGamepadConfig|createDeviceCommandClient/);
});

test('email verification stays outside HID and account dialog reuses the shared blur placement', () => {
  const layoutSource = fs.readFileSync(path.join(
    __dirname,
    '..',
    'app',
    'layout.tsx',
  ), 'utf8');
  const verifySource = fs.readFileSync(path.join(
    __dirname,
    '..',
    'app',
    'auth',
    'verify',
    'page.tsx',
  ), 'utf8');
  const controlSource = fs.readFileSync(path.join(
    __dirname,
    '..',
    'components',
    'user-auth-control.tsx',
  ), 'utf8');

  const standaloneBranch = layoutSource.indexOf(
    'if (isEmailVerification || isAdministration)'
  );
  const deviceProvider = layoutSource.indexOf('<GamepadConfigProvider>', standaloneBranch);
  assert.ok(standaloneBranch >= 0 && deviceProvider > standaloneBranch);
  assert.match(layoutSource.slice(standaloneBranch, deviceProvider), /<UserAuthProvider>/);
  assert.doesNotMatch(verifySource, /useGamepadConfig|navigator\.hid|WebHidTransport/);
  assert.match(controlSource, /<Dialog\.Backdrop backdropFilter="blur\(4px\)"/);
  assert.match(controlSource, /<Dialog\.Positioner alignItems="flex-start" pt=\{16\}/);
});

test('WebHID transport mirrors plaintext only at its existing crypto boundaries', () => {
  const source = fs.readFileSync(path.join(
    __dirname,
    '..',
    'lib',
    'device-transport',
    'webhid-transport.ts',
  ), 'utf8');

  const decode = source.indexOf('const frame = await this.codec.decode(report);');
  const rxTrace = source.indexOf("direction: 'rx'", decode);
  const encode = source.indexOf('const report = await this.codec.encode');
  const txTrace = source.indexOf("direction: 'tx'", encode);
  const sendReport = source.indexOf('await device.sendReport(this.reportId, report);', encode);

  assert.ok(decode >= 0 && rxTrace > decode, 'RX trace must run after authenticated decode');
  assert.ok(encode >= 0 && txTrace > encode, 'TX trace must have both plaintext and wire report');
  assert.ok(sendReport > txTrace, 'TX trace must not add an awaited operation before sendReport');
});
