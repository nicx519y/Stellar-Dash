import type { DeviceRequestOptions } from './types';

export interface DeviceFirmwareChunkRequest {
  sessionId: string;
  componentName: string;
  chunkIndex: number;
  totalChunks: number;
  chunkOffset: number;
  targetAddress: number;
  checksumSha256: string;
  data: Uint8Array;
}

export interface DeviceFirmwareChunkResult {
  success: boolean;
  chunkIndex: number;
  progress: number;
  error: string | null;
}

export type DeviceImageTarget = 'user' | 'system';

export interface DeviceImageMetadata {
  valid: boolean;
  width: number;
  height: number;
  size: number;
  frameCount: number;
  fps: number;
  format: number;
  crc32?: number;
}

export interface DeviceImageCatalog {
  protocolVersion: number;
  maxUserFrames: number;
  maxSystemFrames: number;
  imageTransferVersion: number;
  imageDataBytesPerReport: number;
  imageTransferFlags: number;
  user: DeviceImageMetadata;
  system: DeviceImageMetadata;
}

export interface DeviceImageUploadRequest extends DeviceRequestOptions {
  width: number;
  height: number;
  data: Uint8Array;
  frameCount: number;
  fps: number;
  onProgress?: (received: number, total: number) => void;
}

export interface DeviceImageMutationResult {
  success: boolean;
  received: number;
  total: number;
  error: string | null;
  crc32?: number;
}
