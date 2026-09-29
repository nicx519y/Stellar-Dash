export const JPEG_FORMAT: 3;
export const JPEG_MAX_FRAMES: 255;
export const JPEG_FPS: 12;
export const JPEG_CAPABILITY: 8;
export function jpegInfo(bytes: Uint8Array, width?: number, height?: number): void;
export function validateJpegPayload(payload: Uint8Array, frameCount: number, width?: number, height?: number): { offset: number; length: number }[];
export function buildJpegPayload(frames: Uint8Array[]): Uint8Array;
export function buildJpegUimg(payload: Uint8Array, frameCount: number, fps?: number): Uint8Array;
export function parseJpegUimg(bytes: Uint8Array): { width: number; height: number; frameCount: number; fps: number; format: number; payloadBytes: number; payloadCrc32: number; payload: Uint8Array };
