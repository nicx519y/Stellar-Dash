const test = require('node:test');
const assert = require('node:assert/strict');
const { gifFrameTimelineUs, selectGifFrameIndices, processGifToRGB565Sequence } = require('../lib/screen-control-image.ts');
const { buildUimgV4, parseUimg, prepareUimgInstallation } = require('../lib/uimg-v4.ts');

const frameBytes = 320 * 172 * 2;

test('twelve FPS sampling preserves held frames and variable delays', () => {
  const timeline = gifFrameTimelineUs([{ delay: 500 }, { delay: 500 }]);
  assert.deepEqual(selectGifFrameIndices(timeline.frameTimesUs, timeline.totalUs), [0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1]);
  const varied = gifFrameTimelineUs([{ delay: 100 }, { delay: 700 }, { delay: 200 }]);
  assert.deepEqual(selectGifFrameIndices(varied.frameTimesUs, varied.totalUs), [0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2]);
});

test('long GIFs capture the first two seconds without speeding up or jumping to the end', () => {
  const times = Array.from({ length: 40 }, (_, index) => index * 100_000);
  assert.deepEqual(selectGifFrameIndices(times, 4_000_000, 6), [0, 1, 3, 5, 6, 8, 10, 11, 13, 15, 16, 18]);
  const partial = selectGifFrameIndices([0, 100_000], 210_000, 6);
  assert.equal(partial.length, 2);
  assert.ok(partial.length / 6 >= 0.21); // Never shorten the final partial tick.
});

test('legacy raw installation preserves timing at 12 FPS and rejects truncation', () => {
  const pixels = new Uint8Array(frameBytes * 2); pixels.fill(7, frameBytes);
  const original = parseUimg(buildUimgV4(pixels, 2, 3));
  const converted = prepareUimgInstallation(original);
  assert.equal(converted.fps, 12); assert.equal(converted.frameCount, 8);
  for (let frame = 0; frame < 8; frame++) assert.equal(converted.payload[frame * frameBytes], frame < 4 ? 0 : 7);
  assert.throws(() => prepareUimgInstallation(parseUimg(buildUimgV4(new Uint8Array(frameBytes * 6), 6, 3))), /frame limit exceeded/);
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

test('legacy canvas fallback retains repeated output frames from a real slow GIF', async () => {
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
  assert.equal(fallback.frameCount, 6);
  assert.equal(fallback.fps, 6);
  const colors = Array.from({ length: 6 }, (_, index) => new DataView(fallback.data.buffer).getUint16(index * frameBytes, true));
  assert.deepEqual(colors, [0xf800, 0xf800, 0xf800, 0x07e0, 0x07e0, 0x07e0]);
});
