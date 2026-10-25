import { decompressFrames, parseGIF } from 'gifuct-js';

// Decode only the first three image frames, including their compositing and
// disposal rules. Short GIFs naturally use their last available frame.
export function decodeGalleryGifPoster(data: ArrayBuffer) {
  const gif = parseGIF(data);
  const frames = decompressFrames({
    ...gif,
    frames: gif.frames.filter((frame: { image?: unknown }) => frame.image).slice(0, 3),
  }, true);
  if (!frames.length) throw new Error('GIF has no image frames');
  const { width, height } = gif.lsd;
  const rgba = new Uint8ClampedArray(width * height * 4);
  for (let index = 0; index < frames.length; index++) {
    const frame = frames[index];
    const dims = frame.dims!;
    const restore = frame.disposalType === 3 ? rgba.slice() : null;
    for (let y = 0; y < dims.height; y++) for (let x = 0; x < dims.width; x++) {
      const tx = dims.left + x, ty = dims.top + y;
      if (tx < 0 || ty < 0 || tx >= width || ty >= height) continue;
      const source = (y * dims.width + x) * 4;
      if (frame.patch[source + 3]) rgba.set(frame.patch.subarray(source, source + 4), (ty * width + tx) * 4);
    }
    if (index === frames.length - 1) break;
    if (frame.disposalType === 2) {
      for (let y = Math.max(0, dims.top); y < Math.min(height, dims.top + dims.height); y++) {
        const left = Math.min(width, Math.max(0, dims.left));
        const right = Math.min(width, Math.max(0, dims.left + dims.width));
        rgba.fill(0, (y * width + left) * 4, (y * width + right) * 4);
      }
    } else if (restore) rgba.set(restore);
  }
  return { width, height, rgba };
}

export async function createGalleryGifPoster(source: Blob): Promise<string> {
  const { width, height, rgba } = decodeGalleryGifPoster(await source.arrayBuffer());
  const canvas = document.createElement('canvas');
  canvas.width = width; canvas.height = height;
  const context = canvas.getContext('2d');
  if (!context) throw new Error('Image preview is unavailable');
  context.putImageData(new ImageData(rgba, width, height), 0, 0);
  return canvas.toDataURL('image/png');
}
