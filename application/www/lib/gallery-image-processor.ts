import { processGifToRGB565Sequence, processImageToRGB565 } from './screen-control-image';
import { buildUimgV3 } from './uimg-v3';

export type GalleryProcessedImage = {
  preview: Blob;
  deviceAsset: Uint8Array;
  width: number;
  height: number;
  frameCount: number;
  fps: number;
  payloadCrc32: number;
};

type Pending = { file: File; resolve: (value: GalleryProcessedImage) => void; reject: (reason: unknown) => void };

function dataUrlBlob(value: string): Blob {
  const [header, encoded] = value.split(',', 2);
  const mime = /data:([^;]+)/.exec(header)?.[1] || 'image/png';
  const binary = atob(encoded);
  const bytes = new Uint8Array(binary.length);
  for (let index = 0; index < binary.length; index += 1) bytes[index] = binary.charCodeAt(index);
  return new Blob([bytes], { type: mime });
}

async function fallback(file: File): Promise<GalleryProcessedImage> {
  const gif = file.type === 'image/gif' || file.name.toLowerCase().endsWith('.gif');
  const sequence = gif ? await processGifToRGB565Sequence(file, 3, 6) : null;
  const processed = sequence || await processImageToRGB565(file);
  const frameCount = sequence?.frameCount || 1;
  const fps = frameCount > 1 ? 3 : 0;
  const deviceAsset = buildUimgV3(processed.data, frameCount, fps);
  return {
    preview: dataUrlBlob(processed.previewUrl), deviceAsset,
    width: processed.width, height: processed.height, frameCount, fps,
    payloadCrc32: new DataView(deviceAsset.buffer, deviceAsset.byteOffset).getUint32(84, true),
  };
}

class GalleryImageWorkerPool {
  private idle: Worker[] = [];
  private queue: Pending[] = [];
  private supported = typeof Worker !== 'undefined' && typeof OffscreenCanvas !== 'undefined';
  private fallbackTail: Promise<void> = Promise.resolve();

  constructor() {
    if (!this.supported) return;
    const count = Math.min(4, Math.max(1, (navigator.hardwareConcurrency || 2) - 1));
    try {
      for (let index = 0; index < count; index += 1) this.idle.push(new Worker(new URL('./image-processing.worker.ts', import.meta.url), { type: 'module' }));
    } catch { this.supported = false; this.idle.forEach(worker => worker.terminate()); this.idle = []; }
  }

  process(file: File): Promise<GalleryProcessedImage> {
    if (!this.supported) {
      const task = this.fallbackTail.then(() => fallback(file));
      this.fallbackTail = task.then(() => undefined, () => undefined);
      return task;
    }
    return new Promise((resolve, reject) => { this.queue.push({ file, resolve, reject }); this.pump(); });
  }

  private pump() {
    while (this.idle.length && this.queue.length) {
      const worker = this.idle.pop()!;
      const task = this.queue.shift()!;
      const finish = () => { worker.onmessage = null; worker.onerror = null; this.idle.push(worker); this.pump(); };
      worker.onmessage = event => {
        try {
          if (event.data?.error) throw new Error(event.data.error);
          const payload = new Uint8Array(event.data.payload as ArrayBuffer);
          const deviceAsset = buildUimgV3(payload, event.data.frameCount, event.data.fps);
          task.resolve({
            preview: event.data.preview as Blob, deviceAsset,
            width: 320, height: 172, frameCount: event.data.frameCount, fps: event.data.fps,
            payloadCrc32: new DataView(deviceAsset.buffer, deviceAsset.byteOffset).getUint32(84, true),
          });
        } catch (error) { task.reject(error); }
        finish();
      };
      worker.onerror = event => { task.reject(new Error(event.message || 'Image worker failed')); finish(); };
      worker.postMessage({ file: task.file });
    }
  }
}

let pool: GalleryImageWorkerPool | null = null;
export function processGalleryImage(file: File): Promise<GalleryProcessedImage> {
  pool ||= new GalleryImageWorkerPool();
  return pool.process(file);
}
