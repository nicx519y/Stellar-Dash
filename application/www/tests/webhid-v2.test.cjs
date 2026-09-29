const test = require('node:test');
const assert = require('node:assert/strict');
const { capability } = require('./webhid-v2-fixture.cjs');
const { parseWebHidCapability } = require('../lib/device-transport/webhid-capability.ts');
const { SecureHidReportCodec, SecureHidFrameType } = require('../lib/device-transport/secure-hid-frame.ts');

test('capability rejects FS, old protocol, unready bridge and reserved fields', async () => {
  assert.equal(parseWebHidCapability(await capability()).reportBytes, 1024);
  for (const [offset, value] of [[4,1],[8,1],[9,0],[10,1],[11,9],[20,1]]) {
    const view = await capability(); view.setUint8(offset, value);
    assert.throws(() => parseWebHidCapability(view));
  }
  assert.throws(() => parseWebHidCapability(new DataView(new ArrayBuffer(31))));
});
test('V2 covers 16-bit lengths and rejects legacy size, padding and session generation', async () => {
  const cipher = { epoch: 17, async seal(_h, _s, p) { return { ciphertext: p, tag: new Uint8Array(12) }; }, async open(_h, _s, p) { return p; } };
  const codec = new SecureHidReportCodec(cipher);
  for (const length of [0,1,255,256,995,996]) {
    const report = await codec.encode({ type: SecureHidFrameType.IMAGE_DATA, flags: 0, sequence: 1, payload: new Uint8Array(length), secure: true });
    assert.equal(report.length,1024); assert.equal((await codec.decode(report)).payloadLength,length);
    const wrongEpoch=report.slice(); new DataView(wrongEpoch.buffer).setUint32(12,18,true);
    await assert.rejects(codec.decode(wrongEpoch),/another session/);
  }
  await assert.rejects(codec.encode({type:SecureHidFrameType.IMAGE_DATA,flags:0,sequence:1,payload:new Uint8Array(997),secure:true}));
  await assert.rejects(codec.decode(new Uint8Array(64)));
  const report=await codec.encode({type:SecureHidFrameType.IMAGE_DATA,flags:0,sequence:1,payload:new Uint8Array(1),secure:true});
  report[17]=1; await assert.rejects(codec.decode(report),/padding/);
});

test('latched SPI port faults are classified as bridge failures before session setup', async () => {
  const view = await capability();
  view.setUint8(9, 0);
  view.setUint8(10, 0x1a);
  assert.throws(() => parseWebHidCapability(view), (error) => {
    assert.equal(error.code, 'bridge-not-ready');
    assert.match(error.message, /状态码 10（26）/);
    return true;
  });
});
