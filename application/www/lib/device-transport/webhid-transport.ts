import {
  DeviceEvent,
  DeviceRequestOptions,
  DeviceResponse,
  DeviceSession,
  DeviceStream,
  DeviceTransport,
  DeviceTransportError,
  DeviceTransportState,
  DeviceUploadOptions,
  DeviceUploadResult,
  Unsubscribe,
} from './types';
import {
  createBrowserWebHidDeviceLease,
  DeviceConnectionLease,
  WEBHID_DEVICE_LOCK_TIMEOUT_MS,
} from './device-lease';
import {
  FragmentAssembler,
  HidSessionCipher,
  SecureHidFrame,
  SecureHidFrameFlags,
  SecureHidFrameType,
  SecureHidReportCodec,
  fragmentPayload,
  SECURE_HID_HEADER_SIZE,
  SECURE_HID_PAYLOAD_SIZE,
  SECURE_HID_REPORT_SIZE,
  SECURE_HID_REPORT_VERSION,
} from './secure-hid-frame';
import {
  getWebHidNavigator,
  WebHidDevice,
  WebHidDeviceFilter,
  WebHidInputReportEvent,
  WebHidNavigator,
} from './webhid-types';
import {
  traceWebHidFrame,
  traceWebHidLogical,
  updateWebHidLogicalTrace,
} from './webhid-network-trace';

const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder();
const DEFAULT_REQUEST_TIMEOUT_MS = 15_000;
const DEFAULT_OPEN_TIMEOUT_MS = 5_000;
const DEFAULT_CLOSE_TIMEOUT_MS = 2_000;
const RECOVERABLE_BOOTSTRAP_COMMANDS = new Set([
  'attestation.create',
  'session.install-permit',
]);
// JSON RPC keeps parity with the former WebConfig transport while uploads
// remain separately bounded by WEBHID_MAX_STREAM_SIZE.
export const WEBHID_MAX_LOGICAL_MESSAGE_SIZE = 16 * 1024;
export const WEBHID_MAX_STREAM_SIZE = 8 * 1024;
export const FIRMWARE_BINARY_HEADER_SIZE = 106;
export const WEBHID_FIRMWARE_CHUNK_DATA_SIZE = 4096;
export const WEBHID_MAX_FIRMWARE_PACKET_SIZE =
  FIRMWARE_BINARY_HEADER_SIZE + WEBHID_FIRMWARE_CHUNK_DATA_SIZE;
const STREAM_HEADER_SIZE = 14;
const STREAM_DATA_SIZE = SECURE_HID_PAYLOAD_SIZE - STREAM_HEADER_SIZE;
const KNOWN_REPORT_FLAGS =
  SecureHidFrameFlags.SECURE |
  SecureHidFrameFlags.FRAGMENTED |
  SecureHidFrameFlags.LAST |
  SecureHidFrameFlags.ACK_REQUIRED;
const DISCARDABLE_PRE_SESSION_SECURE_TYPES = new Set<SecureHidFrameType>([
  SecureHidFrameType.RPC_RESPONSE,
  SecureHidFrameType.EVENT,
  SecureHidFrameType.PERF_SAMPLE,
  SecureHidFrameType.PERF_EDGE,
  SecureHidFrameType.PERF_CHECKPOINT,
  SecureHidFrameType.ERROR,
]);

function isDiscardablePreSessionSecureReport(report: Uint8Array): boolean {
  if (
    report.byteLength !== SECURE_HID_REPORT_SIZE ||
    report[0] !== SECURE_HID_REPORT_VERSION ||
    (report[2] & SecureHidFrameFlags.SECURE) === 0 ||
    (report[2] & ~KNOWN_REPORT_FLAGS) !== 0 ||
    report[3] > SECURE_HID_PAYLOAD_SIZE ||
    new DataView(report.buffer, report.byteOffset, report.byteLength)
      .getUint32(4, true) === 0 ||
    !DISCARDABLE_PRE_SESSION_SECURE_TYPES.has(report[1] as SecureHidFrameType)
  ) {
    return false;
  }
  for (
    let index = SECURE_HID_HEADER_SIZE + report[3];
    index < SECURE_HID_HEADER_SIZE + SECURE_HID_PAYLOAD_SIZE;
    index += 1
  ) {
    if (report[index] !== 0) {
      return false;
    }
  }
  return true;
}

interface PendingLogicalRequest {
  command: string;
  generation: number;
  phase: 'queued' | 'writing' | 'awaiting-response';
  traceRecordId: string | null;
  traceOutcomeRecorded: boolean;
  settled: boolean;
  resolve: (value: unknown) => void;
  reject: (error: Error) => void;
}

interface LogicalResponse {
  transactionId: number;
  errNo?: number;
  data?: unknown;
  errorMessage?: string;
}

interface QuarantinedDevice {
  reason: 'write-timeout' | 'close-timeout';
  closeSettled: boolean;
  disconnected: boolean;
  requiresDisconnect: boolean;
}

interface IgnoredLateBootstrapResponse {
  validGeneration: number;
  expiresAt: number;
}

interface IgnoredLateRpcResponse {
  generation: number;
  expiresAt: number;
}

interface PhysicalReleaseRecord {
  promise: Promise<void>;
  resolve: () => void;
  settled: boolean;
}

interface PhysicalConnectAttempt {
  readonly id: number;
  readonly release: PhysicalReleaseRecord;
  cancelled: boolean;
  established: boolean;
  selectedDevice: WebHidDevice | null;
  nativePending: Promise<void> | null;
}

export interface WebHidTransportOptions {
  filters?: WebHidDeviceFilter[];
  reportId?: number;
  requestTimeoutMs?: number;
  openTimeoutMs?: number;
  closeTimeoutMs?: number;
  /**
   * Minimum interval between native HID OUT submissions. The CH585 endpoint
   * intentionally NAKs while its bounded OUT ring drains to STM32; without a
   * host-side burst limit Chromium can queue most of a fragmented JSON request
   * at once and surface that temporary backpressure as NotAllowedError.
   */
  framePacingMs?: number;
  navigator?: WebHidNavigator;
  /**
   * Browser transports always enforce a lease. Injected test navigators may
   * omit it explicitly because they do not own a real navigator.hid handle.
   */
  connectionLease?: DeviceConnectionLease;
}

/**
 * Only a completed cleartext bootstrap write with a missing response may be
 * retried on the same physical handle. A distinct type prevents command-name
 * matching from accidentally retrying a queued or pending native write.
 */
export class RecoverableBootstrapResponseTimeoutError extends DeviceTransportError {
  constructor(
    readonly command: string,
    message: string,
    readonly connectionGeneration?: number,
    cause?: unknown,
  ) {
    super('timeout', message, cause);
    this.name = 'RecoverableBootstrapResponseTimeoutError';
  }
}

export class WebHidTransport implements DeviceTransport {
  readonly kind = 'webhid' as const;
  state = DeviceTransportState.DISCONNECTED;
  session: DeviceSession | null = null;

  private readonly filters: WebHidDeviceFilter[];
  private readonly reportId: number;
  private readonly requestTimeoutMs: number;
  private readonly openTimeoutMs: number;
  private readonly closeTimeoutMs: number;
  private readonly framePacingMs: number;
  private readonly hid: WebHidNavigator | null;
  private readonly codec = new SecureHidReportCodec();
  private device: WebHidDevice | null = null;
  private nextSequence = 1;
  private nextPhysicalWriteAtMs = 0;
  private lastRxSequence = 0;
  private nextTransactionId = 1;
  private writeChain: Promise<void> = Promise.resolve();
  private readChain: Promise<void> = Promise.resolve();
  private connectionGeneration = 0;
  private physicalCloseInFlight: Promise<void> | null = null;
  private readonly physicalReleaseRecords = new WeakMap<WebHidDevice, PhysicalReleaseRecord>();
  private latestPhysicalRelease: Promise<void> = Promise.resolve();
  private physicalConnectAttempt: PhysicalConnectAttempt | null = null;
  private nextPhysicalConnectAttemptId = 1;
  private navigatorDisconnectListening = false;
  private readonly connectionLease: DeviceConnectionLease | null;
  private connectionLeaseHeld = false;
  private connectionLeaseReleaseInFlight: Promise<void> | null = null;
  private transportConnectGeneration = 0;
  private transportConnectAbortController: AbortController | null = null;
  private reauthorizationPending = false;
  private readonly pendingRpc = new Map<number, PendingLogicalRequest>();
  private readonly pendingBootstrap = new Map<number, PendingLogicalRequest>();
  private readonly quarantinedDevices = new WeakMap<WebHidDevice, QuarantinedDevice>();
  private readonly ignoredLateBootstrapResponses = new Map<number, IgnoredLateBootstrapResponse>();
  private readonly ignoredLateRpcResponses = new Map<number, IgnoredLateRpcResponse>();
  private drainingLateBootstrapResponse = false;
  private readonly assemblers = new Map<SecureHidFrameType, FragmentAssembler>();
  private readonly pendingRxTraceFrameIds = new Map<SecureHidFrameType, string[]>();
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
    this.openTimeoutMs = options.openTimeoutMs ?? DEFAULT_OPEN_TIMEOUT_MS;
    this.closeTimeoutMs = options.closeTimeoutMs ?? DEFAULT_CLOSE_TIMEOUT_MS;
    const usesInjectedNavigator = options.navigator !== undefined;
    this.framePacingMs = Math.max(
      0,
      options.framePacingMs ?? (usesInjectedNavigator ? 0 : 8),
    );
    this.hid = options.navigator ?? getWebHidNavigator();
    this.connectionLease = usesInjectedNavigator
      ? options.connectionLease ?? null
      : options.connectionLease ?? createBrowserWebHidDeviceLease();
  }

  async connect(): Promise<DeviceSession> {
    const { generation, controller } = this.beginTransportConnect();
    let attempt: PhysicalConnectAttempt | null = null;
    try {
      await this.waitForPhysicalClose();
      this.assertTransportConnectActive(generation, controller.signal);
      const hid = this.requireWebHid();
      await this.acquireConnectionLease(controller.signal);
      this.assertTransportConnectActive(generation, controller.signal);
      attempt = this.beginPhysicalConnectAttempt(hid);
      this.setState(DeviceTransportState.CONNECTING);
      const discovery = this.trackPhysicalConnectNative(attempt, hid.getDevices());
      const matching = (await this.awaitWithDeadline(
        discovery,
        Date.now() + this.openTimeoutMs,
        undefined,
        'WebHID device discovery',
      )).filter((device) => this.matchesFilter(device));
      this.assertPhysicalConnectAttemptActive(attempt);
      const granted = matching.filter((device) => !this.quarantinedDevices.has(device));
      if (matching.length > 0 && granted.length === 0) {
        throw this.quarantinedHandleError();
      }
      if (granted.length === 0) {
        throw new DeviceTransportError(
          'permission-required',
          '没有已授权的 HBox WebHID 设备，请点击连接并在浏览器选择器中授权设备',
        );
      }
      if (granted.length > 1) {
        throw new DeviceTransportError(
          'permission-required',
          '检测到多台已授权的 HBox WebHID 设备，请点击连接并明确选择要配置的设备',
        );
      }
      return await this.openDevice(granted[0], attempt);
    } catch (error) {
      this.cancelPhysicalConnectAttempt(attempt);
      const superseded = generation !== this.transportConnectGeneration || controller.signal.aborted;
      if (!superseded) {
        this.releaseConnectionLeaseWhenPhysicalSafe();
      }
      if (superseded) {
        throw new DeviceTransportError(
          'disconnected',
          'WebHID 连接已被 close() 或后续连接取消',
          error,
        );
      }
      return this.failConnect(error);
    } finally {
      if (this.transportConnectAbortController === controller) {
        this.transportConnectAbortController = null;
      }
    }
  }

  async requestPermissionAndConnect(): Promise<DeviceSession> {
    const { generation, controller } = this.beginTransportConnect();
    let attempt: PhysicalConnectAttempt | null = null;
    try {
      await this.waitForPhysicalClose();
      this.assertTransportConnectActive(generation, controller.signal);
      const hid = this.requireWebHid();
      await this.acquireConnectionLease(controller.signal);
      this.assertTransportConnectActive(generation, controller.signal);
      attempt = this.beginPhysicalConnectAttempt(hid);
      this.setState(DeviceTransportState.CONNECTING);
      const selection = this.trackPhysicalConnectNative(
        attempt,
        hid.requestDevice({ filters: this.filters }),
      );
      const selected = await this.awaitWithDeadline(
        selection,
        Date.now() + this.openTimeoutMs,
        undefined,
        'WebHID device selection',
      );
      this.assertPhysicalConnectAttemptActive(attempt);
      if (selected.length === 0) {
        throw new DeviceTransportError('permission-denied', '未选择 HBox WebHID 设备');
      }
      const available = selected.filter((device) => !this.quarantinedDevices.has(device));
      if (available.length === 0) {
        throw this.quarantinedHandleError();
      }
      return await this.openDevice(available[0], attempt);
    } catch (error) {
      this.cancelPhysicalConnectAttempt(attempt);
      const superseded = generation !== this.transportConnectGeneration || controller.signal.aborted;
      if (!superseded) {
        this.releaseConnectionLeaseWhenPhysicalSafe();
      }
      if (superseded) {
        throw new DeviceTransportError(
          'disconnected',
          'WebHID 连接已被 close() 或后续连接取消',
          error,
        );
      }
      return this.failConnect(error);
    } finally {
      if (this.transportConnectAbortController === controller) {
        this.transportConnectAbortController = null;
      }
    }
  }

  async request<T = Record<string, unknown> | undefined>(
    command: string,
    params: Record<string, unknown> = {},
    options: DeviceRequestOptions = {},
  ): Promise<DeviceResponse<T>> {
    this.requireAuthenticated();
    const transactionId = this.allocateTransactionId();
    const data = await this.performLogicalRequest(
      this.pendingRpc,
      transactionId,
      command,
      params,
      SecureHidFrameType.RPC_REQUEST,
      true,
      options,
    );
    return { transactionId, data: data as T };
  }

  /**
   * Authentication bootstrap is intentionally not exposed through the generic
   * request API. Only the attestation client can use this unauthenticated path.
   */
  async bootstrapRequest<T>(
    command: string,
    params: Record<string, unknown>,
    options: DeviceRequestOptions = {},
  ): Promise<T> {
    this.requireOpenDevice();
    if (this.session?.authenticated) {
      throw new DeviceTransportError('protocol', 'Bootstrap command is disabled after session authentication');
    }
    const transactionId = this.allocateTransactionId();
    return await this.performLogicalRequest(
      this.pendingBootstrap,
      transactionId,
      command,
      params,
      SecureHidFrameType.BOOTSTRAP_REQUEST,
      false,
      options,
    ) as T;
  }

  establishSecureSession(cipher: HidSessionCipher, session: DeviceSession): void {
    this.requireOpenDevice();
    if (!session.authenticated || !session.sessionId) {
      throw new DeviceTransportError('authentication-failed', 'Refusing to install an unauthenticated HID session');
    }
    this.codec.setCipher(cipher);
    this.drainingLateBootstrapResponse = false;
    this.ignoredLateBootstrapResponses.clear();
    this.ignoredLateRpcResponses.clear();
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
    try {
      const result = await this.performLogicalRequest(
        this.pendingRpc,
        transactionId,
        'session.end',
        {},
        SecureHidFrameType.RPC_REQUEST,
        true,
        {},
      ) as { ended?: boolean };
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
      this.pendingRxTraceFrameIds.clear();
      this.ignoredLateRpcResponses.clear();
      this.rejectAllPending(new DeviceTransportError(
        'authentication-required',
        '设备权限会话正在重新授权',
      ));
      this.reauthorizationPending = false;
      this.setState(DeviceTransportState.AUTHENTICATING);
    } catch (error) {
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
  ): Promise<DeviceUploadResult> {
    this.requireAuthenticated();
    const deadline = this.operationDeadline(options.timeoutMs);
    const bytes = await this.awaitWithDeadline(
      toBytes(source),
      deadline,
      options.signal,
      'WebHID stream upload',
    );
    if (bytes.byteLength === 0 || bytes.byteLength > WEBHID_MAX_STREAM_SIZE) {
      throw new DeviceTransportError(
        'protocol',
        `WebHID stream must contain 1..${WEBHID_MAX_STREAM_SIZE} bytes`,
      );
    }
    const digest = bytesToBase64(new Uint8Array(await this.awaitWithDeadline(
      crypto.subtle.digest('SHA-256', bytes),
      deadline,
      options.signal,
      'WebHID stream upload',
    )));
    const opened = await this.request<{ transferId: number; credit: number }>('stream.begin', {
      stream,
      length: bytes.byteLength,
      sha256: digest,
    }, {
      signal: options.signal,
      timeoutMs: this.remainingOperationTime(deadline, 'WebHID stream upload'),
    });
    let credit = clampCredit(opened.data?.credit);
    const transferId = opened.data?.transferId;
    if (!Number.isInteger(transferId)) {
      throw new DeviceTransportError('protocol', 'Device did not return a valid stream transferId');
    }

    let offset = 0;
    while (offset < bytes.byteLength) {
      if (options.signal?.aborted) {
        this.abortStreamBestEffort(transferId, deadline);
        throw new DOMException('Upload aborted', 'AbortError');
      }
      if (credit === 0) {
        const grant = await this.request<{ credit: number }>(
          'stream.credit',
          { transferId },
          {
            signal: options.signal,
            timeoutMs: this.remainingOperationTime(deadline, 'WebHID stream upload'),
          },
        );
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
      await this.sendFrame(
        SecureHidFrameType.STREAM_CHUNK,
        payload,
        SecureHidFrameFlags.ACK_REQUIRED,
        true,
        {
          signal: options.signal,
          timeoutMs: this.remainingOperationTime(deadline, 'WebHID stream upload'),
        },
      );
      offset += chunk.byteLength;
      credit -= 1;
      options.onProgress?.(offset, bytes.byteLength);
    }
    const completed = await this.request<DeviceUploadResult>(
      'stream.complete',
      { transferId, sha256: digest },
      {
        signal: options.signal,
        timeoutMs: this.remainingOperationTime(deadline, 'WebHID stream upload'),
      },
    );
    return completed.data ?? {};
  }

  async close(): Promise<void> {
    this.cancelTransportConnect();
    await this.shutdownConnection(
      new DeviceTransportError('disconnected', 'WebHID device disconnected'),
    );
  }

  /** Resolves only when native close settles or a physical disconnect occurs. */
  waitForPhysicalRelease(): Promise<void> {
    return this.latestPhysicalRelease;
  }

  /**
   * Reuse an already-open HID handle after a bootstrap request has caused the
   * device to discard a stale encrypted session.
   *
   * The STM32 resets its WebHID sequence and crypto state as soon as it sees a
   * cleartext bootstrap frame while an old secure session is still active. On
   * Windows, HIDDevice.close() can remain pending indefinitely in that exact
   * state, so close-and-reopen prevents the retry from ever being sent. Keep
   * the physical handle and reset only browser-owned logical state instead.
   */
  resynchronizeBootstrap(): void {
    this.requireOpenDevice();
    const resetError = new DeviceTransportError(
      'authentication-required',
      'Resetting stale WebHID bootstrap state',
    );
    this.connectionGeneration += 1;
    this.codec.setCipher(null);
    this.session = {
      transport: 'webhid',
      productName: this.device?.productName,
      authenticated: false,
      scopes: [],
    };
    this.nextSequence = 1;
    this.lastRxSequence = 0;
    this.drainingLateBootstrapResponse = false;
    this.reauthorizationPending = false;
    this.assemblers.forEach((assembler) => assembler.reset());
    this.assemblers.clear();
    this.pendingRxTraceFrameIds.clear();
    this.rejectAllPending(resetError);
    this.readChain = Promise.resolve();
    this.writeChain = Promise.resolve();
    this.setState(DeviceTransportState.AUTHENTICATING);
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

  private async openDevice(
    device: WebHidDevice,
    attempt: PhysicalConnectAttempt,
  ): Promise<DeviceSession> {
    this.assertPhysicalConnectAttemptActive(attempt);
    if (this.quarantinedDevices.has(device)) {
      throw this.quarantinedHandleError();
    }
    attempt.selectedDevice = device;
    this.physicalReleaseRecords.set(device, attempt.release);
    if (!device.opened) {
      const nativeOpen = this.trackPhysicalConnectNative(attempt, device.open());
      await this.awaitWithDeadline(
        nativeOpen,
        Date.now() + this.openTimeoutMs,
        undefined,
        'WebHID device open',
      );
    }
    this.assertPhysicalConnectAttemptActive(attempt);
    this.connectionGeneration += 1;
    this.device = device;
    attempt.established = true;
    if (this.physicalConnectAttempt === attempt) {
      this.physicalConnectAttempt = null;
    }
    this.codec.setCipher(null);
    this.nextSequence = 1;
    this.lastRxSequence = 0;
    this.drainingLateBootstrapResponse = false;
    this.ignoredLateBootstrapResponses.clear();
    this.ignoredLateRpcResponses.clear();
    this.reauthorizationPending = false;
    this.assemblers.clear();
    this.pendingRxTraceFrameIds.clear();
    device.addEventListener('inputreport', this.handleInputReport);
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
    if (event.device) {
      this.notePhysicalDisconnect(event.device);
    }
    if (event.device && event.device !== this.device) {
      return;
    }
    const closing = this.shutdownConnection(
      new DeviceTransportError('disconnected', 'WebHID device disconnected'),
    );
    // Publish the lifecycle edge synchronously after shutdownConnection has
    // invalidated the device/session, rather than after a slow physical close
    // that could otherwise clear a newly authenticated adapter session.
    this.disconnectHandlers.forEach((handler) => handler());
    void closing;
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
    /*
     * Windows can deliver reports that were already queued by the previous
     * encrypted generation after a new page has opened the same HID handle.
     * There is deliberately no old key with which to authenticate them. Drop
     * only syntactically valid protected device-output types while bootstrap
     * is still cleartext; importantly, do this before decode/sequence tracking
     * so the current generation's plaintext response must still begin at one.
     * Once a session is installed, decode and the fail-closed sequence checks
     * below remain authoritative for every encrypted report.
     */
    if (
      !this.session?.authenticated &&
      isDiscardablePreSessionSecureReport(report)
    ) {
      return;
    }
    const frame = await this.codec.decode(report);
    if (!this.isActiveConnection(generation, device)) {
      return;
    }
    const traceFrameRecordId = traceWebHidFrame({
      direction: 'rx',
      reportId: this.reportId,
      type: frame.type,
      flags: frame.flags,
      sequence: frame.sequence,
      secure: frame.secure,
      plaintextPayload: frame.payload,
      wireReport: report,
    });
    /*
     * A timed-out cleartext response may already be queued in the physical
     * USB IN path when a same-handle bootstrap retry starts.  Firmware resets
     * its logical TX sequence for the retry, but it deliberately cannot erase
     * a report already owned by CH585/Windows.  Drain only that pre-session,
     * cleartext response when a recoverable transaction from the immediately
     * previous logical generation is recorded.  Nothing drained here is ever
     * accepted as authentication data, and authenticated/ordinary RPC sequence
     * checking below remains unchanged and fail-closed.
     */
    if (
      !this.session?.authenticated &&
      !frame.secure &&
      frame.type === SecureHidFrameType.BOOTSTRAP_RESPONSE &&
      this.hasIgnoredLateBootstrapResponseForCurrentGeneration()
    ) {
      if (this.drainingLateBootstrapResponse) {
        if (frame.sequence !== 1) {
          if ((frame.flags & SecureHidFrameFlags.LAST) !== 0) {
            this.drainingLateBootstrapResponse = false;
          }
          return;
        }
        // Firmware may have removed the unsent tail of the old response when
        // the retry's sequence-one takeover reset its logical output queue.
        // Sequence one is therefore the authoritative start of the new
        // generation even while an old fragment drain was in progress.
        this.drainingLateBootstrapResponse = false;
      }
      if (this.lastRxSequence === 0 && frame.sequence !== 1) {
        this.drainingLateBootstrapResponse =
          (frame.flags & SecureHidFrameFlags.LAST) === 0;
        return;
      }
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
          ? `Authenticated HID sequence gap: expected ${expectedSequence}, received ${frame.sequence}, type ${frame.type}; reconnect required`
          : `Bootstrap HID sequence gap: expected ${expectedSequence}, received ${frame.sequence}, type ${frame.type}`,
      );
      this.invalidateLogicalStateForSequenceGap(error);
      throw error;
    }
    this.lastRxSequence = frame.sequence;

    switch (frame.type) {
      case SecureHidFrameType.PERF_SAMPLE:
        this.emit('performance.sample', frame.payload);
        break;
      case SecureHidFrameType.PERF_EDGE:
        this.emit('performance.edge', frame.payload);
        break;
      case SecureHidFrameType.PERF_CHECKPOINT:
        this.emit('performance.checkpoint', frame.payload);
        break;
      case SecureHidFrameType.RPC_RESPONSE:
        this.handleLogicalResponse(frame, this.pendingRpc, traceFrameRecordId);
        break;
      case SecureHidFrameType.BOOTSTRAP_RESPONSE:
        if (this.handleLogicalResponse(frame, this.pendingBootstrap, traceFrameRecordId)) {
          // The complete response belonged to the timed-out generation.  The
          // retry's firmware response starts a fresh cleartext sequence at 1.
          this.lastRxSequence = 0;
        }
        break;
      case SecureHidFrameType.EVENT:
        this.handleLogicalFrame(frame, null, traceFrameRecordId);
        break;
      case SecureHidFrameType.ERROR:
        this.handleLogicalError(frame, traceFrameRecordId);
        break;
      default:
        throw new DeviceTransportError('protocol', `Unsupported incoming HID frame type ${frame.type}`);
    }
  }

  private handleLogicalResponse(
    frame: SecureHidFrame,
    pending: Map<number, PendingLogicalRequest>,
    traceFrameRecordId: string | null,
  ): boolean {
    this.rememberRxTraceFrame(frame.type, traceFrameRecordId);
    const complete = this.assembler(frame.type).push(frame);
    if (!complete) {
      return false;
    }
    const frameRecordIds = this.takeRxTraceFrames(frame.type);
    const response = parseJson<LogicalResponse>(complete, 'logical response');
    const request = pending.get(response.transactionId);
    const failed = !request || Boolean(response.errNo && response.errNo !== 0);
    traceWebHidLogical({
      direction: 'rx',
      type: frame.type,
      secure: frame.secure,
      plaintextPayload: complete,
      transactionId: response.transactionId,
      command: request?.command,
      decoded: response,
      status: failed ? 'failed' : 'success',
      errorCode: response.errNo ? String(response.errNo) : undefined,
      errorMessage: !request
        ? `Unknown HID transaction ${response.transactionId}`
        : response.errorMessage,
      frameRecordIds,
    });
    updateWebHidLogicalTrace(request?.traceRecordId ?? null, {
      status: failed ? 'failed' : 'success',
      responseDecoded: response,
      errorCode: response.errNo ? String(response.errNo) : undefined,
      errorMessage: response.errorMessage,
      responseFrameRecordIds: frameRecordIds,
    });
    if (request) request.traceOutcomeRecorded = true;
    if (!request) {
      if (
        pending === this.pendingBootstrap &&
        this.consumeIgnoredLateBootstrapResponse(response.transactionId)
      ) {
        return true;
      }
      if (
        pending === this.pendingRpc &&
        this.consumeIgnoredLateRpcResponse(response.transactionId)
      ) {
        return false;
      }
      throw new DeviceTransportError('protocol', `Unknown HID transaction ${response.transactionId}`);
    }
    if (
      request.settled ||
      request.generation !== this.connectionGeneration
    ) {
      return false;
    }
    request.settled = true;
    pending.delete(response.transactionId);
    if (response.errNo && response.errNo !== 0) {
      request.reject(new DeviceTransportError('protocol', response.errorMessage ?? `Device error ${response.errNo}`));
    } else {
      request.resolve(response.data);
    }
    return false;
  }

  private handleLogicalFrame(
    frame: SecureHidFrame,
    forcedEventName: string | null,
    traceFrameRecordId: string | null,
  ): void {
    this.rememberRxTraceFrame(frame.type, traceFrameRecordId);
    const complete = this.assembler(frame.type).push(frame);
    if (!complete) {
      return;
    }
    const frameRecordIds = this.takeRxTraceFrames(frame.type);
    const event = parseJson<{ command?: string; data?: unknown }>(complete, 'device event');
    const name = forcedEventName ?? event.command;
    traceWebHidLogical({
      direction: 'rx',
      type: frame.type,
      secure: frame.secure,
      plaintextPayload: complete,
      command: name,
      decoded: event,
      status: 'event',
      frameRecordIds,
    });
    if (!name) {
      throw new DeviceTransportError('protocol', 'Device event is missing a command name');
    }
    this.emit(name, event.data);
  }

  private handleLogicalError(frame: SecureHidFrame, traceFrameRecordId: string | null): void {
    this.rememberRxTraceFrame(frame.type, traceFrameRecordId);
    const complete = this.assembler(frame.type).push(frame);
    if (!complete) {
      return;
    }
    const frameRecordIds = this.takeRxTraceFrames(frame.type);
    const error = parseJson<{ message?: string }>(complete, 'device error');
    traceWebHidLogical({
      direction: 'rx',
      type: frame.type,
      secure: frame.secure,
      plaintextPayload: complete,
      command: 'error',
      decoded: error,
      status: 'failed',
      errorCode: 'device-error',
      errorMessage: error.message,
      frameRecordIds,
    });
    throw new DeviceTransportError('protocol', error.message ?? 'Device rejected HID request');
  }

  private async sendLogical(
    type: SecureHidFrameType,
    logicalPayload: Uint8Array,
    secure: boolean,
    logicalRecordId: string | null,
    onWriting?: () => void,
  ): Promise<void> {
    if (logicalPayload.byteLength > WEBHID_MAX_LOGICAL_MESSAGE_SIZE) {
      throw new DeviceTransportError('protocol', 'Logical WebHID message exceeds 16 KiB');
    }
    const fragments = fragmentPayload(logicalPayload);
    await this.enqueueWriteOperation(async (device, generation) => {
      onWriting?.();
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
          logicalRecordId,
        );
      }
    });
  }

  private async sendFrame(
    type: SecureHidFrameType,
    payload: Uint8Array,
    flags: number,
    secure: boolean,
    options: DeviceRequestOptions = {},
  ): Promise<void> {
    if (this.reauthorizationPending) {
      throw new DeviceTransportError(
        'authentication-required',
        '设备权限会话正在重新授权',
      );
    }
    this.requireOpenDevice();
    const device = this.device!;
    const generation = this.connectionGeneration;
    const operation = this.enqueueWriteOperation((activeDevice, activeGeneration) =>
      this.writeFrameNow(activeDevice, activeGeneration, type, payload, flags, secure));
    try {
      await this.awaitWithDeadline(
        operation,
        this.operationDeadline(options.timeoutMs),
        options.signal,
        'WebHID stream write',
      );
    } catch (error) {
      const normalized = asOperationError(error, 'WebHID stream write');
      const writeFailed = normalized.code === 'disconnected';
      if (normalized.code === 'timeout' || isAbortError(normalized) || writeFailed) {
        if (normalized.code === 'timeout' || isAbortError(normalized)) {
          this.quarantineDevice(device, 'write-timeout', true);
        }
        void this.shutdownConnection(normalized, generation, device, true).catch(() => undefined);
      }
      throw normalized;
    }
  }

  private async performLogicalRequest(
    collection: Map<number, PendingLogicalRequest>,
    transactionId: number,
    command: string,
    params: Record<string, unknown>,
    type: SecureHidFrameType,
    secure: boolean,
    options: DeviceRequestOptions,
  ): Promise<unknown> {
    this.requireOpenDevice();
    const generation = this.connectionGeneration;
    const device = this.device!;
    const deadline = this.operationDeadline(options.timeoutMs);
    const pending = this.createPendingRequest(
      collection,
      transactionId,
      command,
      generation,
    );
    const logicalRequest = { transactionId, command, params };
    const logicalPayload = textEncoder.encode(JSON.stringify(logicalRequest));
    pending.record.traceRecordId = traceWebHidLogical({
      direction: 'tx',
      type,
      secure,
      plaintextPayload: logicalPayload,
      transactionId,
      command,
      decoded: logicalRequest,
      status: 'pending',
    });
    const send = this.sendLogical(
      type,
      logicalPayload,
      secure,
      pending.record.traceRecordId,
      () => {
        if (!pending.record.settled) pending.record.phase = 'writing';
      },
    ).then(() => {
      if (!pending.record.settled) pending.record.phase = 'awaiting-response';
    });

    // Attach observers to both promises before the timer can fire. This is
    // important when Windows leaves sendReport() pending: a response timeout
    // must reject the public request without creating an unhandled rejection.
    const operation = Promise.all([send, pending.promise]).then(([, data]) => data);
    try {
      return await this.awaitWithDeadline(operation, deadline, options.signal, `命令 ${command}`);
    } catch (error) {
      const normalized = asOperationError(error, `命令 ${command}`);
      this.rejectPendingRequest(collection, transactionId, normalized);
      if (!pending.record.traceOutcomeRecorded) {
        updateWebHidLogicalTrace(pending.record.traceRecordId, {
          status: 'failed',
          errorCode: normalized.code,
          errorMessage: normalized.message,
        });
        pending.record.traceOutcomeRecorded = true;
      }

      const mayResynchronizeBootstrap =
        collection === this.pendingBootstrap &&
        type === SecureHidFrameType.BOOTSTRAP_REQUEST &&
        !secure &&
        RECOVERABLE_BOOTSTRAP_COMMANDS.has(command) &&
        pending.record.phase === 'awaiting-response' &&
        normalized.code === 'timeout';
      const mayKeepAuthenticatedSession =
        collection === this.pendingRpc &&
        type === SecureHidFrameType.RPC_REQUEST &&
        secure &&
        options.responseTimeoutMode === 'recoverable' &&
        pending.record.phase === 'awaiting-response' &&
        normalized.code === 'timeout';
      if (mayResynchronizeBootstrap) {
        this.rememberLateBootstrapResponse(transactionId, generation);
      }
      if (mayKeepAuthenticatedSession) {
        this.rememberLateRpcResponse(transactionId, generation);
      }
      const physicalWriteFailed =
        pending.record.phase !== 'awaiting-response' &&
        normalized.code === 'disconnected';
      if (
        !mayResynchronizeBootstrap &&
        !mayKeepAuthenticatedSession &&
        (normalized.code === 'timeout' || isAbortError(normalized) || physicalWriteFailed)
      ) {
        if (pending.record.phase !== 'awaiting-response') {
          this.quarantineDevice(device, 'write-timeout', true);
        }
        // A timed-out physical write cannot be cancelled by WebHID. Advance
        // the generation synchronously and close best-effort so its eventual
        // completion cannot mutate the next connection.
        void this.shutdownConnection(normalized, generation, device, true).catch(() => undefined);
      }
      throw mayResynchronizeBootstrap
        ? new RecoverableBootstrapResponseTimeoutError(
          command,
          normalized.message,
          generation,
          normalized,
        )
        : normalized;
    }
  }

  private operationDeadline(timeoutMs?: number): number {
    const duration = timeoutMs ?? this.requestTimeoutMs;
    if (!Number.isFinite(duration) || duration <= 0) {
      throw new DeviceTransportError('protocol', 'WebHID timeout must be a positive number');
    }
    return Date.now() + duration;
  }

  private remainingOperationTime(deadline: number, label: string): number {
    const remaining = deadline - Date.now();
    if (remaining <= 0) {
      throw new DeviceTransportError('timeout', `${label} 操作超时`);
    }
    return remaining;
  }

  private abortStreamBestEffort(transferId: number, deadline: number): void {
    const remaining = deadline - Date.now();
    if (remaining <= 0) return;
    // The caller's signal is already aborted. Do not await cleanup or pass the
    // aborted signal: either choice would delay the public AbortError. This RPC
    // is bounded independently and may never extend the upload deadline.
    void this.request(
      'stream.abort',
      { transferId },
      { timeoutMs: Math.min(1000, remaining) },
    ).catch(() => undefined);
  }

  private awaitWithDeadline<T>(
    operation: Promise<T>,
    deadline: number,
    signal: AbortSignal | undefined,
    label: string,
  ): Promise<T> {
    if (signal?.aborted) {
      return Promise.reject(abortedOperationError(label, signal));
    }
    const remaining = Math.max(0, deadline - Date.now());
    return new Promise<T>((resolve, reject) => {
      let settled = false;
      const finish = (callback: () => void): void => {
        if (settled) return;
        settled = true;
        clearTimeout(timeout);
        signal?.removeEventListener('abort', abort);
        callback();
      };
      const abort = (): void => finish(() => reject(abortedOperationError(label, signal)));
      const timeout = setTimeout(() => finish(() => reject(
        new DeviceTransportError('timeout', `${label} 操作超时`),
      )), remaining);
      signal?.addEventListener('abort', abort, { once: true });
      operation.then(
        (value) => finish(() => resolve(value)),
        (error) => finish(() => reject(error)),
      );
    });
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
    logicalRecordId: string | null = null,
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
    const pacingDelay = this.nextPhysicalWriteAtMs - Date.now();
    if (pacingDelay > 0) {
      await new Promise<void>((resolve) => setTimeout(resolve, pacingDelay));
      if (!this.isActiveConnection(generation, device)) {
        throw new DeviceTransportError(
          'disconnected',
          'WebHID device disconnected while waiting for endpoint capacity',
        );
      }
    }
    traceWebHidFrame({
      direction: 'tx',
      reportId: this.reportId,
      type,
      flags,
      sequence,
      secure,
      plaintextPayload: payload,
      wireReport: report,
      logicalRecordId,
    });
    const writeStartedAtMs = Date.now();
    try {
      await device.sendReport(this.reportId, report);
      this.nextPhysicalWriteAtMs = writeStartedAtMs + this.framePacingMs;
    } catch (error) {
      const nativeName = error instanceof DOMException && error.name
        ? ` (${error.name})`
        : '';
      const nativeMessage = error instanceof Error ? error.message : String(error);
      throw new DeviceTransportError(
        'disconnected',
        `WebHID report write failed${nativeName}: ${nativeMessage}`,
        error,
      );
    }
    if (!this.isActiveConnection(generation, device)) {
      throw new DeviceTransportError('disconnected', 'WebHID device disconnected after write');
    }
  }

  private createPendingRequest(
    collection: Map<number, PendingLogicalRequest>,
    transactionId: number,
    command: string,
    generation: number,
  ): { promise: Promise<unknown>; record: PendingLogicalRequest } {
    let record!: PendingLogicalRequest;
    const promise = new Promise<unknown>((resolve, reject) => {
      record = {
        command,
        generation,
        phase: 'queued',
        traceRecordId: null,
        traceOutcomeRecorded: false,
        settled: false,
        resolve,
        reject,
      };
      collection.set(transactionId, record);
    });
    return { promise, record };
  }

  private rejectPendingRequest(
    collection: Map<number, PendingLogicalRequest>,
    transactionId: number,
    error: Error,
  ): void {
    const pending = collection.get(transactionId);
    if (!pending || pending.settled) return;
    pending.settled = true;
    collection.delete(transactionId);
    pending.reject(error);
  }

  private assembler(type: SecureHidFrameType): FragmentAssembler {
    let assembler = this.assemblers.get(type);
    if (!assembler) {
      assembler = new FragmentAssembler(WEBHID_MAX_LOGICAL_MESSAGE_SIZE);
      this.assemblers.set(type, assembler);
    }
    return assembler;
  }

  private rememberRxTraceFrame(type: SecureHidFrameType, recordId: string | null): void {
    if (!recordId) return;
    const recordIds = this.pendingRxTraceFrameIds.get(type) ?? [];
    recordIds.push(recordId);
    this.pendingRxTraceFrameIds.set(type, recordIds);
  }

  private takeRxTraceFrames(type: SecureHidFrameType): string[] {
    const recordIds = this.pendingRxTraceFrameIds.get(type) ?? [];
    this.pendingRxTraceFrameIds.delete(type);
    return recordIds;
  }

  private invalidateLogicalStateForSequenceGap(error: DeviceTransportError): void {
    this.assemblers.forEach((assembler) => assembler.reset());
    this.assemblers.clear();
    this.pendingRxTraceFrameIds.clear();
    this.rejectAllPending(error);
  }

  private emit(name: string, data: unknown): void {
    const event = { name, data };
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

    // A navigator.hid discovery/chooser/open promise cannot be cancelled.
    // Mark it stale synchronously so a late result can never become the active
    // handle, while its physical ownership record keeps the page lease held
    // until the native promise and any required close actually settle.
    this.cancelPhysicalConnectAttempt(this.physicalConnectAttempt);

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
    this.drainingLateBootstrapResponse = false;
    this.reauthorizationPending = false;
    this.assemblers.clear();
    this.pendingRxTraceFrameIds.clear();
    this.readChain = Promise.resolve();
    this.writeChain = Promise.resolve();
    this.rejectAllPending(error);
    this.ignoredLateBootstrapResponses.clear();
    this.ignoredLateRpcResponses.clear();
    if (device) {
      device.removeEventListener('inputreport', this.handleInputReport);
    }
    // Keep observing navigator disconnect after a bounded close. A quarantined
    // native handle may only become reusable after its old close has settled
    // and a real USB disconnect edge has been observed.
    const closeOperations: Promise<void>[] = [];
    if (this.physicalCloseInFlight) {
      closeOperations.push(this.physicalCloseInFlight.catch(() => undefined));
    }
    if (device?.opened) {
      closeOperations.push(this.closePhysicalDeviceBounded(device));
    }
    const physicalClosing = Promise.all(closeOperations).then(() => undefined);
    const trackedClosing = physicalClosing.finally(() => {
      if (this.physicalCloseInFlight === trackedClosing) {
        this.physicalCloseInFlight = null;
      }
    });
    this.physicalCloseInFlight = trackedClosing;
    this.releaseConnectionLeaseWhenPhysicalSafe();

    if (reportError) {
      this.setState(DeviceTransportState.ERROR);
      this.errorHandlers.forEach((handler) => handler(error));
    } else {
      this.setState(DeviceTransportState.DISCONNECTED);
    }

    await trackedClosing;
    if (this.connectionGeneration === closedGeneration && this.device === null) {
      this.setState(DeviceTransportState.DISCONNECTED);
    }
  }

  private async waitForPhysicalClose(): Promise<void> {
    while (this.physicalCloseInFlight) {
      await this.physicalCloseInFlight.catch(() => undefined);
    }
  }

  private beginTransportConnect(): { generation: number; controller: AbortController } {
    this.transportConnectAbortController?.abort();
    this.cancelPhysicalConnectAttempt(this.physicalConnectAttempt);
    const controller = new AbortController();
    const generation = ++this.transportConnectGeneration;
    this.transportConnectAbortController = controller;
    return { generation, controller };
  }

  private cancelTransportConnect(): void {
    this.transportConnectGeneration += 1;
    this.transportConnectAbortController?.abort();
    this.transportConnectAbortController = null;
  }

  private assertTransportConnectActive(generation: number, signal: AbortSignal): void {
    if (generation !== this.transportConnectGeneration || signal.aborted) {
      throw new DeviceTransportError('disconnected', 'WebHID 连接已被取消');
    }
  }

  private async acquireConnectionLease(signal: AbortSignal): Promise<void> {
    const lease = this.connectionLease;
    if (!lease) return;
    const pendingRelease = this.connectionLeaseReleaseInFlight;
    if (pendingRelease) {
      await this.awaitWithDeadline(
        pendingRelease.catch(() => undefined),
        Date.now() + WEBHID_DEVICE_LOCK_TIMEOUT_MS,
        signal,
        'WebHID device lease release',
      ).catch((error) => {
        throw error instanceof DeviceTransportError && error.code === 'timeout'
          ? new DeviceTransportError(
            'device-busy',
            '此前的 WebHID 原生句柄尚未释放，请稍后重试或重新插拔设备',
            error,
          )
          : error;
      });
    }
    await lease.acquire(signal);
    this.connectionLeaseHeld = true;
    if (signal.aborted) {
      this.releaseConnectionLeaseWhenPhysicalSafe();
      throw new DeviceTransportError('disconnected', 'WebHID device lease acquisition was cancelled');
    }
  }

  private releaseConnectionLeaseWhenPhysicalSafe(): void {
    const lease = this.connectionLease;
    if (!lease || !this.connectionLeaseHeld || this.connectionLeaseReleaseInFlight) return;
    const releasing = this.waitForPhysicalRelease()
      .catch(() => undefined)
      .then(() => {
        if (!this.connectionLeaseHeld) return;
        this.connectionLeaseHeld = false;
        lease.release();
      })
      .finally(() => {
        if (this.connectionLeaseReleaseInFlight === releasing) {
          this.connectionLeaseReleaseInFlight = null;
        }
      });
    this.connectionLeaseReleaseInFlight = releasing;
  }

  private beginPhysicalConnectAttempt(hid: WebHidNavigator): PhysicalConnectAttempt {
    if (this.physicalConnectAttempt) {
      throw new DeviceTransportError(
        'device-busy',
        '已有 WebHID 连接操作尚未安全结束',
      );
    }
    const release = this.createPhysicalReleaseRecord();
    const attempt: PhysicalConnectAttempt = {
      id: this.nextPhysicalConnectAttemptId++,
      release,
      cancelled: false,
      established: false,
      selectedDevice: null,
      nativePending: null,
    };
    this.physicalConnectAttempt = attempt;
    this.latestPhysicalRelease = release.promise;
    if (!this.navigatorDisconnectListening) {
      hid.addEventListener('disconnect', this.handleNavigatorDisconnect);
      this.navigatorDisconnectListening = true;
    }
    return attempt;
  }

  private trackPhysicalConnectNative<T>(
    attempt: PhysicalConnectAttempt,
    operation: Promise<T>,
  ): Promise<T> {
    const native = Promise.resolve(operation);
    const settled = native.then(
      () => undefined,
      () => undefined,
    );
    attempt.nativePending = settled;
    void settled.then(() => {
      if (attempt.nativePending === settled) {
        attempt.nativePending = null;
      }
      this.finishCancelledPhysicalConnectAttempt(attempt);
    });
    return native;
  }

  private assertPhysicalConnectAttemptActive(attempt: PhysicalConnectAttempt): void {
    if (attempt.cancelled || this.physicalConnectAttempt !== attempt) {
      throw new DeviceTransportError(
        'disconnected',
        `WebHID 连接操作 ${attempt.id} 已被断开`,
      );
    }
  }

  private cancelPhysicalConnectAttempt(attempt: PhysicalConnectAttempt | null): void {
    if (!attempt || attempt.established || attempt.cancelled) return;
    attempt.cancelled = true;
    this.finishCancelledPhysicalConnectAttempt(attempt);
  }

  private finishCancelledPhysicalConnectAttempt(attempt: PhysicalConnectAttempt): void {
    if (!attempt.cancelled || attempt.established || attempt.nativePending) return;
    if (this.physicalConnectAttempt === attempt) {
      this.physicalConnectAttempt = null;
    }
    const device = attempt.selectedDevice;
    if (device?.opened) {
      // UI close remains bounded, but the attempt's release record resolves
      // only from the native close settlement (or physical disconnect).
      void this.closePhysicalDeviceBounded(device).catch(() => undefined);
      return;
    }
    this.settlePhysicalReleaseRecord(attempt.release);
  }

  private async closePhysicalDeviceBounded(device: WebHidDevice): Promise<void> {
    // Some Windows HID stacks never settle close() after a device-side session
    // reset. Observe the original promise so a late rejection is consumed, but
    // never let it hold the UI lifecycle barrier indefinitely.
    this.ensurePhysicalReleaseRecord(device);
    let physicalClose: Promise<boolean>;
    try {
      // Start HIDDevice.close() synchronously so a pagehide handler reaches
      // the native browser API before the old document is torn down.
      physicalClose = Promise.resolve(device.close()).then(
        () => true,
        () => false,
      );
    } catch {
      physicalClose = Promise.resolve(false);
    }
    await new Promise<void>((resolve) => {
      let settled = false;
      let timedOut = false;
      const finish = (): void => {
        if (settled) return;
        settled = true;
        clearTimeout(timeout);
        resolve();
      };
      const timeout = setTimeout(() => {
        timedOut = true;
        this.quarantineDevice(device, 'close-timeout', false);
        finish();
      }, this.closeTimeoutMs);
      physicalClose.then((closed) => {
        if (closed) {
          this.settlePhysicalRelease(device);
          this.notePhysicalCloseSettled(device);
        } else {
          // A rejected/throwing close does not prove that the OS released the
          // handle. Keep both quarantine and cross-document lease until a real
          // navigator.hid disconnect edge identifies this exact device.
          this.quarantineDevice(device, 'close-timeout', true);
          const quarantine = this.quarantinedDevices.get(device);
          if (quarantine) {
            // The native close operation is terminal (it rejected), so an
            // exact-device disconnect is now sufficient to clear quarantine;
            // there is no late successful close still able to race a reopen.
            quarantine.closeSettled = true;
          }
        }
        if (!timedOut) finish();
      });
    });
  }

  private quarantineDevice(
    device: WebHidDevice,
    reason: QuarantinedDevice['reason'],
    requiresDisconnect: boolean,
  ): void {
    const existing = this.quarantinedDevices.get(device);
    this.quarantinedDevices.set(device, {
      reason: existing?.reason === 'write-timeout' ? existing.reason : reason,
      closeSettled: existing?.closeSettled ?? false,
      disconnected: existing?.disconnected ?? false,
      requiresDisconnect: (existing?.requiresDisconnect ?? false) || requiresDisconnect,
    });
  }

  private notePhysicalCloseSettled(device: WebHidDevice): void {
    const quarantine = this.quarantinedDevices.get(device);
    if (!quarantine) return;
    quarantine.closeSettled = true;
    if (
      quarantine.disconnected ||
      (!quarantine.requiresDisconnect && !device.opened)
    ) {
      this.quarantinedDevices.delete(device);
    }
  }

  private notePhysicalDisconnect(device: WebHidDevice): void {
    // Ignore other granted HID devices. In particular, an unrelated tab/device
    // disconnect must not replace or resolve the ownership barrier for the
    // handle this transport is still opening or closing.
    const record = this.physicalReleaseRecords.get(device);
    if (!record) return;
    this.settlePhysicalRelease(device);
    const pending = this.physicalConnectAttempt;
    if (pending?.selectedDevice === device) {
      pending.cancelled = true;
      this.finishCancelledPhysicalConnectAttempt(pending);
    }
    const quarantine = this.quarantinedDevices.get(device);
    if (!quarantine) return;
    quarantine.disconnected = true;
    if (quarantine.closeSettled) {
      this.quarantinedDevices.delete(device);
    }
  }

  private ensurePhysicalReleaseRecord(device: WebHidDevice): PhysicalReleaseRecord {
    const existing = this.physicalReleaseRecords.get(device);
    if (existing) {
      this.latestPhysicalRelease = existing.promise;
      return existing;
    }
    const record = this.createPhysicalReleaseRecord();
    this.physicalReleaseRecords.set(device, record);
    this.latestPhysicalRelease = record.promise;
    return record;
  }

  private settlePhysicalRelease(device: WebHidDevice): void {
    const record = this.physicalReleaseRecords.get(device);
    if (!record) return;
    this.settlePhysicalReleaseRecord(record);
  }

  private createPhysicalReleaseRecord(): PhysicalReleaseRecord {
    let resolve!: () => void;
    return {
      promise: new Promise<void>((complete) => { resolve = complete; }),
      resolve: () => resolve(),
      settled: false,
    };
  }

  private settlePhysicalReleaseRecord(record: PhysicalReleaseRecord): void {
    if (record.settled) return;
    record.settled = true;
    record.resolve();
  }

  private quarantinedHandleError(): DeviceTransportError {
    return new DeviceTransportError(
      'disconnected',
      '此前的 WebHID 写入或关闭未安全结束；请拔下设备 USB 后重新插入再连接',
    );
  }

  private rememberLateBootstrapResponse(
    transactionId: number,
    generation: number,
  ): void {
    const now = Date.now();
    for (const [id, ignored] of this.ignoredLateBootstrapResponses) {
      if (ignored.expiresAt <= now) this.ignoredLateBootstrapResponses.delete(id);
    }
    while (this.ignoredLateBootstrapResponses.size >= 4) {
      const oldest = this.ignoredLateBootstrapResponses.keys().next().value as number | undefined;
      if (oldest === undefined) break;
      this.ignoredLateBootstrapResponses.delete(oldest);
    }
    this.ignoredLateBootstrapResponses.set(transactionId, {
      validGeneration: generation + 1,
      expiresAt: now + 30_000,
    });
  }

  private consumeIgnoredLateBootstrapResponse(transactionId: number): boolean {
    const ignored = this.ignoredLateBootstrapResponses.get(transactionId);
    if (!ignored) return false;
    this.ignoredLateBootstrapResponses.delete(transactionId);
    return ignored.validGeneration === this.connectionGeneration &&
      ignored.expiresAt > Date.now();
  }

  private rememberLateRpcResponse(transactionId: number, generation: number): void {
    const now = Date.now();
    this.pruneIgnoredLateRpcResponses(now);
    while (this.ignoredLateRpcResponses.size >= 8) {
      const oldest = this.ignoredLateRpcResponses.keys().next().value as number | undefined;
      if (oldest === undefined) break;
      this.ignoredLateRpcResponses.delete(oldest);
    }
    this.ignoredLateRpcResponses.set(transactionId, {
      generation,
      expiresAt: now + 30_000,
    });
  }

  private consumeIgnoredLateRpcResponse(transactionId: number): boolean {
    const ignored = this.ignoredLateRpcResponses.get(transactionId);
    if (!ignored) return false;
    this.ignoredLateRpcResponses.delete(transactionId);
    return ignored.generation === this.connectionGeneration &&
      ignored.expiresAt > Date.now();
  }

  private pruneIgnoredLateRpcResponses(now = Date.now()): void {
    for (const [transactionId, ignored] of this.ignoredLateRpcResponses) {
      if (ignored.expiresAt <= now || ignored.generation !== this.connectionGeneration) {
        this.ignoredLateRpcResponses.delete(transactionId);
      }
    }
  }

  private hasIgnoredLateBootstrapResponseForCurrentGeneration(): boolean {
    const now = Date.now();
    let found = false;
    for (const [transactionId, ignored] of this.ignoredLateBootstrapResponses) {
      if (ignored.expiresAt <= now) {
        this.ignoredLateBootstrapResponses.delete(transactionId);
      } else if (ignored.validGeneration === this.connectionGeneration) {
        found = true;
      }
    }
    return found;
  }

  private failConnect(error: unknown): never {
    const normalized = asTransportError(error);
    this.handleError(normalized);
    throw normalized;
  }

  private rejectAllPending(error: Error): void {
    for (const collection of [this.pendingRpc, this.pendingBootstrap]) {
      collection.forEach((pending) => {
        if (!pending.settled) {
          pending.settled = true;
          pending.reject(error);
        }
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

class OperationAbortedError extends DeviceTransportError {
  constructor(label: string, cause?: unknown) {
    super('disconnected', `${label} 已取消`, cause);
    this.name = 'OperationAbortedError';
  }
}

function abortedOperationError(
  label: string,
  signal?: AbortSignal,
): OperationAbortedError {
  return new OperationAbortedError(label, signal?.reason);
}

function isAbortError(error: unknown): error is OperationAbortedError {
  return error instanceof OperationAbortedError;
}

function asOperationError(error: unknown, label: string): DeviceTransportError {
  if (error instanceof DeviceTransportError) return error;
  if (error instanceof DOMException && error.name === 'AbortError') {
    return new OperationAbortedError(label, error);
  }
  return asTransportError(error);
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
