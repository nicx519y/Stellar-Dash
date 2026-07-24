export type DeviceTransportKind = 'webhid' | 'legacy-websocket';

export enum DeviceTransportState {
  DISCONNECTED = 'disconnected',
  CONNECTING = 'connecting',
  AUTHENTICATING = 'authenticating',
  CONNECTED = 'connected',
  ERROR = 'error',
}

export type DeviceScope =
  | 'config.read'
  | 'config.write'
  | 'monitor.read'
  | 'device.control'
  | 'asset.write'
  | 'firmware.update';

export const DEFAULT_DEVICE_SCOPES: readonly DeviceScope[] = [
  'config.read',
  'config.write',
  'monitor.read',
] as const;

export const ELEVATED_DEVICE_SCOPES: readonly DeviceScope[] = [
  'device.control',
  'asset.write',
  'firmware.update',
] as const;

export interface DeviceSession {
  transport: DeviceTransportKind;
  deviceId?: string;
  productName?: string;
  hardwareVersion?: string;
  firmwareVersion?: string;
  authenticated: boolean;
  scopes: readonly DeviceScope[];
  sessionId?: string;
  expiresAt?: number;
}

export interface DeviceResponse<T = Record<string, unknown> | undefined> {
  data: T;
  transactionId: number;
}

export interface DeviceEvent<T = unknown> {
  name: string;
  data: T;
  binary?: ArrayBuffer;
}

export type DeviceStream =
  | 'legacy-binary'
  | 'firmware'
  | 'image'
  | 'config-import';

export interface DeviceUploadOptions {
  signal?: AbortSignal;
  onProgress?: (sent: number, total: number) => void;
}

export type Unsubscribe = () => void;

export type DeviceTransportErrorCode =
  | 'unsupported'
  | 'permission-required'
  | 'permission-denied'
  | 'not-connected'
  | 'authentication-required'
  | 'authentication-failed'
  | 'protocol'
  | 'timeout'
  | 'disconnected'
  | 'server';

export class DeviceTransportError extends Error {
  constructor(
    public readonly code: DeviceTransportErrorCode,
    message: string,
    public readonly cause?: unknown,
  ) {
    super(message);
    this.name = 'DeviceTransportError';
  }
}

export interface DeviceTransport {
  readonly kind: DeviceTransportKind;
  readonly state: DeviceTransportState;
  readonly session: DeviceSession | null;

  /**
   * Connect only to an already granted device. Implementations must not open a
   * browser permission chooser from a page-load effect.
   */
  connect(): Promise<DeviceSession>;

  /**
   * May open a browser permission chooser and therefore must only be called
   * from a user activation handler. Legacy transports may alias this to
   * connect().
   */
  requestPermissionAndConnect(): Promise<DeviceSession>;

  request<T = Record<string, unknown> | undefined>(
    command: string,
    params?: Record<string, unknown>,
  ): Promise<DeviceResponse<T>>;

  subscribe<T = unknown>(
    event: string,
    handler: (event: DeviceEvent<T>) => void,
  ): Unsubscribe;

  upload(
    stream: DeviceStream,
    data: Blob | ArrayBuffer | Uint8Array,
    options?: DeviceUploadOptions,
  ): Promise<void>;

  close(): Promise<void>;

  onStateChange(handler: (state: DeviceTransportState) => void): Unsubscribe;
  onError(handler: (error: DeviceTransportError) => void): Unsubscribe;
  onDisconnect(handler: () => void): Unsubscribe;
}
