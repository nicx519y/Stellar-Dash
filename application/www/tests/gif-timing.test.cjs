const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { createRequire } = require('node:module');
const { transform } = require('sucrase');
const { gifFrameTimelineUs, selectGifFrameIndices, processGifToRGB565Sequence } = require('../lib/screen-control-image.ts');
const { buildUimgV4, parseUimg, prepareUimgInstallation } = require('../lib/uimg-v4.ts');

const frameBytes = 320 * 172 * 2;

test('six FPS sampling preserves held frames and variable delays', () => {
  const timeline = gifFrameTimelineUs([{ delay: 500 }, { delay: 500 }]);
  assert.deepEqual(selectGifFrameIndices(timeline.frameTimesUs, timeline.totalUs), [0, 0, 0, 1, 1, 1]);
  const varied = gifFrameTimelineUs([{ delay: 100 }, { delay: 700 }, { delay: 200 }]);
  assert.deepEqual(selectGifFrameIndices(varied.frameTimesUs, varied.totalUs), [0, 1, 1, 1, 1, 2]);
});

test('long GIFs capture the first two seconds without speeding up or jumping to the end', () => {
  const times = Array.from({ length: 40 }, (_, index) => index * 100_000);
  assert.deepEqual(selectGifFrameIndices(times, 4_000_000), [0, 1, 3, 5, 6, 8, 10, 11, 13, 15, 16, 18]);
  const partial = selectGifFrameIndices([0, 100_000], 210_000);
  assert.equal(partial.length, 2);
  assert.ok(partial.length / 6 >= 0.21); // Never shorten the final partial tick.
});

test('legacy installation doubles held frames at six FPS and recomputes the fingerprint', () => {
  for (const count of [2, 6, 12]) {
    const pixels = new Uint8Array(frameBytes * count);
    for (let frame = 0; frame < count; frame++) pixels.fill(frame, frame * frameBytes, (frame + 1) * frameBytes);
    const original = parseUimg(buildUimgV4(pixels, count, 3));
    const converted = prepareUimgInstallation(original);
    assert.equal(converted.fps, 6);
    assert.equal(converted.frameCount / 6, Math.min(count / 3, 2));
    for (let frame = 0; frame < converted.frameCount; frame++) {
      assert.deepEqual(converted.payload.subarray(frame * frameBytes, (frame + 1) * frameBytes),
        pixels.subarray(Math.floor(frame / 2) * frameBytes, (Math.floor(frame / 2) + 1) * frameBytes));
    }
    assert.equal(parseUimg(buildUimgV4(converted.payload, converted.frameCount, 6)).payloadCrc32, converted.payloadCrc32);
    assert.equal(original.fps, 3);
  }
  for (const count of [1, 6]) {
    const original = parseUimg(buildUimgV4(new Uint8Array(frameBytes * count), count, count === 1 ? 0 : 6));
    assert.equal(prepareUimgInstallation(original), original);
  }
});

// A real two-frame GIF: red for 500 ms, then green for 500 ms.
const gif = Buffer.from('R0lGODlhAQABAIEAAP8AAAAAAAAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQAMgAAACwAAAAAAQABAAAIBAABBAQAIfkEATIAAQAsAAAAAAEAAQCBAP8AAAAAAAAAAAAACAQAAQQEADs=', 'base64');
class PixelCanvas {
  constructor(width, height) { this.width = width; this.height = height; this.pixel = [0, 0, 0, 0]; }
  getContext() {
    return {
      putImageData: image => { this.pixel = Array.from(image.data.subarray(0, 4)); },
      drawImage: source => { this.pixel = source.pixel; },
      getImageData: () => {
        const data = new Uint8ClampedArray(this.width * this.height * 4);
        for (let i = 0; i < data.length; i += 4) data.set(this.pixel, i);
        return { data };
      },
    };
  }
  toDataURL() { return 'data:image/png;base64,AA=='; }
  async convertToBlob() { return new Blob(['preview'], { type: 'image/png' }); }
}
class PixelImageData { constructor(data) { this.data = data; } }

test('worker and canvas fallback retain repeated output frames from a real slow GIF', async () => {
  const file = { name: 'held.gif', type: 'image/gif', arrayBuffer: async () => Uint8Array.from(gif).buffer };
  const oldDocument = globalThis.document;
  const oldImageData = globalThis.ImageData;
  let fallback;
  try {
    globalThis.document = { createElement: () => new PixelCanvas(0, 0) };
    globalThis.ImageData = PixelImageData;
    fallback = await processGifToRGB565Sequence(file, 6, 12);
  } finally {
    if (oldDocument === undefined) delete globalThis.document; else globalThis.document = oldDocument;
    if (oldImageData === undefined) delete globalThis.ImageData; else globalThis.ImageData = oldImageData;
  }
  const workerPath = path.resolve(__dirname, '../lib/image-processing.worker.ts');
  const code = transform(fs.readFileSync(workerPath, 'utf8'), { transforms: ['typescript', 'imports'] }).code;
  let output;
  const self = { postMessage: result => { output = result; } };
  vm.runInNewContext(code, { require: createRequire(workerPath), exports: {}, self,
    OffscreenCanvas: PixelCanvas, ImageData: PixelImageData, Uint8Array, Uint8ClampedArray, Blob });
  await self.onmessage({ data: { file } });
  assert.equal(output.error, undefined);
  assert.equal(output.frameCount, 6);
  assert.equal(output.fps, 6);
  assert.equal(fallback.frameCount, 6);
  assert.equal(fallback.fps, 6);
  assert.deepEqual(new Uint8Array(output.payload), fallback.data);
  const colors = Array.from({ length: 6 }, (_, index) => new DataView(output.payload).getUint16(index * frameBytes, true));
  assert.deepEqual(colors, [0xf800, 0xf800, 0xf800, 0x07e0, 0x07e0, 0x07e0]);
});
