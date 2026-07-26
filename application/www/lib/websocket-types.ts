export interface WebSocketUpstreamMessage {
  cid: number;
  command: string;
  params?: Record<string, unknown>;
}

export interface WebSocketDownstreamMessage {
  cid?: number;
  command: string;
  errNo: number;
  data?: Record<string, unknown>;
}

export enum WebSocketState {
  DISCONNECTED = 'disconnected',
  CONNECTING = 'connecting',
  CONNECTED = 'connected',
  ERROR = 'error',
}

export interface WebSocketError {
  type: 'connection' | 'timeout' | 'server' | 'parse';
  message: string;
  code?: number;
  timestamp: Date;
}

export interface WebSocketConfig {
  url?: string;
  heartbeatInterval?: number;
  timeout?: number;
}

export interface UseWebSocketOptions extends WebSocketConfig {
  connectOnMount?: boolean;
}

export type MessageHandler = (message: WebSocketDownstreamMessage) => void;
export type BinaryMessageHandler = (data: ArrayBuffer) => void;
export type StateChangeHandler = (state: WebSocketState) => void;
export type ErrorHandler = (error: WebSocketError) => void;
