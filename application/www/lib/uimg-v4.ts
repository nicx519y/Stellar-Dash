import { parseJpegUimg, JPEG_FORMAT } from '../../../common/uimg-jpeg.cjs';
import { crc32 } from './crc32';
import {
  parseUimgV3,
  UIMG_HEADER_BYTES,
  UIMG_HEIGHT,
  UIMG_WIDTH,
  type ParsedUimgV3,
} from './uimg-v3';

export const UIMG_MAX_FRAMES = 12;
export const UIMG_ANIMATION_FPS = 12;
export const UIMG_LEGACY_ANIMATION_FPS = 3;
export const UIMG_MAX_PAYLOAD_BYTES = UIMG_WIDTH * UIMG_HEIGHT * 2 * UIMG_MAX_FRAMES;
export const IMAGE_TRANSFER_FLAG_6_FPS = 1 << 2;

export function isSupportedImageFps(frameCount: number, fps: number): boolean {
  return frameCount === 1 ? fps === 0 : fps === UIMG_ANIMATION_FPS || fps === 6 || fps === UIMG_LEGACY_ANIMATION_FPS;
}

// Preserve legacy timing when adapting raw frames; never silently truncate.
export function prepareUimgInstallation(image: ParsedUimgV3): ParsedUimgV3 {
  if (image.format === JPEG_FORMAT || image.frameCount <= 1 || image.fps === UIMG_ANIMATION_FPS) return image;
  const frameSize = image.width * image.height * 2;
  const repeat = UIMG_ANIMATION_FPS / image.fps;
  const frameCount = image.frameCount * repeat;
  if (frameCount > UIMG_MAX_FRAMES) throw new Error(`Image frame limit exceeded: ${frameCount}/${UIMG_MAX_FRAMES}`);
  const payload = new Uint8Array(frameSize * frameCount);
  for (let frame = 0; frame < frameCount; frame++) {
    const offset = Math.floor(frame / repeat) * frameSize;
    payload.set(image.payload.subarray(offset, offset + frameSize), frame * frameSize);
  }
  return { ...image, frameCount, fps: UIMG_ANIMATION_FPS, payload,
    payloadBytes: payload.byteLength, payloadCrc32: crc32(payload) };
}
const FRAME_OFFSETS = 12;
const ID_OFFSET = 76;
const PAYLOAD_CRC_OFFSET = 92;
const HEADER_CRC_OFFSET = 96;

export function buildUimgV4(payload: Uint8Array, frameCount: number, fps: number): Uint8Array {
  const frameSize = UIMG_WIDTH * UIMG_HEIGHT * 2;
  if (!Number.isInteger(frameCount) || frameCount < 1 || frameCount > UIMG_MAX_FRAMES ||
      payload.byteLength !== frameSize * frameCount ||
      !isSupportedImageFps(frameCount, fps)) {
    throw new Error('Invalid UIMG image metadata');
  }
  const result = new Uint8Array(UIMG_HEADER_BYTES + payload.byteLength);
  const view = new DataView(result.buffer);
  view.setUint32(0, 0x474d4955, true);
  view.setUint16(4, 4, true);
  view.setUint8(6, 1);
  view.setUint8(7, frameCount === 1 ? 1 : 2);
  view.setUint16(8, UIMG_WIDTH, true);
  view.setUint16(10, UIMG_HEIGHT, true);
  view.setUint8(12, frameCount);
  view.setUint8(13, fps);
  view.setUint32(16, frameSize, true);
  view.setUint32(20, UIMG_HEADER_BYTES, true);
  view.setUint32(24, payload.byteLength, true);
  for (let index = 0; index < FRAME_OFFSETS; index += 1) {
    view.setUint32(28 + index * 4, index < frameCount ? UIMG_HEADER_BYTES + index * frameSize : 0, true);
  }
  result.set(new TextEncoder().encode('USER_IMAGE\0'), ID_OFFSET);
  view.setUint32(PAYLOAD_CRC_OFFSET, crc32(payload), true);
  view.setUint32(HEADER_CRC_OFFSET, crc32(result.subarray(0, HEADER_CRC_OFFSET)), true);
  result.set(payload, UIMG_HEADER_BYTES);
  return result;
}

export function parseUimg(input: ArrayBuffer | Uint8Array): ParsedUimgV3 {
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
  if (bytes.byteLength < UIMG_HEADER_BYTES) throw new Error('UIMG file is truncated');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (view.getUint16(4, true) === 5) return parseJpegUimg(bytes);
  if (view.getUint16(4, true) === 3) return parseUimgV3(bytes);
  const width = view.getUint16(8, true);
  const height = view.getUint16(10, true);
  const frameCount = view.getUint8(12);
  const fps = view.getUint8(13);
  const frameSize = view.getUint32(16, true);
  const framesOffset = view.getUint32(20, true);
  const payloadBytes = view.getUint32(24, true);
  const payloadCrc32 = view.getUint32(PAYLOAD_CRC_OFFSET, true);
  const expectedId = new Uint8Array(16);
  expectedId.set(new TextEncoder().encode('USER_IMAGE'));
  const idValid = bytes.subarray(ID_OFFSET, ID_OFFSET + 16).every((value, index) => value === expectedId[index]);
  const sequence = frameCount > 1;
  if (view.getUint32(0, true) !== 0x474d4955 || view.getUint16(4, true) !== 4 ||
      view.getUint8(6) !== 1 || view.getUint16(14, true) !== 0 || !idValid ||
      width !== UIMG_WIDTH || height !== UIMG_HEIGHT || frameCount < 1 || frameCount > UIMG_MAX_FRAMES ||
      view.getUint8(7) !== (sequence ? 2 : 1) || !isSupportedImageFps(frameCount, fps) ||
      frameSize !== width * height * 2 || framesOffset !== UIMG_HEADER_BYTES ||
      payloadBytes !== frameSize * frameCount || bytes.byteLength !== framesOffset + payloadBytes) {
    throw new Error('UIMG metadata is invalid');
  }
  for (let index = 0; index < FRAME_OFFSETS; index += 1) {
    const expected = index < frameCount ? UIMG_HEADER_BYTES + index * frameSize : 0;
    if (view.getUint32(28 + index * 4, true) !== expected) throw new Error('UIMG frame offsets are invalid');
  }
  if (view.getUint32(HEADER_CRC_OFFSET, true) !== crc32(bytes.subarray(0, HEADER_CRC_OFFSET))) throw new Error('UIMG header CRC is invalid');
  const payload = bytes.subarray(framesOffset);
  if (crc32(payload) !== payloadCrc32) throw new Error('UIMG payload CRC is invalid');
  return { width, height, frameCount, fps, format: sequence ? 2 : 1, payloadBytes, payloadCrc32, payload };
}
