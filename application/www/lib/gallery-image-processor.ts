import type { GalleryImageLimits } from './gallery-image-limits';
import { processGalleryJpeg } from './gallery-jpeg-processor';
import { buildJpegUimg } from '../../../common/uimg-jpeg.cjs';
export type GalleryProcessedImage = {
  preview: Blob;
  deviceAsset: Uint8Array;
  width: number;
  height: number;
  frameCount: number;
  fps: number;
  payloadCrc32: number;
};

type Pending = { file: File; limits: GalleryImageLimits; resolve: (value: GalleryProcessedImage) => void; reject: (reason: unknown) => void };

async function fallback(file: File, limits: GalleryImageLimits): Promise<GalleryProcessedImage> {
  const result = await processGalleryJpeg(file, limits);
  const deviceAsset = buildJpegUimg(new Uint8Array(result.payload), result.frameCount);
  return { ...result, width: 320, height: 172, deviceAsset,
    payloadCrc32: new DataView(deviceAsset.buffer).getUint32(92, true) };
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

  process(file: File, limits: GalleryImageLimits): Promise<GalleryProcessedImage> {
    if (!this.supported) {
      const task = this.fallbackTail.then(() => fallback(file, limits));
      this.fallbackTail = task.then(() => undefined, () => undefined);
      return task;
    }
    return new Promise((resolve, reject) => { this.queue.push({ file, limits, resolve, reject }); this.pump(); });
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
          const deviceAsset = buildJpegUimg(payload, event.data.frameCount);
          task.resolve({
            preview: event.data.preview as Blob, deviceAsset,
            width: 320, height: 172, frameCount: event.data.frameCount, fps: event.data.fps,
            payloadCrc32: new DataView(deviceAsset.buffer, deviceAsset.byteOffset).getUint32(92, true),
          });
        } catch (error) { task.reject(error); }
        finish();
      };
      worker.onerror = event => { task.reject(new Error(event.message || 'Image worker failed')); finish(); };
      worker.postMessage({ file: task.file, limits: task.limits });
    }
  }
}

let pool: GalleryImageWorkerPool | null = null;
export function processGalleryImage(file: File, limits: GalleryImageLimits): Promise<GalleryProcessedImage> {
  pool ||= new GalleryImageWorkerPool();
  return pool.process(file, limits);
}
