import {
  AroundLedsEffectStyle,
  ConnectionMode,
  DEFAULT_NUM_HOTKEYS_MAX,
  DEFAULT_SCREEN_CONTROL_CONFIG,
  GameControllerButton,
  GameProfile,
  GameSocdMode,
  Hotkey,
  HotkeyAction,
  LedsEffectStyle,
  MAX_MACRO_STEPS,
  MAX_NUM_MACROS,
  MacroConfig,
  Platform,
  ScreenControlConfig,
  WirelessReportRate,
} from '../../types/gamepad-config';
import type { ADCValuesMapping, StepInfo } from '../../types/adc';
import type { CalibrationStatus, FirmwareMetadata } from '../../types/types';
import {
  DeviceEvent,
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

type EventHandler = (event: DeviceEvent<unknown>) => void;

const ALL_SCOPES = [
  'config.read',
  'config.write',
  'monitor.read',
  'device.control',
  'asset.write',
  'firmware.update',
] as const;

const MOCK_STATE_VERSION = 3;
const DEFAULT_STORAGE_KEY = `hbox.webconfig.mock-state.v${MOCK_STATE_VERSION}`;

export interface MockStorage {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
}

export interface MockDeviceTransportOptions {
  /**
   * Undefined uses the current tab's sessionStorage when available. Null
   * explicitly disables persistence (useful for isolated tests).
   */
  storage?: MockStorage | null;
  storageKey?: string;
}

interface MockImage {
  width: number;
  height: number;
  frameCount: number;
  fps: number;
  format: number;
  data: Uint8Array;
}

interface MockImageTransfer extends MockImage {
  received: number;
}

interface PersistedImage extends Omit<MockImage, 'data'> {
  data: number[];
}

interface PersistedMockState {
  version: typeof MOCK_STATE_VERSION;
  globalConfig: typeof DEFAULT_GLOBAL_CONFIG;
  screenControl: ScreenControlConfig;
  hotkeys: Hotkey[];
  profiles: GameProfile[];
  defaultProfileId: string;
  mappings: ADCValuesMapping[];
  defaultMappingId: string;
  calibrationComplete: boolean;
  images: {
    user: PersistedImage | null;
    system: PersistedImage | null;
  };
}

const HITBOX_LAYOUT = [
  { x: 125.10, y: 103.10, r: 26 }, { x: 147.34, y: 120.10, r: 34 },
  { x: 175.10, y: 119.10, r: 26 }, { x: 192.80, y: 101.44, r: 26 },
  { x: 73.49, y: 63.76, r: 26 }, { x: 99.05, y: 59.67, r: 26 },
  { x: 122.19, y: 63.76, r: 26 }, { x: 141.50, y: 77.34, r: 26 },
  { x: 131.19, y: 42.04, r: 26 }, { x: 165.45, y: 87.10, r: 26 },
  { x: 163.37, y: 62.80, r: 26 }, { x: 161.29, y: 38.50, r: 26 },
  { x: 185.51, y: 73.05, r: 26 }, { x: 183.43, y: 48.75, r: 26 },
  { x: 209.01, y: 66.10, r: 26 }, { x: 206.93, y: 41.80, r: 26 },
  { x: 233.44, y: 67.98, r: 26 }, { x: 231.36, y: 43.69, r: 26 },
  { x: 84.49, y: 15.49, r: 11.5 }, { x: 62.49, y: 15.49, r: 11.5 },
  { x: 40.49, y: 15.49, r: 11.5 }, { x: 18.48, y: 15.49, r: 11.5 },
] as const;

const DEFAULT_GLOBAL_CONFIG = {
  inputMode: Platform.XINPUT,
  defaultProfileId: 'profile-arcade',
  connectionMode: ConnectionMode.USB,
  connectionModeReadOnly: true,
  connectionModeSource: 'PHYSICAL_SWITCH' as const,
  physicalConnectionMode: ConnectionMode.USB,
  wirelessReportRate: WirelessReportRate.RATE_8K,
  power: { wakeHoldMs: 3000, autoStandbyMs: 300000 },
  hardware: {
    hardwareVersion: '2.0.0',
    batteryTopology: 'SINGLE_1S2P' as const,
    batteryPackCount: 1,
    keyLedCount: 22,
    ambientLedCount: 40,
  },
  ch585: {
    role: 'MAINTENANCE' as const,
    firmwareVersion: '2.0.0-mock',
    capabilitiesValid: true,
  },
  autoCalibrationEnabled: false,
  manualCalibrationActive: false,
};

const DEFAULT_FIRMWARE: FirmwareMetadata = {
  version: '2.0.0-mock',
  currentSlot: 'A',
  targetSlot: 'B',
  buildDate: '2026-07-26T00:00:00.000Z',
  components: [
    {
      name: 'application',
      file: 'application.bin',
      active: true,
      address: '0x90000000',
      size: 786432,
      sha256: 'mock-application-sha256',
    },
    {
      name: 'webresources',
      file: 'ex_fsdata.bin',
      active: true,
      address: '0x90100000',
      size: 1048576,
      sha256: 'mock-webresources-sha256',
    },
    {
      name: 'adc_mapping',
      file: 'adc_mapping.bin',
      active: true,
      address: '0x90280000',
      size: 65536,
      sha256: 'mock-adc-mapping-sha256',
    },
  ],
};

const DEFAULT_MAPPING: ADCValuesMapping = {
  id: 'mapping-default',
  name: 'Factory Hall Curve',
  length: 40,
  step: 100,
  samplingFrequency: 8000,
  samplingNoise: 3,
  originalValues: Array.from({ length: 40 }, (_, index) => 4000 - Math.round(index * (3200 / 39))),
  calibratedValues: Array.from({ length: 40 }, (_, index) => index * 100),
};

/**
 * Stateful, hardware-free HBox V2 device. This transport is only selected
 * when both NEXT_PUBLIC_DEVICE_TRANSPORT=mock and OFFLINE_PREVIEW=true.
 */
export class MockDeviceTransport implements DeviceTransport {
  readonly kind = 'mock' as const;
  state = DeviceTransportState.DISCONNECTED;
  session: DeviceSession | null = null;

  private transactionId = 0;
  private profiles = [makeProfile('profile-arcade', 'Arcade', false), makeProfile('profile-tournament', 'Tournament', true)];
  private defaultProfileId = this.profiles[0].id;
  private globalConfig = clone(DEFAULT_GLOBAL_CONFIG);
  private screenControl: ScreenControlConfig = {
    ...clone(DEFAULT_SCREEN_CONTROL_CONFIG),
    brightness: 72,
    standbyDisplay: 'buttonLayout',
  };
  private hotkeys: Hotkey[] = [
    { key: 20, action: HotkeyAction.WebConfigMode, isHold: true, isLocked: true },
    { key: 19, action: HotkeyAction.CalibrationMode, isHold: true, isLocked: true },
    { key: 15, action: HotkeyAction.LedsEffectStyleNext, isHold: false, isLocked: false },
    { key: 16, action: HotkeyAction.LedsEffectStylePrev, isHold: false, isLocked: false },
    { key: 14, action: HotkeyAction.LedsBrightnessUp, isHold: false, isLocked: false },
    { key: 13, action: HotkeyAction.LedsBrightnessDown, isHold: false, isLocked: false },
    { key: 11, action: HotkeyAction.AmbientLightEffectStyleNext, isHold: false, isLocked: false },
    { key: 12, action: HotkeyAction.AmbientLightEffectStylePrev, isHold: false, isLocked: false },
    { key: 10, action: HotkeyAction.AmbientLightBrightnessUp, isHold: false, isLocked: false },
    { key: 9, action: HotkeyAction.AmbientLightBrightnessDown, isHold: false, isLocked: false },
    { key: 2, action: HotkeyAction.LedsEnableSwitch, isHold: true, isLocked: false },
  ];
  private mappings = [clone(DEFAULT_MAPPING)];
  private defaultMappingId = DEFAULT_MAPPING.id;
  private markingStatus: StepInfo = emptyMarkingStatus();
  private calibrationActive = false;
  private calibrationStartedAt = 0;
  private calibrationComplete = true;
  private calibrationStatus = makeCalibrationStatus('completed');
  private eventHandlers = new Map<string, Set<EventHandler>>();
  private stateHandlers = new Set<(state: DeviceTransportState) => void>();
  private errorHandlers = new Set<(error: DeviceTransportError) => void>();
  private disconnectHandlers = new Set<() => void>();
  private performanceTimer: ReturnType<typeof setInterval> | null = null;
  private calibrationTimers: Array<ReturnType<typeof setTimeout>> = [];
  private sampleCounter = 0;
  private imageTransfers = new Map<number, MockImageTransfer>();
  private images: { user: MockImage | null; system: MockImage | null } = {
    user: null,
    system: makeSystemImage(),
  };
  private importStaging: Array<{ section: string; data: unknown }> | null = null;
  private importReplaceProfiles = false;
  private importStrict = false;
  private firmwareSessions = new Set<string>();
  private readonly storage: MockStorage | null;
  private readonly storageKey: string;

  constructor(options: MockDeviceTransportOptions = {}) {
    this.storage = options.storage === undefined
      ? getBrowserSessionStorage()
      : options.storage;
    this.storageKey = options.storageKey ?? DEFAULT_STORAGE_KEY;
    this.restoreState();
  }

  async connect(): Promise<DeviceSession> {
    if (this.session && this.state === DeviceTransportState.CONNECTED) {
      return clone(this.session);
    }
    this.setState(DeviceTransportState.CONNECTING);
    await Promise.resolve();
    this.session = {
      transport: 'mock',
      deviceId: 'HBOX-V2-MOCK-0001',
      productName: 'HBox V2 Mock Device',
      hardwareVersion: '2.0.0',
      firmwareVersion: DEFAULT_FIRMWARE.version,
      authenticated: true,
      scopes: ALL_SCOPES,
      sessionId: 'mock-session',
    };
    this.setState(DeviceTransportState.CONNECTED);
    return clone(this.session);
  }

  requestPermissionAndConnect(): Promise<DeviceSession> {
    return this.connect();
  }

  async request<T = Record<string, unknown> | undefined>(
    command: string,
    params: Record<string, unknown> = {},
  ): Promise<DeviceResponse<T>> {
    this.assertConnected();
    // Match the JSON RPC wire semantics used by the real transports. In
    // particular, nested `undefined` fields are omitted rather than overwriting
    // existing values in the device fixture.
    const data = await this.handleRequest(command, clone(params));
    return {
      data: clone(data) as T,
      transactionId: ++this.transactionId,
    };
  }

  subscribe<T = unknown>(
    event: string,
    handler: (event: DeviceEvent<T>) => void,
  ): Unsubscribe {
    const handlers = this.eventHandlers.get(event) ?? new Set<EventHandler>();
    handlers.add(handler as EventHandler);
    this.eventHandlers.set(event, handlers);
    return () => handlers.delete(handler as EventHandler);
  }

  async upload(
    stream: DeviceStream,
    data: Blob | ArrayBuffer | Uint8Array,
    options?: DeviceUploadOptions,
  ): Promise<DeviceUploadResult> {
    this.assertConnected();
    const bytes = data instanceof Blob
      ? new Uint8Array(await data.arrayBuffer())
      : data instanceof Uint8Array
        ? data
        : new Uint8Array(data);
    options?.onProgress?.(bytes.byteLength, bytes.byteLength);
    if (stream === 'firmware' || stream === 'image') {
      const response = new Uint8Array(this.handleBinaryExchange(bytes));
      const request = new DataView(
        bytes.buffer,
        bytes.byteOffset,
        bytes.byteLength,
      );
      const ack = stream === 'firmware'
        ? {
            requestOpcode: 0x01,
            opcode: response[0],
            success: response[1] === 1,
            kind: 'firmware.chunk',
            chunkIndex: new DataView(response.buffer).getUint32(2, true),
            progress: new DataView(response.buffer).getUint32(6, true),
          }
        : {
            requestOpcode: 0x31,
            opcode: response[0],
            success: response[1] === 1,
            kind: 'image.chunk',
            cid: new DataView(response.buffer).getUint32(2, true),
            offset: request.getUint32(6, true),
            chunkSize: request.getUint16(10, true),
            received: new DataView(response.buffer).getUint32(6, true),
            total: new DataView(response.buffer).getUint32(10, true),
          };
      return {
        complete: true,
        encoding: 'base64',
        data: bytesToBase64(response),
        ack,
      };
    }
    return { complete: true };
  }

  async authorizedFetch(input: RequestInfo | URL, _init?: RequestInit): Promise<Response> {
    const rawUrl = typeof input === 'string'
      ? input
      : input instanceof URL
        ? input.href
        : input.url;
    const url = new URL(rawUrl, 'http://localhost');
    if (url.pathname.endsWith('/api/firmware-check-update')) {
      return jsonResponse({
        errNo: 0,
        data: {
          currentVersion: DEFAULT_FIRMWARE.version,
          updateAvailable: false,
          updateCount: 0,
          checkTime: new Date().toISOString(),
          latestVersion: DEFAULT_FIRMWARE.version,
          latestFirmware: {
            id: 'mock-current',
            name: 'HBox V2 Mock Firmware',
            version: DEFAULT_FIRMWARE.version,
            desc: 'Offline fixture: device is up to date.',
            createTime: DEFAULT_FIRMWARE.buildDate,
            updateTime: DEFAULT_FIRMWARE.buildDate,
          },
          availableUpdates: [],
        },
      });
    }
    return jsonResponse(
      { errNo: 404, errorMessage: `No offline fixture for ${url.pathname}` },
      404,
    );
  }

  async close(): Promise<void> {
    const wasConnected = this.state !== DeviceTransportState.DISCONNECTED;
    this.stopPerformanceMonitor();
    this.clearCalibrationTimers();
    this.session = null;
    this.setState(DeviceTransportState.DISCONNECTED);
    if (wasConnected) {
      this.disconnectHandlers.forEach((handler) => handler());
    }
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

  private async handleRequest(
    command: string,
    params: Record<string, unknown>,
  ): Promise<unknown> {
    switch (command) {
      case 'ping':
        return { pong: true, timestamp: Date.now() };
      case 'binary.exchange': {
        if (params.encoding !== 'base64' || typeof params.data !== 'string') {
          throw new DeviceTransportError('protocol', 'Invalid mock binary.exchange request');
        }
        const request = base64ToBytes(params.data);
        const response = new Uint8Array(this.handleBinaryExchange(request));
        return {
          encoding: 'base64',
          data: bytesToBase64(response),
        };
      }
      case 'get_global_config':
        return {
          globalConfig: {
            ...this.globalConfig,
            defaultProfileId: this.defaultProfileId,
          },
        };
      case 'update_global_config': {
        const patch = asObject(params.globalConfig);
        if (typeof patch.defaultProfileId === 'string') {
          this.defaultProfileId = this.findProfile(patch.defaultProfileId).id;
        }
        this.globalConfig = {
          ...this.globalConfig,
          ...patch,
          defaultProfileId: this.defaultProfileId,
        };
        this.persistState();
        return { globalConfig: this.globalConfig, success: true };
      }
      case 'get_screen_control_config':
        return { screenControl: this.screenControl };
      case 'update_screen_control_config':
        this.screenControl = {
          ...this.screenControl,
          ...asObject(params.screenControl),
        } as ScreenControlConfig;
        this.persistState();
        return { screenControl: this.screenControl, success: true };
      case 'get_profile_list':
        return this.profilePayload();
      case 'get_default_profile':
        return { defaultProfileDetails: this.defaultProfile() };
      case 'get_profile_details':
        return { profileDetails: this.findProfile(asString(params.profileId)) };
      case 'create_profile': {
        const id = `profile-${Date.now().toString(36)}`;
        this.profiles.push(makeProfile(id, asString(params.profileName) || 'New Profile', false));
        this.persistState();
        return this.profilePayload();
      }
      case 'delete_profile': {
        const id = asString(params.profileId);
        if (this.profiles.length > 1) {
          this.profiles = this.profiles.filter((profile) => profile.id !== id);
          if (!this.profiles.some((profile) => profile.id === this.defaultProfileId)) {
            this.defaultProfileId = this.profiles[0].id;
            this.globalConfig.defaultProfileId = this.defaultProfileId;
          }
          this.persistState();
        }
        return this.profilePayload();
      }
      case 'switch_default_profile':
        this.defaultProfileId = this.findProfile(asString(params.profileId)).id;
        this.globalConfig.defaultProfileId = this.defaultProfileId;
        this.persistState();
        return this.profilePayload();
      case 'update_profile': {
        const requestedId = asString(params.profileId) || asString(asObject(params.profileDetails).id);
        const index = this.profiles.findIndex((profile) => profile.id === requestedId);
        if (index < 0) throw new DeviceTransportError('protocol', `Unknown mock profile ${requestedId}`);
        this.profiles[index] = mergeProfile(this.profiles[index], asObject(params.profileDetails));
        this.persistState();
        return { defaultProfileDetails: this.defaultProfile(), success: true };
      }
      case 'get_profile_macros':
        return { m: profileMacrosToWire(this.findProfile(asString(params.pid)).keysConfig?.macros ?? []) };
      case 'update_profile_macros': {
        const profile = this.findProfile(asString(params.pid));
        const macros = profileMacrosFromWire(params.m);
        if (profile.keysConfig) profile.keysConfig.macros = macros;
        this.persistState();
        return { m: profileMacrosToWire(macros), success: true };
      }
      case 'get_macro': {
        const profile = this.findProfile(asString(params.profileId));
        const index = asNumber(params.index);
        const macro = profile.keysConfig?.macros?.find((item) => item.index === index)
          ?? { index, triggerKeys: [], steps: [] };
        return { macro: { index, data: encodeMacroData(macro) } };
      }
      case 'update_macro': {
        const profile = this.findProfile(asString(params.profileId));
        const wireMacro = asObject(params.macro);
        const index = asNumber(wireMacro.index);
        const macro = decodeMacroData(index, asString(wireMacro.data));
        if (profile.keysConfig) {
          const macros = profile.keysConfig.macros ?? [];
          profile.keysConfig.macros = [...macros.filter((item) => item.index !== macro.index), macro];
        }
        this.persistState();
        return { macro: { index, data: encodeMacroData(macro) }, success: true };
      }
      case 'get_hotkeys_config':
        return { hotkeysConfig: this.hotkeys };
      case 'update_hotkeys_config': {
        const incoming = clone((params.hotkeysConfig as Hotkey[]) ?? []);
        this.hotkeys = this.hotkeys.map((current, index) => {
          const hotkey = incoming[index];
          if (!hotkey || current.isLocked) return clone(current);
          return { ...hotkey, isLocked: false };
        });
        this.persistState();
        return { hotkeysConfig: this.hotkeys, success: true };
      }
      case 'get_hitbox_layout':
        return HITBOX_LAYOUT;
      case 'get_device_logs_list':
        return {
          items: [
            '[MOCK] HBox V2 transport connected',
            '[MOCK] Loaded profile: Arcade',
            '[MOCK] ADC calibration data valid (18/18)',
            '[MOCK] USB report scheduler running at 1000 Hz',
          ],
        };
      case 'export_all_config':
        this.scheduleConfigExport();
        return { accepted: true };
      case 'import_config_begin':
        this.importStaging = [];
        this.importReplaceProfiles = params.replaceProfiles === true;
        this.importStrict = params.strict === true;
        return {
          success: true,
          strict: this.importStrict,
          replaceProfiles: this.importReplaceProfiles,
        };
      case 'import_config_part':
        this.stageImportPart(asString(params.section), params.data);
        return { success: true };
      case 'import_config_finish':
        this.finishImport();
        return { success: true };
      case 'import_config_abort': {
        const aborted = this.importStaging !== null;
        this.importStaging = null;
        this.importReplaceProfiles = false;
        this.importStrict = false;
        return { success: true, aborted };
      }
      case 'ms_get_list':
        return this.mappingListPayload();
      case 'get_adc_config_backup':
        return { adcConfig: this.adcConfigBackup() };
      case 'ms_get_default':
      case 'ms_set_default':
        if (command === 'ms_set_default') {
          this.defaultMappingId = this.findMapping(asString(params.id)).id;
          this.persistState();
        }
        return { id: this.defaultMappingId };
      case 'ms_create_mapping': {
        const mapping = makeMapping(
          `mapping-${Date.now().toString(36)}`,
          asString(params.name) || 'Mock Mapping',
          asNumber(params.length) || 40,
          asNumber(params.step) || 100,
        );
        this.mappings.push(mapping);
        this.persistState();
        return this.mappingListPayload();
      }
      case 'ms_delete_mapping':
        if (this.mappings.length > 1) this.mappings = this.mappings.filter((item) => item.id !== asString(params.id));
        if (!this.mappings.some((item) => item.id === this.defaultMappingId)) this.defaultMappingId = this.mappings[0].id;
        this.persistState();
        return this.mappingListPayload();
      case 'ms_rename_mapping': {
        this.findMapping(asString(params.id)).name = asString(params.name);
        this.persistState();
        return this.mappingListPayload();
      }
      case 'ms_get_mapping':
        return { mapping: this.findMapping(asString(params.id)) };
      case 'ms_mark_mapping_start': {
        const mapping = this.findMapping(asString(params.id));
        this.markingStatus = {
          id: mapping.id,
          mapping_name: mapping.name,
          step: mapping.step,
          length: mapping.length,
          index: 0,
          values: [],
          is_marking: true,
          is_sampling: false,
          is_completed: false,
          sampling_noise: mapping.samplingNoise,
          sampling_frequency: mapping.samplingFrequency,
        };
        return { status: this.markingStatus };
      }
      case 'ms_mark_mapping_step': {
        if (!this.markingStatus.is_marking || this.markingStatus.is_completed) {
          return { status: this.markingStatus };
        }
        const sampleIndex = this.markingStatus.values.length;
        if (sampleIndex < this.markingStatus.length) {
          const divisor = Math.max(1, this.markingStatus.length - 1);
          const value = Math.round(4000 - (3200 * sampleIndex) / divisor);
          this.markingStatus.values.push(value);
        }
        this.markingStatus.index = Math.min(this.markingStatus.values.length, this.markingStatus.length);
        this.markingStatus.is_completed = this.markingStatus.index >= this.markingStatus.length;
        this.markingStatus.is_marking = !this.markingStatus.is_completed;
        if (this.markingStatus.is_completed) {
          const mapping = this.findMapping(this.markingStatus.id);
          mapping.originalValues = [...this.markingStatus.values];
          mapping.calibratedValues = Array.from(
            { length: mapping.length },
            (_, index) => index * mapping.step,
          );
          this.persistState();
        }
        return { status: this.markingStatus };
      }
      case 'ms_mark_mapping_stop':
        this.markingStatus.is_marking = false;
        this.markingStatus.is_sampling = false;
        return { status: this.markingStatus };
      case 'ms_get_mark_status':
        return { status: this.markingStatus };
      case 'start_manual_calibration':
        return { calibrationStatus: this.startCalibration() };
      case 'stop_manual_calibration':
        return { calibrationStatus: this.stopCalibration() };
      case 'clear_manual_calibration_data':
        this.clearCalibrationData();
        return { calibrationStatus: this.calibrationStatus, success: true };
      case 'check_is_manual_calibration_completed':
        return { isCompleted: this.calibrationComplete };
      case 'start_button_monitoring':
        queueMicrotask(() => this.emit('button.state', {
          isActive: true,
          triggerMask: 0,
          totalButtons: 22,
        }));
        return { isActive: true };
      case 'stop_button_monitoring':
        queueMicrotask(() => this.emit('button.state', {
          isActive: false,
          triggerMask: 0,
          totalButtons: 22,
        }));
        return { isActive: false };
      case 'get_button_states':
        return { triggerMask: 0, triggerBinary: '0'.repeat(32), totalButtons: 22, timestamp: Date.now() };
      case 'start_button_performance_monitoring':
        this.startPerformanceMonitor();
        return { isActive: true };
      case 'stop_button_performance_monitoring':
        this.stopPerformanceMonitor();
        return { isActive: false };
      case 'performance.clock-sync':
        return {
          sampleId: params.sampleId,
          deviceTimestampUs: Math.floor((typeof performance === 'undefined' ? Date.now() : performance.now()) * 1000) >>> 0,
        };
      case 'push_leds_config':
      case 'clear_leds_preview':
      case 'reboot':
        return { success: true };
      case 'get_firmware_metadata':
        return DEFAULT_FIRMWARE;
      case 'create_firmware_upgrade_session': {
        const sessionId = asString(params.session_id) || `mock-upgrade-${Date.now()}`;
        this.firmwareSessions.add(sessionId);
        return { success: true, session_id: sessionId, status: 'uploading', progress: 0 };
      }
      case 'upload_firmware_chunk':
        return { success: true, progress: { received_size: asNumber(params.chunk_size), total_size: asNumber(params.chunk_size) } };
      case 'complete_firmware_upgrade_session':
        this.firmwareSessions.delete(asString(params.session_id));
        return { success: true, status: 'completed', progress: 100 };
      case 'abort_firmware_upgrade_session':
        this.firmwareSessions.delete(asString(params.session_id));
        return { success: true, status: 'aborted' };
      default:
        throw new DeviceTransportError('protocol', `Mock fixture missing command: ${command}`);
    }
  }

  private handleBinaryExchange(bytes: Uint8Array): ArrayBuffer {
    if (bytes.byteLength === 0) {
      throw new DeviceTransportError('protocol', 'Binary request is empty');
    }
    const command = bytes[0];
    const request = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);

    if (command === 0x30 && bytes.byteLength >= 18) {
      const cid = request.getUint32(2, true);
      const total = request.getUint32(10, true);
      const width = request.getUint16(6, true);
      const height = request.getUint16(8, true);
      const frameCount = Math.max(1, request.getUint8(14));
      const fps = request.getUint8(15);
      const valid = total > 0 && total <= 4 * 1024 * 1024 && width > 0 && height > 0;
      if (valid) {
        this.imageTransfers.set(cid, {
          width,
          height,
          frameCount,
          fps,
          format: request.getUint8(1) === 1 ? 2 : 1,
          data: new Uint8Array(total),
          received: 0,
        });
      }
      return this.makeImageAck(0xb0, cid, valid, 0, total, valid ? undefined : 'Invalid image metadata');
    }

    if (command === 0x31 && bytes.byteLength >= 14) {
      const cid = request.getUint32(2, true);
      const offset = request.getUint32(6, true);
      const length = request.getUint16(10, true);
      const transfer = this.imageTransfers.get(cid);
      const valid = !!transfer
        && length <= bytes.byteLength - 14
        && offset <= transfer.data.byteLength
        && offset + length <= transfer.data.byteLength;
      if (transfer && valid) {
        transfer.data.set(bytes.subarray(14, 14 + length), offset);
        transfer.received = Math.max(transfer.received, offset + length);
      }
      return this.makeImageAck(
        0xb1,
        cid,
        valid,
        transfer?.received ?? 0,
        transfer?.data.byteLength ?? 0,
        valid ? undefined : 'Invalid image chunk',
      );
    }

    if (command === 0x32 && bytes.byteLength >= 6) {
      const cid = request.getUint32(2, true);
      const transfer = this.imageTransfers.get(cid);
      const valid = !!transfer && transfer.received === transfer.data.byteLength;
      if (transfer && valid) {
        this.images.user = {
          width: transfer.width,
          height: transfer.height,
          frameCount: transfer.frameCount,
          fps: transfer.fps,
          format: transfer.format,
          data: transfer.data.slice(),
        };
        this.persistState();
      }
      const response = this.makeImageAck(
        0xb2,
        cid,
        valid,
        transfer?.received ?? 0,
        transfer?.data.byteLength ?? 0,
        valid ? undefined : 'Image upload is incomplete',
      );
      this.imageTransfers.delete(cid);
      return response;
    }

    if (command === 0x33 && bytes.byteLength >= 6) {
      const cid = request.getUint32(2, true);
      this.images.user = null;
      this.imageTransfers.delete(cid);
      this.persistState();
      return this.makeImageAck(0xb3, cid, true, 0, 0);
    }

    if (command === 0x34 && bytes.byteLength >= 6) {
      const response = new ArrayBuffer(64);
      const view = new DataView(response);
      view.setUint8(0, 0xb4);
      view.setUint8(1, 1);
      view.setUint32(2, request.getUint32(2, true), true);
      writeImageInfo(view, 6, this.images.user);
      writeImageInfo(view, 7, this.images.system);
      return response;
    }

    if (command === 0x35 && bytes.byteLength >= 14) {
      const target = request.getUint8(1) === 1 ? 1 : 0;
      const cid = request.getUint32(2, true);
      const offset = request.getUint32(6, true);
      const requestedLength = request.getUint16(10, true);
      const image = target === 1 ? this.images.system : this.images.user;
      const valid = !!image && offset < image.data.byteLength;
      const length = valid
        ? Math.min(requestedLength, image.data.byteLength - offset)
        : 0;
      const response = new ArrayBuffer(55 + length);
      const view = new DataView(response);
      view.setUint8(0, 0xb5);
      view.setUint8(1, valid ? 1 : 0);
      view.setUint8(2, target);
      view.setUint8(3, image?.format ?? 0);
      view.setUint32(4, cid, true);
      view.setUint16(8, image?.width ?? 0, true);
      view.setUint16(10, image?.height ?? 0, true);
      view.setUint32(12, image?.data.byteLength ?? 0, true);
      view.setUint32(16, offset, true);
      view.setUint16(20, length, true);
      view.setUint8(22, 0);
      if (image && length > 0) {
        new Uint8Array(response, 55).set(image.data.subarray(offset, offset + length));
      }
      return response;
    }

    if (command === 0x01 && bytes.byteLength >= 62) {
      const response = new ArrayBuffer(75);
      const view = new DataView(response);
      const chunkIndex = request.getUint32(54, true);
      const totalChunks = request.getUint32(58, true);
      view.setUint8(0, 0x81);
      view.setUint8(1, 1);
      view.setUint32(2, chunkIndex, true);
      view.setUint32(6, totalChunks ? Math.floor(((chunkIndex + 1) * 100) / totalChunks) : 100, true);
      return response;
    }
    throw new DeviceTransportError('protocol', `Unsupported binary command 0x${command.toString(16)}`);
  }

  private scheduleConfigExport(): void {
    const sections = [
      {
        section: 'global',
        data: {
          ...this.globalConfig,
          defaultProfileId: this.defaultProfileId,
        },
      },
      { section: 'hotkeys', data: this.hotkeys },
      { section: 'screenControl', data: this.screenControl },
      ...this.profiles.map((profile) => ({ section: 'profile', data: profile })),
      { section: 'end' },
    ];
    sections.forEach((section, index) => {
      setTimeout(() => this.emit('export_all_config', clone(section)), index);
    });
  }

  private stageImportPart(section: string, data: unknown): void {
    if (!['global', 'hotkeys', 'screenControl', 'profile', 'adcConfig'].includes(section)) {
      throw new DeviceTransportError('protocol', `Unknown import section: ${section}`);
    }
    this.importStaging ??= [];
    this.importStaging.push({ section, data: clone(data) });
  }

  private finishImport(): void {
    const staged = this.importStaging ?? [];
    const candidate = clone(this.captureState());
    const allowedProfileIds = new Set(candidate.profiles.map((profile) => profile.id));
    const importedProfileIds = new Set<string>();
    try {
      if (this.importStrict) {
        const sections = new Set(staged.map((part) => part.section));
        if (
          !sections.has('global') || !sections.has('hotkeys') ||
          !sections.has('screenControl') || !sections.has('adcConfig')
        ) {
          throw new DeviceTransportError(
            'protocol',
            'Configuration backup is missing a required section',
          );
        }
      }
      if (this.importReplaceProfiles) candidate.profiles = [];
      for (const part of staged) {
        if (part.section === 'global') {
          const globalPart = asObject(part.data);
          candidate.globalConfig = {
            ...candidate.globalConfig,
            ...globalPart,
          } as PersistedMockState['globalConfig'];
          if (typeof globalPart.defaultProfileId === 'string') {
            candidate.defaultProfileId = globalPart.defaultProfileId;
          }
        } else if (part.section === 'hotkeys') {
          if (
            !Array.isArray(part.data) ||
            (this.importStrict && part.data.length !== DEFAULT_NUM_HOTKEYS_MAX)
          ) {
            throw new DeviceTransportError('protocol', 'Imported hotkeys must be an array');
          }
          candidate.hotkeys = clone(part.data as Hotkey[]).map((hotkey, index) =>
            candidate.hotkeys[index]?.isLocked
              ? clone(candidate.hotkeys[index])
              : { ...hotkey, isLocked: false },
          );
        } else if (part.section === 'screenControl') {
          candidate.screenControl = {
            ...candidate.screenControl,
            ...asObject(part.data),
          } as ScreenControlConfig;
        } else if (part.section === 'profile') {
          const profile = asObject(part.data) as unknown as GameProfile;
          if (
            !profile.id || !profile.name || !allowedProfileIds.has(profile.id) ||
            importedProfileIds.has(profile.id)
          ) {
            throw new DeviceTransportError('protocol', 'Imported profile is missing id or name');
          }
          importedProfileIds.add(profile.id);
          const index = candidate.profiles.findIndex((item) => item.id === profile.id);
          if (index >= 0) candidate.profiles[index] = clone(profile);
          else candidate.profiles.push(clone(profile));
        } else if (part.section === 'adcConfig') {
          const adc = asObject(part.data);
          if (!Array.isArray(adc.mappings) || adc.mappings.length === 0) {
            throw new DeviceTransportError('protocol', 'Imported ADC data has no mappings');
          }
          candidate.mappings = clone(adc.mappings as ADCValuesMapping[]).map((mapping) => ({
            ...mapping,
            calibratedValues: Array.from(
              { length: mapping.length },
              (_, index) => index * mapping.step,
            ),
          }));
          candidate.defaultMappingId = asString(adc.defaultMappingId);
          const manual = Array.isArray(adc.manualCalibrationValues)
            ? adc.manualCalibrationValues.map(asObject)
            : [];
          candidate.calibrationComplete = manual.length === 18 && manual.every(
            (pair) => asNumber(pair.topValue) > 0 && asNumber(pair.bottomValue) > 0,
          );
        }
      }
      if (!candidate.profiles.some((profile) => profile.id === candidate.defaultProfileId)) {
        throw new DeviceTransportError(
          'protocol',
          `Imported default profile does not exist: ${candidate.defaultProfileId}`,
        );
      }
      candidate.globalConfig.defaultProfileId = candidate.defaultProfileId;
      this.applyState(candidate);
      this.persistState();
    } finally {
      this.importStaging = null;
      this.importReplaceProfiles = false;
      this.importStrict = false;
    }
  }

  private adcConfigBackup(): Record<string, unknown> {
    const manualCalibrationValues = this.calibrationStatus.buttons.map((button) => ({
      topValue: button.isCalibrated ? button.topValue : 0,
      bottomValue: button.isCalibrated ? button.bottomValue : 0,
    }));
    return {
      version: 1,
      defaultMappingId: this.defaultMappingId,
      calibratedMappingId: this.calibrationComplete ? this.defaultMappingId : '',
      mappings: this.mappings.map((mapping) => ({
        id: mapping.id,
        name: mapping.name,
        length: mapping.length,
        step: mapping.step,
        samplingFrequency: mapping.samplingFrequency,
        samplingNoise: mapping.samplingNoise,
        originalValues: [...mapping.originalValues],
      })),
      manualCalibrationValues,
      autoCalibrationValues: Array.from({ length: 18 }, () => ({
        topValue: 0,
        bottomValue: 0,
      })),
    };
  }

  private startCalibration(): CalibrationStatus {
    this.clearCalibrationTimers();
    this.calibrationActive = true;
    this.calibrationStartedAt = Date.now();
    this.calibrationComplete = false;
    this.globalConfig.manualCalibrationActive = true;
    this.calibrationStatus = makeCalibrationStatus('uncalibrated');
    this.persistState();
    this.scheduleCalibrationPhase('uncalibrated', 0);
    this.scheduleCalibrationPhase('top', 60);
    this.scheduleCalibrationPhase('bottom', 140);
    this.scheduleCalibrationPhase('completed', 230);
    return this.calibrationStatus;
  }

  private stopCalibration(): CalibrationStatus {
    // React development Strict Mode mounts, cleans up and mounts effects once
    // more immediately. Ignore that synthetic cleanup so the second mounted
    // calibration view still receives the deterministic phase sequence.
    if (
      this.calibrationActive
      && Date.now() - this.calibrationStartedAt < 75
    ) {
      return this.calibrationStatus;
    }
    this.clearCalibrationTimers();
    this.calibrationActive = false;
    this.calibrationStartedAt = 0;
    this.globalConfig.manualCalibrationActive = false;
    this.calibrationStatus = makeCalibrationStatus(
      this.calibrationComplete ? 'completed' : 'uncalibrated',
    );
    this.persistState();
    this.emitCalibrationStatus();
    return this.calibrationStatus;
  }

  private clearCalibrationData(): void {
    this.clearCalibrationTimers();
    this.calibrationActive = false;
    this.calibrationComplete = false;
    this.globalConfig.manualCalibrationActive = false;
    this.calibrationStatus = makeCalibrationStatus('uncalibrated');
    this.persistState();
    this.emitCalibrationStatus();
  }

  private scheduleCalibrationPhase(
    phase: 'uncalibrated' | 'top' | 'bottom' | 'completed',
    delayMs: number,
  ): void {
    const timer = setTimeout(() => {
      if (!this.calibrationActive && phase !== 'completed') return;
      if (phase === 'completed') {
        this.calibrationActive = false;
        this.calibrationStartedAt = 0;
        this.calibrationComplete = true;
        this.globalConfig.manualCalibrationActive = false;
      }
      this.calibrationStatus = makeCalibrationStatus(phase);
      if (phase === 'completed') this.persistState();
      this.emitCalibrationStatus();
    }, delayMs);
    this.calibrationTimers.push(timer);
  }

  private emitCalibrationStatus(): void {
    this.emit('calibration_update', {
      calibrationStatus: clone(this.calibrationStatus),
    });
  }

  private clearCalibrationTimers(): void {
    this.calibrationTimers.forEach((timer) => clearTimeout(timer));
    this.calibrationTimers = [];
  }

  private makeImageAck(
    responseCommand: number,
    cid: number,
    success: boolean,
    received: number,
    total: number,
    error?: string,
  ): ArrayBuffer {
    const errorBytes = error
      ? new TextEncoder().encode(error).subarray(0, 64)
      : new Uint8Array(0);
    // BinaryUploadBgImageResponse is a fixed 79-byte packed ABI:
    // 15 bytes of metadata followed by a 64-byte error field.
    const response = new ArrayBuffer(79);
    const view = new DataView(response);
    view.setUint8(0, responseCommand);
    view.setUint8(1, success ? 1 : 0);
    view.setUint32(2, cid, true);
    view.setUint32(6, received, true);
    view.setUint32(10, total, true);
    view.setUint8(14, errorBytes.byteLength);
    new Uint8Array(response, 15).set(errorBytes);
    return response;
  }

  private startPerformanceMonitor(): void {
    if (this.performanceTimer) return;
    this.performanceTimer = setInterval(() => {
      const response = new Uint8Array(44);
      const view = new DataView(response.buffer);
      const frame = this.sampleCounter++;
      view.setUint32(0, (Date.now() * 1000) >>> 0, true);
      view.setUint32(4, 0, true);
      for (let index = 0; index < 18; index += 1) {
        const distanceUm = Math.max(0, Math.round(2000 + 1800 * Math.sin((frame + index * 2) / 10)));
        view.setUint16(8 + index * 2, distanceUm, true);
      }
      this.emit('performance.sample', response);
    }, 100);
  }

  private stopPerformanceMonitor(): void {
    if (this.performanceTimer) clearInterval(this.performanceTimer);
    this.performanceTimer = null;
  }

  private emit(name: string, data: unknown): void {
    const event: DeviceEvent<unknown> = { name, data };
    this.eventHandlers.get(name)?.forEach((handler) => handler(event));
    this.eventHandlers.get('*')?.forEach((handler) => handler(event));
  }

  private profilePayload() {
    return {
      profileList: {
        defaultId: this.defaultProfileId,
        maxNumProfiles: 8,
        items: this.profiles,
      },
      defaultProfileDetails: this.defaultProfile(),
    };
  }

  private mappingListPayload() {
    return {
      mappingList: this.mappings.map(({ id, name }) => ({ id, name })),
      defaultMappingId: this.defaultMappingId,
    };
  }

  private defaultProfile(): GameProfile {
    return this.findProfile(this.defaultProfileId);
  }

  private findProfile(id: string): GameProfile {
    const profile = this.profiles.find((item) => item.id === id);
    if (!profile) throw new DeviceTransportError('protocol', `Unknown mock profile ${id}`);
    return profile;
  }

  private findMapping(id: string): ADCValuesMapping {
    const mapping = this.mappings.find((item) => item.id === id);
    if (!mapping) throw new DeviceTransportError('protocol', `Unknown mock mapping ${id}`);
    return mapping;
  }

  private captureState(): PersistedMockState {
    return {
      version: MOCK_STATE_VERSION,
      globalConfig: clone(this.globalConfig),
      screenControl: clone(this.screenControl),
      hotkeys: clone(this.hotkeys),
      profiles: clone(this.profiles),
      defaultProfileId: this.defaultProfileId,
      mappings: clone(this.mappings),
      defaultMappingId: this.defaultMappingId,
      calibrationComplete: this.calibrationComplete,
      images: {
        user: serializeImage(this.images.user),
        system: serializeImage(this.images.system),
      },
    };
  }

  private applyState(state: PersistedMockState): void {
    if (!Array.isArray(state.profiles) || state.profiles.length === 0) {
      throw new DeviceTransportError('protocol', 'Persisted mock state has no profiles');
    }
    if (!Array.isArray(state.mappings) || state.mappings.length === 0) {
      throw new DeviceTransportError('protocol', 'Persisted mock state has no ADC mappings');
    }
    this.globalConfig = {
      ...clone(state.globalConfig),
      manualCalibrationActive: false,
    };
    this.screenControl = clone(state.screenControl);
    this.hotkeys = clone(state.hotkeys);
    this.profiles = clone(state.profiles);
    this.defaultProfileId = this.profiles.some((profile) => profile.id === state.defaultProfileId)
      ? state.defaultProfileId
      : this.profiles[0].id;
    this.globalConfig.defaultProfileId = this.defaultProfileId;
    this.mappings = clone(state.mappings);
    this.defaultMappingId = this.mappings.some((mapping) => mapping.id === state.defaultMappingId)
      ? state.defaultMappingId
      : this.mappings[0].id;
    this.calibrationComplete = !!state.calibrationComplete;
    this.calibrationActive = false;
    this.calibrationStatus = makeCalibrationStatus(
      this.calibrationComplete ? 'completed' : 'uncalibrated',
    );
    this.images = {
      user: deserializeImage(state.images?.user),
      system: deserializeImage(state.images?.system) ?? makeSystemImage(),
    };
  }

  private persistState(): void {
    if (!this.storage) return;
    try {
      this.storage.setItem(this.storageKey, JSON.stringify(this.captureState()));
    } catch {
      // Storage is a convenience for local preview. A full/quarantined
      // sessionStorage must never make the mock device unusable.
    }
  }

  private restoreState(): void {
    if (!this.storage) return;
    try {
      const raw = this.storage.getItem(this.storageKey);
      if (!raw) return;
      const parsed = JSON.parse(raw) as Partial<PersistedMockState>;
      if (parsed.version !== MOCK_STATE_VERSION) return;
      this.applyState(parsed as PersistedMockState);
    } catch {
      // Corrupt or stale tab state falls back to deterministic fixtures.
    }
  }

  private assertConnected(): void {
    if (this.state !== DeviceTransportState.CONNECTED) {
      throw new DeviceTransportError('not-connected', 'Mock device is not connected');
    }
  }

  private setState(state: DeviceTransportState): void {
    if (this.state === state) return;
    this.state = state;
    this.stateHandlers.forEach((handler) => handler(state));
  }
}

function makeProfile(id: string, name: string, isCompetitionProfile: boolean): GameProfile {
  const buttons = Object.values(GameControllerButton);
  return {
    id,
    name,
    isCompetitionProfile,
    keysConfig: {
      inputMode: Platform.XINPUT,
      socdMode: GameSocdMode.SOCD_MODE_UP_PRIORITY,
      invertXAxis: false,
      invertYAxis: false,
      fourWayMode: false,
      keysEnableTag: Array.from({ length: 22 }, () => true),
      keyMapping: Object.fromEntries(buttons.map((button, index) => [button, [index]])),
      keyCombinations: [],
      macros: [
        { index: 0, triggerKeys: [18, 19], steps: [{ timeMs: 0, buttonMask: 1 << 10, dynamicMask: 0 }, { timeMs: 80, buttonMask: 0, dynamicMask: 0 }] },
      ],
    },
    triggerConfigs: {
      isAllBtnsConfiguring: false,
      debounceAlgorithm: 1,
      triggerConfigs: Array.from({ length: 18 }, () => ({
        topDeadzone: 0.3,
        bottomDeadzone: 0.3,
        pressAccuracy: 0.1,
        releaseAccuracy: 0.1,
      })),
    },
    ledsConfigs: {
      ledEnabled: true,
      ledsEffectStyle: LedsEffectStyle.RIPPLE,
      ledColors: ['#A15A9F', '#24C8DB', '#F5A524'],
      ledBrightness: 70,
      ledAnimationSpeed: 3,
      hasAroundLed: true,
      aroundLedEnabled: true,
      aroundLedSyncToMainLed: false,
      aroundLedTriggerByButton: true,
      aroundLedEffectStyle: AroundLedsEffectStyle.METEOR,
      aroundLedColors: ['#A15A9F', '#24C8DB', '#F5A524'],
      aroundLedBrightness: 55,
      aroundLedAnimationSpeed: 3,
    },
  };
}

function mergeProfile(profile: GameProfile, patch: Record<string, unknown>): GameProfile {
  const typed = patch as unknown as Partial<GameProfile>;
  return {
    ...profile,
    ...typed,
    id: profile.id,
    keysConfig: typed.keysConfig
      ? { ...profile.keysConfig, ...typed.keysConfig }
      : profile.keysConfig,
    triggerConfigs: typed.triggerConfigs
      ? { ...profile.triggerConfigs, ...typed.triggerConfigs }
      : profile.triggerConfigs,
    ledsConfigs: typed.ledsConfigs
      ? { ...profile.ledsConfigs, ...typed.ledsConfigs } as GameProfile['ledsConfigs']
      : profile.ledsConfigs,
  };
}

function profileMacrosToWire(
  macros: NonNullable<GameProfile['keysConfig']>['macros'],
): Array<{ k: number[]; s: number[][] } | null> {
  const byIndex = new Map((macros ?? []).map((macro) => [macro.index, macro]));
  return Array.from({ length: MAX_NUM_MACROS }, (_, index) => {
    const macro = byIndex.get(index);
    if (!macro) return null;
    const triggerKeys = macro.triggerKeys
      .slice(0, 4)
      .map((key) => Math.max(0, Math.min(255, key | 0)));
    const steps = macro.steps
      .slice(0, MAX_MACRO_STEPS)
      .map((step) => [
        Math.max(0, Math.min(65535, step.timeMs | 0)),
        step.buttonMask >>> 0,
        step.dynamicMask >>> 0,
      ]);
    return triggerKeys.length > 0 || steps.length > 0
      ? { k: triggerKeys, s: steps }
      : null;
  });
}

function profileMacrosFromWire(
  value: unknown,
): NonNullable<GameProfile['keysConfig']>['macros'] {
  if (!Array.isArray(value)) return [];
  const macros: MacroConfig[] = [];
  for (let index = 0; index < Math.min(MAX_NUM_MACROS, value.length); index += 1) {
    if (!value[index]) continue;
    const record = asObject(value[index]);
    const triggerKeys = Array.isArray(record.k)
      ? record.k.map(asNumber).slice(0, 4)
      : [];
    const steps = Array.isArray(record.s)
      ? record.s.slice(0, MAX_MACRO_STEPS).map((step) => {
          const values = Array.isArray(step) ? step : [];
          return {
            timeMs: Math.max(0, Math.min(65535, asNumber(values[0]) | 0)),
            buttonMask: asNumber(values[1]) >>> 0,
            dynamicMask: asNumber(values[2]) >>> 0,
          };
        })
      : [];
    if (triggerKeys.length > 0 || steps.length > 0) {
      macros.push({ index, triggerKeys, steps });
    }
  }
  return macros;
}

function encodeMacroData(macro: MacroConfig): string {
  const triggerKeys = (macro.triggerKeys ?? [])
    .slice(0, 4)
    .map((key) => Math.max(0, Math.min(255, key | 0)));
  const steps = (macro.steps ?? []).slice(0, MAX_MACRO_STEPS);
  const bytes = new Uint8Array(1 + triggerKeys.length + 1 + steps.length * 10);
  const view = new DataView(bytes.buffer);
  let offset = 0;
  bytes[offset++] = triggerKeys.length;
  triggerKeys.forEach((key) => { bytes[offset++] = key; });
  bytes[offset++] = steps.length;
  steps.forEach((step) => {
    view.setUint16(offset, Math.max(0, Math.min(65535, step.timeMs | 0)), true);
    offset += 2;
    view.setUint32(offset, step.buttonMask >>> 0, true);
    offset += 4;
    view.setUint32(offset, step.dynamicMask >>> 0, true);
    offset += 4;
  });
  return bytesToBase64(bytes);
}

function decodeMacroData(index: number, encoded: string): MacroConfig {
  const bytes = base64ToBytes(encoded);
  if (bytes.byteLength < 2) {
    throw new DeviceTransportError('protocol', 'Invalid mock macro data');
  }
  const triggerCount = bytes[0];
  let offset = 1;
  if (triggerCount > 4 || offset + triggerCount + 1 > bytes.byteLength) {
    throw new DeviceTransportError('protocol', 'Invalid mock macro trigger keys');
  }
  const triggerKeys = Array.from(bytes.subarray(offset, offset + triggerCount));
  offset += triggerCount;
  const stepCount = bytes[offset++];
  if (stepCount > MAX_MACRO_STEPS || bytes.byteLength !== offset + stepCount * 10) {
    throw new DeviceTransportError('protocol', 'Invalid mock macro steps');
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const steps = Array.from({ length: stepCount }, () => {
    const timeMs = view.getUint16(offset, true);
    offset += 2;
    const buttonMask = view.getUint32(offset, true);
    offset += 4;
    const dynamicMask = view.getUint32(offset, true);
    offset += 4;
    return { timeMs, buttonMask, dynamicMask };
  });
  return { index, triggerKeys, steps };
}

function makeMapping(id: string, name: string, length: number, step: number): ADCValuesMapping {
  return {
    id,
    name,
    length,
    step,
    samplingFrequency: 8000,
    samplingNoise: 3,
    originalValues: Array.from({ length }, (_, index) => 4000 - index * Math.max(1, Math.floor(3200 / length))),
    calibratedValues: Array.from({ length }, (_, index) => index * step),
  };
}

function emptyMarkingStatus(): StepInfo {
  return {
    id: '',
    mapping_name: '',
    step: 0,
    length: 0,
    index: 0,
    values: [],
    is_marking: false,
    is_sampling: false,
    is_completed: false,
    sampling_noise: 0,
    sampling_frequency: 0,
  };
}

function makeCalibrationStatus(
  phase: 'uncalibrated' | 'top' | 'bottom' | 'completed',
): CalibrationStatus {
  const completed = phase === 'completed';
  const active = phase === 'top' || phase === 'bottom';
  const buttonPhase = phase === 'top'
    ? 'TOP_SAMPLING' as const
    : phase === 'bottom'
      ? 'BOTTOM_SAMPLING' as const
      : completed
        ? 'COMPLETED' as const
        : 'IDLE' as const;
  const ledColor = phase === 'top'
    ? 'CYAN' as const
    : phase === 'bottom'
      ? 'DARK_BLUE' as const
      : completed
        ? 'GREEN' as const
        : 'RED' as const;
  return {
    isActive: active,
    uncalibratedCount: completed ? 0 : 18,
    activeCalibrationCount: active ? 18 : 0,
    allCalibrated: completed,
    buttons: Array.from({ length: 18 }, (_, index) => ({
      index,
      phase: buttonPhase,
      isCalibrated: completed,
      topValue: 3920 - index * 5,
      bottomValue: 720 + index * 4,
      ledColor,
    })),
  };
}

function makeSystemImage(): MockImage {
  const width = 160;
  const height = 80;
  const data = new Uint8Array(width * height * 2);
  const view = new DataView(data.buffer);
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const red = Math.floor((x / (width - 1)) * 31);
      const green = Math.floor((y / (height - 1)) * 63);
      const blue = 18;
      view.setUint16((y * width + x) * 2, (red << 11) | (green << 5) | blue, true);
    }
  }
  return { width, height, frameCount: 1, fps: 0, format: 1, data };
}

function writeImageInfo(view: DataView, validOffset: 6 | 7, image: MockImage | null): void {
  view.setUint8(validOffset, image ? 1 : 0);
  if (!image) return;
  const base = validOffset === 6 ? 8 : 20;
  view.setUint16(base, image.width, true);
  view.setUint16(base + 2, image.height, true);
  view.setUint32(base + 4, image.data.byteLength, true);
  view.setUint8(base + 8, image.frameCount);
  view.setUint8(base + 9, image.fps);
  view.setUint8(base + 10, image.format);
}

function serializeImage(image: MockImage | null): PersistedImage | null {
  if (!image) return null;
  return {
    width: image.width,
    height: image.height,
    frameCount: image.frameCount,
    fps: image.fps,
    format: image.format,
    data: Array.from(image.data),
  };
}

function deserializeImage(image: PersistedImage | null | undefined): MockImage | null {
  if (!image || !Array.isArray(image.data)) return null;
  return {
    width: image.width,
    height: image.height,
    frameCount: image.frameCount,
    fps: image.fps,
    format: image.format,
    data: Uint8Array.from(image.data),
  };
}

function bytesToBase64(bytes: Uint8Array): string {
  let binary = '';
  for (let index = 0; index < bytes.byteLength; index += 1) {
    binary += String.fromCharCode(bytes[index]);
  }
  return btoa(binary);
}

function base64ToBytes(encoded: string): Uint8Array {
  let binary: string;
  try {
    binary = atob(encoded);
  } catch {
    throw new DeviceTransportError('protocol', 'Invalid mock macro Base64');
  }
  return Uint8Array.from(binary, (character) => character.charCodeAt(0));
}

function getBrowserSessionStorage(): MockStorage | null {
  try {
    return typeof window === 'undefined' ? null : window.sessionStorage;
  } catch {
    return null;
  }
}

function jsonResponse(data: unknown, status = 200): Response {
  return new Response(JSON.stringify(data), {
    status,
    headers: { 'content-type': 'application/json' },
  });
}

function clone<T>(value: T): T {
  if (value === undefined) return value;
  return JSON.parse(JSON.stringify(value)) as T;
}

function asObject(value: unknown): Record<string, unknown> {
  return value && typeof value === 'object' && !Array.isArray(value)
    ? value as Record<string, unknown>
    : {};
}

function asString(value: unknown): string {
  return typeof value === 'string' ? value : '';
}

function asNumber(value: unknown): number {
  const number = Number(value);
  return Number.isFinite(number) ? number : 0;
}
