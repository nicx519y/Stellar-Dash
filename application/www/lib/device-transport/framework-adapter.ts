import {
  WebSocketDownstreamMessage,
  WebSocketError,
  WebSocketState,
} from '@/components/websocket-framework';
import { WebSocketQueueManager } from '@/lib/websocket-queue-manager';
import { DeviceAuthClient } from './device-auth-client';
import {
  DEFAULT_DEVICE_SCOPES,
  DeviceScope,
  DeviceTransport,
  DeviceTransportError,
  DeviceTransportState,
} from './types';
import {
  FIRMWARE_BINARY_HEADER_SIZE,
  WEBHID_FIRMWARE_CHUNK_DATA_SIZE,
  WEBHID_MAX_FIRMWARE_PACKET_SIZE,
  WEBHID_MAX_STREAM_SIZE,
  WebHidTransport,
} from './webhid-transport';
import {
  binaryOpcodeScope,
  elevatedScopesForCommand,
} from './scope-policy';
import {
  exportWebHidConfigSections,
  WebHidExportSection,
} from './webhid-config-export';

if (WEBHID_MAX_FIRMWARE_PACKET_SIZE > WEBHID_MAX_STREAM_SIZE) {
  throw new Error('WebHID firmware packet exceeds the device stream boundary');
}

type MessageHandler = (message: WebSocketDownstreamMessage) => void;
type BinaryHandler = (data: ArrayBuffer) => void;

/**
 * Preserves the existing context surface while routing every command through
 * DeviceTransport.request(). Names remain WebSocket-shaped for one transition
 * release so the configuration schema and UI components do not change.
 */
export class DeviceTransportFrameworkAdapter {
  private state = WebSocketState.DISCONNECTED;
  private readonly queue = new WebSocketQueueManager();
  private readonly messageHandlers = new Set<MessageHandler>();
  private readonly binaryHandlers = new Set<BinaryHandler>();
  private readonly stateHandlers = new Set<(state: WebSocketState) => void>();
  private readonly errorHandlers = new Set<(error: WebSocketError) => void>();
  private readonly disconnectHandlers = new Set<() => void>();
  private readonly unsubscribe: Array<() => void> = [];
  private readonly imageTransferTotals = new Map<number, number>();
  private scopeUpgrade: Promise<void> | null = null;

  constructor(
    readonly transport: DeviceTransport,
    private readonly authClient: DeviceAuthClient | null = null,
  ) {
    this.queue.setSendFunction((command, params) => this.sendMessage(command, params));
    this.unsubscribe.push(
      transport.onStateChange((state) => this.handleTransportState(state)),
      transport.onError((error) => this.handleTransportError(error)),
      transport.onDisconnect(() => {
        this.authClient?.clear();
        this.disconnectHandlers.forEach((handler) => handler());
      }),
      transport.subscribe('*', (event) => {
        const message: WebSocketDownstreamMessage = {
          command: event.name,
          errNo: 0,
          data: asRecord(event.data),
        };
        this.messageHandlers.forEach((handler) => handler(message));
        if (event.name === 'legacy.binary') {
          let binary = event.binary;
          if (!binary) {
            const encoded = asRecord(event.data);
            if (encoded?.encoding === 'base64' && typeof encoded.data === 'string') {
              try {
                binary = base64ToBytes(encoded.data).buffer;
              } catch (error) {
                this.handleTransportError(
                  error instanceof DeviceTransportError
                    ? error
                    : new DeviceTransportError(
                      'protocol',
                      'legacy.binary event contains invalid Base64',
                      error,
                    ),
                );
                return;
              }
            }
          }
          if (binary) {
            this.emitBinary(binary);
          }
        }
      }),
    );
  }

  async connect(requestPermission = false): Promise<void> {
    this.setState(WebSocketState.CONNECTING);
    try {
      if (requestPermission) {
        await this.transport.requestPermissionAndConnect();
      } else {
        await this.transport.connect();
      }
      if (this.transport instanceof WebHidTransport) {
        if (!this.authClient) {
          throw new DeviceTransportError('authentication-failed', 'WebHID authentication client is not configured');
        }
        await this.authClient.authenticate(this.transport);
      }
      this.setState(WebSocketState.CONNECTED);
    } catch (error) {
      if (this.transport.kind === 'webhid') {
        await this.transport.close().catch(() => undefined);
      }
      this.handleTransportError(
        error instanceof DeviceTransportError
          ? error
          : new DeviceTransportError('protocol', String(error), error),
      );
      throw error;
    }
  }

  disconnect(): void {
    this.queue.clear();
    this.authClient?.clear();
    void this.transport.close();
    this.setState(WebSocketState.DISCONNECTED);
  }

  dispose(): void {
    this.disconnect();
    this.unsubscribe.splice(0).forEach((unsubscribe) => unsubscribe());
  }

  async sendMessage(
    command: string,
    params: Record<string, unknown> = {},
  ): Promise<Record<string, unknown> | undefined> {
    if (this.state !== WebSocketState.CONNECTED) {
      throw new DeviceTransportError('not-connected', '设备未连接或尚未完成认证');
    }
    await this.ensureScopes(elevatedScopesForCommand(command));
    return (await this.transport.request(command, params)).data;
  }

  sendMessageNoResponse(command: string, params: Record<string, unknown> = {}): void {
    const operation =
      this.transport.kind === 'webhid' && command === 'export_all_config'
        ? exportWebHidConfigSections(
            (nextCommand, nextParams = {}) =>
              this.sendMessage(nextCommand, nextParams),
            (section) => this.emitExportSection(section),
          )
        : this.sendMessage(command, params).then(() => undefined);
    void operation.catch((error) => {
      if (command === 'export_all_config') {
        const normalized = error instanceof Error
          ? error
          : new Error(String(error));
        this.messageHandlers.forEach((handler) => handler({
          command: 'export_all_config',
          errNo: 1,
          data: { section: 'error', message: normalized.message },
        }));
      }
      this.handleTransportError(
        error instanceof DeviceTransportError
          ? error
          : new DeviceTransportError('protocol', String(error), error),
      );
    });
  }

  sendBinaryMessage(data: ArrayBuffer | Uint8Array): void {
    const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
    const operation = this.transport.kind === 'webhid'
      ? this.sendWebHidBinary(bytes)
      : this.transport.upload('legacy-binary', bytes);
    void operation.catch((error) => {
      this.handleTransportError(
        error instanceof DeviceTransportError
          ? error
          : new DeviceTransportError('protocol', String(error), error),
      );
    });
  }

  private async sendWebHidBinary(bytes: Uint8Array): Promise<void> {
    const command = bytes[0];
    const requiredScope = binaryOpcodeScope(command);
    await this.ensureScopes([requiredScope]);
    // Existing image BEGIN tells the device and this adapter the total size.
    if (command === 0x30 && bytes.byteLength >= 18) {
      const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
      this.imageTransferTotals.set(view.getUint32(2, true), view.getUint32(10, true));
    }

    if (command === 0x01 && bytes.byteLength >= FIRMWARE_BINARY_HEADER_SIZE) {
      if (bytes.byteLength > WEBHID_MAX_FIRMWARE_PACKET_SIZE) {
        throw new DeviceTransportError(
          'protocol',
          `WebHID firmware data chunks are limited to ${WEBHID_FIRMWARE_CHUNK_DATA_SIZE} bytes`,
        );
      }
      await this.transport.upload('firmware', bytes);
      this.emitBinary(makeFirmwareChunkSuccess(bytes));
      return;
    }
    if (command === 0x31 && bytes.byteLength > 14) {
      await this.transport.upload('image', bytes);
      this.emitBinary(makeImageChunkSuccess(bytes, this.imageTransferTotals));
      return;
    }

    const response = await this.transport.request<{ data: string }>('binary.exchange', {
      encoding: 'base64',
      data: bytesToBase64(bytes),
    });
    if (!response.data?.data) {
      throw new DeviceTransportError('protocol', 'binary.exchange response is missing data');
    }
    this.emitBinary(base64ToBytes(response.data.data).buffer);
    if ((command === 0x32 || command === 0x33) && bytes.byteLength >= 6) {
      const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
      this.imageTransferTotals.delete(view.getUint32(2, true));
    }
  }

  private emitBinary(binary: ArrayBuffer): void {
    this.binaryHandlers.forEach((handler) => handler(binary));
  }

  private emitExportSection(section: WebHidExportSection): void {
    const data: Record<string, unknown> = { section: section.section };
    if ('data' in section) {
      data.data = section.data;
    }
    const message: WebSocketDownstreamMessage = {
      command: 'export_all_config',
      errNo: 0,
      data,
    };
    this.messageHandlers.forEach((handler) => handler(message));
  }

  enqueue(
    command: string,
    params: Record<string, unknown> = {},
    immediate = false,
  ): Promise<Record<string, unknown> | undefined> {
    return this.queue.enqueue(command, params, immediate);
  }

  flushQueue(): Promise<void> {
    return this.queue.flushQueue();
  }

  getQueueStatus() {
    return this.queue.getStatus();
  }

  sendPendingCommandImmediately(command: string): boolean {
    return this.queue.sendPendingCommandImmediately(command);
  }

  getPendingRequests(): Array<{ cid: number; command: string; timestamp: Date }> {
    return [];
  }

  cancelPendingCommand(_command: string): number {
    return 0;
  }

  cancelPendingRequest(_cid: number): boolean {
    return false;
  }

  getState(): WebSocketState {
    return this.state;
  }

  async authorizedFetch(
    input: RequestInfo | URL,
    init?: RequestInit,
    requiredScopes: readonly DeviceScope[] = [],
  ): Promise<Response> {
    if (!this.authClient) {
      return fetch(input, init);
    }
    await this.ensureScopes(requiredScopes);
    return this.authClient.authorizedFetch(input, init, requiredScopes);
  }

  onMessage(handler: MessageHandler): () => void {
    this.messageHandlers.add(handler);
    return () => this.messageHandlers.delete(handler);
  }

  onBinaryMessage(handler: BinaryHandler): () => void {
    this.binaryHandlers.add(handler);
    return () => this.binaryHandlers.delete(handler);
  }

  onStateChange(handler: (state: WebSocketState) => void): () => void {
    this.stateHandlers.add(handler);
    return () => this.stateHandlers.delete(handler);
  }

  onError(handler: (error: WebSocketError) => void): () => void {
    this.errorHandlers.add(handler);
    return () => this.errorHandlers.delete(handler);
  }

  onDisconnect(handler: () => void): () => void {
    this.disconnectHandlers.add(handler);
    return () => this.disconnectHandlers.delete(handler);
  }

  private handleTransportState(state: DeviceTransportState): void {
    switch (state) {
      case DeviceTransportState.DISCONNECTED:
        this.authClient?.clear();
        this.setState(WebSocketState.DISCONNECTED);
        break;
      case DeviceTransportState.ERROR:
        this.authClient?.clear();
        this.setState(WebSocketState.ERROR);
        break;
      case DeviceTransportState.CONNECTING:
      case DeviceTransportState.AUTHENTICATING:
        this.setState(WebSocketState.CONNECTING);
        break;
      case DeviceTransportState.CONNECTED:
        // WebHID only reaches CONNECTED after permit installation.
        this.setState(WebSocketState.CONNECTED);
        break;
    }
  }

  private handleTransportError(error: DeviceTransportError): void {
    this.authClient?.clear();
    const mapped: WebSocketError = {
      type:
        error.code === 'timeout'
          ? 'timeout'
          : error.code === 'protocol'
            ? 'parse'
            : 'connection',
      message: error.message,
      timestamp: new Date(),
    };
    this.setState(WebSocketState.ERROR);
    this.errorHandlers.forEach((handler) => handler(mapped));
  }

  private async ensureScopes(requiredScopes: readonly DeviceScope[]): Promise<void> {
    if (this.transport.kind !== 'webhid' || requiredScopes.length === 0) {
      return;
    }
    if (!(this.transport instanceof WebHidTransport) || !this.authClient) {
      throw new DeviceTransportError(
        'authentication-required',
        'WebHID 权限升级客户端不可用',
      );
    }

    while (
      !requiredScopes.every((scope) =>
        this.transport.session?.scopes.includes(scope))
    ) {
      if (!this.scopeUpgrade) {
        const targetScopes = Array.from(new Set<DeviceScope>([
          ...DEFAULT_DEVICE_SCOPES,
          ...(this.transport.session?.scopes ?? []),
          ...requiredScopes,
        ]));
        this.scopeUpgrade = this.authClient
          .reauthorize(this.transport, targetScopes)
          .then(() => undefined)
          .finally(() => {
            this.scopeUpgrade = null;
          });
      }
      await this.scopeUpgrade;
    }

    if (
      !this.authClient.hasScopes(requiredScopes) ||
      !requiredScopes.every((scope) =>
        this.transport.session?.scopes.includes(scope))
    ) {
      throw new DeviceTransportError(
        'authentication-required',
        '设备许可与服务器令牌权限不一致',
      );
    }
  }

  private setState(state: WebSocketState): void {
    if (state !== this.state) {
      this.state = state;
      this.stateHandlers.forEach((handler) => handler(state));
    }
  }
}

function asRecord(value: unknown): Record<string, unknown> | undefined {
  if (value && typeof value === 'object' && !ArrayBuffer.isView(value)) {
    return value as Record<string, unknown>;
  }
  return value === undefined ? undefined : { value };
}

function bytesToBase64(bytes: Uint8Array): string {
  let binary = '';
  for (const value of bytes) binary += String.fromCharCode(value);
  return btoa(binary);
}

function base64ToBytes(value: string): Uint8Array {
  try {
    const binary = atob(value);
    return Uint8Array.from(binary, (character) => character.charCodeAt(0));
  } catch (error) {
    throw new DeviceTransportError('protocol', 'binary.exchange returned invalid Base64', error);
  }
}

function makeFirmwareChunkSuccess(request: Uint8Array): ArrayBuffer {
  const source = new DataView(request.buffer, request.byteOffset, request.byteLength);
  const chunkIndex = source.getUint32(54, true);
  const totalChunks = source.getUint32(58, true);
  const response = new ArrayBuffer(11);
  const view = new DataView(response);
  view.setUint8(0, 0x81);
  view.setUint8(1, 1);
  view.setUint32(2, chunkIndex, true);
  view.setUint32(
    6,
    totalChunks > 0 ? Math.min(100, Math.floor(((chunkIndex + 1) * 100) / totalChunks)) : 100,
    true,
  );
  view.setUint8(10, 0);
  return response;
}

function makeImageChunkSuccess(
  request: Uint8Array,
  totals: ReadonlyMap<number, number>,
): ArrayBuffer {
  const source = new DataView(request.buffer, request.byteOffset, request.byteLength);
  const cid = source.getUint32(2, true);
  const offset = source.getUint32(6, true);
  const length = source.getUint16(10, true);
  const received = offset + length;
  const response = new ArrayBuffer(15);
  const view = new DataView(response);
  view.setUint8(0, 0xb1);
  view.setUint8(1, 1);
  view.setUint32(2, cid, true);
  view.setUint32(6, received, true);
  view.setUint32(10, totals.get(cid) ?? received, true);
  view.setUint8(14, 0);
  return response;
}
