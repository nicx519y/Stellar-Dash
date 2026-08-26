import { DeviceRequestQueue } from './device-request-queue';
import {
  DeviceCommandMessage,
  DeviceConnectionError,
  DeviceConnectionPhase,
} from './device-command-types';
import {
  DeviceFirmwareChunkRequest,
  DeviceFirmwareChunkResult,
  DeviceImageCatalog,
  DeviceImageMutationResult,
  DeviceImageTarget,
  DeviceImageUploadRequest,
} from './device-feature-types';
import { DeviceAuthClient } from './device-auth-client';
import {
  DEFAULT_DEVICE_SCOPES,
  DeviceScope,
  DeviceTransport,
  DeviceTransportError,
  DeviceRequestOptions,
  DeviceTransportState,
} from './types';
import {
  FIRMWARE_BINARY_HEADER_SIZE,
  RecoverableBootstrapResponseTimeoutError,
  WEBHID_FIRMWARE_CHUNK_DATA_SIZE,
  WEBHID_MAX_FIRMWARE_PACKET_SIZE,
  WEBHID_MAX_STREAM_SIZE,
  WebHidTransport,
} from './webhid-transport';
import {
  binaryOpcodeScope,
  elevatedScopesForCommand,
} from './scope-policy';
import { exportWebHidConfigSections } from './webhid-config-export';

if (WEBHID_MAX_FIRMWARE_PACKET_SIZE > WEBHID_MAX_STREAM_SIZE) {
  throw new Error('WebHID firmware packet exceeds the device stream boundary');
}

type MessageHandler = (message: DeviceCommandMessage) => void;
type ScopeUpgradeOperation = {
  generation: number;
  controller: AbortController;
  promise: Promise<void>;
};

const MAX_BOOTSTRAP_RESYNCHRONIZATIONS = 2;
const HBOX_CONFIG_BACKUP_FORMAT = 'hbox-webconfig-backup';
const HBOX_CONFIG_BACKUP_VERSION = 2;

/** HID-native command/session facade used by React and typed feature clients. */
export class DeviceCommandClient {
  private state = DeviceTransportState.DISCONNECTED;
  private phase = DeviceConnectionPhase.IDLE;
  private readonly queue = new DeviceRequestQueue();
  private readonly messageHandlers = new Set<MessageHandler>();
  private readonly stateHandlers = new Set<(state: DeviceTransportState) => void>();
  private readonly phaseHandlers = new Set<(phase: DeviceConnectionPhase) => void>();
  private readonly errorHandlers = new Set<(error: DeviceConnectionError) => void>();
  private readonly disconnectHandlers = new Set<() => void>();
  private readonly unsubscribe: Array<() => void> = [];
  private readonly imageTransferTotals = new Map<number, number>();
  private scopeUpgrade: ScopeUpgradeOperation | null = null;
  private closeInFlight: Promise<void> | null = null;
  private connectInFlight: Promise<void> | null = null;
  private connectAbortController: AbortController | null = null;
  private sessionAbortController: AbortController | null = null;
  private httpAbortController: AbortController | null = null;
  private startupTimer: ReturnType<typeof setTimeout> | null = null;
  private startupDeadlineMs: number | null = null;
  private startupGeneration = 0;
  private startupStageDetail: string = DeviceConnectionPhase.IDLE;
  private lastDiagnosticPhase = DeviceConnectionPhase.IDLE;
  private activeTransportOperations = 0;
  private readonly transportIdleWaiters = new Set<() => void>();
  private internalClosePending = false;
  private externalDisconnectInvalidated = false;
  private lifecycleGeneration = 0;
  private disposed = false;

  constructor(
    readonly transport: DeviceTransport,
    private readonly authClient: DeviceAuthClient | null = null,
    private readonly initialScopes: readonly DeviceScope[] = DEFAULT_DEVICE_SCOPES,
    private readonly startupTimeoutMs = 30_000,
  ) {
    if (!Number.isFinite(startupTimeoutMs) || startupTimeoutMs <= 0) {
      throw new DeviceTransportError('protocol', '设备启动超时必须是正数');
    }
    this.queue.setSendFunction((command, params, options) => this.request(command, params, options));
    this.unsubscribe.push(
      transport.onStateChange((state) => this.handleTransportState(state)),
      transport.onError((error) => this.handleTransportError(error)),
      transport.onDisconnect(() => {
        this.invalidateExternalDisconnect();
        this.authClient?.clear();
        this.disconnectHandlers.forEach((handler) => handler());
      }),
      transport.subscribe('*', (event) => {
        const message: DeviceCommandMessage = {
          command: event.name,
          errNo: 0,
          data: asRecord(event.data),
        };
        this.messageHandlers.forEach((handler) => handler(message));
      }),
    );
  }

  async connect(requestPermission = false): Promise<void> {
    this.assertNotDisposed();
    const invocationGeneration = this.lifecycleGeneration;
    if (this.getState() === DeviceTransportState.CONNECTED) {
      return;
    }
    while (this.connectInFlight) {
      await this.connectInFlight.catch(() => undefined);
      if (this.getState() === DeviceTransportState.CONNECTED) {
        return;
      }
    }
    if (this.closeInFlight || this.scopeUpgrade) {
      await this.waitForLifecycleBarriers();
    }
    if (this.getState() === DeviceTransportState.CONNECTED) {
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
    this.beginStartupDeadline(generation);
    const authController = new AbortController();
    this.connectAbortController = authController;
    const operation = (async () => {
      this.setPhase(requestPermission
        ? DeviceConnectionPhase.DISCOVERING
        : DeviceConnectionPhase.OPENING);
      this.setState(DeviceTransportState.CONNECTING);
      try {
        this.assertLifecycleActive(generation, authController.signal);
        const openTransport = async (askPermission: boolean): Promise<void> => {
          if (askPermission) {
            await this.transport.requestPermissionAndConnect();
          } else {
            await this.transport.connect();
          }
          this.setPhase(DeviceConnectionPhase.ATTESTING);
          this.assertLifecycleActive(generation, authController.signal);
          // A prior physical disconnect has now fully settled and a fresh USB
          // handle belongs to this generation. Future disconnect events must
          // invalidate it independently.
          this.externalDisconnectInvalidated = false;
        };

        await openTransport(requestPermission);
        if (this.transport instanceof WebHidTransport) {
          if (!this.authClient) {
            throw new DeviceTransportError('authentication-failed', 'WebHID authentication client is not configured');
          }
          let authenticationAttempt = 0;
          for (;;) {
            authenticationAttempt += 1;
            try {
              await this.authClient.authenticate(
                this.transport,
                this.initialScopes,
                authController.signal,
                () => {
                  this.assertLifecycleActive(generation, authController.signal);
                  this.setPhase(DeviceConnectionPhase.AUTHORIZING);
                },
              );
              if (authenticationAttempt > 1) {
                this.logBootstrapRecovery(
                  'recovered',
                  authenticationAttempt,
                  generation,
                );
              }
              break;
            } catch (error) {
              if (
                !this.isRecoverableBootstrapResync(error) ||
                authenticationAttempt > MAX_BOOTSTRAP_RESYNCHRONIZATIONS
              ) {
                if (this.isRecoverableBootstrapResync(error)) {
                  this.logBootstrapRecovery(
                    'exhausted',
                    authenticationAttempt,
                    generation,
                    error,
                  );
                }
                throw error;
              }

              /*
               * A browser can disappear without giving STM32 a close message.
               * A bootstrap response may be lost after STM32 has already acted:
               * either stale-session attestation reset the old session, or the
               * permit was accepted before its cleartext ACK disappeared. Reset
               * our logical HID generation and repeat the complete authenticate
               * flow on the same already-open handle. Two bounded resyncs cover
               * consecutive lost ACKs while the original 30-second startup
               * AbortSignal remains authoritative. In particular, do not wait
               * for HIDDevice.close(): Windows can leave that promise pending
               * after a device-side channel reset. Secure RPCs, physical-write
               * timeouts and server failures are never retried here.
               */
              this.logBootstrapRecovery(
                'retrying',
                authenticationAttempt,
                generation,
                error,
              );
              this.authClient.clear();
              this.transport.resynchronizeBootstrap();
              this.assertLifecycleActive(generation, authController.signal);
              this.setState(DeviceTransportState.CONNECTING);
              this.setPhase(DeviceConnectionPhase.ATTESTING);
            }
          }
        }
        this.assertLifecycleActive(generation, authController.signal);
        this.setPhase(DeviceConnectionPhase.AUTHORIZING);
        this.replaceSessionAbortControllers();
        this.setState(DeviceTransportState.CONNECTED);
        this.setPhase(DeviceConnectionPhase.INITIALIZING);
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

  private isRecoverableBootstrapResync(error: unknown): boolean {
    return error instanceof RecoverableBootstrapResponseTimeoutError;
  }

  private logBootstrapRecovery(
    event: 'retrying' | 'recovered' | 'exhausted',
    attempt: number,
    generation: number,
    error?: unknown,
  ): void {
    const remainingStartupMs = this.startupDeadlineMs === null
      ? null
      : Math.max(0, this.startupDeadlineMs - Date.now());
    const normalized = error instanceof DeviceTransportError ? error : null;
    const bootstrapTimeout = error instanceof RecoverableBootstrapResponseTimeoutError
      ? error
      : null;
    console.info('[HBox WebHID bootstrap]', {
      event,
      attempt,
      maximumAttempts: MAX_BOOTSTRAP_RESYNCHRONIZATIONS + 1,
      lifecycleGeneration: generation,
      bootstrapGeneration: bootstrapTimeout?.connectionGeneration,
      command: bootstrapTimeout?.command,
      remainingStartupMs,
      errorCode: normalized?.code,
      errorMessage: normalized?.message,
    });
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
    const preserveErrorPhase = this.phase === DeviceConnectionPhase.ERROR;
    // Invoke close in this stack frame. During pagehide the browser may stop
    // servicing microtasks immediately after the handler returns, so deferring
    // the native HID close by even one Promise turn can lose the handoff.
    let closeStarted: Promise<void>;
    try {
      closeStarted = Promise.resolve(this.transport.close());
    } catch {
      closeStarted = Promise.resolve();
    }
    const closing = closeStarted
      .catch(() => undefined)
      .finally(() => {
        this.internalClosePending = false;
        if (generation === this.lifecycleGeneration) {
          this.setState(DeviceTransportState.DISCONNECTED);
          if (!preserveErrorPhase) {
            this.setPhase(DeviceConnectionPhase.IDLE);
          }
        }
        if (this.closeInFlight === closing) {
          this.closeInFlight = null;
        }
      });
    this.closeInFlight = closing;
    return closing;
  }

  disconnect(): void {
    this.clearStartupDeadline();
    const generation = ++this.lifecycleGeneration;
    this.setPhase(DeviceConnectionPhase.CLOSING);
    this.connectAbortController?.abort();
    this.abortSessionRequests();
    this.abortScopeUpgrade();
    this.queue.clear();
    this.imageTransferTotals.clear();
    this.authClient?.clear();
    this.setState(DeviceTransportState.DISCONNECTED);
    void this.beginTransportClose(generation);
  }

  dispose(): void {
    if (this.disposed) return;
    this.disposed = true;
    this.disconnect();
    this.unsubscribe.splice(0).forEach((unsubscribe) => unsubscribe());
  }

  async request(
    command: string,
    params: Record<string, unknown> = {},
    options: DeviceRequestOptions = {},
  ): Promise<Record<string, unknown> | undefined> {
    this.assertNotDisposed();
    const generation = this.lifecycleGeneration;
    const activeUpgrade = this.scopeUpgrade?.generation === generation;
    if (this.state !== DeviceTransportState.CONNECTED && !activeUpgrade) {
      throw new DeviceTransportError('not-connected', '设备未连接或尚未完成认证');
    }
    const requiredScopes = elevatedScopesForCommand(command);
    if (requiredScopes.length > 0) {
      await this.ensureScopes(requiredScopes, generation);
    }
    return this.runAfterScopeUpgrade(generation, async () => {
      const response = await this.transport.request(command, params, {
        signal: combineAbortSignals(
          options.signal,
          this.sessionAbortController?.signal,
        ),
        timeoutMs: options.timeoutMs,
      });
      this.assertLifecycleActive(generation);
      return response.data;
    });
  }

  async uploadFirmwareChunk(
    request: DeviceFirmwareChunkRequest,
    options: DeviceRequestOptions = {},
  ): Promise<DeviceFirmwareChunkResult> {
    const frame = encodeFirmwareChunkRequest(request);
    const response = await this.exchangeFeatureFrame(frame, options);
    return parseFirmwareChunkResult(response, request.chunkIndex);
  }

  async getImageCatalog(
    options: DeviceRequestOptions = {},
  ): Promise<DeviceImageCatalog> {
    const cid = nextCorrelationId();
    const frame = new Uint8Array(6);
    const view = new DataView(frame.buffer);
    view.setUint8(0, 0x34);
    view.setUint32(2, cid, true);
    const response = await this.exchangeFeatureFrame(frame, options);
    return parseImageCatalog(response, cid);
  }

  async readImage(
    target: DeviceImageTarget,
    totalSize: number,
    options: DeviceRequestOptions = {},
  ): Promise<Uint8Array> {
    const normalizedTotal = checkedUnsignedInteger(totalSize, 0xffff_ffff, 'Image size');
    const targetCode = imageTargetCode(target);
    const result = new Uint8Array(normalizedTotal);
    const cid = nextCorrelationId();
    const chunkSize = 4096;
    for (let offset = 0; offset < normalizedTotal; offset += chunkSize) {
      const wanted = Math.min(chunkSize, normalizedTotal - offset);
      const frame = new Uint8Array(14);
      const view = new DataView(frame.buffer);
      view.setUint8(0, 0x35);
      view.setUint8(1, targetCode);
      view.setUint32(2, cid, true);
      view.setUint32(6, offset, true);
      view.setUint16(10, wanted, true);
      const response = await this.exchangeFeatureFrame(frame, options);
      const bytes = parseImageReadResponse(
        response,
        targetCode,
        cid,
        offset,
        wanted,
        normalizedTotal,
      );
      result.set(bytes, offset);
    }
    return result;
  }

  async uploadImage(request: DeviceImageUploadRequest): Promise<DeviceImageMutationResult> {
    const width = checkedUnsignedInteger(request.width, 0xffff, 'Image width');
    const height = checkedUnsignedInteger(request.height, 0xffff, 'Image height');
    const total = checkedUnsignedInteger(request.data.byteLength, 0xffff_ffff, 'Image size');
    const frameCount = checkedUnsignedInteger(request.frameCount, 10, 'Image frame count');
    const fps = checkedUnsignedInteger(request.fps, 5, 'Image FPS');
    if (frameCount < 1) {
      throw new DeviceTransportError('protocol', 'Image frame count must be at least one');
    }
    const options: DeviceRequestOptions = {
      signal: request.signal,
      timeoutMs: request.timeoutMs,
    };
    const cid = nextCorrelationId();
    this.imageTransferTotals.set(cid, total);
    try {
      const begin = new Uint8Array(18);
      const beginView = new DataView(begin.buffer);
      beginView.setUint8(0, 0x30);
      beginView.setUint8(1, frameCount > 1 ? 1 : 0);
      beginView.setUint32(2, cid, true);
      beginView.setUint16(6, width, true);
      beginView.setUint16(8, height, true);
      beginView.setUint32(10, total, true);
      beginView.setUint8(14, frameCount);
      beginView.setUint8(15, fps);
      const beginStatus = parseImageMutationResult(
        await this.exchangeFeatureFrame(begin, options),
        0xb0,
        cid,
        'begin',
      );
      if (!beginStatus.success) return beginStatus;
      if (beginStatus.received !== 0 || beginStatus.total !== total) {
        throw new DeviceTransportError('protocol', 'Image begin ACK does not match the upload');
      }

      const chunkSize = 4096;
      for (let offset = 0; offset < total; offset += chunkSize) {
        const part = request.data.subarray(offset, Math.min(total, offset + chunkSize));
        const frame = new Uint8Array(14 + part.byteLength);
        const view = new DataView(frame.buffer);
        view.setUint8(0, 0x31);
        view.setUint32(2, cid, true);
        view.setUint32(6, offset, true);
        view.setUint16(10, part.byteLength, true);
        frame.set(part, 14);
        const status = parseImageMutationResult(
          await this.exchangeFeatureFrame(frame, options),
          0xb1,
          cid,
          'chunk',
        );
        if (!status.success) return status;
        const expectedReceived = offset + part.byteLength;
        if (status.received !== expectedReceived || status.total !== total) {
          throw new DeviceTransportError(
            'protocol',
            `Image chunk ACK does not match the uploaded range: expected ${expectedReceived}/${total}, received ${status.received}/${status.total}`,
          );
        }
        request.onProgress?.(status.received, status.total);
      }

      const commit = new Uint8Array(6);
      const commitView = new DataView(commit.buffer);
      commitView.setUint8(0, 0x32);
      commitView.setUint32(2, cid, true);
      const commitStatus = parseImageMutationResult(
        await this.exchangeFeatureFrame(commit, options),
        0xb2,
        cid,
        'commit',
      );
      if (
        commitStatus.success &&
        (commitStatus.received !== total || commitStatus.total !== total)
      ) {
        throw new DeviceTransportError('protocol', 'Image commit ACK does not match the upload');
      }
      return commitStatus;
    } finally {
      // The total is only stream-correlation state. Never retain it after a
      // rejected BEGIN/CHUNK, an exception, an abort, or a completed COMMIT.
      this.imageTransferTotals.delete(cid);
    }
  }

  async deleteImage(options: DeviceRequestOptions = {}): Promise<DeviceImageMutationResult> {
    const cid = nextCorrelationId();
    const frame = new Uint8Array(6);
    const view = new DataView(frame.buffer);
    view.setUint8(0, 0x33);
    view.setUint32(2, cid, true);
    return parseImageMutationResult(
      await this.exchangeFeatureFrame(frame, options),
      0xb3,
      cid,
      'delete',
    );
  }

  private async exchangeFeatureFrame(
    data: ArrayBuffer | Uint8Array,
    options: DeviceRequestOptions = {},
  ): Promise<ArrayBuffer> {
    const generation = this.lifecycleGeneration;
    const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
    return this.sendWebHidBinary(bytes, generation, options);
  }

  private async sendWebHidBinary(
    bytes: Uint8Array,
    generation: number,
    options: DeviceRequestOptions = {},
  ): Promise<ArrayBuffer> {
    const activeUpgrade = this.scopeUpgrade?.generation === generation;
    if (this.state !== DeviceTransportState.CONNECTED && !activeUpgrade) {
      throw new DeviceTransportError('not-connected', '设备未连接或尚未完成认证');
    }
    const command = bytes[0];
    const requiredScope = binaryOpcodeScope(command);
    await this.ensureScopes([requiredScope], generation);
    return this.runAfterScopeUpgrade(generation, async () => {
      if (command === 0x01 && bytes.byteLength >= FIRMWARE_BINARY_HEADER_SIZE) {
        if (bytes.byteLength > WEBHID_MAX_FIRMWARE_PACKET_SIZE) {
          throw new DeviceTransportError(
            'protocol',
            `WebHID firmware data chunks are limited to ${WEBHID_FIRMWARE_CHUNK_DATA_SIZE} bytes`,
          );
        }
        const completed = await this.transport.upload('firmware', bytes, {
          signal: combineAbortSignals(options.signal, this.sessionAbortController?.signal),
          timeoutMs: options.timeoutMs,
        });
        this.assertLifecycleActive(generation);
        return validateFirmwareStreamAck(bytes, completed);
      }
      if (command === 0x31 && bytes.byteLength > 14) {
        const completed = await this.transport.upload('image', bytes, {
          signal: combineAbortSignals(options.signal, this.sessionAbortController?.signal),
          timeoutMs: options.timeoutMs,
        });
        this.assertLifecycleActive(generation);
        return validateImageStreamAck(bytes, completed, this.imageTransferTotals);
      }

      const response = await this.transport.request<{ data: string }>('binary.exchange', {
        encoding: 'base64',
        data: bytesToBase64(bytes),
      }, {
        signal: combineAbortSignals(options.signal, this.sessionAbortController?.signal),
        timeoutMs: options.timeoutMs,
      });
      this.assertLifecycleActive(generation);
      if (!response.data?.data) {
        throw new DeviceTransportError('protocol', 'binary.exchange response is missing data');
      }
      const binary = base64ToBytes(response.data.data).buffer;
      return binary;
    });
  }

  async exportConfig(): Promise<Record<string, unknown>> {
    const result: Record<string, unknown> = {
      backupFormat: HBOX_CONFIG_BACKUP_FORMAT,
      backupVersion: HBOX_CONFIG_BACKUP_VERSION,
      globalConfig: {},
      hotkeysConfig: [],
      screenControl: {},
      profiles: [],
    };
    await exportWebHidConfigSections(
      (command, params = {}) => this.request(command, params),
      (section) => {
        if (section.section === 'global') result.globalConfig = section.data;
        if (section.section === 'hotkeys') result.hotkeysConfig = section.data;
        if (section.section === 'screenControl') result.screenControl = section.data;
        if (section.section === 'profile') {
          (result.profiles as unknown[]).push(section.data);
        }
      },
    );

    const adcResponse = await this.request('get_adc_config_backup');
    const adcConfig = asRecord(adcResponse?.adcConfig);
    if (!adcConfig) {
      throw new DeviceTransportError(
        'protocol',
        'get_adc_config_backup response is malformed',
      );
    }
    result.adcConfig = adcConfig;
    result.userImage = await this.exportUserImageBackup();
    return result;
  }

  async importConfig(input: unknown): Promise<void> {
    const backup = validateConfigBackup(input);
    const hasUserImage = Object.prototype.hasOwnProperty.call(backup, 'userImage');
    const previousImage = hasUserImage ? await this.exportUserImageBackup() : null;
    let transactionActive = false;
    let imageTouched = false;

    try {
      await this.request('import_config_begin', {
        strict: backup.backupVersion === HBOX_CONFIG_BACKUP_VERSION,
        replaceProfiles: backup.backupVersion === HBOX_CONFIG_BACKUP_VERSION,
      });
      transactionActive = true;

      await this.request('import_config_part', {
        section: 'global',
        data: backup.globalConfig,
      });
      await this.request('import_config_part', {
        section: 'hotkeys',
        data: backup.hotkeysConfig,
      });
      await this.request('import_config_part', {
        section: 'screenControl',
        data: backup.screenControl,
      });
      for (const profile of backup.profiles) {
        await this.request('import_config_part', {
          section: 'profile',
          data: profile,
        });
      }
      if (backup.adcConfig) {
        await this.request('import_config_part', {
          section: 'adcConfig',
          data: backup.adcConfig,
        });
      }

      if (hasUserImage) {
        imageTouched = true;
        await this.restoreUserImageBackup(backup.userImage);
      }

      await this.request('import_config_finish');
      transactionActive = false;
    } catch (error) {
      if (transactionActive) {
        await this.request('import_config_abort').catch(() => undefined);
      }
      if (imageTouched) {
        try {
          await this.restoreUserImageBackup(previousImage);
        } catch (rollbackError) {
          throw new DeviceTransportError(
            'protocol',
            `Configuration import failed and the previous user image could not be restored: ${formatError(rollbackError)}`,
            error,
          );
        }
      }
      throw error;
    }
  }

  private async exportUserImageBackup(): Promise<Record<string, unknown> | null> {
    const catalog = await this.getImageCatalog();
    if (!catalog.user.valid) return null;
    const data = await this.readImage('user', catalog.user.size);
    return {
      width: catalog.user.width,
      height: catalog.user.height,
      size: catalog.user.size,
      frameCount: catalog.user.frameCount,
      fps: catalog.user.fps,
      format: catalog.user.format,
      encoding: 'base64',
      data: bytesToBase64(data),
    };
  }

  private async restoreUserImageBackup(value: unknown): Promise<void> {
    if (value === null) {
      const deleted = await this.deleteImage();
      if (!deleted.success) {
        throw new DeviceTransportError(
          'protocol',
          deleted.error || 'Device rejected user image deletion',
        );
      }
      return;
    }
    const image = asRecord(value);
    if (!image || image.encoding !== 'base64' || typeof image.data !== 'string') {
      throw new DeviceTransportError('protocol', 'User image backup is malformed');
    }
    const data = base64ToBytes(image.data);
    const width = checkedUnsignedInteger(Number(image.width), 0xffff, 'Image width');
    const height = checkedUnsignedInteger(Number(image.height), 0xffff, 'Image height');
    const size = checkedUnsignedInteger(Number(image.size), 0xffff_ffff, 'Image size');
    const frameCount = checkedUnsignedInteger(Number(image.frameCount), 10, 'Image frame count');
    const fps = checkedUnsignedInteger(Number(image.fps), 5, 'Image FPS');
    if (size !== data.byteLength || size === 0 || frameCount < 1) {
      throw new DeviceTransportError('protocol', 'User image backup length is invalid');
    }
    const uploaded = await this.uploadImage({ width, height, data, frameCount, fps });
    if (!uploaded.success) {
      throw new DeviceTransportError(
        'protocol',
        uploaded.error || 'Device rejected user image restore',
      );
    }
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

  getState(): DeviceTransportState {
    return this.state;
  }

  getPhase(): DeviceConnectionPhase {
    return this.phase;
  }

  markReady(): boolean {
    if (this.state !== DeviceTransportState.CONNECTED) return false;
    if (this.startupDeadlineMs !== null && Date.now() >= this.startupDeadlineMs) {
      this.expireStartup(this.startupGeneration);
      return false;
    }
    this.clearStartupDeadline(this.lifecycleGeneration);
    this.setPhase(DeviceConnectionPhase.READY);
    return true;
  }

  getStartupDeadlineMs(): number | null {
    return this.startupDeadlineMs;
  }

  setInitializationStage(stage: string): void {
    if (
      this.phase === DeviceConnectionPhase.INITIALIZING &&
      this.startupDeadlineMs !== null
    ) {
      this.startupStageDetail = `${DeviceConnectionPhase.INITIALIZING}/${stage}`;
    }
  }

  async authorizedFetch(
    input: RequestInfo | URL,
    init?: RequestInit,
    requiredScopes: readonly DeviceScope[] = [],
  ): Promise<Response> {
    const generation = this.lifecycleGeneration;
    const activeUpgrade = this.scopeUpgrade?.generation === generation;
    if (this.state !== DeviceTransportState.CONNECTED && !activeUpgrade) {
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
          this.httpAbortController?.signal,
        ),
      };
      let response: Response;
      if (this.transport.authorizedFetch) {
        response = await this.transport.authorizedFetch(input, requestInit);
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
    }, false);
  }

  onMessage(handler: MessageHandler): () => void {
    this.messageHandlers.add(handler);
    return () => this.messageHandlers.delete(handler);
  }

  onStateChange(handler: (state: DeviceTransportState) => void): () => void {
    this.stateHandlers.add(handler);
    return () => this.stateHandlers.delete(handler);
  }

  onPhaseChange(handler: (phase: DeviceConnectionPhase) => void): () => void {
    this.phaseHandlers.add(handler);
    return () => this.phaseHandlers.delete(handler);
  }

  onError(handler: (error: DeviceConnectionError) => void): () => void {
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
        this.setState(DeviceTransportState.DISCONNECTED);
        break;
      case DeviceTransportState.ERROR:
        this.invalidateExternalDisconnect(DeviceTransportState.ERROR);
        this.authClient?.clear();
        this.setState(DeviceTransportState.ERROR);
        this.setPhase(DeviceConnectionPhase.ERROR);
        break;
      case DeviceTransportState.CONNECTING:
      case DeviceTransportState.AUTHENTICATING:
        // A scope upgrade deliberately replaces the secure session while the
        // physical HID handle remains open.  Keep the framework logically
        // connected so React initialization and monitoring effects do not
        // interpret the transient AUTHENTICATING state as a disconnect and
        // abort the very reauthorization they are waiting for.
        if (this.scopeUpgrade) {
          break;
        }
        this.setState(state);
        break;
      case DeviceTransportState.CONNECTED:
        // WebHID only reaches CONNECTED after permit installation.
        this.setState(DeviceTransportState.CONNECTED);
        break;
    }
  }

  private invalidateExternalDisconnect(
    terminalState = DeviceTransportState.DISCONNECTED,
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
    this.clearStartupDeadline();
    this.lifecycleGeneration += 1;
    this.connectAbortController?.abort();
    this.abortSessionRequests();
    this.abortScopeUpgrade();
    this.queue.clear();
    this.imageTransferTotals.clear();
    this.authClient?.clear();
    this.setState(terminalState);
    void this.beginTransportClose(this.lifecycleGeneration);
  }

  private handleTransportError(error: DeviceTransportError): void {
    this.authClient?.clear();
    const mapped: DeviceConnectionError = {
      type:
        error.code === 'timeout'
          ? 'timeout'
          : error.code === 'protocol'
            ? 'protocol'
            : 'connection',
      message: error.message,
      transportCode: error.code,
      phase: this.lastDiagnosticPhase,
      timestamp: new Date(),
    };
    this.setState(DeviceTransportState.ERROR);
    this.setPhase(DeviceConnectionPhase.ERROR);
    this.errorHandlers.forEach((handler) => handler(mapped));
  }

  private failAndClose(
    error: unknown,
    expectedGeneration = this.lifecycleGeneration,
  ): Promise<void> {
    if (expectedGeneration !== this.lifecycleGeneration) {
      return Promise.resolve();
    }
    this.clearStartupDeadline(expectedGeneration);
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
    if (this.state !== DeviceTransportState.ERROR) {
      this.handleTransportError(normalized);
    }

    return this.beginTransportClose(closingGeneration);
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
        // Publish the upgrade before reauthorize() can synchronously drive the
        // transport into AUTHENTICATING.  handleTransportState() then knows
        // this is an in-session scope change, not a physical reconnect.
        this.scopeUpgrade = upgrade;
        // HTTP bodies use a separate controller, so invalidating an old bearer
        // token cannot abort an in-flight HID sendReport() and tear down the
        // physical transport. Publish the barrier first, then drain active HID
        // RPCs before ending the encrypted device session.
        this.abortHttpRequests();
        upgrade.promise = (async () => {
          await this.waitForActiveTransportOperations(generation, controller.signal);
          await this.authClient!.reauthorize(
            this.transport as WebHidTransport,
            targetScopes,
            controller.signal,
          );
          this.assertLifecycleActive(generation, controller.signal);
          this.replaceSessionAbortControllers();
        })()
          .then(() => {
            this.assertLifecycleActive(generation, controller.signal);
          })
          .finally(() => {
            if (this.scopeUpgrade === upgrade) {
              this.scopeUpgrade = null;
            }
          });
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
    if (this.disposed || generation !== this.lifecycleGeneration || signal?.aborted) {
      throw new DeviceTransportError(
        'disconnected',
        '操作已被后续的断开或重连会话替代',
      );
    }
  }

  private assertNotDisposed(): void {
    if (this.disposed) {
      throw new DeviceTransportError(
        'disconnected',
        '设备命令客户端已释放，拒绝重新打开 HID 句柄',
      );
    }
  }

  private async runAfterScopeUpgrade<T>(
    generation: number,
    operation: () => Promise<T>,
    trackPhysicalOperation = true,
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
    if (this.state !== DeviceTransportState.CONNECTED) {
      throw new DeviceTransportError('not-connected', '设备未连接或尚未完成认证');
    }
    // Increment synchronously at the no-upgrade boundary. A later scope
    // request publishes its barrier only after this operation is visible and
    // therefore cannot send session.end until the operation settles.
    if (!trackPhysicalOperation) return operation();
    this.activeTransportOperations += 1;
    try {
      return await operation();
    } finally {
      this.activeTransportOperations -= 1;
      if (this.activeTransportOperations === 0) {
        const waiters = [...this.transportIdleWaiters];
        this.transportIdleWaiters.clear();
        waiters.forEach((resolve) => resolve());
      }
    }
  }

  private abortSessionRequests(): void {
    this.sessionAbortController?.abort();
    this.sessionAbortController = null;
    this.abortHttpRequests();
  }

  private abortHttpRequests(): void {
    this.httpAbortController?.abort();
    this.httpAbortController = null;
  }

  private replaceSessionAbortControllers(): void {
    this.abortSessionRequests();
    this.sessionAbortController = new AbortController();
    this.httpAbortController = new AbortController();
  }

  private setState(state: DeviceTransportState): void {
    if (state !== this.state) {
      this.state = state;
      this.stateHandlers.forEach((handler) => handler(state));
    }
  }

  private setPhase(phase: DeviceConnectionPhase): void {
    if (
      phase !== DeviceConnectionPhase.ERROR &&
      phase !== DeviceConnectionPhase.CLOSING &&
      phase !== DeviceConnectionPhase.IDLE
    ) {
      this.lastDiagnosticPhase = phase;
      if (phase !== DeviceConnectionPhase.READY) {
        this.startupStageDetail = phase;
      }
    }
    if (phase !== this.phase) {
      this.phase = phase;
      this.phaseHandlers.forEach((handler) => handler(phase));
    }
  }

  private waitForActiveTransportOperations(
    generation: number,
    signal: AbortSignal,
  ): Promise<void> {
    this.assertLifecycleActive(generation, signal);
    if (this.activeTransportOperations === 0) return Promise.resolve();
    return new Promise<void>((resolve, reject) => {
      let settled = false;
      const finish = (callback: () => void): void => {
        if (settled) return;
        settled = true;
        signal.removeEventListener('abort', onAbort);
        this.transportIdleWaiters.delete(onIdle);
        callback();
      };
      const onIdle = (): void => finish(resolve);
      const onAbort = (): void => finish(() => reject(new DeviceTransportError(
        'disconnected',
        '权限升级在等待现有设备请求完成时被取消',
        signal.reason,
      )));
      this.transportIdleWaiters.add(onIdle);
      signal.addEventListener('abort', onAbort, { once: true });
      if (this.activeTransportOperations === 0) onIdle();
    });
  }

  private beginStartupDeadline(generation: number): void {
    this.clearStartupDeadline();
    this.startupGeneration = generation;
    this.startupDeadlineMs = Date.now() + this.startupTimeoutMs;
    this.startupStageDetail = DeviceConnectionPhase.OPENING;
    this.startupTimer = setTimeout(
      () => this.expireStartup(generation),
      this.startupTimeoutMs,
    );
  }

  private clearStartupDeadline(expectedGeneration?: number): void {
    if (
      expectedGeneration !== undefined &&
      this.startupGeneration !== expectedGeneration
    ) {
      return;
    }
    if (this.startupTimer !== null) clearTimeout(this.startupTimer);
    this.startupTimer = null;
    this.startupDeadlineMs = null;
  }

  private expireStartup(generation: number): void {
    if (
      generation !== this.lifecycleGeneration ||
      this.startupGeneration !== generation ||
      this.phase === DeviceConnectionPhase.READY
    ) {
      return;
    }
    const detail = this.startupStageDetail;
    const error = new DeviceTransportError(
      'timeout',
      `设备连接启动在 ${detail} 阶段超过 ${this.startupTimeoutMs}ms`,
    );
    void this.failAndClose(error, generation);
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

type ValidatedConfigBackup = {
  backupVersion: number;
  globalConfig: Record<string, unknown>;
  hotkeysConfig: unknown[];
  screenControl: Record<string, unknown>;
  profiles: Record<string, unknown>[];
  adcConfig?: Record<string, unknown>;
  userImage?: unknown;
};

function validateConfigBackup(value: unknown): ValidatedConfigBackup {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    throw new DeviceTransportError('protocol', 'Configuration backup must be a JSON object');
  }
  const source = value as Record<string, unknown>;
  if (
    source.backupFormat !== undefined &&
    source.backupFormat !== HBOX_CONFIG_BACKUP_FORMAT
  ) {
    throw new DeviceTransportError('protocol', 'Unsupported configuration backup format');
  }
  const version = source.backupVersion === undefined ? 1 : Number(source.backupVersion);
  if (!Number.isInteger(version) || version < 1 || version > HBOX_CONFIG_BACKUP_VERSION) {
    throw new DeviceTransportError('protocol', 'Unsupported configuration backup version');
  }

  const globalConfig = source.globalConfig;
  const hotkeysConfig = source.hotkeysConfig;
  const screenControl = source.screenControl;
  const profiles = source.profiles;
  if (!globalConfig || typeof globalConfig !== 'object' || Array.isArray(globalConfig)) {
    throw new DeviceTransportError('protocol', 'Configuration backup is missing globalConfig');
  }
  if (!Array.isArray(hotkeysConfig)) {
    throw new DeviceTransportError('protocol', 'Configuration backup is missing hotkeysConfig');
  }
  if (!screenControl || typeof screenControl !== 'object' || Array.isArray(screenControl)) {
    throw new DeviceTransportError('protocol', 'Configuration backup is missing screenControl');
  }
  if (!Array.isArray(profiles) || profiles.length === 0 || profiles.length > 16) {
    throw new DeviceTransportError('protocol', 'Configuration backup has an invalid profile list');
  }

  const normalizedProfiles: Record<string, unknown>[] = [];
  const profileIds = new Set<string>();
  for (const profile of profiles) {
    if (!profile || typeof profile !== 'object' || Array.isArray(profile)) {
      throw new DeviceTransportError('protocol', 'Configuration backup contains an invalid profile');
    }
    const item = profile as Record<string, unknown>;
    if (typeof item.id !== 'string' || item.id.length === 0 || profileIds.has(item.id)) {
      throw new DeviceTransportError('protocol', 'Configuration backup contains an invalid profile ID');
    }
    profileIds.add(item.id);
    normalizedProfiles.push(item);
  }

  const adcConfig = source.adcConfig;
  const hasUserImage = Object.prototype.hasOwnProperty.call(source, 'userImage');
  if (version === HBOX_CONFIG_BACKUP_VERSION) {
    const defaultProfileId = (globalConfig as Record<string, unknown>).defaultProfileId;
    if (typeof defaultProfileId !== 'string' || !profileIds.has(defaultProfileId)) {
      throw new DeviceTransportError(
        'protocol',
        'Configuration backup default profile is missing or disabled',
      );
    }
    if (!adcConfig || typeof adcConfig !== 'object' || Array.isArray(adcConfig)) {
      throw new DeviceTransportError('protocol', 'Configuration backup is missing ADC data');
    }
    if (!hasUserImage) {
      throw new DeviceTransportError('protocol', 'Configuration backup is missing user image state');
    }
  }

  return {
    backupVersion: version,
    globalConfig: globalConfig as Record<string, unknown>,
    hotkeysConfig,
    screenControl: screenControl as Record<string, unknown>,
    profiles: normalizedProfiles,
    ...(adcConfig && typeof adcConfig === 'object' && !Array.isArray(adcConfig)
      ? { adcConfig: adcConfig as Record<string, unknown> }
      : {}),
    ...(hasUserImage ? { userImage: source.userImage } : {}),
  };
}

function formatError(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function asDeviceTransportError(error: unknown): DeviceTransportError {
  return error instanceof DeviceTransportError
    ? error
    : new DeviceTransportError('protocol', String(error), error);
}

function encodeFirmwareChunkRequest(request: DeviceFirmwareChunkRequest): Uint8Array {
  const sessionId = new TextEncoder().encode(request.sessionId);
  const componentName = new TextEncoder().encode(request.componentName);
  if (sessionId.byteLength === 0 || sessionId.byteLength > 31) {
    throw new DeviceTransportError('protocol', 'Firmware session ID must contain 1 to 31 UTF-8 bytes');
  }
  if (componentName.byteLength === 0 || componentName.byteLength > 15) {
    throw new DeviceTransportError('protocol', 'Firmware component name must contain 1 to 15 UTF-8 bytes');
  }
  if (!/^[0-9a-f]{64}$/i.test(request.checksumSha256)) {
    throw new DeviceTransportError('protocol', 'Firmware chunk SHA-256 must contain 64 hex characters');
  }
  const chunkIndex = checkedUnsignedInteger(request.chunkIndex, 0xffff_ffff, 'Firmware chunk index');
  const totalChunks = checkedUnsignedInteger(request.totalChunks, 0xffff_ffff, 'Firmware total chunks');
  const chunkOffset = checkedUnsignedInteger(request.chunkOffset, 0xffff_ffff, 'Firmware chunk offset');
  const targetAddress = checkedUnsignedInteger(request.targetAddress, 0xffff_ffff, 'Firmware target address');
  if (totalChunks === 0 || chunkIndex >= totalChunks) {
    throw new DeviceTransportError('protocol', 'Firmware chunk index is outside the transfer range');
  }
  if (request.data.byteLength > WEBHID_FIRMWARE_CHUNK_DATA_SIZE) {
    throw new DeviceTransportError(
      'protocol',
      `WebHID firmware data chunks are limited to ${WEBHID_FIRMWARE_CHUNK_DATA_SIZE} bytes`,
    );
  }

  const frame = new Uint8Array(FIRMWARE_BINARY_HEADER_SIZE + request.data.byteLength);
  const view = new DataView(frame.buffer);
  frame[0] = 0x01;
  view.setUint16(2, sessionId.byteLength, true);
  frame.set(sessionId, 4);
  view.setUint16(36, componentName.byteLength, true);
  frame.set(componentName, 38);
  view.setUint32(54, chunkIndex, true);
  view.setUint32(58, totalChunks, true);
  view.setUint32(62, request.data.byteLength, true);
  view.setUint32(66, chunkOffset, true);
  view.setUint32(70, targetAddress, true);
  for (let index = 0; index < 32; index += 1) {
    frame[74 + index] = Number.parseInt(request.checksumSha256.slice(index * 2, index * 2 + 2), 16);
  }
  frame.set(request.data, FIRMWARE_BINARY_HEADER_SIZE);
  return frame;
}

function parseFirmwareChunkResult(
  response: ArrayBuffer,
  expectedChunkIndex: number,
): DeviceFirmwareChunkResult {
  const view = new DataView(response);
  if (
    view.byteLength !== 75 ||
    view.getUint8(0) !== 0x81 ||
    view.getUint8(1) > 1 ||
    view.getUint8(10) > 64
  ) {
    throw new DeviceTransportError('protocol', 'Firmware chunk returned an invalid ACK');
  }
  const chunkIndex = view.getUint32(2, true);
  if (chunkIndex !== expectedChunkIndex) {
    throw new DeviceTransportError(
      'protocol',
      `Firmware ACK mismatch: expected ${expectedChunkIndex}, received ${chunkIndex}`,
    );
  }
  const success = view.getUint8(1) === 1;
  const progress = view.getUint32(6, true);
  if (progress > 100) {
    throw new DeviceTransportError('protocol', `Firmware ACK has invalid progress ${progress}`);
  }
  const errorLength = view.getUint8(10);
  if (success && errorLength !== 0) {
    throw new DeviceTransportError('protocol', 'Successful firmware ACK contains an error');
  }
  const error = !success && errorLength > 0
    ? new TextDecoder().decode(new Uint8Array(response, 11, errorLength))
    : null;
  return { success, chunkIndex, progress, error };
}

function parseImageCatalog(response: ArrayBuffer, expectedCid: number): DeviceImageCatalog {
  const view = new DataView(response);
  if (
    view.byteLength !== 64 ||
    view.getUint8(0) !== 0xb4 ||
    view.getUint8(1) > 1 ||
    view.getUint8(6) > 1 ||
    view.getUint8(7) > 1
  ) {
    throw new DeviceTransportError('protocol', 'Image catalog returned an invalid response');
  }
  if (view.getUint32(2, true) !== expectedCid) {
    throw new DeviceTransportError('protocol', 'Image catalog response CID mismatch');
  }
  if (view.getUint8(1) !== 1) {
    throw new DeviceTransportError('protocol', 'Image catalog request was rejected');
  }
  return {
    user: {
      valid: view.getUint8(6) === 1,
      width: view.getUint16(8, true),
      height: view.getUint16(10, true),
      size: view.getUint32(12, true),
      frameCount: view.getUint8(16),
      fps: view.getUint8(17),
      format: view.getUint8(18),
    },
    system: {
      valid: view.getUint8(7) === 1,
      width: view.getUint16(20, true),
      height: view.getUint16(22, true),
      size: view.getUint32(24, true),
      frameCount: view.getUint8(28),
      fps: view.getUint8(29),
      format: view.getUint8(30),
    },
  };
}

function parseImageReadResponse(
  response: ArrayBuffer,
  expectedTarget: number,
  expectedCid: number,
  expectedOffset: number,
  expectedLength: number,
  expectedTotal: number,
): Uint8Array {
  const view = new DataView(response);
  if (
    view.byteLength < 55 ||
    view.getUint8(0) !== 0xb5 ||
    view.getUint8(1) > 1 ||
    view.getUint8(22) > 32 ||
    view.getUint8(2) !== expectedTarget ||
    view.getUint32(4, true) !== expectedCid ||
    view.getUint32(16, true) !== expectedOffset
  ) {
    throw new DeviceTransportError('protocol', 'Image chunk response does not match the request');
  }
  const success = view.getUint8(1) === 1;
  const length = view.getUint16(20, true);
  const errorLength = view.getUint8(22);
  if (
    view.byteLength !== 55 + length ||
    (success && errorLength !== 0) ||
    (!success && length !== 0)
  ) {
    throw new DeviceTransportError('protocol', 'Image chunk response has an invalid length');
  }
  if (!success) {
    const error = errorLength > 0
      ? new TextDecoder().decode(new Uint8Array(response, 23, errorLength))
      : 'Read failed';
    throw new DeviceTransportError('protocol', error);
  }
  // BinaryReadBgImageChunkResponseHeader is packed as:
  // cid@4, width@8, height@10, total@12, offset@16, chunk_size@20.
  const total = view.getUint32(12, true);
  if (
    total !== expectedTotal ||
    length !== expectedLength ||
    view.byteLength !== 55 + length
  ) {
    throw new DeviceTransportError('protocol', 'Image chunk response has an invalid length');
  }
  return new Uint8Array(response.slice(55, 55 + length));
}

function parseImageMutationResult(
  response: ArrayBuffer,
  expectedOpcode: number,
  expectedCid: number,
  stage: string,
): DeviceImageMutationResult {
  const view = new DataView(response);
  if (
    view.byteLength !== 79 ||
    view.getUint8(0) !== expectedOpcode ||
    view.getUint8(1) > 1 ||
    view.getUint32(2, true) !== expectedCid ||
    view.getUint8(14) > 64
  ) {
    throw new DeviceTransportError('protocol', `Image ${stage} returned an unrelated ACK`);
  }
  const success = view.getUint8(1) === 1;
  const received = view.getUint32(6, true);
  const total = view.getUint32(10, true);
  const errorLength = view.getUint8(14);
  if (received > total || (success && errorLength !== 0)) {
    throw new DeviceTransportError('protocol', `Image ${stage} returned an invalid ACK`);
  }
  const error = !success && errorLength > 0
    ? new TextDecoder().decode(new Uint8Array(response, 15, errorLength))
    : null;
  return { success, received, total, error };
}

function imageTargetCode(target: DeviceImageTarget): number {
  return target === 'user' ? 0 : 1;
}

function checkedUnsignedInteger(value: number, maximum: number, label: string): number {
  if (!Number.isSafeInteger(value) || value < 0 || value > maximum) {
    throw new DeviceTransportError('protocol', `${label} is outside the supported range`);
  }
  return value;
}

function nextCorrelationId(): number {
  const crypto = globalThis.crypto;
  if (crypto?.getRandomValues) {
    const value = new Uint32Array(1);
    crypto.getRandomValues(value);
    return value[0];
  }
  return Math.floor(Math.random() * 0x1_0000_0000) >>> 0;
}

function bytesToBase64(bytes: Uint8Array): string {
  const parts: string[] = [];
  const chunkSize = 0x8000;
  for (let offset = 0; offset < bytes.length; offset += chunkSize) {
    const part = bytes.subarray(offset, Math.min(offset + chunkSize, bytes.length));
    parts.push(String.fromCharCode(...part));
  }
  return btoa(parts.join(''));
}

function base64ToBytes(value: string): Uint8Array {
  try {
    const binary = atob(value);
    return Uint8Array.from(binary, (character) => character.charCodeAt(0));
  } catch (error) {
    throw new DeviceTransportError('protocol', 'binary.exchange returned invalid Base64', error);
  }
}

function validateFirmwareStreamAck(
  request: Uint8Array,
  completed: Awaited<ReturnType<DeviceTransport['upload']>>,
): ArrayBuffer {
  const response = decodeStreamCompletion(completed, 'firmware.chunk');
  if (request.byteLength < FIRMWARE_BINARY_HEADER_SIZE) {
    throw new DeviceTransportError('protocol', 'Firmware stream returned an invalid ACK size');
  }
  const source = new DataView(request.buffer, request.byteOffset, request.byteLength);
  const chunkIndex = source.getUint32(54, true);
  const raw = exactArrayBuffer(response);
  const result = parseFirmwareChunkResult(raw, chunkIndex);
  const ack = completed.ack!;
  if (
    ack.requestOpcode !== 0x01 || ack.opcode !== 0x81 ||
    typeof ack.success !== 'boolean' || ack.success !== result.success ||
    ack.kind !== 'firmware.chunk' ||
    ack.chunkIndex !== chunkIndex || ack.progress !== result.progress
  ) {
    throw new DeviceTransportError('protocol', 'Firmware stream ACK does not match the uploaded chunk');
  }
  return raw;
}

function validateImageStreamAck(
  request: Uint8Array,
  completed: Awaited<ReturnType<DeviceTransport['upload']>>,
  totals: ReadonlyMap<number, number>,
): ArrayBuffer {
  const response = decodeStreamCompletion(completed, 'image.chunk');
  if (request.byteLength < 14) {
    throw new DeviceTransportError('protocol', 'Image stream returned an invalid ACK size');
  }
  const source = new DataView(request.buffer, request.byteOffset, request.byteLength);
  const cid = source.getUint32(2, true);
  const offset = source.getUint32(6, true);
  const length = source.getUint16(10, true);
  const received = offset + length;
  const expectedTotal = totals.get(cid);
  const raw = exactArrayBuffer(response);
  const result = parseImageMutationResult(raw, 0xb1, cid, 'chunk');
  const ack = completed.ack!;
  if (
    ack.requestOpcode !== 0x31 || ack.opcode !== 0xb1 ||
    typeof ack.success !== 'boolean' || ack.success !== result.success ||
    ack.kind !== 'image.chunk' ||
    ack.cid !== cid || ack.offset !== offset || ack.chunkSize !== length ||
    ack.received !== result.received || ack.total !== result.total ||
    (result.success && (
      result.received !== received ||
      (expectedTotal !== undefined && result.total !== expectedTotal)
    ))
  ) {
    throw new DeviceTransportError('protocol', 'Image stream ACK does not match the uploaded chunk');
  }
  return raw;
}

function decodeStreamCompletion(
  completed: Awaited<ReturnType<DeviceTransport['upload']>>,
  expectedKind: string,
): Uint8Array {
  if (
    completed.complete !== true || completed.encoding !== 'base64' ||
    typeof completed.data !== 'string' || !completed.ack ||
    completed.ack.kind !== expectedKind
  ) {
    throw new DeviceTransportError('protocol', 'Stream completion is missing its correlated binary ACK');
  }
  return base64ToBytes(completed.data);
}

function exactArrayBuffer(bytes: Uint8Array): ArrayBuffer {
  return bytes.buffer.slice(
    bytes.byteOffset,
    bytes.byteOffset + bytes.byteLength,
  ) as ArrayBuffer;
}
