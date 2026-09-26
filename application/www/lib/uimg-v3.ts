import { crc32 } from './crc32';

export const UIMG_HEADER_BYTES = 4096;
export const UIMG_WIDTH = 320;
export const UIMG_HEIGHT = 172;
export const UIMG_MAX_FRAMES = 6;

export type ParsedUimgV3 = {
  width: number;
  height: number;
  frameCount: number;
  fps: number;
  payloadBytes: number;
  payloadCrc32: number;
  payload: Uint8Array;
};

export function buildUimgV3(payload: Uint8Array, frameCount: number, fps: number): Uint8Array {
  const frameSize = UIMG_WIDTH * UIMG_HEIGHT * 2;
  if (!Number.isInteger(frameCount) || frameCount < 1 || frameCount > UIMG_MAX_FRAMES ||
      payload.byteLength !== frameSize * frameCount ||
      (frameCount === 1 ? fps !== 0 : fps !== 3)) {
    throw new Error('Invalid UIMG image metadata');
  }
  const result = new Uint8Array(UIMG_HEADER_BYTES + payload.byteLength);
  const view = new DataView(result.buffer);
  view.setUint32(0, 0x474d4955, true);
  view.setUint16(4, 3, true);
  view.setUint8(6, 1);
  view.setUint8(7, frameCount === 1 ? 1 : 2);
  view.setUint16(8, UIMG_WIDTH, true);
  view.setUint16(10, UIMG_HEIGHT, true);
  view.setUint8(12, frameCount);
  view.setUint8(13, fps);
  view.setUint16(14, 0, true);
  view.setUint32(16, frameSize, true);
  view.setUint32(20, UIMG_HEADER_BYTES, true);
  view.setUint32(24, payload.byteLength, true);
  for (let index = 0; index < 10; index += 1) {
    view.setUint32(28 + index * 4, index < frameCount ? UIMG_HEADER_BYTES + index * frameSize : 0, true);
  }
  result.set(new TextEncoder().encode('USER_IMAGE\0'), 68);
  view.setUint32(84, crc32(payload), true);
  view.setUint32(88, crc32(result.subarray(0, 88)), true);
  result.set(payload, UIMG_HEADER_BYTES);
  return result;
}

export function parseUimgV3(input: ArrayBuffer | Uint8Array): ParsedUimgV3 {
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
  if (bytes.byteLength < UIMG_HEADER_BYTES) throw new Error('UIMG file is truncated');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const width = view.getUint16(8, true);
  const height = view.getUint16(10, true);
  const frameCount = view.getUint8(12);
  const fps = view.getUint8(13);
  const frameSize = view.getUint32(16, true);
  const framesOffset = view.getUint32(20, true);
  const payloadBytes = view.getUint32(24, true);
  const payloadCrc32 = view.getUint32(84, true);
  const expectedId = new Uint8Array(16);
  expectedId.set(new TextEncoder().encode('USER_IMAGE'));
  const idValid = bytes.subarray(68, 84).every((value, index) => value === expectedId[index]);
  const sequence = frameCount > 1;
  if (view.getUint32(0, true) !== 0x474d4955 || view.getUint16(4, true) !== 3 ||
      view.getUint8(6) !== 1 || view.getUint16(14, true) !== 0 || !idValid ||
      width !== UIMG_WIDTH || height !== UIMG_HEIGHT || frameCount < 1 || frameCount > UIMG_MAX_FRAMES ||
      view.getUint8(7) !== (sequence ? 2 : 1) || (sequence ? fps !== 3 : fps !== 0) ||
      frameSize !== width * height * 2 || framesOffset !== UIMG_HEADER_BYTES ||
      payloadBytes !== frameSize * frameCount || bytes.byteLength !== framesOffset + payloadBytes) {
    throw new Error('UIMG metadata is invalid');
  }
  for (let index = 0; index < 10; index += 1) {
    const expected = index < frameCount ? UIMG_HEADER_BYTES + index * frameSize : 0;
    if (view.getUint32(28 + index * 4, true) !== expected) throw new Error('UIMG frame offsets are invalid');
  }
  if (view.getUint32(88, true) !== crc32(bytes.subarray(0, 88))) throw new Error('UIMG header CRC is invalid');
  const payload = bytes.subarray(framesOffset);
  if (crc32(payload) !== payloadCrc32) throw new Error('UIMG payload CRC is invalid');
  return { width, height, frameCount, fps, payloadBytes, payloadCrc32, payload };
}

export async function sha256Hex(bytes: ArrayBuffer | Uint8Array): Promise<string> {
  const data = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const digest = await crypto.subtle.digest('SHA-256', data);
  return Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, '0')).join('');
}
