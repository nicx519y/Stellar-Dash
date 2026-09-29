import { DeviceTransportError } from './types';

export type ImageTransferFailure =
  | 'catalog-request-failed'
  | 'fast-transfer-required'
  | 'jpeg-required'
  | 'animation-rate'
  | 'frame-limit';

export class ImageTransferError extends DeviceTransportError {
  constructor(
    public readonly reason: ImageTransferFailure,
    message: string,
    public readonly maxFrames?: number,
    cause?: unknown,
  ) {
    super('unsupported', message, cause);
    this.name = 'ImageTransferError';
  }
}
