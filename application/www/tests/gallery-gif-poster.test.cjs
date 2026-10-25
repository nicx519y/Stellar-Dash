const assert = require('node:assert/strict');
const { test } = require('node:test');
const { decodeGalleryGifPoster } = require('../lib/gallery-gif-poster.ts');

// Small real GIFs with transparent, partial image frames. Clear codes keep
// the fixture's LZW code width fixed at four bits.
function gif(frames) {
  const bytes = [...Buffer.from('GIF89a'), 3, 0, 1, 0, 0x82, 0, 0,
    0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255,
    255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0];
  for (const { left = 0, pixels, disposal = 1 } of frames) {
    bytes.push(0x21, 0xf9, 4, disposal * 4 + 1, 10, 0, 0, 0);
    bytes.push(0x2c, left, 0, 0, 0, pixels.length, 0, 1, 0, 0);
    const codes = pixels.flatMap(pixel => [8, pixel]).concat(9);
    const compressed = [];
    for (let i = 0; i < codes.length; i += 2) compressed.push(codes[i] | ((codes[i + 1] || 0) << 4));
    bytes.push(3, compressed.length, ...compressed, 0);
  }
  return Uint8Array.from([...bytes, 0x3b]).buffer;
}
const red = [255, 0, 0, 255], green = [0, 255, 0, 255], blue = [0, 0, 255, 255], clear = [0, 0, 0, 0];
const pixels = frames => Array.from(decodeGalleryGifPoster(gif(frames)).rgba);

test('uses the third frame after two empty frames, never the fourth', () => {
  assert.deepEqual(pixels([
    { pixels: [0, 0, 0] }, { pixels: [0, 0, 0] },
    { pixels: [3, 3, 3] }, { pixels: [1, 1, 1] },
  ]), [...blue, ...blue, ...blue]);
});
test('composites transparent partial frames on earlier content', () => {
  assert.deepEqual(pixels([
    { pixels: [1, 1, 1] }, { left: 1, pixels: [2] }, { pixels: [0, 0, 3] },
  ]), [...red, ...green, ...blue]);
});
test('clears only the disposed frame rectangle before frame three', () => {
  assert.deepEqual(pixels([
    { pixels: [1, 1, 1] }, { left: 1, pixels: [2], disposal: 2 }, { left: 2, pixels: [3] },
  ]), [...red, ...clear, ...blue]);
});
test('restores the previous canvas before frame three', () => {
  assert.deepEqual(pixels([
    { pixels: [1, 1, 1] }, { left: 1, pixels: [2], disposal: 3 }, { left: 2, pixels: [3] },
  ]), [...red, ...red, ...blue]);
});
test('one and two frame GIFs use their last frame', () => {
  assert.deepEqual(pixels([{ pixels: [1, 1, 1] }]), [...red, ...red, ...red]);
  assert.deepEqual(pixels([{ pixels: [1, 1, 1] }, { left: 1, pixels: [2] }]), [...red, ...green, ...red]);
});
test('rejects GIFs without image frames', () => {
  assert.throws(() => decodeGalleryGifPoster(gif([])), /no image frames/);
});
