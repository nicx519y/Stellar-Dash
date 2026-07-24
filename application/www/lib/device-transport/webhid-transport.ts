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
import {
  FragmentAssembler,
  HidSessionCipher,
  SecureHidFrame,
  SecureHidFrameFlags,
  SecureHidFrameType,
  SecureHidReportCodec,
  fragmentPayload,
  SECURE_HID_PAYLOAD_SIZE,
  SECURE_HID_REPORT_SIZE,
} from './secure-hid-frame';
import {
  getWebHidNavigator,
  WebHidDevice,
  WebHidDeviceFilter,
  WebHidInputReportEvent,
  WebHidNavigator,
} from './webhid-types';

const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder();
const DEFAULT_REQUEST_TIMEOUT_MS = 15_000;
export const WEBHID_MAX_LOGICAL_MESSAGE_SIZE = 8 * 1024;
export const WEBHID_MAX_STREAM_SIZE = 8 * 1024;
export const FIRMWARE_BINARY_HEADER_SIZE = 106;
export const WEBHID_FIRMWARE_CHUNK_DATA_SIZE = 4096;
export const WEBHID_MAX_FIRMWARE_PACKET_SIZE =
  FIRMWARE_BINARY_HEADER_SIZE + WEBHID_FIRMWARE_CHUNK_DATA_SIZE;
const STREAM_HEADER_SIZE = 14;
const STREAM_DATA_SIZE = SECURE_HID_PAYLOAD_SIZE - STREAM_HEADER_SIZE;

interface PendingLogicalRequest {
  resolve: (value: unknown) => void;
  reject: (error: Error) => void;
  timeout: ReturnType<typeof setTimeout>;
}

interface LogicalResponse {
  transactionId: number;
  errNo?: number;
  data?: unknown;
  errorMessage?: string;
}

export interface WebHidTransportOptions {
  filters?: WebHidDeviceFilter[];
  reportId?: number;
  requestTimeoutMs?: number;
  navigator?: WebHidNavigator;
}

export class WebHidTransport implements DeviceTransport {
  readonly kind = 'webhid' as const;
  state = DeviceTransportState.DISCONNECTED;
  session: DeviceSession | null = null;

  private readonly filters: WebHidDeviceFilter[];
  private readonly reportId: number;
  private readonly requestTimeoutMs: number;
  private readonly hid: WebHidNavigator | null;
  private readonly codec = new SecureHidReportCodec();
  private device: WebHidDevice | null = null;
  private nextSequence = 1;
  private lastRxSequence = 0;
  private nextTransactionId = 1;
  private writeChain: Promise<void> = Promise.resolve();
  private readChain: Promise<void> = Promise.resolve();
  private connectionGeneration = 0;
  private reauthorizationPending = false;
  private readonly pendingRpc = new Map<number, PendingLogicalRequest>();
  private readonly pendingBootstrap = new Map<number, PendingLogicalRequest>();
  private readonly assemblers = new Map<SecureHidFrameType, FragmentAssembler>();
  private readonly eventHandlers = new Map<string, Set<(event: DeviceEvent) => void>>();
  private readonly stateHandlers = new Set<(state: DeviceTransportState) => void>();
  private readonly errorHandlers = new Set<(error: DeviceTransportError) => void>();
  private readonly disconnectHandlers = new Set<() => void>();

  constructor(options: WebHidTransportOptions = {}) {
    this.filters = options.filters ?? [{
      vendorId: 0xcafe,
      productId: 0x4021,
      usagePage: 0xff00,
      usage: 0x01,
    }];
    this.reportId = options.reportId ?? 0;
    this.requestTimeoutMs = options.requestTimeoutMs ?? DEFAULT_REQUEST_TIMEOUT_MS;
    this.hid = options.navigator ?? getWebHidNavigator();
  }

  async connect(): Promise<DeviceSession> {
    const hid = this.requireWebHid();
    this.setState(DeviceTransportState.CONNECTING);
    try {
      const granted = (await hid.getDevices()).filter((device) => this.matchesFilter(device));
      if (granted.length === 0) {
        throw new DeviceTransportError(
          'permission-required',
          '没有已授权的 HBox WebHID 设备，请点击连接并在浏览器选择器中授权设备',
        );
      }
      return await this.openDevice(granted[0]);
    } catch (error) {
      return this.failConnect(error);
    }
  }

  async requestPermissionAndConnect(): Promise<DeviceSession> {
    const hid = this.requireWebHid();
    this.setState(DeviceTransportState.CONNECTING);
    try {
      const selected = await hid.requestDevice({ filters: this.filters });
      if (selected.length === 0) {
        throw new DeviceTransportError('permission-denied', '未选择 HBox WebHID 设备');
      }
      return await this.openDevice(selected[0]);
    } catch (error) {
      return this.failConnect(error);
    }
  }

  async request<T = Record<string, unknown> | undefined>(
    command: string,
    params: Record<string, unknown> = {},
  ): Promise<DeviceResponse<T>> {
    this.requireAuthenticated();
    const transactionId = this.allocateTransactionId();
    const response = this.createPendingRequest(this.pendingRpc, transactionId, command);
    try {
      await this.sendLogical(
        SecureHidFrameType.RPC_REQUEST,
        textEncoder.encode(JSON.stringify({ transactionId, command, params })),
        true,
      );
    } catch (error) {
      this.dropPendingRequest(this.pendingRpc, transactionId);
      throw error;
    }
    const data = await response;
    return { transactionId, data: data as T };
  }

  /**
   * Authentication bootstrap is intentionally not exposed through the generic
   * request API. Only the attestation client can use this unauthenticated path.
   */
  async bootstrapRequest<T>(
    command: string,
    params: Record<string, unknown>,
  ): Promise<T> {
    this.requireOpenDevice();
    if (this.session?.authenticated) {
      throw new DeviceTransportError('protocol', 'Bootstrap command is disabled after session authentication');
    }
    const transactionId = this.allocateTransactionId();
    const response = this.createPendingRequest(this.pendingBootstrap, transactionId, command);
    try {
      await this.sendLogical(
        SecureHidFrameType.BOOTSTRAP_REQUEST,
        textEncoder.encode(JSON.stringify({ transactionId, command, params })),
        false,
      );
    } catch (error) {
      this.dropPendingRequest(this.pendingBootstrap, transactionId);
      throw error;
    }
    return await response as T;
  }

  establishSecureSession(cipher: HidSessionCipher, session: DeviceSession): void {
    this.requireOpenDevice();
    if (!session.authenticated || !session.sessionId) {
      throw new DeviceTransportError('authentication-failed', 'Refusing to install an unauthenticated HID session');
    }
    this.codec.setCipher(cipher);
    this.session = Object.freeze({ ...session, scopes: [...session.scopes] });
    this.reauthorizationPending = false;
    this.setState(DeviceTransportState.CONNECTED);
  }

  async endSecureSessionForReauthorization(): Promise<void> {
    this.requireAuthenticated();
    if (this.reauthorizationPending) {
      throw new DeviceTransportError(
        'authentication-required',
        '设备权限重新授权已在进行中',
      );
    }
    this.reauthorizationPending = true;
    const transactionId = this.allocateTransactionId();
    const response = this.createPendingRequest(
      this.pendingRpc,
      transactionId,
      'session.end',
    );
    try {
      await this.sendLogical(
        SecureHidFrameType.RPC_REQUEST,
        textEncoder.encode(JSON.stringify({
          transactionId,
          command: 'session.end',
          params: {},
        })),
        true,
      );
      const result = await response as { ended?: boolean };
      if (!result?.ended) {
        throw new DeviceTransportError(
          'authentication-failed',
          '设备未确认结束当前授权会话',
        );
      }

      const productName = this.session?.productName;
      this.codec.setCipher(null);
      this.session = {
        transport: 'webhid',
        productName,
        authenticated: false,
        scopes: [],
      };
      this.nextSequence = 1;
      this.lastRxSequence = 0;
      this.assemblers.clear();
      this.rejectAllPending(new DeviceTransportError(
        'authentication-required',
        '设备权限会话正在重新授权',
      ));
      this.reauthorizationPending = false;
      this.setState(DeviceTransportState.AUTHENTICATING);
    } catch (error) {
      this.dropPendingRequest(this.pendingRpc, transactionId);
      this.reauthorizationPending = false;
      throw error;
    }
  }

  setAuthenticating(): void {
    this.requireOpenDevice();
    this.setState(DeviceTransportState.AUTHENTICATING);
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
      if (handlers.size === 0) {
        this.eventHandlers.delete(event);
      }
    };
  }

  async upload(
    stream: DeviceStream,
    source: Blob | ArrayBuffer | Uint8Array,
    options: DeviceUploadOptions = {},
  ): Promise<void> {
    this.requireAuthenticated();
    const bytes = await toBytes(source);
    if (bytes.byteLength === 0 || bytes.byteLength > WEBHID_MAX_STREAM_SIZE) {
      throw new DeviceTransportError(
        'protocol',
        `WebHID stream must contain 1..${WEBHID_MAX_STREAM_SIZE} bytes`,
      );
    }
    const digest = bytesToBase64(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)));
    const opened = await this.request<{ transferId: number; credit: number }>('stream.begin', {
      stream,
      length: bytes.byteLength,
      sha256: digest,
    });
    let credit = clampCredit(opened.data?.credit);
    const transferId = opened.data?.transferId;
    if (!Number.isInteger(transferId)) {
      throw new DeviceTransportError('protocol', 'Device did not return a valid stream transferId');
    }

    let offset = 0;
    while (offset < bytes.byteLength) {
      if (options.signal?.aborted) {
        await this.request('stream.abort', { transferId }).catch(() => undefined);
        throw new DOMException('Upload aborted', 'AbortError');
      }
      if (credit === 0) {
        const grant = await this.request<{ credit: number }>('stream.credit', { transferId });
        credit = clampCredit(grant.data?.credit);
        if (credit === 0) {
          throw new DeviceTransportError('protocol', 'Device returned zero stream credit');
        }
      }
      const chunk = bytes.slice(offset, offset + STREAM_DATA_SIZE);
      const payload = new Uint8Array(STREAM_HEADER_SIZE + chunk.byteLength);
      const view = new DataView(payload.buffer);
      payload[0] = streamCode(stream);
      view.setUint32(1, transferId, true);
      view.setUint32(5, offset, true);
      view.setUint32(9, bytes.byteLength, true);
      payload[13] = chunk.byteLength;
      payload.set(chunk, STREAM_HEADER_SIZE);
      await this.sendFrame(SecureHidFrameType.STREAM_CHUNK, payload, SecureHidFrameFlags.ACK_REQUIRED, true);
      offset += chunk.byteLength;
      credit -= 1;
      options.onProgress?.(offset, bytes.byteLength);
    }
    await this.request('stream.complete', { transferId, sha256: digest });
  }

  async close(): Promise<void> {
    await this.shutdownConnection(
      new DeviceTransportError('disconnected', 'WebHID device disconnected'),
    );
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

  private async openDevice(device: WebHidDevice): Promise<DeviceSession> {
    if (!device.opened) {
      await device.open();
    }
    this.connectionGeneration += 1;
    this.device = device;
    this.codec.setCipher(null);
    this.nextSequence = 1;
    this.lastRxSequence = 0;
    this.reauthorizationPending = false;
    this.assemblers.clear();
    device.addEventListener('inputreport', this.handleInputReport);
    this.hid?.addEventListener('disconnect', this.handleNavigatorDisconnect);
    this.session = {
      transport: 'webhid',
      productName: device.productName,
      authenticated: false,
      scopes: [],
    };
    // Connected at the USB layer, but protected requests remain fail-closed.
    this.setState(DeviceTransportState.AUTHENTICATING);
    return this.session;
  }

  private readonly handleInputReport = (event: WebHidInputReportEvent): void => {
    if (event.device !== this.device || event.reportId !== this.reportId) {
      return;
    }
    const generation = this.connectionGeneration;
    const device = this.device;
    const report = new Uint8Array(
      event.data.buffer,
      event.data.byteOffset,
      event.data.byteLength,
    ).slice();
    const processing = this.readChain.then(async () => {
      if (!this.isActiveConnection(generation, device)) {
        return;
      }
      try {
        await this.processInputReport(report, generation, device);
      } catch (error) {
        await this.shutdownConnection(
          asTransportError(error),
          generation,
          device,
          true,
        );
      }
    });
    // Keep the queue reusable without attaching a late error handler that could
    // revive ERROR after close(). shutdownConnection() consumes protocol errors
    // synchronously and invalidates all callbacks from this connection generation.
    this.readChain = processing.catch(() => undefined);
  };

  private readonly handleNavigatorDisconnect = (event: Event & { device?: WebHidDevice }): void => {
    if (event.device && event.device !== this.device) {
      return;
    }
    void this.shutdownConnection(
      new DeviceTransportError('disconnected', 'WebHID device disconnected'),
    ).finally(() => {
      this.disconnectHandlers.forEach((handler) => handler());
    });
  };

  private async processInputReport(
    report: Uint8Array,
    generation: number,
    device: WebHidDevice,
  ): Promise<void> {
    if (report.byteLength !== SECURE_HID_REPORT_SIZE) {
      throw new DeviceTransportError(
        'protocol',
        `Unexpected WebHID input report size ${report.byteLength}`,
      );
    }
    const frame = await this.codec.decode(report);
    if (!this.isActiveConnection(generation, device)) {
      return;
    }
    if (this.session?.authenticated && !frame.secure) {
      throw new DeviceTransportError(
        'authentication-failed',
        'Unauthenticated HID frame received after the secure session was established',
      );
    }
    /*
     * SecureHidReportV1 has one sequence space for control, fragmented RPC,
     * events and telemetry.  A later PERF frame therefore cannot prove that
     * every skipped report was droppable telemetry: one of the missing
     * reports may have been a control/RPC fragment.  Until a future protocol
     * carries an authenticated reliable-sequence watermark, every physical
     * sequence gap is fail-closed.  Reset assemblers and reject pending RPCs
     * before closing so no fragment from either side of the gap can be joined.
     */
    const expectedSequence = this.lastRxSequence === 0
      ? 1
      : this.lastRxSequence + 1;
    if (frame.sequence <= this.lastRxSequence) {
      throw new DeviceTransportError(
        'authentication-failed',
        'Repeated, backward, or wrapped HID sequence',
      );
    }
    if (frame.sequence !== expectedSequence) {
      const error = new DeviceTransportError(
        'authentication-failed',
        frame.secure
          ? 'Authenticated HID sequence gap cannot be proven telemetry-only; reconnect required'
          : 'Bootstrap HID sequence contains a gap',
      );
      this.invalidateLogicalStateForSequenceGap(error);
      throw error;
    }
    this.lastRxSequence = frame.sequence;

    switch (frame.type) {
      case SecureHidFrameType.PERF_SAMPLE:
        this.emit('performance.sample', frame.payload, frame.payload.buffer.slice(0));
        break;
      case SecureHidFrameType.PERF_EDGE:
        this.emit('performance.edge', frame.payload, frame.payload.buffer.slice(0));
        break;
      case SecureHidFrameType.PERF_CHECKPOINT:
        this.emit('performance.checkpoint', frame.payload, frame.payload.buffer.slice(0));
        break;
      case SecureHidFrameType.RPC_RESPONSE:
        this.handleLogicalResponse(frame, this.pendingRpc);
        break;
      case SecureHidFrameType.BOOTSTRAP_RESPONSE:
        this.handleLogicalResponse(frame, this.pendingBootstrap);
        break;
      case SecureHidFrameType.EVENT:
        this.handleLogicalFrame(frame, null);
        break;
      case SecureHidFrameType.ERROR:
        this.handleLogicalError(frame);
        break;
      default:
        throw new DeviceTransportError('protocol', `Unsupported incoming HID frame type ${frame.type}`);
    }
  }

  private handleLogicalResponse(
    frame: SecureHidFrame,
    pending: Map<number, PendingLogicalRequest>,
  ): void {
    const complete = this.assembler(frame.type).push(frame);
    if (!complete) {
      return;
    }
    const response = parseJson<LogicalResponse>(complete, 'logical response');
    const request = pending.get(response.transactionId);
    if (!request) {
      throw new DeviceTransportError('protocol', `Unknown HID transaction ${response.transactionId}`);
    }
    clearTimeout(request.timeout);
    pending.delete(response.transactionId);
    if (response.errNo && response.errNo !== 0) {
      request.reject(new DeviceTransportError('protocol', response.errorMessage ?? `Device error ${response.errNo}`));
    } else {
      request.resolve(response.data);
    }
  }

  private handleLogicalFrame(frame: SecureHidFrame, forcedEventName: string | null): void {
    const complete = this.assembler(frame.type).push(frame);
    if (!complete) {
      return;
    }
    const event = parseJson<{ command?: string; data?: unknown }>(complete, 'device event');
    const name = forcedEventName ?? event.command;
    if (!name) {
      throw new DeviceTransportError('protocol', 'Device event is missing a command name');
    }
    this.emit(name, event.data);
  }

  private handleLogicalError(frame: SecureHidFrame): void {
    const complete = this.assembler(frame.type).push(frame);
    if (!complete) {
      return;
    }
    const error = parseJson<{ message?: string }>(complete, 'device error');
    throw new DeviceTransportError('protocol', error.message ?? 'Device rejected HID request');
  }

  private async sendLogical(
    type: SecureHidFrameType,
    logicalPayload: Uint8Array,
    secure: boolean,
  ): Promise<void> {
    if (logicalPayload.byteLength > WEBHID_MAX_LOGICAL_MESSAGE_SIZE) {
      throw new DeviceTransportError('protocol', 'Logical WebHID message exceeds 8 KiB');
    }
    const fragments = fragmentPayload(logicalPayload);
    await this.enqueueWriteOperation(async (device, generation) => {
      for (let index = 0; index < fragments.length; index += 1) {
        let flags = 0;
        if (fragments.length > 1) flags |= SecureHidFrameFlags.FRAGMENTED;
        if (index === fragments.length - 1) flags |= SecureHidFrameFlags.LAST;
        await this.writeFrameNow(
          device,
          generation,
          type,
          fragments[index],
          flags,
          secure,
        );
      }
    });
  }

  private async sendFrame(
    type: SecureHidFrameType,
    payload: Uint8Array,
    flags: number,
    secure: boolean,
  ): Promise<void> {
    if (this.reauthorizationPending) {
      throw new DeviceTransportError(
        'authentication-required',
        '设备权限会话正在重新授权',
      );
    }
    await this.enqueueWriteOperation((device, generation) =>
      this.writeFrameNow(device, generation, type, payload, flags, secure));
  }

  private async enqueueWriteOperation(
    operation: (device: WebHidDevice, generation: number) => Promise<void>,
  ): Promise<void> {
    this.requireOpenDevice();
    const device = this.device!;
    const generation = this.connectionGeneration;
    const queued = this.writeChain.then(
      () => operation(device, generation),
      () => operation(device, generation),
    );
    // The caller observes its own failure, while the queue remains usable for
    // the next operation. One operation owns the writer until all of its
    // fragments have been emitted.
    this.writeChain = queued.catch(() => undefined);
    await queued;
  }

  private async writeFrameNow(
    device: WebHidDevice,
    generation: number,
    type: SecureHidFrameType,
    payload: Uint8Array,
    flags: number,
    secure: boolean,
  ): Promise<void> {
    if (!this.isActiveConnection(generation, device)) {
      throw new DeviceTransportError('disconnected', 'WebHID device disconnected before write');
    }
    // Allocate inside the serialized write operation so sequence numbers match
    // physical HID write order, including stream frames.
    let sequence: number;
    try {
      sequence = this.allocateSequence();
    } catch (error) {
      const exhausted = error instanceof DeviceTransportError
        ? error
        : new DeviceTransportError(
          'authentication-failed',
          'HID sequence exhausted; reconnect required',
          error,
        );
      await this.shutdownConnection(
        exhausted,
        generation,
        device,
        true,
      );
      throw exhausted;
    }
    const report = await this.codec.encode({ type, flags, sequence, payload, secure });
    if (!this.isActiveConnection(generation, device)) {
      throw new DeviceTransportError('disconnected', 'WebHID device disconnected during write');
    }
    await device.sendReport(this.reportId, report);
  }

  private createPendingRequest(
    collection: Map<number, PendingLogicalRequest>,
    transactionId: number,
    command: string,
  ): Promise<unknown> {
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        collection.delete(transactionId);
        reject(new DeviceTransportError('timeout', `命令 ${command} 响应超时`));
      }, this.requestTimeoutMs);
      collection.set(transactionId, { resolve, reject, timeout });
    });
  }

  private dropPendingRequest(
    collection: Map<number, PendingLogicalRequest>,
    transactionId: number,
  ): void {
    const pending = collection.get(transactionId);
    if (!pending) return;
    clearTimeout(pending.timeout);
    collection.delete(transactionId);
  }

  private assembler(type: SecureHidFrameType): FragmentAssembler {
    let assembler = this.assemblers.get(type);
    if (!assembler) {
      assembler = new FragmentAssembler(WEBHID_MAX_LOGICAL_MESSAGE_SIZE);
      this.assemblers.set(type, assembler);
    }
    return assembler;
  }

  private invalidateLogicalStateForSequenceGap(error: DeviceTransportError): void {
    this.assemblers.forEach((assembler) => assembler.reset());
    this.assemblers.clear();
    this.rejectAllPending(error);
  }

  private emit(name: string, data: unknown, binary?: ArrayBuffer): void {
    const event = { name, data, binary };
    this.eventHandlers.get(name)?.forEach((handler) => handler(event));
    this.eventHandlers.get('*')?.forEach((handler) => handler(event));
  }

  private requireWebHid(): WebHidNavigator {
    if (!globalThis.isSecureContext && typeof window !== 'undefined') {
      throw new DeviceTransportError('unsupported', 'WebHID 需要 HTTPS 安全上下文');
    }
    if (!this.hid) {
      throw new DeviceTransportError('unsupported', '当前浏览器不支持 WebHID');
    }
    return this.hid;
  }

  private requireOpenDevice(): void {
    if (!this.device?.opened) {
      throw new DeviceTransportError('not-connected', 'WebHID device is not connected');
    }
  }

  private requireAuthenticated(): void {
    this.requireOpenDevice();
    if (!this.session?.authenticated || this.state !== DeviceTransportState.CONNECTED) {
      throw new DeviceTransportError('authentication-required', '设备尚未通过在线证明，受保护命令已拒绝');
    }
    if (this.reauthorizationPending) {
      throw new DeviceTransportError('authentication-required', '设备权限会话正在重新授权');
    }
    if (this.session.expiresAt && Date.now() >= this.session.expiresAt) {
      const error = new DeviceTransportError(
        'authentication-required',
        '设备授权会话已过期，请重新连接',
      );
      void this.shutdownConnection(error, undefined, undefined, true);
      throw error;
    }
  }

  private matchesFilter(device: WebHidDevice): boolean {
    return this.filters.some((filter) =>
      (filter.vendorId === undefined || device.vendorId === filter.vendorId) &&
      (filter.productId === undefined || device.productId === filter.productId)
    );
  }

  private allocateSequence(): number {
    if (this.nextSequence > 0xffffffff) {
      throw new DeviceTransportError('authentication-failed', 'HID sequence exhausted; reconnect required');
    }
    return this.nextSequence++;
  }

  private allocateTransactionId(): number {
    const value = this.nextTransactionId;
    this.nextTransactionId = this.nextTransactionId >= 0xffffffff ? 1 : this.nextTransactionId + 1;
    return value;
  }

  private setState(state: DeviceTransportState): void {
    if (state !== this.state) {
      this.state = state;
      this.stateHandlers.forEach((handler) => handler(state));
    }
  }

  private handleError(error: DeviceTransportError): void {
    this.setState(DeviceTransportState.ERROR);
    this.errorHandlers.forEach((handler) => handler(error));
  }

  private isActiveConnection(
    generation: number,
    device: WebHidDevice | null,
  ): device is WebHidDevice {
    return (
      device !== null &&
      generation === this.connectionGeneration &&
      this.device === device &&
      device.opened
    );
  }

  private async shutdownConnection(
    error: DeviceTransportError,
    expectedGeneration?: number,
    expectedDevice?: WebHidDevice | null,
    reportError = false,
  ): Promise<void> {
    if (
      expectedGeneration !== undefined &&
      (
        expectedGeneration !== this.connectionGeneration ||
        expectedDevice !== this.device
      )
    ) {
      return;
    }

    const device = this.device;
    // Everything below this point is synchronous until device.close(). This is
    // the fail-closed boundary: no queued reader/writer can observe a live
    // session after the generation is advanced.
    const closedGeneration = ++this.connectionGeneration;
    this.device = null;
    this.codec.setCipher(null);
    this.session = null;
    this.nextSequence = 1;
    this.lastRxSequence = 0;
    this.reauthorizationPending = false;
    this.assemblers.clear();
    this.readChain = Promise.resolve();
    this.writeChain = Promise.resolve();
    this.rejectAllPending(error);
    if (device) {
      device.removeEventListener('inputreport', this.handleInputReport);
    }
    this.hid?.removeEventListener('disconnect', this.handleNavigatorDisconnect);
    const closing = device?.opened
      ? device.close().catch(() => undefined)
      : Promise.resolve();

    if (reportError) {
      this.setState(DeviceTransportState.ERROR);
      this.errorHandlers.forEach((handler) => handler(error));
    } else {
      this.setState(DeviceTransportState.DISCONNECTED);
    }

    await closing;
    if (this.connectionGeneration === closedGeneration && this.device === null) {
      this.setState(DeviceTransportState.DISCONNECTED);
    }
  }

  private failConnect(error: unknown): never {
    const normalized = asTransportError(error);
    this.handleError(normalized);
    throw normalized;
  }

  private rejectAllPending(error: Error): void {
    for (const collection of [this.pendingRpc, this.pendingBootstrap]) {
      collection.forEach((pending) => {
        clearTimeout(pending.timeout);
        pending.reject(error);
      });
      collection.clear();
    }
  }
}

function parseJson<T>(bytes: Uint8Array, label: string): T {
  try {
    return JSON.parse(textDecoder.decode(bytes)) as T;
  } catch (error) {
    throw new DeviceTransportError('protocol', `Invalid ${label} JSON`, error);
  }
}

function asTransportError(error: unknown): DeviceTransportError {
  if (error instanceof DeviceTransportError) {
    return error;
  }
  if (error instanceof DOMException && error.name === 'NotFoundError') {
    return new DeviceTransportError('permission-denied', 'WebHID 设备选择已取消', error);
  }
  return new DeviceTransportError('protocol', error instanceof Error ? error.message : String(error), error);
}

async function toBytes(source: Blob | ArrayBuffer | Uint8Array): Promise<Uint8Array> {
  if (source instanceof Uint8Array) return source.slice();
  if (source instanceof ArrayBuffer) return new Uint8Array(source.slice(0));
  return new Uint8Array(await source.arrayBuffer());
}

function streamCode(stream: DeviceStream): number {
  switch (stream) {
    case 'firmware': return 1;
    case 'image': return 2;
    case 'config-import': return 3;
    case 'legacy-binary': return 0x7f;
  }
}

function clampCredit(value: unknown): number {
  return typeof value === 'number' && Number.isInteger(value) && value >= 0
    ? Math.min(value, 255)
    : 0;
}

function bytesToBase64(bytes: Uint8Array): string {
  let binary = '';
  for (const value of bytes) binary += String.fromCharCode(value);
  return btoa(binary);
}
