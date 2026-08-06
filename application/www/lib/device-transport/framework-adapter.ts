import {
  WebSocketDownstreamMessage,
  WebSocketError,
  WebSocketState,
} from '@/lib/websocket-types';
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
type ScopeUpgradeOperation = {
  generation: number;
  controller: AbortController;
  promise: Promise<void>;
};

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
  private scopeUpgrade: ScopeUpgradeOperation | null = null;
  private closeInFlight: Promise<void> | null = null;
  private connectInFlight: Promise<void> | null = null;
  private connectAbortController: AbortController | null = null;
  private sessionAbortController: AbortController | null = null;
  private internalClosePending = false;
  private externalDisconnectInvalidated = false;
  private lifecycleGeneration = 0;

  constructor(
    readonly transport: DeviceTransport,
    private readonly authClient: DeviceAuthClient | null = null,
  ) {
    this.queue.setSendFunction((command, params) => this.sendMessage(command, params));
    this.unsubscribe.push(
      transport.onStateChange((state) => this.handleTransportState(state)),
      transport.onError((error) => this.handleTransportError(error)),
      transport.onDisconnect(() => {
        this.invalidateExternalDisconnect();
        this.authClient?.clear();
        this.disconnectHandlers.forEach((handler) => handler());
      }),
      transport.subscribe('*', (event) => {
        if (event.name === 'legacy.binary') {
          let binary = event.binary;
          if (!binary) {
            const encoded = asRecord(event.data);
            if (encoded?.encoding === 'base64' && typeof encoded.data === 'string') {
              try {
                binary = base64ToBytes(encoded.data).buffer;
              } catch (error) {
                this.handleAsyncFailure(
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
          return;
        }

        const message: WebSocketDownstreamMessage = {
          command: event.name,
          errNo: 0,
          data: asRecord(event.data),
        };
        this.messageHandlers.forEach((handler) => handler(message));
      }),
    );
  }

  async connect(requestPermission = false): Promise<void> {
    const invocationGeneration = this.lifecycleGeneration;
    if (this.getState() === WebSocketState.CONNECTED) {
      return;
    }
    while (this.connectInFlight) {
      await this.connectInFlight.catch(() => undefined);
      if (this.getState() === WebSocketState.CONNECTED) {
        return;
      }
    }
    if (this.closeInFlight || this.scopeUpgrade) {
      await this.waitForLifecycleBarriers();
    }
    if (this.getState() === WebSocketState.CONNECTED) {
      return;
    }
    // A second caller may have passed the first check while both were waiting
    // for the same asynchronous HID close. Join it instead of authenticating
    // twice on one physical handle.
    if (this.connectInFlight) {
      return this.connectInFlight;
    }
    this.assertLifecycleActive(invocationGeneration);

    const generation = ++this.lifecycleGeneration;
    const authController = new AbortController();
    this.connectAbortController = authController;
    const operation = (async () => {
      this.setState(WebSocketState.CONNECTING);
      try {
        if (requestPermission) {
          await this.transport.requestPermissionAndConnect();
        } else {
          await this.transport.connect();
        }
        this.assertLifecycleActive(generation, authController.signal);
        // A prior physical disconnect has now fully settled and a fresh USB
        // handle belongs to this generation. Future disconnect events must
        // invalidate it independently.
        this.externalDisconnectInvalidated = false;
        if (this.transport instanceof WebHidTransport) {
          if (!this.authClient) {
            throw new DeviceTransportError('authentication-failed', 'WebHID authentication client is not configured');
          }
          await this.authClient.authenticate(
            this.transport,
            DEFAULT_DEVICE_SCOPES,
            authController.signal,
          );
        }
        this.assertLifecycleActive(generation, authController.signal);
        this.replaceSessionAbortController();
        this.setState(WebSocketState.CONNECTED);
      } catch (error) {
        if (generation !== this.lifecycleGeneration) {
          // disconnect() may have run before navigator.hid.getDevices() or
          // device.open() completed. Close any handle opened by that stale
          // attempt before a later connect is allowed to proceed.
          await this.beginTransportClose(this.lifecycleGeneration);
          // The first barrier may have been the earlier disconnect that found
          // no device yet. Re-run close after it settles to catch a handle
          // opened late by this superseded connect attempt.
          await this.beginTransportClose(this.lifecycleGeneration);
          throw error;
        }
        if (this.transport.kind === 'legacy-websocket') {
          if (this.state !== WebSocketState.ERROR) {
            this.handleTransportError(asDeviceTransportError(error));
          }
          throw error;
        }
        await this.failAndClose(error, generation);
        throw error;
      }
    })();
    this.connectInFlight = operation;
    try {
      await operation;
    } finally {
      if (this.connectInFlight === operation) {
        this.connectInFlight = null;
      }
      if (this.connectAbortController === authController) {
        this.connectAbortController = null;
      }
    }
  }

  private async waitForLifecycleBarriers(): Promise<void> {
    while (this.closeInFlight || this.scopeUpgrade) {
      const barriers: Promise<unknown>[] = [];
      if (this.closeInFlight) {
        barriers.push(this.closeInFlight);
      }
      if (this.scopeUpgrade) {
        barriers.push(this.scopeUpgrade.promise.catch(() => undefined));
      }
      await Promise.all(barriers);
    }
  }

  private abortScopeUpgrade(): void {
    const active = this.scopeUpgrade;
    if (!active) {
      return;
    }
    active.controller.abort();
    // The initiating request observes the rejection. This catch also prevents
    // a disconnect with no remaining caller from producing an unhandled one.
    void active.promise.catch(() => undefined);
  }

  private beginTransportClose(generation: number): Promise<void> {
    if (this.closeInFlight) {
      return this.closeInFlight;
    }
    this.internalClosePending = true;
    const closing = Promise.resolve()
      .then(() => this.transport.close())
      .catch(() => undefined)
      .finally(() => {
        this.internalClosePending = false;
        if (generation === this.lifecycleGeneration) {
          this.setState(WebSocketState.DISCONNECTED);
        }
        if (this.closeInFlight === closing) {
          this.closeInFlight = null;
        }
      });
    this.closeInFlight = closing;
    return closing;
  }

  disconnect(): void {
    const generation = ++this.lifecycleGeneration;
    this.connectAbortController?.abort();
    this.abortSessionRequests();
    this.abortScopeUpgrade();
    this.queue.clear();
    this.imageTransferTotals.clear();
    this.authClient?.clear();
    this.setState(WebSocketState.DISCONNECTED);
    void this.beginTransportClose(generation);
  }

  dispose(): void {
    this.disconnect();
    this.unsubscribe.splice(0).forEach((unsubscribe) => unsubscribe());
  }

  async sendMessage(
    command: string,
    params: Record<string, unknown> = {},
  ): Promise<Record<string, unknown> | undefined> {
    const generation = this.lifecycleGeneration;
    const activeUpgrade = this.scopeUpgrade?.generation === generation;
    if (this.state !== WebSocketState.CONNECTED && !activeUpgrade) {
      throw new DeviceTransportError('not-connected', '设备未连接或尚未完成认证');
    }
    const requiredScopes = elevatedScopesForCommand(command);
    if (requiredScopes.length > 0) {
      await this.ensureScopes(requiredScopes, generation);
    }
    return this.runAfterScopeUpgrade(generation, async () => {
      const response = await this.transport.request(command, params);
      this.assertLifecycleActive(generation);
      return response.data;
    });
  }

  sendMessageNoResponse(command: string, params: Record<string, unknown> = {}): void {
    const generation = this.lifecycleGeneration;
    const operation =
      this.transport.kind === 'webhid' && command === 'export_all_config'
        ? exportWebHidConfigSections(
            async (nextCommand, nextParams = {}) => {
              this.assertLifecycleActive(generation);
              const response = await this.sendMessage(nextCommand, nextParams);
              this.assertLifecycleActive(generation);
              return response;
            },
            (section) => {
              this.assertLifecycleActive(generation);
              this.emitExportSection(section);
            },
          )
        : this.sendMessage(command, params).then(() => undefined);
    void operation.catch((error) => {
      if (generation !== this.lifecycleGeneration) {
        return;
      }
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
      this.handleAsyncFailure(error, generation);
    });
  }

  sendBinaryMessage(data: ArrayBuffer | Uint8Array): void {
    const generation = this.lifecycleGeneration;
    const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
    const operation = this.transport.kind === 'webhid'
      ? this.sendWebHidBinary(bytes, generation)
      : this.transport.upload('legacy-binary', bytes);
    void operation.catch((error) => {
      this.handleAsyncFailure(error, generation);
    });
  }

  private async sendWebHidBinary(
    bytes: Uint8Array,
    generation: number,
  ): Promise<void> {
    const activeUpgrade = this.scopeUpgrade?.generation === generation;
    if (this.state !== WebSocketState.CONNECTED && !activeUpgrade) {
      throw new DeviceTransportError('not-connected', '设备未连接或尚未完成认证');
    }
    const command = bytes[0];
    const requiredScope = binaryOpcodeScope(command);
    await this.ensureScopes([requiredScope], generation);
    await this.runAfterScopeUpgrade(generation, async () => {
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
        this.assertLifecycleActive(generation);
        this.emitBinary(makeFirmwareChunkSuccess(bytes));
        return;
      }
      if (command === 0x31 && bytes.byteLength > 14) {
        await this.transport.upload('image', bytes);
        this.assertLifecycleActive(generation);
        this.emitBinary(makeImageChunkSuccess(bytes, this.imageTransferTotals));
        return;
      }

      const response = await this.transport.request<{ data: string }>('binary.exchange', {
        encoding: 'base64',
        data: bytesToBase64(bytes),
      });
      this.assertLifecycleActive(generation);
      if (!response.data?.data) {
        throw new DeviceTransportError('protocol', 'binary.exchange response is missing data');
      }
      this.emitBinary(base64ToBytes(response.data.data).buffer);
      if ((command === 0x32 || command === 0x33) && bytes.byteLength >= 6) {
        const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        this.imageTransferTotals.delete(view.getUint32(2, true));
      }
    });
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
    const generation = this.lifecycleGeneration;
    const activeUpgrade = this.scopeUpgrade?.generation === generation;
    if (this.state !== WebSocketState.CONNECTED && !activeUpgrade) {
      throw new DeviceTransportError('not-connected', '设备未连接或尚未完成认证');
    }
    if (requiredScopes.length > 0) {
      await this.ensureScopes(requiredScopes, generation);
    }
    return this.runAfterScopeUpgrade(generation, async () => {
      const requestInit: RequestInit = {
        ...init,
        signal: combineAbortSignals(
          init?.signal,
          this.sessionAbortController?.signal,
        ),
      };
      let response: Response;
      if (this.transport.authorizedFetch) {
        response = await this.transport.authorizedFetch(input, requestInit);
      } else if (this.transport.kind === 'legacy-websocket') {
        response = await fetch(input, requestInit);
      } else {
        if (!this.authClient) {
          throw new DeviceTransportError(
            'authentication-required',
            `${this.transport.kind} transport does not provide an authorized HTTP client`,
          );
        }
        response = await this.authClient.authorizedFetch(
          input,
          requestInit,
          requiredScopes,
        );
      }
      this.assertLifecycleActive(generation);
      return response;
    });
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
        this.invalidateExternalDisconnect();
        this.authClient?.clear();
        this.setState(WebSocketState.DISCONNECTED);
        break;
      case DeviceTransportState.ERROR:
        this.invalidateExternalDisconnect(WebSocketState.ERROR);
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

  private invalidateExternalDisconnect(
    terminalState = WebSocketState.DISCONNECTED,
  ): void {
    // Adapter-owned close() already invalidated the generation before invoking
    // the transport. Only an unsolicited WebHID disconnect needs a new one.
    if (
      this.transport.kind !== 'webhid' ||
      this.internalClosePending ||
      this.externalDisconnectInvalidated
    ) {
      return;
    }
    this.externalDisconnectInvalidated = true;
    this.lifecycleGeneration += 1;
    this.connectAbortController?.abort();
    this.abortSessionRequests();
    this.abortScopeUpgrade();
    this.queue.clear();
    this.imageTransferTotals.clear();
    this.authClient?.clear();
    this.setState(terminalState);
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

  private failAndClose(
    error: unknown,
    expectedGeneration = this.lifecycleGeneration,
  ): Promise<void> {
    if (expectedGeneration !== this.lifecycleGeneration) {
      return Promise.resolve();
    }
    const closingGeneration = ++this.lifecycleGeneration;
    this.connectAbortController?.abort();
    this.abortSessionRequests();
    this.abortScopeUpgrade();
    const normalized = asDeviceTransportError(error);
    this.queue.clear();
    this.imageTransferTotals.clear();
    this.authClient?.clear();
    // A transport can already have published the same fatal error while
    // failing connect(). Do not notify the UI twice for one failure.
    if (this.state !== WebSocketState.ERROR) {
      this.handleTransportError(normalized);
    }

    return this.beginTransportClose(closingGeneration);
  }

  private handleAsyncFailure(
    error: unknown,
    expectedGeneration = this.lifecycleGeneration,
  ): void {
    if (expectedGeneration !== this.lifecycleGeneration) {
      return;
    }
    const normalized = asDeviceTransportError(error);
    if (this.transport.kind === 'legacy-websocket') {
      this.handleTransportError(normalized);
      return;
    }
    void this.failAndClose(normalized, expectedGeneration);
  }

  private async ensureScopes(
    requiredScopes: readonly DeviceScope[],
    generation = this.lifecycleGeneration,
  ): Promise<void> {
    this.assertLifecycleActive(generation);
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
      this.assertLifecycleActive(generation);
      if (!this.scopeUpgrade) {
        const targetScopes = Array.from(new Set<DeviceScope>([
          ...DEFAULT_DEVICE_SCOPES,
          ...(this.transport.session?.scopes ?? []),
          ...requiredScopes,
        ]));
        const controller = new AbortController();
        const upgrade: ScopeUpgradeOperation = {
          generation,
          controller,
          promise: Promise.resolve(),
        };
        // Reauthorization replaces the bearer token/session epoch. Abort any
        // HTTP body still streaming under the old permit before requesting it.
        this.abortSessionRequests();
        upgrade.promise = this.authClient
          .reauthorize(this.transport, targetScopes, controller.signal)
          .then(() => {
            this.assertLifecycleActive(generation, controller.signal);
            this.replaceSessionAbortController();
          })
          .finally(() => {
            if (this.scopeUpgrade === upgrade) {
              this.scopeUpgrade = null;
            }
          });
        this.scopeUpgrade = upgrade;
      } else if (this.scopeUpgrade.generation !== generation) {
        const staleUpgrade = this.scopeUpgrade;
        staleUpgrade.controller.abort();
        await staleUpgrade.promise.catch(() => undefined);
        this.assertLifecycleActive(generation);
        continue;
      }
      await this.scopeUpgrade.promise;
      this.assertLifecycleActive(generation);
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

  private assertLifecycleActive(
    generation: number,
    signal?: AbortSignal,
  ): void {
    if (generation !== this.lifecycleGeneration || signal?.aborted) {
      throw new DeviceTransportError(
        'disconnected',
        '操作已被后续的断开或重连会话替代',
      );
    }
  }

  private async runAfterScopeUpgrade<T>(
    generation: number,
    operation: () => Promise<T>,
  ): Promise<T> {
    while (this.transport.kind === 'webhid' && this.scopeUpgrade) {
      const active = this.scopeUpgrade;
      if (active.generation !== generation) {
        active.controller.abort();
        await active.promise.catch(() => undefined);
        this.assertLifecycleActive(generation);
        continue;
      }
      await active.promise;
      this.assertLifecycleActive(generation);
    }
    this.assertLifecycleActive(generation);
    if (this.state !== WebSocketState.CONNECTED) {
      throw new DeviceTransportError('not-connected', '设备未连接或尚未完成认证');
    }
    // Invoke synchronously at the no-upgrade boundary so a later caller cannot
    // start reauthorization before this transport operation has been issued.
    return operation();
  }

  private abortSessionRequests(): void {
    this.sessionAbortController?.abort();
    this.sessionAbortController = null;
  }

  private replaceSessionAbortController(): void {
    this.abortSessionRequests();
    this.sessionAbortController = new AbortController();
  }

  private setState(state: WebSocketState): void {
    if (state !== this.state) {
      this.state = state;
      this.stateHandlers.forEach((handler) => handler(state));
    }
  }
}

function combineAbortSignals(
  callerSignal?: AbortSignal | null,
  sessionSignal?: AbortSignal | null,
): AbortSignal | undefined {
  if (!callerSignal) return sessionSignal ?? undefined;
  if (!sessionSignal || callerSignal === sessionSignal) return callerSignal;
  const controller = new AbortController();
  const abort = () => {
    callerSignal.removeEventListener('abort', abort);
    sessionSignal.removeEventListener('abort', abort);
    controller.abort();
  };
  if (callerSignal.aborted || sessionSignal.aborted) {
    controller.abort();
  } else {
    callerSignal.addEventListener('abort', abort, { once: true });
    sessionSignal.addEventListener('abort', abort, { once: true });
  }
  return controller.signal;
}

function asRecord(value: unknown): Record<string, unknown> | undefined {
  if (value && typeof value === 'object' && !ArrayBuffer.isView(value)) {
    return value as Record<string, unknown>;
  }
  return value === undefined ? undefined : { value };
}

function asDeviceTransportError(error: unknown): DeviceTransportError {
  return error instanceof DeviceTransportError
    ? error
    : new DeviceTransportError('protocol', String(error), error);
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
