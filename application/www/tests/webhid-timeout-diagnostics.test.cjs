const test = require('node:test');
const assert = require('node:assert/strict');
const { capability } = require('./webhid-v2-fixture.cjs');
const { WebHidTransport } = require('../lib/device-transport/webhid-transport.ts');
const { SecureHidReportCodec, SecureHidFrameType, SecureHidFrameFlags } = require('../lib/device-transport/secure-hid-frame.ts');

for (const scenario of ['write-stuck', 'response-missing', 'response-before-write']) {
  test(`timeout diagnostics distinguish ${scenario}`, async () => {
    const codec = new SecureHidReportCodec();
    let inputListener, releaseWrite;
    const nativeWrite = new Promise(resolve => { releaseWrite = resolve; });
    const device = {
      opened: false, vendorId: 0xcafe, productId: 0x4021, productName: 'XORA timeout fixture', collections: [],
      receiveFeatureReport: capability,
      async open() { this.opened = true; }, async close() { this.opened = false; },
      addEventListener(_, listener) { inputListener = listener; }, removeEventListener() {},
      async sendReport(_, bytes) {
        if (scenario === 'response-before-write') {
          const frame = await codec.decode(new Uint8Array(bytes));
          const request = JSON.parse(new TextDecoder().decode(frame.payload));
          const report = await codec.encode({ type: SecureHidFrameType.BOOTSTRAP_RESPONSE,
            flags: SecureHidFrameFlags.LAST, sequence: 1, secure: false,
            payload: new TextEncoder().encode(JSON.stringify({ transactionId: request.transactionId, errNo: 0, data: {} })) });
          inputListener({ device, reportId: 0, data: new DataView(report.buffer) });
        }
        if (scenario !== 'response-missing') await nativeWrite;
      },
    };
    const transport = new WebHidTransport({ navigator: {
      getDevices: async () => [device], requestDevice: async () => [device],
      addEventListener() {}, removeEventListener() {},
    }, closeTimeoutMs: 20 });
    await transport.connect();
    try {
      await assert.rejects(transport.bootstrapRequest('diagnostic.read', {}, { timeoutMs: 100 }), error => {
        assert.equal(error.code, 'timeout');
        assert.equal(error.cause.command, 'diagnostic.read');
        assert.equal(error.cause.nativeWriteComplete, scenario === 'response-missing');
        assert.equal(error.cause.responseReceived, scenario === 'response-before-write');
        assert.equal(error.cause.responseFramesObserved, scenario === 'response-before-write' ? 1 : 0);
        assert.ok(error.cause.elapsedMs >= 80);
        assert.match(error.message, /writeComplete=.*responseReceived=/);
        return true;
      });
      assert.equal(transport.pendingBootstrap.size, 0);
      const generation = transport.connectionGeneration;
      releaseWrite();
      await new Promise(resolve => setTimeout(resolve, 10));
      assert.equal(transport.connectionGeneration, generation);
    } finally { releaseWrite(); await transport.close(); }
  });
}
