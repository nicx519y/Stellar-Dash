import {
  DeviceTransportErrorCode,
  DeviceTransportState,
} from './types';

export interface DeviceCommandMessage {
  transactionId?: number;
  command: string;
  errNo: number;
  data?: Record<string, unknown>;
}

export interface DeviceConnectionError {
  type: 'connection' | 'timeout' | 'server' | 'protocol';
  message: string;
  code?: number;
  /** Stable transport failure classification used by reconnect UX. */
  transportCode?: DeviceTransportErrorCode;
  phase?: DeviceConnectionPhase;
  command?: string;
  timestamp: Date;
}

/** A native chooser may only be opened from the user's reconnect click. */
export function reconnectRequiresPermission(
  error: DeviceConnectionError | null | undefined,
): boolean {
  return error?.transportCode === 'permission-required' ||
    error?.transportCode === 'permission-denied';
}

export enum DeviceConnectionPhase {
  IDLE = 'idle',
  DISCOVERING = 'discovering',
  OPENING = 'opening',
  ATTESTING = 'attesting',
  AUTHORIZING = 'authorizing',
  INITIALIZING = 'initializing',
  READY = 'ready',
  CLOSING = 'closing',
  ERROR = 'error',
}

export interface DeviceTransportConfig {
  requestTimeoutMs: number;
  startupTimeoutMs: number;
  openTimeoutMs: number;
  closeTimeoutMs: number;
}

export const DEFAULT_DEVICE_TRANSPORT_CONFIG: DeviceTransportConfig = {
  requestTimeoutMs: 15_000,
  startupTimeoutMs: 30_000,
  openTimeoutMs: 5_000,
  closeTimeoutMs: 2_000,
};

export function isDeviceConnected(state: DeviceTransportState): boolean {
  return state === DeviceTransportState.CONNECTED;
}
