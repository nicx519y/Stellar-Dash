export type DeviceTransportKind = 'webhid' | 'mock';

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
  productId?: string;
  pcbRevision?: string;
  webConfigProfile?: string;
  webConfigBasePath?: string;
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

/**
 * Bounds one logical device operation, including time spent waiting for the
 * physical HID writer, every report write, and the matching response.
 */
export interface DeviceRequestOptions {
  signal?: AbortSignal;
  timeoutMs?: number;
  /**
   * Only applies after every physical HID report has been written. The
   * default remains fail-closed; optional diagnostics may keep the authenticated
   * session alive and discard their specifically tracked late response.
   */
  responseTimeoutMode?: 'fatal' | 'recoverable';
}

export interface DeviceEvent<T = unknown> {
  name: string;
  data: T;
}

export type DeviceStream =
  | 'firmware'
  | 'image'
  | 'config-import';

export interface DeviceUploadOptions {
  signal?: AbortSignal;
  /** One absolute deadline for the complete stream transaction. */
  timeoutMs?: number;
  onProgress?: (sent: number, total: number) => void;
}

export interface DeviceUploadResult {
  complete?: boolean;
  encoding?: string;
  data?: string;
  ack?: Record<string, unknown>;
}

export type Unsubscribe = () => void;

export type DeviceTransportErrorCode =
  | 'unsupported'
  | 'permission-required'
  | 'permission-denied'
  | 'device-busy'
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
   * from a user activation handler.
   */
  requestPermissionAndConnect(): Promise<DeviceSession>;

  request<T = Record<string, unknown> | undefined>(
    command: string,
    params?: Record<string, unknown>,
    options?: DeviceRequestOptions,
  ): Promise<DeviceResponse<T>>;

  subscribe<T = unknown>(
    event: string,
    handler: (event: DeviceEvent<T>) => void,
  ): Unsubscribe;

  upload(
    stream: DeviceStream,
    data: Blob | ArrayBuffer | Uint8Array,
    options?: DeviceUploadOptions,
  ): Promise<DeviceUploadResult>;

  /**
   * Optional transport-owned HTTP implementation. The mock transport uses it
   * to keep firmware checks and downloads completely offline.
   */
  authorizedFetch?(
    input: RequestInfo | URL,
    init?: RequestInit,
  ): Promise<Response>;

  close(): Promise<void>;

  onStateChange(handler: (state: DeviceTransportState) => void): Unsubscribe;
  onError(handler: (error: DeviceTransportError) => void): Unsubscribe;
  onDisconnect(handler: () => void): Unsubscribe;
}
