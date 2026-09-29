const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const jpeg = require('../../../common/uimg-jpeg.cjs');
const { parseUimg, prepareUimgInstallation } = require('../lib/uimg-v4.ts');
const { processGalleryJpeg } = require('../lib/gallery-jpeg-processor.ts');
const fixture = name => new Uint8Array(fs.readFileSync(path.resolve(__dirname, '../../../common/test_vectors/uimg-jpeg', name)));

test('JPEG golden file preserves 18 playback ticks and three unique frames across readers', () => {
  const bytes = fixture('sequence.uimg');
  const parsed = parseUimg(bytes);
  assert.equal(parsed.format, 3); assert.equal(parsed.frameCount, 18); assert.equal(parsed.fps, 6);
  assert.equal(prepareUimgInstallation(parsed), parsed);
  assert.deepEqual(parsed, jpeg.parseJpegUimg(bytes));
  const entries = jpeg.validateJpegPayload(parsed.payload, 18);
  assert.equal(new Set(entries.map(e => e.offset)).size, 3);
  assert.ok(parsed.payload.length < 320 * 172 * 2);
  assert.deepEqual(jpeg.buildJpegUimg(parsed.payload, 18, 6), bytes);
});

test('JPEG index rejects overflow, overlapping frames, changed lengths, CRC and padding', () => {
  const parsed = jpeg.parseJpegUimg(fixture('sequence.uimg'));
  for (const [offset, value] of [[8, 0xffffffff], [8, 8], [16, 156], [20, 4], [4, 19]]) {
    const broken = parsed.payload.slice();
    new DataView(broken.buffer).setUint32(offset, value, true);
    assert.throws(() => jpeg.validateJpegPayload(broken, 18));
  }
  for (const index of [13, 28, 96, 100, 4096 + 200]) {
    const broken = fixture('sequence.uimg'); broken[index] ^= 1;
    assert.throws(() => parseUimg(broken));
  }
  const frame = fixture('red.jpg');
  const progressive = frame.slice(); const sof = Buffer.from(frame).indexOf(Buffer.from([255, 192]));
  progressive[sof + 1] = 194;
  assert.throws(() => jpeg.buildJpegPayload([progressive]));
  assert.throws(() => jpeg.buildJpegPayload([frame.subarray(0, frame.length - 1)]));
  const payload = jpeg.buildJpegPayload(Array(180).fill(frame));
  assert.equal(jpeg.parseJpegUimg(jpeg.buildJpegUimg(payload, 180)).frameCount, 180);
  assert.ok(payload.length < frame.length + 1452);
  assert.throws(() => jpeg.buildJpegPayload(Array(256).fill(frame)));
});

const slowGif = Buffer.from('R0lGODlhAQABAIEAAP8AAAAAAAAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQAMgAAACwAAAAAAQABAAAIBAABBAQAIfkEATIAAQAsAAAAAAEAAQCBAP8AAAAAAAAAAAAACAQAAQQEADs=', 'base64');
class Canvas {
  constructor(width, height) { this.width = width; this.height = height; this.pixel = [0, 0, 0, 0]; }
  getContext() { return {
    fillRect() {}, putImageData: image => { this.pixel = Array.from(image.data.subarray(0, 4)); },
    drawImage: source => { this.pixel = source.pixel; },
  }; }
  async convertToBlob({ type }) { return new Blob([type === 'image/jpeg' ? fixture(this.pixel[0] ? 'red.jpg' : 'green.jpg') : 'preview'], { type }); }
  toBlob(callback, type) { this.convertToBlob({ type }).then(callback); }
}
test('worker and main-thread canvas paths retain 500 ms GIF holds at twelve FPS', async () => {
  const limits = { maxPayloadBytes: 10000, maxFrames: 180, fps: 12 };
  const saved = { OffscreenCanvas: globalThis.OffscreenCanvas, document: globalThis.document, ImageData: globalThis.ImageData };
  try {
    globalThis.ImageData = class { constructor(data) { this.data = data; } };
    for (const offscreen of [true, false]) {
      globalThis.OffscreenCanvas = offscreen ? Canvas : undefined;
      globalThis.document = { createElement: () => new Canvas(0, 0) };
      const result = await processGalleryJpeg({ name: 'held.gif', type: 'image/gif', arrayBuffer: async () => Uint8Array.from(slowGif).buffer }, limits);
      assert.equal(result.frameCount, 12); assert.equal(result.fps, 12); assert.equal(result.format, 3);
      const entries = jpeg.validateJpegPayload(new Uint8Array(result.payload), 12);
      assert.equal(entries[0].offset, entries[5].offset);
      assert.equal(entries[6].offset, entries[11].offset);
      assert.notEqual(entries[5].offset, entries[6].offset);
      const file = { name: 'held.gif', type: 'image/gif', arrayBuffer: async () => Uint8Array.from(slowGif).buffer };
      const exact = { ...limits, maxPayloadBytes: result.payload.byteLength };
      assert.equal((await processGalleryJpeg(file, exact)).payload.byteLength, exact.maxPayloadBytes);
      await assert.rejects(processGalleryJpeg(file, { ...exact, maxPayloadBytes: exact.maxPayloadBytes - 1 }), /Image capacity exceeded/);
      await assert.rejects(processGalleryJpeg(file, { ...limits, maxFrames: 11 }), /Image frame limit exceeded: 12\/11/);
      await assert.rejects(processGalleryJpeg(file), /Image capacity unavailable/);
    }
  } finally {
    for (const [key, value] of Object.entries(saved)) { if (value === undefined) delete globalThis[key]; else globalThis[key] = value; }
  }
});
