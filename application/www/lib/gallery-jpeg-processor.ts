import { decompressFrames, parseGIF } from 'gifuct-js';
import { calculateImageCoverRect } from './image-cover';
import { gifFrameTimelineUs, selectGifFrameIndices } from './screen-control-image';
import { buildJpegPayload, JPEG_FPS, JPEG_FORMAT } from '../../../common/uimg-jpeg.cjs';

import { assertImageFits, validateGalleryImageLimits, type GalleryImageLimits } from './gallery-image-limits';

type Canvas = OffscreenCanvas | HTMLCanvasElement;
function canvas(width: number, height: number): Canvas {
  if (typeof OffscreenCanvas !== 'undefined') return new OffscreenCanvas(width, height);
  const value = document.createElement('canvas'); value.width = width; value.height = height; return value;
}
function context(value: Canvas) {
  return value.getContext('2d', { willReadFrequently: true }) as OffscreenCanvasRenderingContext2D | CanvasRenderingContext2D;
}
async function encode(value: Canvas, type: string): Promise<Blob> {
  const result = 'convertToBlob' in value
    ? await value.convertToBlob({ type, quality: 0.82 })
    : await new Promise<Blob>((resolve, reject) => value.toBlob(blob => blob ? resolve(blob) : reject(new Error('Image encoding failed')), type, 0.82));
  if (result.type !== type) throw new Error('Browser does not support JPEG image encoding');
  return result;
}
function render(source: CanvasImageSource, width: number, height: number) {
  const target = canvas(320, 172), ctx = context(target);
  const crop = calculateImageCoverRect(width, height, 320, 172);
  ctx.fillStyle = '#000'; ctx.fillRect(0, 0, 320, 172);
  ctx.imageSmoothingEnabled = true; ctx.imageSmoothingQuality = 'high';
  ctx.drawImage(source, crop.sourceX, crop.sourceY, crop.sourceWidth, crop.sourceHeight, 0, 0, 320, 172);
  return target;
}
function blend(target: Uint8ClampedArray, targetWidth: number, patch: Uint8ClampedArray, width: number, height: number, left: number, top: number) {
  for (let y = 0; y < height; y += 1) for (let x = 0; x < width; x += 1) {
    const tx = left + x; const ty = top + y;
    if (tx < 0 || ty < 0 || tx >= targetWidth || ty >= target.length / 4 / targetWidth) continue;
    const sourceIndex = (y * width + x) * 4; const targetIndex = (ty * targetWidth + tx) * 4;
    const alpha = patch[sourceIndex + 3];
    if (!alpha) continue;
    const inverse = 255 - alpha;
    target[targetIndex] = (patch[sourceIndex] * alpha + target[targetIndex] * inverse) / 255;
    target[targetIndex + 1] = (patch[sourceIndex + 1] * alpha + target[targetIndex + 1] * inverse) / 255;
    target[targetIndex + 2] = (patch[sourceIndex + 2] * alpha + target[targetIndex + 2] * inverse) / 255;
    target[targetIndex + 3] = 255;
  }
}


export async function processGalleryJpeg(file: File, limits: GalleryImageLimits) {
  validateGalleryImageLimits(limits);
  let preview: Blob | null = null;
  const outputs: Uint8Array[] = [];
  if (file.type !== 'image/gif' && !file.name.toLowerCase().endsWith('.gif')) {
    const bitmap = await createImageBitmap(file);
    try {
      const target = render(bitmap, bitmap.width, bitmap.height);
      preview = await encode(target, 'image/png');
      outputs.push(new Uint8Array(await (await encode(target, 'image/jpeg')).arrayBuffer()));
    } finally { bitmap.close(); }
  } else {
    const gif = parseGIF(new Uint8Array(await file.arrayBuffer()));
    const frames = decompressFrames(gif, true);
    if (!frames.length) throw new Error('GIF has no image frames');
    const logical = (gif as unknown as { lsd?: { width?: number; height?: number } }).lsd;
    const width = logical?.width || Math.max(...frames.map(frame => (frame.dims?.left || 0) + (frame.dims?.width || 0)));
    const height = logical?.height || Math.max(...frames.map(frame => (frame.dims?.top || 0) + (frame.dims?.height || 0)));
    const { frameTimesUs, totalUs } = gifFrameTimelineUs(frames);
    const count = frames.length <= 1 ? 1 : Math.max(1, Math.ceil(totalUs * JPEG_FPS / 1_000_000));
    assertImageFits(0, count, limits);
    const selected = selectGifFrameIndices(frameTimesUs, totalUs, JPEG_FPS, limits.maxFrames);
    const rgba = new Uint8ClampedArray(width * height * 4);
    const source = canvas(width, height), ctx = context(source);
    let next = 0;
    for (let index = 0; index < frames.length && next < selected.length; index++) {
      const frame = frames[index], dims = frame.dims || { left: 0, top: 0, width, height };
      const restore = frame.disposalType === 3 ? rgba.slice() : null;
      blend(rgba, width, frame.patch, dims.width, dims.height, dims.left, dims.top);
      if (selected[next] === index) {
        ctx.putImageData(new ImageData(rgba.slice(), width, height), 0, 0);
        const target = render(source, width, height);
        const jpeg = new Uint8Array(await (await encode(target, 'image/jpeg')).arrayBuffer());
        preview ||= await encode(target, 'image/png');
        while (selected[next] === index) {
          outputs.push(jpeg); next++;
        }
      }
      if (frame.disposalType === 2) {
        for (let y = Math.max(0, dims.top); y < Math.min(height, dims.top + dims.height); y++) {
          rgba.fill(0, (y * width + Math.max(0, dims.left)) * 4, (y * width + Math.min(width, dims.left + dims.width)) * 4);
        }
      } else if (restore) rgba.set(restore);
    }
  }
  const payload = buildJpegPayload(outputs);
  assertImageFits(payload.length, outputs.length, limits);
  return { payload: payload.buffer as ArrayBuffer, preview: preview!, frameCount: outputs.length, fps: outputs.length > 1 ? JPEG_FPS : 0, format: JPEG_FORMAT };
}
