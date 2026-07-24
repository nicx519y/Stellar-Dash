import {
  WebSocketDownstreamMessage,
  WebSocketError,
  WebSocketFramework,
  WebSocketState,
} from '@/components/websocket-framework';
import {
  DeviceEvent,
  DeviceResponse,
  DeviceSession,
  DeviceStream,
  DeviceTransport,
  DeviceTransportError,
  DeviceTransportState,
  DeviceUploadOptions,
  Unsubscribe,
} from './types';

export interface LegacyWebSocketTransportOptions {
  url: string;
  heartbeatInterval: number;
  timeout: number;
}

/**
 * Explicit V1 compatibility adapter. The V2 factory never falls back to this
 * transport when WebHID or attestation fails.
 */
export class LegacyWebSocketTransport implements DeviceTransport {
  readonly kind = 'legacy-websocket' as const;
  state = DeviceTransportState.DISCONNECTED;
  session: DeviceSession | null = null;

  private readonly framework: WebSocketFramework;
  private readonly eventHandlers = new Map<string, Set<(event: DeviceEvent) => void>>();
  private readonly stateHandlers = new Set<(state: DeviceTransportState) => void>();
  private readonly errorHandlers = new Set<(error: DeviceTransportError) => void>();
  private readonly disconnectHandlers = new Set<() => void>();

  constructor(options: LegacyWebSocketTransportOptions) {
    this.framework = new WebSocketFramework(options);
    this.framework.onStateChange((state) => this.handleState(state));
    this.framework.onError((error) => this.handleError(error));
    this.framework.onDisconnect(() => {
      this.disconnectHandlers.forEach((handler) => handler());
    });
    this.framework.onMessage((message) => this.handleMessage(message));
    this.framework.onBinaryMessage((binary) => {
      this.emit('legacy.binary', binary, binary);
    });
  }

  async connect(): Promise<DeviceSession> {
    await this.framework.connect();
    this.session = {
      transport: 'legacy-websocket',
      authenticated: false,
      scopes: [],
    };
    return this.session;
  }

  requestPermissionAndConnect(): Promise<DeviceSession> {
    return this.connect();
  }

  async request<T = Record<string, unknown> | undefined>(
    command: string,
    params: Record<string, unknown> = {},
  ): Promise<DeviceResponse<T>> {
    const data = await this.framework.sendMessage(command, params);
    return { transactionId: 0, data: data as T };
  }

  subscribe<T = unknown>(
    event: string,
    handler: (event: DeviceEvent<T>) => void,
  ): Unsubscribe {
    const handlers = this.eventHandlers.get(event) ?? new Set();
    handlers.add(handler as (event: DeviceEvent) => void);
    this.eventHandlers.set(event, handlers);
    return () => {
      handlers.delete(handler as (event: DeviceEvent) => void);
      if (handlers.size === 0) this.eventHandlers.delete(event);
    };
  }

  async upload(
    _stream: DeviceStream,
    source: Blob | ArrayBuffer | Uint8Array,
    options: DeviceUploadOptions = {},
  ): Promise<void> {
    const bytes = source instanceof Blob
      ? new Uint8Array(await source.arrayBuffer())
      : source instanceof Uint8Array
        ? source
        : new Uint8Array(source);
    if (options.signal?.aborted) throw new DOMException('Upload aborted', 'AbortError');
    this.framework.sendBinaryMessage(bytes);
    options.onProgress?.(bytes.byteLength, bytes.byteLength);
  }

  async close(): Promise<void> {
    this.framework.disconnect();
    this.session = null;
  }

  onStateChange(handler: (state: DeviceTransportState) => void): Unsubscribe {
    this.stateHandlers.add(handler);
    return () => this.stateHandlers.delete(handler);
  }

  onError(handler: (error: DeviceTransportError) => void): Unsubscribe {
    this.errorHandlers.add(handler);
    return () => this.errorHandlers.delete(handler);
  }

  onDisconnect(handler: () => void): Unsubscribe {
    this.disconnectHandlers.add(handler);
    return () => this.disconnectHandlers.delete(handler);
  }

  private handleState(state: WebSocketState): void {
    const mapped = {
      [WebSocketState.DISCONNECTED]: DeviceTransportState.DISCONNECTED,
      [WebSocketState.CONNECTING]: DeviceTransportState.CONNECTING,
      [WebSocketState.CONNECTED]: DeviceTransportState.CONNECTED,
      [WebSocketState.ERROR]: DeviceTransportState.ERROR,
    }[state];
    this.state = mapped;
    this.stateHandlers.forEach((handler) => handler(mapped));
  }

  private handleError(error: WebSocketError): void {
    const mapped = new DeviceTransportError(
      error.type === 'timeout' ? 'timeout' : 'protocol',
      error.message,
    );
    this.errorHandlers.forEach((handler) => handler(mapped));
  }

  private handleMessage(message: WebSocketDownstreamMessage): void {
    this.emit(message.command, message.data);
  }

  private emit(name: string, data: unknown, binary?: ArrayBuffer): void {
    const event = { name, data, binary };
    this.eventHandlers.get(name)?.forEach((handler) => handler(event));
    this.eventHandlers.get('*')?.forEach((handler) => handler(event));
  }
}

