import { JPEG_CAPABILITY, JPEG_FPS, JPEG_MAX_FRAMES } from '../../../common/uimg-jpeg.cjs';
import type { DeviceImageCatalog } from './device-transport/device-feature-types';
export type GalleryImageLimits = { maxPayloadBytes: number; maxFrames: number; fps: number };
export function validateGalleryImageLimits(limits: GalleryImageLimits): void {
  if (!limits || !Number.isSafeInteger(limits.maxPayloadBytes) || limits.maxPayloadBytes < 20 ||
      !Number.isInteger(limits.maxFrames) || limits.maxFrames < 1 || limits.maxFrames > JPEG_MAX_FRAMES || limits.fps !== JPEG_FPS) {
    throw new Error('Image capacity unavailable');
  }
}
export function galleryImageLimits(catalog: DeviceImageCatalog): GalleryImageLimits {
  if (!(catalog.imageTransferFlags & JPEG_CAPABILITY) || catalog.maxAnimationFps < JPEG_FPS) throw new Error('Image capacity unavailable');
  const limits = { maxPayloadBytes: catalog.maxImagePayloadBytes, maxFrames: catalog.maxJpegFrames, fps: JPEG_FPS };
  validateGalleryImageLimits(limits);
  return limits;
}
export function assertImageFits(bytes: number, frames: number, limits: GalleryImageLimits): void {
  validateGalleryImageLimits(limits);
  if (frames > limits.maxFrames) throw new Error(`Image frame limit exceeded: ${frames}/${limits.maxFrames}`);
  if (bytes > limits.maxPayloadBytes) throw new Error(`Image capacity exceeded: ${bytes}/${limits.maxPayloadBytes}`);
}
