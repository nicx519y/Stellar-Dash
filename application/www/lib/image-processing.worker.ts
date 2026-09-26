import { decompressFrames, parseGIF } from 'gifuct-js';
import { calculateImageCoverRect } from './image-cover';

const WIDTH = 320;
const HEIGHT = 172;

function toRgb565(rgba: Uint8ClampedArray): Uint8Array {
  const output = new Uint8Array(WIDTH * HEIGHT * 2);
  for (let source = 0, target = 0; source < rgba.length; source += 4) {
    const value = ((rgba[source] >> 3) << 11) | ((rgba[source + 1] >> 2) << 5) | (rgba[source + 2] >> 3);
    output[target++] = value & 0xff;
    output[target++] = value >> 8;
  }
  return output;
}

function render(source: CanvasImageSource, sourceWidth: number, sourceHeight: number) {
  const canvas = new OffscreenCanvas(WIDTH, HEIGHT);
  const context = canvas.getContext('2d', { willReadFrequently: true })!;
  const crop = calculateImageCoverRect(sourceWidth, sourceHeight, WIDTH, HEIGHT);
  context.imageSmoothingEnabled = true;
  context.imageSmoothingQuality = 'high';
  context.drawImage(
    source,
    crop.sourceX,
    crop.sourceY,
    crop.sourceWidth,
    crop.sourceHeight,
    0,
    0,
    WIDTH,
    HEIGHT,
  );
  return { canvas, data: toRgb565(context.getImageData(0, 0, WIDTH, HEIGHT).data) };
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

function selectedIndices(times: number[], total: number) {
  const count = Math.min(6, times.length, Math.max(1, Math.round(total / 1000 * 3)));
  const result: number[] = [];
  for (let sample = 0; sample < count; sample += 1) {
    const target = count === 1 ? 0 : sample * Math.max(0, total - 1) / (count - 1);
    let best = 0;
    for (let index = 1; index < times.length; index += 1) if (Math.abs(times[index] - target) < Math.abs(times[best] - target)) best = index;
    if (!result.includes(best)) result.push(best);
  }
  const last = times.length - 1;
  if (!result.includes(last)) {
    if (result.length >= 6) result[result.length - 1] = last;
    else result.push(last);
  }
  return result.sort((a, b) => a - b);
}

async function processFile(file: File) {
  if (file.type !== 'image/gif' && !file.name.toLowerCase().endsWith('.gif')) {
    const bitmap = await createImageBitmap(file);
    const rendered = render(bitmap, bitmap.width, bitmap.height);
    bitmap.close();
    return { payload: rendered.data.buffer, preview: await rendered.canvas.convertToBlob({ type: 'image/png' }), frameCount: 1, fps: 0 };
  }
  const gif = parseGIF(new Uint8Array(await file.arrayBuffer()));
  const frames = decompressFrames(gif, true);
  const logical = (gif as unknown as { lsd?: { width?: number; height?: number } }).lsd;
  const sourceWidth = logical?.width || Math.max(...frames.map(frame => (frame.dims?.left || 0) + (frame.dims?.width || 0)));
  const sourceHeight = logical?.height || Math.max(...frames.map(frame => (frame.dims?.top || 0) + (frame.dims?.height || 0)));
  const times: number[] = []; let total = 0;
  frames.forEach(frame => { times.push(total); total += Math.max(1, frame.delay || 10) * 10; });
  const selected = new Set(selectedIndices(times, total));
  const rgba = new Uint8ClampedArray(sourceWidth * sourceHeight * 4);
  const sourceCanvas = new OffscreenCanvas(sourceWidth, sourceHeight);
  const sourceContext = sourceCanvas.getContext('2d')!;
  const outputs: Uint8Array[] = []; let preview: Blob | null = null;
  for (let index = 0; index < frames.length; index += 1) {
    const frame = frames[index];
    const dims = frame.dims || { left: 0, top: 0, width: sourceWidth, height: sourceHeight };
    const restore = frame.disposalType === 3 ? new Uint8ClampedArray(rgba) : null;
    blend(rgba, sourceWidth, frame.patch, dims.width, dims.height, dims.left, dims.top);
    if (selected.has(index)) {
      sourceContext.putImageData(new ImageData(new Uint8ClampedArray(rgba), sourceWidth, sourceHeight), 0, 0);
      const rendered = render(sourceCanvas, sourceWidth, sourceHeight);
      outputs.push(rendered.data);
      preview ||= await rendered.canvas.convertToBlob({ type: 'image/png' });
    }
    if (frame.disposalType === 2) {
      for (let y = dims.top; y < dims.top + dims.height; y += 1) rgba.fill(0, (y * sourceWidth + dims.left) * 4, (y * sourceWidth + dims.left + dims.width) * 4);
    } else if (restore) rgba.set(restore);
  }
  const payload = new Uint8Array(outputs.length * WIDTH * HEIGHT * 2);
  outputs.forEach((frame, index) => payload.set(frame, index * frame.byteLength));
  return { payload: payload.buffer, preview, frameCount: outputs.length, fps: outputs.length > 1 ? 3 : 0 };
}

self.onmessage = async (event: MessageEvent<{ file: File }>) => {
  try {
    const result = await processFile(event.data.file);
    self.postMessage(result, { transfer: [result.payload] });
  } catch (error) {
    self.postMessage({ error: error instanceof Error ? error.message : String(error) });
  }
};
