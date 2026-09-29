import type { GalleryImageLimits } from './gallery-image-limits';
import { processGalleryJpeg } from './gallery-jpeg-processor';
self.onmessage = async (event: MessageEvent<{ file: File; limits: GalleryImageLimits }>) => {
  try {
    const result = await processGalleryJpeg(event.data.file, event.data.limits);
    self.postMessage(result, { transfer: [result.payload] });
  } catch (error) {
    self.postMessage({ error: error instanceof Error ? error.message : String(error) });
  }
};
