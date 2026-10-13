import {
    type SharedButtonMonitorLeaseToken
} from '@/lib/button-monitor-lifecycle';
import {
    type ConfigSyncState
} from '@/lib/deferred-config-coordinator';
import {
    configuredTransportMode,
    DeviceConnectionError,
    DeviceConnectionPhase,
    DeviceImageCatalog,
    DeviceImageTarget,
    DeviceImageUploadRequest,
    DeviceRequestOptions,
    DeviceSession,
    DeviceTransportConfig,
    DeviceTransportState
} from '@/lib/device-transport';
import { type ConfigSyncProgress } from '@/lib/device-transport/config-sync';
import { resolveDefaultFirmwareServerHost } from '@/lib/device-transport/firmware-server-origin';
import { type FirmwareInventory, type PreparedRelease, type ReleaseProgress } from '@/lib/device-transport/release-install-client';
import { BindingArgs, BindingSnapshot } from '@/lib/device-transport/rf-binding';
import type { ResourceInventory, ResourceSender } from "@/lib/resources";
import {
    ADCValuesMapping,
    StepInfo,
    SwitchMappingCatalogDetail,
    SwitchMappingCatalogItem,
    SwitchMappingPayload
} from '@/types/adc';
import {
    AroundLedsEffectStyle,
    GameControllerButton,
    GameProfile,
    GameProfileList,
    GameSocdMode,
    GlobalConfig,
    Hotkey,
    KeyCombination,
    LedsEffectStyle,
    MacroConfig,
    Platform,
    ScreenControlConfig
} from '@/types/gamepad-config';
import {
    ButtonStates,
    CalibrationStatus,
    DeviceFirmwareInfo,
    FirmwarePackage,
    FirmwarePackageDownloadProgress,
    FirmwareUpdateCheckResponse,
    FirmwareUpgradeConfig,
    FirmwareUpgradeSession,
    LEDsConfig
} from '@/types/types';

export interface HitboxLayoutItem {
    x: number;
    y: number;
    r: number;
}

export const FIRMWARE_SERVER_CONFIG = {
    // Hosted V2 pins API tokens to the page/auth origin.
    defaultHost: resolveDefaultFirmwareServerHost(
        configuredTransportMode(),
        process.env.NEXT_PUBLIC_FIRMWARE_SERVER_HOST,
    ),
    // API 端点
    endpoints: {
        checkUpdate: '/api/firmware-check-update'
    }
};

export const EXIT_WEB_CONFIG_TIMEOUT_MS = 8_000;

export type DeviceTransportConfigType = DeviceTransportConfig;

export interface GamepadConfigContextType {
    rfBindingBusy: boolean;
    rfBindingRequest: (op: number, args?: BindingArgs) => Promise<BindingSnapshot>;
    runRfBindingOperation: <T, >(action: () => Promise<T>) => Promise<T>;
    contextJsReady: boolean;
    setContextJsReady: (ready: boolean) => void;

    // WebHID 连接状态
    deviceConnected: boolean;
    deviceState: DeviceTransportState;
    devicePhase: DeviceConnectionPhase;
    showReconnect: boolean;
    deviceError: DeviceConnectionError | null;
    deviceSession: DeviceSession | null;
    connectDevice: () => Promise<void>;
    reconnectDevice: () => Promise<void>;
    disconnectDevice: () => void;
    getDeviceImageCatalog: () => Promise<DeviceImageCatalog>;
    readDeviceImage: (target: DeviceImageTarget, totalSize: number) => Promise<Uint8Array>;
    uploadDeviceImage: (request: DeviceImageUploadRequest) => Promise<void>;
    deleteDeviceImage: () => Promise<void>;
    fetchDeviceAuthorizedResource: (input: RequestInfo | URL, init?: RequestInit) => Promise<Response>;

    installMappingResource: (bytes: Uint8Array) => Promise<SwitchMappingPayload>;
    resourceCommand: ResourceSender;
    resourceTransaction: <T>(operation: (send: ResourceSender) => Promise<T>) => Promise<T>;
    syncLightingResourceReferences: (profiles: ResourceInventory['profiles']) => void;
    profileList: GameProfileList;
    defaultProfile: GameProfile;
    hotkeysConfig: Hotkey[];
    globalConfig: GlobalConfig;
    screenControl: ScreenControlConfig;
    dataIsReady: boolean;
    setUserRebooting: (rebooting: boolean) => void;
    deviceBackgroundReadsPaused: boolean;
    firmwareUpdating: boolean;
    getReleaseInventory: () => Promise<FirmwareInventory>;
    downloadSelectedRelease: (id: string, inventory: FirmwareInventory, progress?: (p: ReleaseProgress) => void) => Promise<PreparedRelease>;
    installSelectedRelease: (release: PreparedRelease, progress: (p: ReleaseProgress) => void) => Promise<void>;
    releaseInstallAction: (action: 'abort' | 'retry' | 'activate', sessionId: string) => Promise<void>;
    setFirmwareUpdating: (updating: boolean) => void;

    previewScreenBrightness: (brightness: number) => Promise<void>;
    getProfileMacros: (profileId: string) => Promise<MacroConfig[]>;
    switchProfile: (profileId: string) => Promise<void>;
    stageDeferredProfileDetails: (profileId: string, profileDetails: GameProfile) => void;
    stageDeferredProfileMacros: (profileId: string, macros: MacroConfig[]) => void;
    stageDeferredHotkeysConfig: (hotkeysConfig: Hotkey[]) => void;
    stageDeferredGlobalConfig: (globalConfig: GlobalConfig) => void;
    stageDeferredScreenControl: (screenControl: ScreenControlConfig) => void;
    flushDeferredConfig: (
        afterFlush?: () => void | Promise<void>,
        resumeMonitoringAfterSuccess?: boolean,
        beforeFlush?: () => void | Promise<void>,
    ) => Promise<void>;
    terminateWebConfigActivities: () => Promise<void>;
    deferredConfigDirty: boolean;
    deferredConfigSaving: boolean;
    configSyncState: ConfigSyncState;
    configReadProgress: ConfigSyncProgress;
    configEditingBlocked: boolean;
    configRecovery: string[];
    resolveConfigRecovery: (restore: boolean) => void;
    retrySync: () => void;
    flushAndWait: () => Promise<void>;
    isLoading: boolean;
    error: string | null;
    setError: (error: string | null) => void;
    rebootSystem: () => Promise<void>;
    exitWebConfig: () => Promise<void>;
    // 校准相关
    calibrationStatus: CalibrationStatus;
    fetchCalibrationStatus: () => Promise<CalibrationStatus>;
    startManualCalibration: () => Promise<CalibrationStatus>;
    stopManualCalibration: () => Promise<CalibrationStatus>;
    clearManualCalibrationData: () => Promise<void>;
    checkIsManualCalibrationCompleted: () => Promise<boolean>;
    // ADC Mapping 相关
    defaultMappingId: string;
    markingStatus: StepInfo;
    mappingList: { id: string, name: string }[];
    activeMapping: ADCValuesMapping | null;
    mappingStorageMode: string | null;
    mappingSource: 'server-installed' | 'factory-fallback' | null;
    fetchMappingList: () => Promise<void>;
    fetchDefaultMapping: () => Promise<void>;
    fetchActiveMapping: (id: string) => Promise<void>;
    createMapping: (name: string, length: number, step: number) => Promise<string>;
    deleteMapping: (id: string) => Promise<void>;
    updateDefaultMapping: (id: string) => Promise<void>;
    startMarking: (id: string) => Promise<void>;
    stopMarking: () => Promise<void>;
    stepMarking: () => Promise<void>;
    syncMarkingProgress: () => Promise<void>;
    fetchMarkingStatus: () => Promise<void>;
    renameMapping: (id: string, name: string) => Promise<void>;
    fetchSwitchMappingCatalog: (includeDrafts?: boolean) => Promise<SwitchMappingCatalogItem[]>;
    fetchSwitchMappingDetail: (catalogId: string, admin?: boolean) => Promise<SwitchMappingCatalogDetail>;
    fetchSwitchMappingImage: (catalogId: string, admin?: boolean) => Promise<Blob | null>;
    uploadSwitchMappingImage: (catalogId: string, image: File) => Promise<void>;
    updateSwitchMappingMetadata: (catalogId: string, displayName: string, description: string) => Promise<void>;
    updateSwitchMappingCurve: (
        catalogId: string,
        mapping: SwitchMappingPayload,
    ) => Promise<SwitchMappingCatalogDetail>;
    deleteSwitchMapping: (catalogId: string) => Promise<void>;
    installSwitchMapping: (
        catalogId: string,
        prefetchedDetail?: SwitchMappingCatalogDetail,
    ) => Promise<SwitchMappingPayload>;
    clearInstalledSwitchMapping: (mappingId: string) => Promise<ADCValuesMapping>;
    createSwitchMappingFromCurrent: (input: {
        displayName: string;
        description: string;
        length: number;
        step: number;
        image?: File | null;
    }) => Promise<SwitchMappingCatalogDetail>;
    beginMappingDraft: (name: string, length: number, step: number) => Promise<void>;
    publishMappingDraft: (input: {
        catalogId?: string;
        displayName: string;
        description: string;
    }) => Promise<SwitchMappingCatalogDetail>;
    // 按键监控相关
    buttonMonitoringActive: boolean;
    startButtonMonitoring: () => Promise<SharedButtonMonitorLeaseToken>;
    stopButtonMonitoring: (lease: SharedButtonMonitorLeaseToken) => Promise<void>;
    getButtonStates: () => Promise<ButtonStates>;
    // 按键性能监控相关
    startButtonPerformanceMonitoring: () => Promise<void>;
    stopButtonPerformanceMonitoring: () => Promise<void>;
    // LED 配置相关
    pushLedsConfig: (ledsConfig: LEDsConfig, immediate?: boolean) => Promise<void>;
    clearLedsPreview: (immediate?: boolean) => Promise<void>;
    // 固件元数据相关
    firmwareInfo: DeviceFirmwareInfo | null;
    fetchFirmwareMetadata: () => Promise<void>;
    // 固件更新检查相关
    firmwareUpdateInfo: FirmwareUpdateCheckResponse | null;
    checkFirmwareUpdate: (currentVersion: string, customServerHost?: string) => Promise<void>;
    // 固件升级包下载和传输相关
    upgradeSession: FirmwareUpgradeSession | null;
    downloadFirmwarePackage: (downloadUrl: string, onProgress?: (progress: FirmwarePackageDownloadProgress) => void) => Promise<FirmwarePackage>;
    uploadFirmwareToDevice: (firmwarePackage: FirmwarePackage, onProgress?: (progress: FirmwarePackageDownloadProgress) => void) => Promise<void>;
    uploadCh585Firmware: (image: Uint8Array, onProgress?: (progress: number) => void) => Promise<void>;
    setUpgradeConfig: (config: Partial<FirmwareUpgradeConfig>) => void;
    getUpgradeConfig: () => FirmwareUpgradeConfig;
    getValidChunkSizes: () => number[];
    updateMarkingStatus: (status: StepInfo) => void;
    // 立即发送队列中的特定命令
    sendPendingCommandImmediately: (command: string) => boolean;
    // 快速清空队列
    flushQueue: () => Promise<void>;
    // WebHID transport timeout configuration
    getDeviceTransportConfig: () => DeviceTransportConfigType;
    updateDeviceTransportConfig: (config: Partial<DeviceTransportConfigType>) => void;
    // 是否禁用完成配置按钮
    finishConfigDisabled: boolean;
    setFinishConfigDisabled: (disabled: boolean) => void;
    // 按键索引映射到游戏控制器按钮或组合键
    indexMapToGameControllerButtonOrCombination: (
        keyMapping: { [key in GameControllerButton]?: number[] },
        keyCombinations: KeyCombination[],
        inputMode: Platform
    ) => { [key: number]: GameControllerButton | string };
    // 设备日志相关（服务端固定返回最近50条）
    fetchDeviceLogsList: () => Promise<string[]>;

    // 导出所有配置

    // 导入所有配置

    // 获取Hitbox布局
    getHitboxLayout: () => Promise<HitboxLayoutItem[]>;
    hitboxLayout: HitboxLayoutItem[];
}

export function makeEmptyMarkingStatus(): StepInfo {
    return {
        id: "",
        mapping_name: "",
        step: 0,
        length: 0,
        index: 0,
        values: [],
        sampling_noise: 0,
        sampling_frequency: 0,
        is_marking: false,
        is_sampling: false,
        is_completed: false
    };
}

export function makeEmptyCalibrationStatus(): CalibrationStatus {
    return {
        isActive: false,
        uncalibratedCount: 0,
        activeCalibrationCount: 0,
        allCalibrated: false,
        buttons: [],
    };
}

export function calibrationStatusFrom(value: unknown): CalibrationStatus | null {
    if (!value || typeof value !== 'object' || Array.isArray(value)) return null;
    const candidate = value as Partial<CalibrationStatus>;
    if (!Array.isArray(candidate.buttons)) return null;
    return {
        isActive: candidate.isActive === true,
        uncalibratedCount: Number(candidate.uncalibratedCount ?? 0),
        activeCalibrationCount: Number(candidate.activeCalibrationCount ?? 0),
        allCalibrated: candidate.allCalibrated === true,
        buttons: candidate.buttons,
    };
}

export const converProfileDetails = (profile: any) => {
    const rawMacros = profile.keysConfig?.macros;
    const macros: MacroConfig[] = Array.isArray(rawMacros)
        ? rawMacros.map((m: unknown) => {
            const obj = (m && typeof m === "object") ? (m as Record<string, unknown>) : {};
            const index = typeof obj.index === "number" ? obj.index : 0;
            const triggerKeys = Array.isArray(obj.triggerKeys)
                ? obj.triggerKeys.filter((x): x is number => typeof x === "number")
                : [];
            const steps = Array.isArray(obj.steps)
                ? obj.steps
                    .map((s: unknown) => (s && typeof s === "object") ? (s as Record<string, unknown>) : null)
                    .filter((s): s is Record<string, unknown> => !!s)
                    .map((s) => ({
                        timeMs: typeof s.timeMs === "number" ? s.timeMs : 0,
                        buttonMask: typeof s.buttonMask === "number"
                            ? s.buttonMask
                            : (((typeof s.pressButtonMask === "number" ? s.pressButtonMask : 0) & ~((typeof s.releaseButtonMask === "number" ? s.releaseButtonMask : 0))) >>> 0),
                        dynamicMask: typeof s.dynamicMask === "number" ? s.dynamicMask : 0,
                    }))
                : [];
            return { index, triggerKeys, steps };
        })
        : [];
    const newProfile: GameProfile = {
        ...profile,
        isCompetitionProfile: profile.isCompetitionProfile as boolean ?? false,
        keysConfig: {
            inputMode: profile.keysConfig?.inputMode as Platform ?? Platform.XINPUT,
            socdMode: profile.keysConfig?.socdMode as GameSocdMode ?? GameSocdMode.SOCD_MODE_UP_PRIORITY,
            invertXAxis: profile.keysConfig?.invertXAxis as boolean ?? false,
            invertYAxis: profile.keysConfig?.invertYAxis as boolean ?? false,
            fourWayMode: profile.keysConfig?.fourWayMode as boolean ?? false,
            keyMapping: profile.keysConfig?.keyMapping as { [key in GameControllerButton]?: number[] } ?? {},
            keyCombinations: profile.keysConfig?.keyCombinations as KeyCombination[] ?? [],
            keysEnableTag: profile.keysConfig?.keysEnableTag as boolean[] ?? [],
            macros,
        },
        ledsConfigs: {
            ledEnabled: profile.ledsConfigs?.ledEnabled as boolean ?? false,
            ledsEffectStyle: profile.ledsConfigs?.ledsEffectStyle as LedsEffectStyle ?? LedsEffectStyle.STATIC,
            ledColors: profile.ledsConfigs?.ledColors as string[] ?? ["#000000", "#000000", "#000000"],
            ledBrightness: profile.ledsConfigs?.ledBrightness as number ?? 100,
            ledAnimationSpeed: profile.ledsConfigs?.ledAnimationSpeed as number ?? 1,
            // 环绕灯配置
            aroundLedEnabled: profile.ledsConfigs?.aroundLedEnabled as boolean ?? false,
            aroundLedSyncToMainLed: profile.ledsConfigs?.aroundLedSyncToMainLed as boolean ?? false,
            aroundLedTriggerByButton: profile.ledsConfigs?.aroundLedTriggerByButton as boolean ?? false,
            aroundLedEffectStyle: profile.ledsConfigs?.aroundLedEffectStyle as AroundLedsEffectStyle ?? AroundLedsEffectStyle.STATIC,
            aroundLedColors: profile.ledsConfigs?.aroundLedColors as string[] ?? ["#000000", "#000000", "#000000"],
            aroundLedBrightness: profile.ledsConfigs?.aroundLedBrightness as number ?? 100,
            aroundLedAnimationSpeed: profile.ledsConfigs?.aroundLedAnimationSpeed as number ?? 1,
        },
        hotkeys: profile.hotkeys as Hotkey[] ?? [],
        triggerConfigs: profile.triggerConfigs ?? {},
    }
    return newProfile;
}

export const processResponse = async (response: Response, setError: (error: string | null) => void) => {
    if (!response.ok) {
        setError(response.statusText);
        return;
    }
    const data = await response.json();
    if (data.errNo) {
        setError(data.errorMessage);
        return;
    }
    return data.data;
}

export type DeviceRequestSender = (
    command: string,
    params?: Record<string, unknown>,
    immediate?: boolean,
    options?: DeviceRequestOptions,
) => Promise<any>;
