'use client';

import React, { createContext, useContext, useState, useEffect, useRef, useMemo, useCallback } from 'react';
import {
    GameProfile,
    KeyCombination,
    LedsEffectStyle,
    AroundLedsEffectStyle,
    Platform, GameSocdMode,
    GameControllerButton, Hotkey, GameProfileList, GlobalConfig,
    XInputButtonMap, PS4ButtonMap, SwitchButtonMap,
    MacroConfig,
    MAX_NUM_MACROS,
    MAX_MACRO_STEPS,
    ScreenControlConfig,
    DEFAULT_SCREEN_CONTROL_CONFIG
} from '@/types/gamepad-config';
import { StepInfo, ADCValuesMapping } from '@/types/adc';
import {
    ButtonStates,
    CalibrationStatus,
    DeviceFirmwareInfo,
    FirmwareUpgradeConfig,
    FirmwareUpgradeSession,
    FirmwarePackage,
    FirmwareUpdateCheckResponse,
    LEDsConfig,
    FirmwarePackageDownloadProgress,
    FirmwareUpdateCheckRequest
} from '@/types/types';

export interface HitboxLayoutItem {
    x: number;
    y: number;
    r: number;
}

import {
    DEFAULT_FIRMWARE_PACKAGE_CHUNK_SIZE,
    DEFAULT_FIRMWARE_UPGRADE_TIMEOUT
} from '@/types/gamepad-config';

import {
    configuredTransportMode,
    createDeviceCommandClient,
    DEFAULT_DEVICE_TRANSPORT_CONFIG,
    DeviceCommandMessage,
    DeviceSession,
    DeviceCommandClient,
    DeviceConnectionError,
    DeviceConnectionPhase,
    DeviceImageCatalog,
    DeviceImageTarget,
    DeviceImageUploadRequest,
    DeviceRequestOptions,
    DeviceResponse,
    DeviceTransportConfig,
    DeviceTransportState,
    PerformanceTelemetryController,
    PostReadyRequestScheduler,
    registerDevicePageLifecycle,
    registerDeviceVisibilityLifecycle,
    scheduleInitialDeviceAutoConnect,
    WEBHID_FIRMWARE_CHUNK_DATA_SIZE,
} from '@/lib/device-transport';

// 导入事件总线
import { eventBus, EVENTS } from '@/lib/event-manager';
import { BUTTON_STATE_CHANGED_CMD, type ButtonStateBinaryData } from '@/lib/button-binary-parser';
import {
    SharedButtonMonitorLease,
    type SharedButtonMonitorLeaseToken,
} from '@/lib/button-monitor-lifecycle';

// 导入固件工具函数
import { calculateSHA256, extractFirmwarePackage } from '@/lib/firmware-utils';

import { initializeDeviceSession } from '@/lib/device-transport/device-initialization';
import { resolveDefaultFirmwareServerHost } from '@/lib/device-transport/firmware-server-origin';
import {
    abortFirmwareSessionIfSafe,
    FirmwareFinalizationUncertainError,
    sendFirmwareChunkWithoutAmbiguousRetry,
} from '@/lib/device-transport/firmware-upgrade-reliability';

// 固件服务器配置
const FIRMWARE_SERVER_CONFIG = {
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

export type DeviceTransportConfigType = DeviceTransportConfig;

interface GamepadConfigContextType {
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

    profileList: GameProfileList;
    defaultProfile: GameProfile;
    hotkeysConfig: Hotkey[];
    globalConfig: GlobalConfig;
    screenControl: ScreenControlConfig;
    dataIsReady: boolean;
    setUserRebooting: (rebooting: boolean) => void;
    firmwareUpdating: boolean;
    setFirmwareUpdating: (updating: boolean) => void;

    fetchGlobalConfig: () => Promise<void>;
    updateGlobalConfig: (globalConfig: GlobalConfig) => Promise<void>;
    fetchScreenControl: () => Promise<void>;
    updateScreenControl: (screenControl: ScreenControlConfig, immediate?: boolean) => Promise<void>;
    fetchDefaultProfile: () => Promise<void>;
    fetchProfileList: () => Promise<void>;
    fetchHotkeysConfig: () => Promise<void>;
    updateProfileDetails: (profileId: string, profileDetails: GameProfile, immediate?: boolean, showError?: boolean, showLoading?: boolean) => Promise<void>;
    getMacro: (profileId: string, index: number) => Promise<MacroConfig>;
    updateMacro: (profileId: string, macro: MacroConfig) => Promise<MacroConfig>;
    getProfileMacros: (profileId: string) => Promise<MacroConfig[]>;
    updateProfileMacros: (profileId: string, macros: MacroConfig[]) => Promise<MacroConfig[]>;
    resetProfileDetails: () => Promise<void>;
    createProfile: (profileName: string) => Promise<void>;
    deleteProfile: (profileId: string) => Promise<void>;
    switchProfile: (profileId: string) => Promise<void>;
    updateHotkeysConfig: (hotkeysConfig: Hotkey[]) => Promise<void>;
    isLoading: boolean;
    error: string | null;
    setError: (error: string | null) => void;
    rebootSystem: () => Promise<void>;
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
    fetchMappingList: () => Promise<void>;
    fetchDefaultMapping: () => Promise<void>;
    fetchActiveMapping: (id: string) => Promise<void>;
    createMapping: (name: string, length: number, step: number) => Promise<string>;
    deleteMapping: (id: string) => Promise<void>;
    updateDefaultMapping: (id: string) => Promise<void>;
    startMarking: (id: string) => Promise<void>;
    stopMarking: () => Promise<void>;
    stepMarking: () => Promise<void>;
    fetchMarkingStatus: () => Promise<void>;
    renameMapping: (id: string, name: string) => Promise<void>;
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
    exportAllConfig: () => Promise<any>;

    // 导入所有配置
    importAllConfig: (configData: any) => Promise<void>;

    // 获取Hitbox布局
    getHitboxLayout: () => Promise<HitboxLayoutItem[]>;
    hitboxLayout: HitboxLayoutItem[];
}

function makeEmptyMarkingStatus(): StepInfo {
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

function makeEmptyCalibrationStatus(): CalibrationStatus {
    return {
        isActive: false,
        uncalibratedCount: 0,
        activeCalibrationCount: 0,
        allCalibrated: false,
        buttons: [],
    };
}

function calibrationStatusFrom(value: unknown): CalibrationStatus | null {
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

const GamepadConfigContext = createContext<GamepadConfigContextType | undefined>(undefined);

/**
 * convert profile details
 * @param profile - GameProfile
 * @returns 
 */
const converProfileDetails = (profile: any) => {
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
            hasAroundLed: profile.ledsConfigs?.hasAroundLed as boolean ?? false,
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

/**
 * process response
 * @param response - Response
 * @returns 
 */
const processResponse = async (response: Response, setError: (error: string | null) => void) => {
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

/**
 * GamepadConfigProvider
 * @param children - React.ReactNode
 * @returns 
 */
export function GamepadConfigProvider({ children }: { children: React.ReactNode }) {
    const [globalConfig, setGlobalConfig] = useState<GlobalConfig>({ inputMode: Platform.XINPUT });
    const globalConfigRef = useRef<GlobalConfig>({ inputMode: Platform.XINPUT });
    const confirmedGlobalConfigRef = useRef<GlobalConfig>({ inputMode: Platform.XINPUT });
    const [screenControl, setScreenControl] = useState<ScreenControlConfig>(DEFAULT_SCREEN_CONTROL_CONFIG);
    const screenControlRef = useRef<ScreenControlConfig>(DEFAULT_SCREEN_CONTROL_CONFIG);
    const confirmedScreenControlRef = useRef<ScreenControlConfig>(DEFAULT_SCREEN_CONTROL_CONFIG);
    const [profileList, setProfileList] = useState<GameProfileList>({ defaultId: "", maxNumProfiles: 0, items: [] });
    const [defaultProfile, setDefaultProfile] = useState<GameProfile>({ id: "", name: "" });
    const [operationLoading, setIsLoading] = useState(false);
    const [error, setError] = useState<string | null>(null);
    const [hotkeysConfig, setHotkeysConfig] = useState<Hotkey[]>([]);
    const [jsReady, setJsReady] = useState(false);

    useEffect(() => {
        globalConfigRef.current = globalConfig;
    }, [globalConfig]);

    useEffect(() => {
        screenControlRef.current = screenControl;
    }, [screenControl]);

    // WebHID connection lifecycle
    const [deviceConnected, setDeviceConnected] = useState(false);
    const [deviceState, setDeviceState] = useState<DeviceTransportState>(DeviceTransportState.DISCONNECTED);
    const [devicePhase, setDevicePhase] = useState<DeviceConnectionPhase>(DeviceConnectionPhase.IDLE);
    const [deviceError, setDeviceError] = useState<DeviceConnectionError | null>(null);
    const [deviceClient, setDeviceClient] = useState<DeviceCommandClient | null>(null);
    const deviceClientRef = useRef<DeviceCommandClient | null>(null);
    const [deviceSession, setDeviceSession] = useState<DeviceSession | null>(null);
    const [showReconnect, setShowReconnect] = useState(false);

    const [defaultMappingId, setDefaultMappingId] = useState<string>("");
    const [mappingList, setMappingList] = useState<{ id: string, name: string }[]>([]);
    const [markingStatus, setMarkingStatus] = useState<StepInfo>(makeEmptyMarkingStatus);
    const [activeMapping, setActiveMapping] = useState<ADCValuesMapping | null>(null);
    const [calibrationStatus, setCalibrationStatus] = useState<CalibrationStatus>(makeEmptyCalibrationStatus);
    const [buttonMonitoringActive, setButtonMonitoringActive] = useState<boolean>(false);
    const [firmwareInfo, setFirmwareInfo] = useState<DeviceFirmwareInfo | null>(null);
    const [firmwareUpdateInfo, setFirmwareUpdateInfo] = useState<FirmwareUpdateCheckResponse | null>(null);
    const [firmwareServerHost, _setFirmwareServerHostState] = useState<string>(FIRMWARE_SERVER_CONFIG.defaultHost);
    const [upgradeSession, setUpgradeSession] = useState<FirmwareUpgradeSession | null>(null);
    const [upgradeConfig, setUpgradeConfigState] = useState<FirmwareUpgradeConfig>({
        chunkSize: DEFAULT_FIRMWARE_PACKAGE_CHUNK_SIZE, // 4KB默认分片大小
        timeout: DEFAULT_FIRMWARE_UPGRADE_TIMEOUT // 30秒超时
    });

    const [transportConfig, setTransportConfig] = useState<DeviceTransportConfigType>(DEFAULT_DEVICE_TRANSPORT_CONFIG);

    const contextJsReady = useMemo(() => jsReady, [jsReady]);
    const connectionLoading = devicePhase !== DeviceConnectionPhase.IDLE
        && devicePhase !== DeviceConnectionPhase.READY
        && devicePhase !== DeviceConnectionPhase.ERROR;
    const isLoading = connectionLoading || operationLoading;

    const [dataIsReady, setDataIsReady] = useState(false);
    const [userRebooting, setUserRebooting] = useState(false); // 是否是用户手动重启
    const [firmwareUpdating, setFirmwareUpdating] = useState(false); // 是否正在固件升级

    const [finishConfigDisabled, setFinishConfigDisabled] = useState(false);

    const [hitboxLayout, setHitboxLayout] = useState<HitboxLayoutItem[]>([]);
    const initializationGenerationRef = useRef(0);
    const postReadyRequestSchedulerRef = useRef<PostReadyRequestScheduler | null>(null);
    if (!postReadyRequestSchedulerRef.current) {
        postReadyRequestSchedulerRef.current = new PostReadyRequestScheduler();
    }
    const calibrationCompletionRequestRef = useRef<{
        generation: number;
        promise: Promise<boolean>;
    } | null>(null);
    const buttonMonitorLeaseRef = useRef<SharedButtonMonitorLease | null>(null);
    if (!buttonMonitorLeaseRef.current) {
        buttonMonitorLeaseRef.current = new SharedButtonMonitorLease();
    }

    const resetDeviceSessionState = useCallback(() => {
        postReadyRequestSchedulerRef.current?.endSession();
        buttonMonitorLeaseRef.current?.endSession();
        calibrationCompletionRequestRef.current = null;
        initializationGenerationRef.current += 1;
        const resetGlobalConfig = { inputMode: Platform.XINPUT };
        globalConfigRef.current = resetGlobalConfig;
        confirmedGlobalConfigRef.current = resetGlobalConfig;
        setGlobalConfig(resetGlobalConfig);
        screenControlRef.current = DEFAULT_SCREEN_CONTROL_CONFIG;
        confirmedScreenControlRef.current = DEFAULT_SCREEN_CONTROL_CONFIG;
        setScreenControl(DEFAULT_SCREEN_CONTROL_CONFIG);
        setProfileList({ defaultId: "", maxNumProfiles: 0, items: [] });
        setDefaultProfile({ id: "", name: "" });
        setHotkeysConfig([]);
        setDefaultMappingId("");
        setMappingList([]);
        setMarkingStatus(makeEmptyMarkingStatus());
        setActiveMapping(null);
        setCalibrationStatus(makeEmptyCalibrationStatus());
        setButtonMonitoringActive(false);
        setFirmwareInfo(null);
        setFirmwareUpdateInfo(null);
        setUpgradeSession(null);
        setHitboxLayout([]);
        setDataIsReady(false);
        setFinishConfigDisabled(false);
        setDeviceSession(null);
    }, []);


    // 处理通知消息
    const handleNotificationMessage = (message: DeviceCommandMessage): void => {
        const { command, data } = message;

        switch (command) {
            case 'calibration_update':
                {
                    const status = calibrationStatusFrom(data?.calibrationStatus);
                    if (status) setCalibrationStatus(status);
                }
                eventBus.emit(EVENTS.CALIBRATION_UPDATE, data);
                break;
            case 'marking_status_update':
                if (data?.status) {
                    setMarkingStatus(data.status as StepInfo);
                }
                eventBus.emit(EVENTS.MARKING_STATUS_UPDATE, data);
                break;
            case 'button.state': {
                const payload = data ?? {};
                const buttonState: ButtonStateBinaryData = {
                    command: BUTTON_STATE_CHANGED_CMD,
                    isActive: payload.isActive === true,
                    triggerMask: Number(payload.triggerMask ?? 0) >>> 0,
                    totalButtons: Number(payload.totalButtons ?? 0) & 0xff,
                };
                eventBus.emit(EVENTS.BUTTON_STATE_CHANGED, buttonState);
                break;
            }
            case 'performance.sample':
            case 'performance.edge':
            case 'performance.checkpoint':
            case 'transport.sequence-gap':
                // PerformanceTelemetryController consumes these typed events.
                break;
            default:
                console.log('收到未知通知消息:', message);
        }
    };

    const performanceTelemetryRef = useRef<PerformanceTelemetryController | null>(null);
    // Refs are tied to the concrete client so StrictMode can dispose its first
    // instance without suppressing the replacement instance's one auto-open.
    const initialAutoConnectClientRef = useRef<DeviceCommandClient | null>(null);
    const initialAutoConnectCancelRef = useRef<(() => void) | null>(null);
    const foregroundReconnectCancelRef = useRef<(() => void) | null>(null);
    const pageHiddenRef = useRef(
        typeof document !== 'undefined' && document.visibilityState === 'hidden',
    );

    const cancelPendingAutomaticConnects = useCallback(() => {
        initialAutoConnectCancelRef.current?.();
        initialAutoConnectCancelRef.current = null;
        foregroundReconnectCancelRef.current?.();
        foregroundReconnectCancelRef.current = null;
    }, []);

    // Hosted builds use WebHID exclusively; mock mode is explicit and offline.
    useEffect(() => {
        const client = createDeviceCommandClient({
            mode: configuredTransportMode(),
            config: transportConfig,
        });
        deviceClientRef.current = client;

        const unsubscribeState = client.onStateChange((state) => {
            setDeviceState(state);
            setDeviceConnected(state === DeviceTransportState.CONNECTED);
            setDeviceSession(
                state === DeviceTransportState.CONNECTED
                    ? client.transport.session
                    : null,
            );
        });
        const unsubscribePhase = client.onPhaseChange(setDevicePhase);
        const unsubscribeError = client.onError((error) => {
            setDeviceError(error);
            console.error(
                '设备传输错误:',
                `type=${error.type}`,
                `transport=${error.transportCode ?? 'none'}`,
                `phase=${error.phase ?? 'none'}`,
                `command=${error.command ?? 'none'}`,
                error.message,
            );
        });
        const unsubscribeMessage = client.onMessage(handleNotificationMessage);
        const unsubscribeDisconnect = client.onDisconnect(() => {
            console.log('设备传输连接断开，触发全局断开事件');
            eventBus.emit(EVENTS.DEVICE_DISCONNECTED);
        });

        const performanceTelemetry = new PerformanceTelemetryController(client.transport, {
            requester: {
                request: async <T = Record<string, unknown> | undefined>(
                    command: string,
                    params: Record<string, unknown> = {},
                    options: DeviceRequestOptions = {},
                ): Promise<DeviceResponse<T>> => {
                    const data = await postReadyRequestSchedulerRef.current!.schedule(
                        () => client.request(command, params, options),
                    );
                    return {
                        transactionId: 0,
                        data: data as T,
                    };
                },
            },
        });
        performanceTelemetryRef.current = performanceTelemetry;
        const unsubscribePerformance = performanceTelemetry.subscribe((snapshot) => {
            eventBus.emit(EVENTS.BUTTON_PERFORMANCE_MONITORING, snapshot);
        });
        const restoreForegroundConnection = () => {
            pageHiddenRef.current = false;
            cancelPendingAutomaticConnects();
            if (client.getState() === DeviceTransportState.CONNECTED) {
                performanceTelemetry.startClockSync();
                return;
            }
            initialAutoConnectClientRef.current = client;
            resetDeviceSessionState();
            setShowReconnect(false);
            const cancel = scheduleInitialDeviceAutoConnect(
                client.transport.kind,
                transportConfig.closeTimeoutMs,
                () => {
                    foregroundReconnectCancelRef.current = null;
                    return client.connect(false);
                },
                (error) => {
                    foregroundReconnectCancelRef.current = null;
                    console.error('前台 WebHID 自动重连失败:', error);
                    if (!pageHiddenRef.current) setShowReconnect(true);
                },
            );
            foregroundReconnectCancelRef.current = cancel;
        };
        const removeVisibilityLifecycle = registerDeviceVisibilityLifecycle({
            pauseBackgroundActivity: () => {
                pageHiddenRef.current = true;
                cancelPendingAutomaticConnects();
                setShowReconnect(false);
                // Do not abort a request that has already reached WebHID. Just
                // stop the optional 10-second clock probes from creating new
                // writes while Chromium may throttle or freeze this document.
                performanceTelemetry.pauseClockSync();
            },
            restoreForegroundActivity: restoreForegroundConnection,
        });
        const removePageLifecycle = registerDevicePageLifecycle({
            suspendForBfcache: () => {
                pageHiddenRef.current = true;
                cancelPendingAutomaticConnects();
                // Physical close only. The live JS client is retained so the
                // same bfcache document can explicitly reconnect on pageshow.
                client.disconnect();
            },
            destroyDocument: () => {
                pageHiddenRef.current = true;
                cancelPendingAutomaticConnects();
                client.dispose();
            },
            restoreFromBfcache: () => {
                restoreForegroundConnection();
            },
        });
        setDeviceClient(client);

        return () => {
            if (deviceClientRef.current === client) {
                deviceClientRef.current = null;
            }
            cancelPendingAutomaticConnects();
            removeVisibilityLifecycle();
            removePageLifecycle();
            unsubscribeState();
            unsubscribePhase();
            unsubscribeError();
            unsubscribeMessage();
            unsubscribeDisconnect();
            unsubscribePerformance();
            performanceTelemetry.stop();
            performanceTelemetryRef.current = null;
            client.dispose();
        };
    }, []);

    useEffect(() => {
        const telemetry = performanceTelemetryRef.current;
        if (!telemetry) return;
        if (devicePhase === DeviceConnectionPhase.READY) {
            telemetry.start({ deferClockSync: true });
            postReadyRequestSchedulerRef.current?.releaseInitialBatchWhenIdle(
                () => {
                    if (!pageHiddenRef.current) telemetry.startClockSync();
                },
            );
        } else {
            postReadyRequestSchedulerRef.current?.endSession();
            telemetry.stop();
        }
    }, [devicePhase]);

    // Page load only reopens a browser-authorized HID handle; no chooser.
    useEffect(() => {
        if (pageHiddenRef.current) return;
        if (deviceClient && deviceState === DeviceTransportState.DISCONNECTED) {
            if (initialAutoConnectClientRef.current !== deviceClient) {
                resetDeviceSessionState();
                const cancel = scheduleInitialDeviceAutoConnect(
                    deviceClient.transport.kind,
                    transportConfig.closeTimeoutMs,
                    () => {
                        initialAutoConnectCancelRef.current = null;
                        initialAutoConnectClientRef.current = deviceClient;
                        return deviceClient.connect(false);
                    },
                    (error) => {
                        console.error('首次 WebHID 连接失败:', error);
                        setShowReconnect(true);
                    },
                );
                initialAutoConnectCancelRef.current = cancel;
                return () => {
                    if (initialAutoConnectCancelRef.current === cancel) {
                        initialAutoConnectCancelRef.current = null;
                    }
                    cancel();
                };
            } else {
                if (!userRebooting && !firmwareUpdating) {
                    setShowReconnect(true);
                }
            }
        }
    }, [
        deviceClient,
        deviceState,
        userRebooting,
        firmwareUpdating,
        resetDeviceSessionState,
        transportConfig.closeTimeoutMs,
    ]);

    useEffect(() => {
        if (deviceState === DeviceTransportState.DISCONNECTED || deviceState === DeviceTransportState.ERROR) {
            resetDeviceSessionState();
        }
    }, [deviceState, resetDeviceSessionState]);

    // Read the six startup resources sequentially under one 30 second deadline.
    useEffect(() => {
        if (deviceClient && deviceConnected && deviceState === DeviceTransportState.CONNECTED && !dataIsReady) {
            const client = deviceClient;
            const controller = new AbortController();
            setShowReconnect(false);
            const generation = initializationGenerationRef.current;
            void initializeDeviceSession({
                loaders: {
                    globalConfig: fetchGlobalConfig,
                    screenControl: fetchScreenControl,
                    profileList: fetchProfileList,
                    hotkeys: fetchHotkeysConfig,
                    firmwareMetadata: fetchFirmwareMetadata,
                    hitboxLayout: getHitboxLayout,
                },
                isCurrent: () => generation === initializationGenerationRef.current,
                signal: controller.signal,
                timeoutMs: transportConfig.startupTimeoutMs,
                deadlineAtMs: client.getStartupDeadlineMs() ?? undefined,
                onStage: (stage, status) => {
                    if (status === 'started') client.setInitializationStage(stage);
                },
                onReady: (layout) => {
                    if (!client.markReady()) return;
                    postReadyRequestSchedulerRef.current?.beginSession();
                    buttonMonitorLeaseRef.current?.beginSession();
                    setHitboxLayout(layout);
                    setDataIsReady(true);
                },
                onFailure: (initializationError) => {
                    const message = initializationError instanceof Error
                        ? initializationError.message
                        : String(initializationError);
                    setError(`设备数据初始化失败: ${message}`);
                    setShowReconnect(true);
                    client.disconnect();
                },
            });
            return () => controller.abort();
        }
    }, [deviceConnected, deviceState, dataIsReady, deviceClient]);

    // useEffect(() => {
    //     if (profileList.defaultId !== "") {
    //         fetchDefaultProfile();
    //     }
    // }, [profileList]);

    const setContextJsReady = (ready: boolean) => {
        setJsReady(ready);
    };

    const connectDevice = useCallback(async (): Promise<void> => {
        if (deviceClient) {
            // 该入口由用户点击连接按钮触发，允许打开 WebHID chooser。
            cancelPendingAutomaticConnects();
            initialAutoConnectClientRef.current = deviceClient;
            resetDeviceSessionState();
            setError(null);
            setDeviceError(null);
            return deviceClient.connect(true);
        }
        throw new Error('设备传输层未初始化');
    }, [deviceClient, resetDeviceSessionState, cancelPendingAutomaticConnects]);

    const reconnectDevice = useCallback(async (): Promise<void> => {
        if (deviceClient) {
            // 程序化重连只能使用浏览器已授权的设备，不能打开 chooser。
            cancelPendingAutomaticConnects();
            initialAutoConnectClientRef.current = deviceClient;
            resetDeviceSessionState();
            setError(null);
            setDeviceError(null);
            return deviceClient.connect(false);
        }
        throw new Error('设备传输层未初始化');
    }, [deviceClient, resetDeviceSessionState, cancelPendingAutomaticConnects]);

    const disconnectDevice = (): void => {
        if (deviceClient) {
            deviceClient.disconnect();
        }
    };

    const getDeviceImageCatalog = useCallback(async (): Promise<DeviceImageCatalog> => {
        if (!deviceClient) throw new Error('设备命令客户端未初始化');
        return deviceClient.getImageCatalog();
    }, [deviceClient]);

    const readDeviceImage = useCallback(async (
        target: DeviceImageTarget,
        totalSize: number,
    ): Promise<Uint8Array> => {
        if (!deviceClient) throw new Error('设备命令客户端未初始化');
        return deviceClient.readImage(target, totalSize);
    }, [deviceClient]);

    const uploadDeviceImage = useCallback(async (
        request: DeviceImageUploadRequest,
    ): Promise<void> => {
        if (!deviceClient) throw new Error('设备命令客户端未初始化');
        const result = await deviceClient.uploadImage(request);
        if (!result.success) {
            throw new Error(`Device rejected image upload: ${result.error || 'Unknown error'}`);
        }
    }, [deviceClient]);

    const deleteDeviceImage = useCallback(async (): Promise<void> => {
        if (!deviceClient) throw new Error('设备命令客户端未初始化');
        const result = await deviceClient.deleteImage();
        if (!result.success) {
            throw new Error(`Device rejected image deletion: ${result.error || 'Unknown error'}`);
        }
    }, [deviceClient]);

    /**
     * Send a device RPC. Only explicit preview writes use the debounce queue.
     * @param command 命令
     * @param params 参数
     * @param immediate 是否立即发送，忽略延迟 true: 立即发送 false: 延迟发送
     * @returns 
     */
    const sendDeviceRequest = async (command: string, params: Record<string, unknown> = {}, immediate: boolean = false): Promise<any> => {
        if (!deviceClient) {
            return Promise.reject(new Error('设备命令客户端未初始化'));
        }
        // Read the concrete client's synchronous state. React state is only a
        // rendering projection and can remain CONNECTED for one render after a
        // physical disconnect or failed native write.
        if (deviceClient.getState() !== DeviceTransportState.CONNECTED) {
            throw new Error('设备尚未连接');
        }

        try {
            // DeviceCommandClient owns the only RPC lane and the centralized
            // command policy decides whether this operation is coalesced.
            return await deviceClient.enqueue(command, params, immediate);
        } catch (error) {
            if (error instanceof Error) {
                throw error;
            }
            throw new Error(`设备请求失败: ${error}`);
        }
    };

    const fetchDefaultProfile = async (immediate: boolean = true): Promise<void> => {
        try {
            // setIsLoading(true);
            const data = await sendDeviceRequest('get_default_profile', {}, immediate);
            if (data && 'defaultProfileDetails' in data) {
                setDefaultProfile(converProfileDetails(data.defaultProfileDetails) ?? {});
            }
            return Promise.resolve();
        } catch (err) {
            // setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch default profile"));
        } finally {
            // setIsLoading(false);
        }
    };

    const fetchProfileList = async (immediate: boolean = true): Promise<void> => {
        try {
            // setIsLoading(true);
            const data = await sendDeviceRequest('get_profile_list', {}, immediate);
            if (data && 'profileList' in data) {
                setProfileList(data.profileList as GameProfileList);
            }

            if (data && 'defaultProfileDetails' in data) {
                setDefaultProfile(converProfileDetails(data.defaultProfileDetails) ?? {});
            }

            return Promise.resolve();
        } catch (err) {
            // setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch profile list"));
        } finally {
            // setIsLoading(false);
        }
    };

    const fetchHotkeysConfig = async (immediate: boolean = true): Promise<void> => {
        try {
            // setIsLoading(true);
            const data = await sendDeviceRequest('get_hotkeys_config', {}, immediate);
            if (data && 'hotkeysConfig' in data) {
                setHotkeysConfig(data.hotkeysConfig as Hotkey[]);
            }
            return Promise.resolve();
        } catch (err) {
            // setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch hotkeys config"));
        } finally {
            // setIsLoading(false);
        }
    };

    const updateProfileDetails = async (profileId: string, profileDetails: GameProfile, immediate: boolean = false, showError: boolean = false, showLoading: boolean = false): Promise<void> => {
        try {
            if (showLoading) {
                setIsLoading(true);
            }
            const profileDetailsNoMacros: GameProfile = {
                ...profileDetails,
                keysConfig: profileDetails.keysConfig
                    ? { ...profileDetails.keysConfig, macros: undefined }
                    : undefined,
            };
            const data = await sendDeviceRequest('update_profile', { profileId, profileDetails: profileDetailsNoMacros }, immediate);

            // 如果更新的是 profile 的 name， 或者更新的profile不是defaultProfile，则需要重新获取 profile list
            if (profileDetails.name != undefined && profileDetails.name !== defaultProfile.name || profileDetails.id !== defaultProfile.id) {
                void fetchProfileList().catch(() => undefined);
            } else if (data && 'defaultProfileDetails' in data) {
                // 否则更新 default profile
                const nextDefault = converProfileDetails(data.defaultProfileDetails) ?? {};
                setDefaultProfile(nextDefault);
                setProfileList((prev) => ({
                    ...prev,
                    items: prev.items.map((p) => (
                        p.id === nextDefault.id
                            ? { ...p, name: nextDefault.name, isCompetitionProfile: nextDefault.isCompetitionProfile }
                            : p
                    ))
                }));
            }
            setError(null);
            return Promise.resolve();
        } catch (err) {
            if (showError) {
                setError(err instanceof Error ? err.message : 'An error occurred');
            }
            return Promise.reject(
                err instanceof Error ? err : new Error('Failed to update profile details'),
            );
        } finally {
            if (showLoading) {
                setIsLoading(false);
            }
        }
    };

    const decodeBase64ToBytes = (b64: string): Uint8Array => {
        const bin = atob(b64);
        const bytes = new Uint8Array(bin.length);
        for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i) & 0xff;
        return bytes;
    };

    const encodeBytesToBase64 = (bytes: Uint8Array): string => {
        let bin = "";
        for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
        return btoa(bin);
    };

    const decodeMacroData = (index: number, b64: string): MacroConfig => {
        const bytes = decodeBase64ToBytes(b64);
        if (bytes.length < 2) throw new Error("Invalid macro data");
        let off = 0;
        const numTriggerKeys = bytes[off++];
        if (off + numTriggerKeys + 1 > bytes.length) throw new Error("Invalid macro data");
        const triggerKeys: number[] = [];
        for (let i = 0; i < numTriggerKeys; i++) triggerKeys.push(bytes[off++]);
        const numSteps = bytes[off++];
        const expectedV2 = 1 + numTriggerKeys + 1 + numSteps * 10;
        const expectedV1 = 1 + numTriggerKeys + 1 + numSteps * 6;
        const isV2 = bytes.length === expectedV2;
        if (!isV2 && bytes.length !== expectedV1) throw new Error("Invalid macro data");

        const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        const steps: { timeMs: number; buttonMask: number; dynamicMask: number }[] = [];
        const stepsToRead = Math.min(numSteps, MAX_MACRO_STEPS);
        for (let i = 0; i < numSteps; i++) {
            const timeMs = view.getUint16(off, true); off += 2;
            const buttonMask = view.getUint32(off, true); off += 4;
            const dynamicMask = isV2 ? view.getUint32(off, true) : 0; off += isV2 ? 4 : 0;
            if (i < stepsToRead) steps.push({ timeMs, buttonMask, dynamicMask });
        }
        return { index, triggerKeys: triggerKeys.slice(0, 4), steps };
    };

    const encodeMacroData = (macro: MacroConfig): string => {
        const triggerKeys = (macro.triggerKeys ?? []).slice(0, 4).map(v => Math.max(0, Math.min(255, v | 0)));
        const steps = (macro.steps ?? []).slice(0, MAX_MACRO_STEPS);
        const len = 1 + triggerKeys.length + 1 + steps.length * 10;
        const bytes = new Uint8Array(len);
        let off = 0;
        bytes[off++] = triggerKeys.length;
        for (const k of triggerKeys) bytes[off++] = k;
        bytes[off++] = steps.length;
        const view = new DataView(bytes.buffer);
        for (const s of steps) {
            view.setUint16(off, Math.max(0, Math.min(65535, (s.timeMs ?? 0) | 0)), true); off += 2;
            view.setUint32(off, (s.buttonMask ?? 0) >>> 0, true); off += 4;
            view.setUint32(off, (s.dynamicMask ?? 0) >>> 0, true); off += 4;
        }
        return encodeBytesToBase64(bytes);
    };

    const decodeProfileMacrosData = (b64: string): MacroConfig[] => {
        const bytes = decodeBase64ToBytes(b64);
        if (bytes.length < 2) throw new Error("Invalid macros data");
        let off = 0;
        const version = bytes[off++];
        const count = bytes[off++];
        if (version !== 1) throw new Error("Invalid macros data");
        if (count !== MAX_NUM_MACROS) throw new Error("Invalid macros data");
        const bodyLen = bytes.length - 2;
        if (bodyLen % count !== 0) throw new Error("Invalid macros data");
        const perMacro = bodyLen / count;
        const headerLen = 1 + 1 + 4 + 1;
        if (perMacro < headerLen) throw new Error("Invalid macros data");
        const stepsBytes = perMacro - headerLen;
        const isV2 = stepsBytes % 10 === 0;
        if (!isV2 && stepsBytes % 6 !== 0) throw new Error("Invalid macros data");
        const stepsPerMacro = isV2 ? (stepsBytes / 10) : (stepsBytes / 6);

        const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        const macros: MacroConfig[] = [];
        for (let i = 0; i < count; i++) {
            const numSteps = bytes[off++];
            const numTriggerKeys = bytes[off++];
            const triggerKeysRaw: number[] = [];
            for (let k = 0; k < 4; k++) triggerKeysRaw.push(bytes[off++]);
            off++;

            const triggerKeys = triggerKeysRaw.slice(0, Math.min(4, numTriggerKeys));
            const steps: { timeMs: number; buttonMask: number; dynamicMask: number }[] = [];
            const stepsToRead = Math.min(numSteps, MAX_MACRO_STEPS);
            for (let s = 0; s < stepsPerMacro; s++) {
                const timeMs = view.getUint16(off, true); off += 2;
                const buttonMask = view.getUint32(off, true); off += 4;
                const dynamicMask = isV2 ? view.getUint32(off, true) : 0; off += isV2 ? 4 : 0;
                if (s < stepsToRead) steps.push({ timeMs, buttonMask, dynamicMask });
            }

            if (triggerKeys.length > 0 || steps.length > 0) {
                macros.push({ index: i, triggerKeys, steps });
            }
        }
        return macros;
    };

    const encodeProfileMacrosData = (macrosList: MacroConfig[]): string => {
        const macrosByIndex = new Map<number, MacroConfig>();
        for (const m of macrosList ?? []) {
            if (typeof m?.index !== "number") continue;
            if (m.index < 0 || m.index >= MAX_NUM_MACROS) continue;
            macrosByIndex.set(m.index, m);
        }

        const count = MAX_NUM_MACROS;
        const perMacro = 1 + 1 + 4 + 1 + MAX_MACRO_STEPS * 10;
        const bytes = new Uint8Array(2 + count * perMacro);
        let off = 0;
        bytes[off++] = 1;
        bytes[off++] = count;
        const view = new DataView(bytes.buffer);

        for (let i = 0; i < count; i++) {
            const m = macrosByIndex.get(i) ?? { index: i, triggerKeys: [], steps: [] };
            const triggerKeys = (m.triggerKeys ?? []).slice(0, 4).map(v => Math.max(0, Math.min(255, v | 0)));
            const steps = (m.steps ?? []).slice(0, MAX_MACRO_STEPS);

            bytes[off++] = steps.length;
            bytes[off++] = triggerKeys.length;
            for (let k = 0; k < 4; k++) bytes[off++] = triggerKeys[k] ?? 0;
            bytes[off++] = 0;

            for (let s = 0; s < MAX_MACRO_STEPS; s++) {
                const step = steps[s] ?? { timeMs: 0, buttonMask: 0, dynamicMask: 0 };
                view.setUint16(off, Math.max(0, Math.min(65535, (step.timeMs ?? 0) | 0)), true); off += 2;
                view.setUint32(off, (step.buttonMask ?? 0) >>> 0, true); off += 4;
                view.setUint32(off, (step.dynamicMask ?? 0) >>> 0, true); off += 4;
            }
        }
        return encodeBytesToBase64(bytes);
    };

    const getMacro = async (profileId: string, index: number): Promise<MacroConfig> => {
        const data = await sendDeviceRequest('get_macro', { profileId, index }, true);
        const macroObj = (data?.macro ?? null) as { index: number; data: string } | null;
        if (!macroObj || typeof macroObj.data !== "string") throw new Error("Invalid macro response");
        return decodeMacroData(index, macroObj.data);
    };

    const updateMacro = async (profileId: string, macro: MacroConfig): Promise<MacroConfig> => {
        const payload = { index: macro.index, data: encodeMacroData(macro) };
        const data = await sendDeviceRequest('update_macro', { profileId, macro: payload }, true);
        const macroObj = (data?.macro ?? null) as { index: number; data: string } | null;
        if (!macroObj || typeof macroObj.data !== "string") throw new Error("Invalid macro response");
        return decodeMacroData(macro.index, macroObj.data);
    };

    const getProfileMacros = async (profileId: string): Promise<MacroConfig[]> => {
        const data = await sendDeviceRequest('get_profile_macros', { pid: profileId }, true);
        const raw = ((data as any)?.m ?? (data as any)?.data?.m ?? (data as any)?.macros ?? null) as unknown;
        const macrosJSON = Array.isArray(raw)
            ? raw
            : (raw && typeof raw === "object")
                ? Array.from({ length: MAX_NUM_MACROS }).map((_, i) => (raw as any)[i])
                : null;
        if (Array.isArray(macrosJSON)) {
            const macros: MacroConfig[] = [];
            for (let i = 0; i < MAX_NUM_MACROS; i++) {
                const item = (macrosJSON as any[])[i];
                if (!item) continue;
                const obj = item as any;
                const triggerKeys = Array.isArray(obj.k)
                    ? obj.k.map((x: any) => Number(x)).filter((x: any) => Number.isFinite(x)).slice(0, 4)
                    : [];
                const steps = Array.isArray(obj.s)
                    ? obj.s
                        .filter((x: any) => Array.isArray(x) && x.length >= 2)
                        .slice(0, MAX_MACRO_STEPS)
                        .map((x: any[]) => ({
                            timeMs: (Number(x[0]) || 0) | 0,
                            buttonMask: (Number(x[1]) || 0) >>> 0,
                            dynamicMask: (Number(x[2]) || 0) >>> 0,
                        }))
                    : [];
                if (triggerKeys.length > 0 || steps.length > 0) {
                    macros.push({ index: i, triggerKeys, steps });
                }
            }
            return macros;
        }

        const b64 = (data?.data ?? null) as string | null;
        if (!b64) return [];
        return decodeProfileMacrosData(b64);
    };

    const updateProfileMacros = async (profileId: string, macros: MacroConfig[]): Promise<MacroConfig[]> => {
        const macrosByIndex = new Map<number, MacroConfig>();
        for (const m of macros ?? []) {
            if (typeof m?.index !== "number") continue;
            if (m.index < 0 || m.index >= MAX_NUM_MACROS) continue;
            macrosByIndex.set(m.index, m);
        }
        const payload = Array.from({ length: MAX_NUM_MACROS }).map((_, i) => {
            const m = macrosByIndex.get(i);
            if (!m) return null;
            const triggerKeys = (m.triggerKeys ?? []).slice(0, 4).map(v => Math.max(0, Math.min(255, v | 0)));
            const steps = (m.steps ?? [])
                .slice(0, MAX_MACRO_STEPS)
                .map(s => [
                    Math.max(0, Math.min(65535, (s.timeMs ?? 0) | 0)),
                    (s.buttonMask ?? 0) >>> 0,
                    (s.dynamicMask ?? 0) >>> 0,
                ]);
            if (triggerKeys.length === 0 && steps.length === 0) return null;
            return { k: triggerKeys, s: steps };
        });

        const data = await sendDeviceRequest('update_profile_macros', { pid: profileId, m: payload }, true);
        const raw = ((data as any)?.m ?? (data as any)?.data?.m ?? (data as any)?.macros ?? null) as unknown;
        const macrosJSON = Array.isArray(raw)
            ? raw
            : (raw && typeof raw === "object")
                ? Array.from({ length: MAX_NUM_MACROS }).map((_, i) => (raw as any)[i])
                : null;
        if (!Array.isArray(macrosJSON)) return [];

        const updated: MacroConfig[] = [];
        for (let i = 0; i < MAX_NUM_MACROS; i++) {
            const item = (macrosJSON as any[])[i];
            if (!item) continue;
            const obj = item as any;
            const triggerKeys = Array.isArray(obj.k)
                ? obj.k.map((x: any) => Number(x)).filter((x: any) => Number.isFinite(x)).slice(0, 4)
                : [];
            const steps = Array.isArray(obj.s)
                ? obj.s
                    .filter((x: any) => Array.isArray(x) && x.length >= 2)
                    .slice(0, MAX_MACRO_STEPS)
                    .map((x: any[]) => ({
                        timeMs: (Number(x[0]) || 0) | 0,
                        buttonMask: (Number(x[1]) || 0) >>> 0,
                        dynamicMask: (Number(x[2]) || 0) >>> 0,
                    }))
                : [];
            if (triggerKeys.length > 0 || steps.length > 0) {
                updated.push({ index: i, triggerKeys, steps });
            }
        }
        return updated;
    };

    const resetProfileDetails = async (immediate: boolean = true): Promise<void> => {
        await fetchDefaultProfile();
    };

    // Device logs are fetched through the authenticated HID command session.
    const fetchDeviceLogsList = async (): Promise<string[]> => {
        try {
            const data = await sendDeviceRequest('get_device_logs_list', {}, true);
            const items = (data?.items as string[]) || [];
            return items;
        } catch (err) {
            // 不在上下文里打断 UI，只将错误向上抛出即可
            throw err instanceof Error ? err : new Error('获取设备日志失败');
        }
    };

    const exportAllConfig = async (): Promise<any> => {
        if (!deviceClient) throw new Error('设备命令客户端未初始化');
        return deviceClient.exportConfig();
    };

    const importAllConfig = async (configData: any): Promise<void> => {
        try {
            setIsLoading(true);
            if (!deviceClient) throw new Error('设备命令客户端未初始化');
            await deviceClient.importConfig(configData);
            
            setError(null);
            return Promise.resolve();
        } catch (err) {
            console.error("[Import] Error:", err);
            setError(err instanceof Error ? err.message : 'An error occurred during import');
            return Promise.reject(new Error("Failed to import config"));
        } finally {
            setIsLoading(false);
        }
    };

    const getHitboxLayout = async (immediate: boolean = true): Promise<HitboxLayoutItem[]> => {
        try {
            const data = await sendDeviceRequest('get_hitbox_layout', {}, immediate);
            return Promise.resolve(data as HitboxLayoutItem[]);
        } catch (err) {
            // setError(err instanceof Error ? err.message : '获取Hitbox布局失败');
            return Promise.reject(new Error("Failed to get Hitbox layout"));
        }
    };

    const createProfile = async (profileName: string, immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendDeviceRequest('create_profile', { profileName }, immediate);
            if (data && 'profileList' in data) {
                setProfileList(data.profileList as GameProfileList);
            }
            if (data && 'defaultProfileDetails' in data) {
                setDefaultProfile(converProfileDetails(data.defaultProfileDetails) ?? {});
            }
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to create profile"));
        } finally {
            setIsLoading(false);
        }
    };

    const deleteProfile = async (profileId: string, immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendDeviceRequest('delete_profile', { profileId }, immediate);
            if (data && 'profileList' in data) {
                setProfileList(data.profileList as GameProfileList);
            }
            if (data && 'defaultProfileDetails' in data) {
                setDefaultProfile(converProfileDetails(data.defaultProfileDetails) ?? {});
            }
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to delete profile"));
        } finally {
            setIsLoading(false);
        }
    };

    const switchProfile = async (profileId: string, immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendDeviceRequest('switch_default_profile', { profileId }, immediate);
            if (data && 'profileList' in data) {
                setProfileList(data.profileList as GameProfileList);
            }
            if (data && 'defaultProfileDetails' in data) {
                setDefaultProfile(converProfileDetails(data.defaultProfileDetails) ?? {});
            }
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to switch profile"));
        } finally {
            setIsLoading(false);
        }
    };

    const updateHotkeysConfig = async (hotkeysConfig: Hotkey[], immediate: boolean = false): Promise<void> => {
        try {
            const data = await sendDeviceRequest('update_hotkeys_config', { hotkeysConfig }, immediate);
            if (data) {
                setHotkeysConfig(data.hotkeysConfig as Hotkey[]);
            }
            return Promise.resolve();
        } catch (err) {
            if (deviceState === DeviceTransportState.CONNECTED) {
                await fetchHotkeysConfig(true).catch(() => undefined);
            }
            const error = err instanceof Error ? err : new Error('Failed to update hotkeys config');
            setError(error.message);
            return Promise.reject(error);
        } finally {
        }
    };

    const rebootSystem = async (immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            await sendDeviceRequest('reboot', {}, immediate);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            // setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to reboot system"));
        } finally {
            setIsLoading(false);
        }
    };

    const fetchMappingList = async (immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendDeviceRequest('ms_get_list', {}, immediate);
            if (data && 'mappingList' in data && 'defaultMappingId' in data) {
                setMappingList(data.mappingList as { id: string, name: string }[]);
                setDefaultMappingId(data.defaultMappingId as string);
            }
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch mapping list"));
        } finally {
            setIsLoading(false);
        }
    };

    const fetchDefaultMapping = async (immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendDeviceRequest('ms_get_default', {}, immediate);
            if (data && 'id' in data) {
                setDefaultMappingId(data.id as string ?? "");
            }
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch default mapping"));
        } finally {
            setIsLoading(false);
        }
    };

    const createMapping = async (name: string, length: number, step: number, immediate: boolean = true): Promise<string> => {
        try {
            setIsLoading(true);
            const data = await sendDeviceRequest('ms_create_mapping', { name, length, step }, immediate);
            if (data && 'mappingList' in data && 'defaultMappingId' in data) {
                setMappingList(data.mappingList as { id: string, name: string }[]);
                setDefaultMappingId(data.defaultMappingId as string);
            }
            const createdMappingId = typeof data?.createdMappingId === 'string'
                ? data.createdMappingId
                : '';
            if (!createdMappingId) {
                throw new Error('Device did not return the created mapping id');
            }
            setError(null);
            return createdMappingId;
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to create mapping"));
        } finally {
            setIsLoading(false);
        }
    };

    const deleteMapping = async (id: string, immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendDeviceRequest('ms_delete_mapping', { id }, immediate);
            if (data && 'mappingList' in data && 'defaultMappingId' in data) {
                setMappingList(data.mappingList as { id: string, name: string }[]);
                setDefaultMappingId(data.defaultMappingId as string);
            }
            if (activeMapping?.id === id) {
                setActiveMapping(null);
            }
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to delete mapping"));
        } finally {
            setIsLoading(false);
        }
    };

    const updateDefaultMapping = async (id: string, immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendDeviceRequest('ms_set_default', { id }, immediate);
            setDefaultMappingId(data.id);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to set default mapping"));
        } finally {
            setIsLoading(false);
        }
    };

    const startMarking = async (id: string, immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendDeviceRequest('ms_mark_mapping_start', { id }, immediate);
            if (data.status) {
                setMarkingStatus(data.status);
            }
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to start marking"));
        } finally {
            setIsLoading(false);
        }
    };

    const stopMarking = async (immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendDeviceRequest('ms_mark_mapping_stop', {}, immediate);
            if (data.status) {
                setMarkingStatus(data.status);
            }
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to stop marking"));
        } finally {
            setIsLoading(false);
        }
    };

    const stepMarking = async (immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendDeviceRequest('ms_mark_mapping_step', {}, immediate);
            if (data.status) {
                const status = data.status as StepInfo;
                setMarkingStatus(status);
                if (status.is_completed && status.id) {
                    await fetchActiveMapping(status.id, immediate);
                }
            }
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to step marking"));
        } finally {
            setIsLoading(false);
        }
    };

    const fetchMarkingStatus = async (immediate: boolean = true): Promise<void> => {
        try {
            const data = await sendDeviceRequest('ms_get_mark_status', {}, immediate);
            if (data?.status) {
                setMarkingStatus(data.status as StepInfo);
            }
            setError(null);
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            throw new Error("Failed to fetch marking status");
        }
    };

    const fetchActiveMapping = async (id: string, immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendDeviceRequest('ms_get_mapping', { id }, immediate);
            setActiveMapping(data.mapping);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch mapping"));
        } finally {
            setIsLoading(false);
        }
    };

    const renameMapping = async (id: string, name: string, immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendDeviceRequest('ms_rename_mapping', { id, name }, immediate);
            setMappingList(data.mappingList);
            setDefaultMappingId(data.defaultMappingId);
            setActiveMapping({ ...(activeMapping as ADCValuesMapping), name: name });
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to rename mapping"));
        } finally {
            setIsLoading(false);
        }
    };

    const fetchGlobalConfig = async (immediate: boolean = true): Promise<void> => {
        try {
            // setIsLoading(true);
            const data = await sendDeviceRequest('get_global_config', {}, immediate);
            console.log('fetchGlobalConfig', data);
            const next = data.globalConfig as GlobalConfig;
            globalConfigRef.current = next;
            confirmedGlobalConfigRef.current = next;
            setGlobalConfig(next);
            return Promise.resolve();
        } catch (err) {
            // setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch global config"));
        } finally {
            // setIsLoading(false);
        }
    };

    const updateGlobalConfig = async (nextGlobalConfig: GlobalConfig, immediate: boolean = false): Promise<void> => {
        const merged = { ...globalConfigRef.current, ...nextGlobalConfig };
        globalConfigRef.current = merged;
        setGlobalConfig(merged);
        try {
            const data = await sendDeviceRequest('update_global_config', { globalConfig: merged }, immediate);
            const confirmed = data?.globalConfig && typeof data.globalConfig === 'object'
                ? data.globalConfig as GlobalConfig
                : merged;
            confirmedGlobalConfigRef.current = confirmed;
            if (globalConfigRef.current === merged) {
                globalConfigRef.current = confirmed;
                setGlobalConfig(confirmed);
            }
            return Promise.resolve();
        } catch (err) {
            const error = err instanceof Error ? err : new Error('Failed to update global config');
            if (globalConfigRef.current === merged) {
                const confirmed = confirmedGlobalConfigRef.current;
                globalConfigRef.current = confirmed;
                setGlobalConfig(confirmed);
            }
            setError(error.message);
            return Promise.reject(error);
        }
    };

    const fetchScreenControl = async (immediate: boolean = true): Promise<void> => {
        try {
            const data = await sendDeviceRequest('get_screen_control_config', {}, immediate);
            const remote = data.screenControl ?? {};
            const normalizeFeaturesOrder = (order: unknown): ScreenControlConfig['featuresOrder'] => {
                const fallback = DEFAULT_SCREEN_CONTROL_CONFIG.featuresOrder;
                if (!Array.isArray(order)) return fallback;
                const seen = new Set<string>();
                const next: string[] = [];
                for (const k of order) {
                    if (typeof k !== 'string') continue;
                    if (!(k in DEFAULT_SCREEN_CONTROL_CONFIG.features)) continue;
                    if (seen.has(k)) continue;
                    seen.add(k);
                    next.push(k);
                }
                for (const k of fallback) {
                    if (!seen.has(k)) next.push(k);
                }
                return next as ScreenControlConfig['featuresOrder'];
            };
            const normalizeScreenStyle = (style: unknown): ScreenControlConfig['screenStyle'] => {
                return style === 'light' ? 'light' : 'dark';
            };
            const merged = {
                ...DEFAULT_SCREEN_CONTROL_CONFIG,
                ...remote,
                screenStyle: normalizeScreenStyle(remote.screenStyle),
                features: {
                    ...DEFAULT_SCREEN_CONTROL_CONFIG.features,
                    ...(remote.features ?? {})
                },
                featuresOrder: normalizeFeaturesOrder(remote.featuresOrder),
            };
            screenControlRef.current = merged;
            confirmedScreenControlRef.current = merged;
            setScreenControl(merged);
            return Promise.resolve();
        } catch (err) {
            return Promise.reject(new Error("Failed to fetch screen control config"));
        }
    };

    const updateScreenControl = async (next: ScreenControlConfig, immediate: boolean = false): Promise<void> => {
        screenControlRef.current = next;
        setScreenControl(next);
        try {
            const data = await sendDeviceRequest(
                'update_screen_control_config',
                { screenControl: next },
                immediate,
            );
            const remote = data?.screenControl;
            if (!remote || typeof remote !== 'object' || remote.screenStyle !== next.screenStyle) {
                throw new Error('Device did not confirm the screen control update');
            }

            const confirmed: ScreenControlConfig = {
                ...next,
                ...remote,
                features: {
                    ...next.features,
                    ...(remote.features ?? {}),
                },
            };
            confirmedScreenControlRef.current = confirmed;
            if (screenControlRef.current === next) {
                screenControlRef.current = confirmed;
                setScreenControl(confirmed);
            }
            return Promise.resolve();
        } catch (err) {
            // The controls are optimistic so the page remains responsive, but a
            // transport failure must never look like a saved device setting.
            if (screenControlRef.current === next) {
                const confirmed = confirmedScreenControlRef.current;
                screenControlRef.current = confirmed;
                setScreenControl(confirmed);
            }
            const error = err instanceof Error
                ? err
                : new Error('Failed to update screen control config');
            setError(error.message);
            return Promise.reject(error);
        }
    };

    const fetchCalibrationStatus = async (immediate: boolean = true): Promise<CalibrationStatus> => {
        try {
            const data = await sendDeviceRequest('get_calibration_status', {}, immediate);
            const status = calibrationStatusFrom(data?.calibrationStatus);
            if (!status) throw new Error('Device returned an invalid calibration status');
            setCalibrationStatus(status);
            setError(null);
            return status;
        } catch (err) {
            const error = err instanceof Error ? err : new Error('Failed to fetch calibration status');
            setError(error.message);
            throw error;
        }
    };

    const startManualCalibration = async (immediate: boolean = true): Promise<CalibrationStatus> => {
        try {
            setIsLoading(true);
            const data = await sendDeviceRequest('start_manual_calibration', {}, immediate);
            const status = calibrationStatusFrom(data?.calibrationStatus);
            if (!status) throw new Error('Device returned an invalid calibration status');
            setCalibrationStatus(status);
            setError(null);
            return status;
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to start manual calibration"));
        } finally {
            setIsLoading(false);
        }
    };

    const stopManualCalibration = async (immediate: boolean = true): Promise<CalibrationStatus> => {
        try {
            setIsLoading(true);
            const data = await sendDeviceRequest('stop_manual_calibration', {}, immediate);
            const status = calibrationStatusFrom(data?.calibrationStatus);
            if (!status) throw new Error('Device returned an invalid calibration status');
            setCalibrationStatus(status);
            setError(null);
            return status;
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to stop manual calibration"));
        } finally {
            setIsLoading(false);
        }
    };

    const clearManualCalibrationData = async (immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendDeviceRequest('clear_manual_calibration_data', {}, immediate);
            const status = calibrationStatusFrom(data?.calibrationStatus);
            if (!status) throw new Error('Device returned an invalid calibration status');
            setCalibrationStatus(status);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            // setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to clear manual calibration data"));
        } finally {
            setIsLoading(false);
        }
    };

    const checkIsManualCalibrationCompleted = (immediate: boolean = true): Promise<boolean> => {
        const generation = initializationGenerationRef.current;
        const existing = calibrationCompletionRequestRef.current;
        if (existing?.generation === generation) {
            return existing.promise;
        }

        const promise = postReadyRequestSchedulerRef.current!.schedule(async () => {
            try {
                const data = await sendDeviceRequest(
                    'check_is_manual_calibration_completed',
                    {},
                    immediate,
                );
                return data.isCompleted === true;
            } catch {
                throw new Error("Failed to check if manual calibration is completed");
            }
        });
        calibrationCompletionRequestRef.current = { generation, promise };
        void promise.finally(() => {
            if (calibrationCompletionRequestRef.current?.promise === promise) {
                calibrationCompletionRequestRef.current = null;
            }
        }).catch(() => undefined);
        return promise;
    };

    const startButtonMonitoring = (
        immediate: boolean = true,
    ): Promise<SharedButtonMonitorLeaseToken> => {
        const promise = buttonMonitorLeaseRef.current!.acquire(async () => {
            try {
                const completed = await checkIsManualCalibrationCompleted(immediate);
                if (!completed) {
                    setButtonMonitoringActive(false);
                    throw new Error("Manual calibration not completed");
                }

                const data = await postReadyRequestSchedulerRef.current!.schedule(
                    () => sendDeviceRequest('start_button_monitoring', {}, immediate),
                );
                setButtonMonitoringActive(data.isActive ?? true);
                setError(null);
            } catch {
                throw new Error("Failed to start button monitoring");
            }
        });
        postReadyRequestSchedulerRef.current!.track(promise);
        return promise;
    };

    const stopButtonMonitoring = async (
        lease: SharedButtonMonitorLeaseToken,
        immediate: boolean = true,
    ): Promise<void> => {
        try {
            await buttonMonitorLeaseRef.current!.release(
                lease,
                async () => {
                    const data = await postReadyRequestSchedulerRef.current!.schedule(
                        () => sendDeviceRequest('stop_button_monitoring', {}, immediate),
                    );
                    setButtonMonitoringActive(data.isActive ?? false);
                    setError(null);
                },
            );
        } catch (err) {
            return Promise.reject(new Error("Failed to stop button monitoring"));
        }
    };

    /**
     * @deprecated 已废弃：现在使用服务器推送模式，请监听 EVENTS.BUTTON_STATE_CHANGED 事件
     * 保留此方法仅用于兼容性，推荐使用推送模式获取按键状态变化
     */
    const getButtonStates = async (): Promise<ButtonStates> => {
        setError(null);
        try {
            const data = await sendDeviceRequest('get_button_states');
            return Promise.resolve(data as ButtonStates);
        } catch (error) {
            console.error('获取按键状态失败:', error);
            setError(error instanceof Error ? error.message : '获取按键状态失败');
            throw error;
        }
    };

    // 按键性能监控相关
    const startButtonPerformanceMonitoring = async (immediate: boolean = true): Promise<void> => {
        try {
            const data = await sendDeviceRequest('start_button_performance_monitoring', {}, immediate);
            setButtonMonitoringActive(data.isActive ?? false);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            return Promise.reject(new Error("Failed to start button performance monitoring"));
        }
    };

    const stopButtonPerformanceMonitoring = async (immediate: boolean = true): Promise<void> => {
        try {
            const data = await sendDeviceRequest('stop_button_performance_monitoring', {}, immediate);
            setButtonMonitoringActive(data.isActive ?? false);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            return Promise.reject(new Error("Failed to stop button performance monitoring"));
        }
    };

    // LED 配置相关
    const pushLedsConfig = async (ledsConfig: LEDsConfig, immediate: boolean = false): Promise<void> => {
        setError(null);
        try {
            await sendDeviceRequest('push_leds_config', ledsConfig as unknown as Record<string, unknown>, immediate);
            return Promise.resolve();
        } catch (error) {
            const normalized = error instanceof Error
                ? error
                : new Error("Failed to push LED configuration");
            setError(normalized.message);
            return Promise.reject(normalized);
        }
    };

    const clearLedsPreview = async (immediate: boolean = true): Promise<void> => {
        setError(null);
        try {
            await sendDeviceRequest('clear_leds_preview', {}, immediate);
            return Promise.resolve();
        } catch (error) {
            const normalized = error instanceof Error
                ? error
                : new Error("Failed to clear LED preview");
            setError(normalized.message);
            return Promise.reject(normalized);
        }
    };

    const fetchFirmwareMetadata = async (immediate: boolean = true): Promise<void> => {
        try {
            const data = await sendDeviceRequest('get_firmware_metadata', {}, immediate);
            setFirmwareInfo({
                firmware: data
            });
            return Promise.resolve();
        } catch (err) {
            return Promise.reject(new Error("Failed to fetch firmware metadata"));
        }
    };

    const checkFirmwareUpdate = async (currentVersion: string, customServerHost?: string): Promise<void> => {
        try {
            if (!deviceClient || deviceState !== DeviceTransportState.CONNECTED) {
                throw new Error('设备传输尚未连接');
            }
            const serverHost = customServerHost || firmwareServerHost || FIRMWARE_SERVER_CONFIG.defaultHost;
            const response = await deviceClient.authorizedFetch(
                `${serverHost}${FIRMWARE_SERVER_CONFIG.endpoints.checkUpdate}`,
                {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ currentVersion: currentVersion.trim() } satisfies FirmwareUpdateCheckRequest),
                },
                ['config.read'],
            );
            if (!response.ok) {
                throw new Error(`HTTP error: ${response.status} ${response.statusText}`);
            }
            const envelope = await response.json();
            if (envelope.errNo && envelope.errNo !== 0) {
                throw new Error(envelope.errorMessage || `Server error ${envelope.errNo}`);
            }
            setFirmwareUpdateInfo(envelope.data);
        } catch (cause) {
            setFirmwareUpdateInfo(null);
            const message = cause instanceof Error ? cause.message : '固件更新认证失败';
            setError(message);
            throw cause;
        }
    };

    // 工具函数：生成会话ID
    const generateSessionId = (): string => {
        return 'session_' + Date.now() + '_' + Math.random().toString(36).substr(2, 9);
    };

    /**
     * 下载固件包
     * 解压
     * 计算总大小
     * 创建固件包对象
     * @param downloadUrl 固件包下载地址
     * @param onProgress 下载进度回调
     * @returns 固件包对象
     */
    const downloadFirmwarePackage = async (downloadUrl: string, onProgress?: (progress: FirmwarePackageDownloadProgress) => void): Promise<FirmwarePackage> => {
        try {
            setError(null);

            // 初始化进度
            const initialProgress: FirmwarePackageDownloadProgress = {
                stage: 'downloading',
                progress: 0,
                message: 'Starting to download firmware package...'
            };
            onProgress?.(initialProgress);

            // 1. 下载固件包 (进度 0% - 30%)
            if (!deviceClient) throw new Error('Device transport is not connected');
            const response = await deviceClient.authorizedFetch(
                downloadUrl,
                undefined,
                ['firmware.update'],
            );
            if (!response.ok) {
                throw new Error(`Failed to download firmware package: ${response.status} ${response.statusText}`);
            }

            const contentLength = parseInt(response.headers.get('content-length') || '0', 10);
            if (contentLength === 0) {
                throw new Error('Failed to get file size');
            }

            const reader = response.body?.getReader();
            if (!reader) {
                throw new Error('Failed to read response data');
            }

            const chunks: Uint8Array[] = [];
            let receivedLength = 0;

            while (true) {
                const { done, value } = await reader.read();
                if (done) break;

                chunks.push(value);
                receivedLength += value.length;

                // 下载进度占总进度的80%
                const downloadProgress = (receivedLength / contentLength) * 80;
                onProgress?.({
                    stage: 'downloading',
                    progress: downloadProgress,
                    message: `Downloading firmware package... ${Math.round(receivedLength / 1024)}KB/${Math.round(contentLength / 1024)}KB`,
                    bytes_transferred: receivedLength,
                    total_bytes: contentLength
                });
            }

            // 合并所有数据块
            const packageData = new Uint8Array(receivedLength);
            let position = 0;
            for (const chunk of chunks) {
                packageData.set(chunk, position);
                position += chunk.length;
            }

            onProgress?.({
                stage: 'extracting',
                progress: 80,
                message: 'Extracting firmware package...'
            });

            // 2. 解压和验证包 (进度 30% - 40%)
            const { manifest, components } = await extractFirmwarePackage(packageData);

            // 3. 计算总大小
            onProgress?.({
                stage: 'extracting',
                progress: 85,
                message: 'Preparing firmware package...'
            });

            let totalSize = 0;
            for (const component of Object.values(components)) {
                if (component.data) {
                    totalSize += component.data.length;
                }
            }

            // 4. 创建固件包对象
            const firmwarePackage: FirmwarePackage = {
                manifest,
                components,
                totalSize
            };

            onProgress?.({
                stage: 'downcompleted',
                progress: 100,
                message: `Firmware package download completed! Total size: ${Math.round(totalSize / 1024)}KB`
            });

            setError(null);
            return Promise.resolve(firmwarePackage);

        } catch (err) {
            const errorMessage = err instanceof Error ? err.message : 'Failed to download firmware package: unknown error';
            setError(errorMessage);

            onProgress?.({
                stage: 'failed',
                progress: 0,
                message: 'Download failed',
                error: errorMessage
            });

            return Promise.reject(new Error(errorMessage));
        }
    };

    // 上传固件到设备
    const uploadFirmwareToDevice = async (firmwarePackage: FirmwarePackage, onProgress?: (progress: FirmwarePackageDownloadProgress) => void): Promise<void> => {
        let activeSessionId: string | null = null;
        let completionIssued = false;
        let completionSettled = false;
        try {
            // 生成会话ID
            const sessionId = generateSessionId();

            // 创建升级会话
            const sessionData = await sendDeviceRequest('create_firmware_upgrade_session', {
                session_id: sessionId,
                manifest: firmwarePackage.manifest
            }, true);

            const deviceSessionId = sessionData.session_id || sessionId;
            activeSessionId = deviceSessionId;

            // 更新升级会话状态
            setUpgradeSession({
                sessionId: deviceSessionId,
                status: 'uploading',
                progress: 0,
                currentComponent: null,
                error: null
            });

            onProgress?.({
                stage: 'uploading',
                progress: 0,
                message: 'Starting to upload firmware package...'
            });

            const componentNames = Object.keys(firmwarePackage.components);
            const totalComponents = componentNames.length;

            // 逐个上传组件
            for (let i = 0; i < componentNames.length; i++) {
                const componentName = componentNames[i];
                const component = firmwarePackage.components[componentName];

                // 更新当前组件状态
                setUpgradeSession(prev => prev ? {
                    ...prev,
                    currentComponent: componentName,
                    progress: Math.round((i / totalComponents) * 100)
                } : null);

                onProgress?.({
                    stage: 'uploading',
                    progress: Math.round((i / totalComponents) * 100),
                    message: `Uploading component: ${componentName}`
                });

                // 分片传输单个组件
                const componentData = component.data;
                if (!componentData) {
                    throw new Error(`Component ${componentName} data is missing`);
                }
                const transportChunkSize =
                    deviceClient?.transport.kind === 'webhid'
                        ? WEBHID_FIRMWARE_CHUNK_DATA_SIZE
                        : upgradeConfig.chunkSize;
                const totalChunks = Math.ceil(componentData.length / transportChunkSize);

                // 解析组件基地址（支持十六进制格式）
                let baseAddress: number;

                if (component.address.toString().startsWith('0x')) {
                    baseAddress = parseInt(component.address.toString(), 16);
                } else if (component.address.toString().startsWith('0X')) {
                    baseAddress = parseInt(component.address.toString(), 16);
                } else {
                    baseAddress = parseInt(component.address.toString(), 10);
                }

                // 分片传输
                for (let chunkIndex = 0; chunkIndex < totalChunks; chunkIndex++) {
                    const start = chunkIndex * transportChunkSize;
                    const end = Math.min(start + transportChunkSize, componentData.length);
                    const chunkData = componentData.slice(start, end);

                    // 计算当前chunk的精确写入地址和偏移
                    const chunkOffset = parseInt(start.toString(), 10);
                    const targetAddress = baseAddress + chunkOffset;

                    // 直接计算二进制数据的校验和（移除Intel HEX相关逻辑）
                    // 使用异步SHA256计算
                    const checksum = await calculateSHA256(chunkData);

                    // 添加调试输出
                    console.log(`Frontend calculated chunk ${chunkIndex} SHA256:`, checksum);
                    console.log(`Chunk size: ${chunkData.length} bytes`);
                    console.log(`First 32 bytes of chunk data:`, Array.from(chunkData.slice(0, 32)).map(b => b.toString(16).padStart(2, '0')).join(' '));
                    if (chunkData.length > 32) {
                        console.log(`Last 32 bytes of chunk data:`, Array.from(chunkData.slice(-32)).map(b => b.toString(16).padStart(2, '0')).join(' '));
                    }

                    if (!deviceClient) throw new Error('Device command client is not available');
                    await sendFirmwareChunkWithoutAmbiguousRetry(
                        () => deviceClient.uploadFirmwareChunk({
                            sessionId: deviceSessionId,
                            componentName,
                            chunkIndex,
                            totalChunks,
                            chunkOffset,
                            targetAddress,
                            checksumSha256: checksum,
                            data: chunkData,
                        }, { timeoutMs: upgradeConfig.timeout }),
                    );

                    // 更新进度
                    const componentProgress = ((chunkIndex + 1) / totalChunks) * 100;
                    const overallProgress = ((i + (chunkIndex + 1) / totalChunks) / totalComponents) * 100;

                    onProgress?.({
                        stage: 'uploading',
                        progress: Math.round(overallProgress),
                        message: `Uploading component ${componentName}: ${Math.round(componentProgress)}% (${chunkIndex + 1}/${totalChunks})`
                    });
                }
            }

            // 完成升级会话
            completionIssued = true;
            const completeResult = await sendDeviceRequest('complete_firmware_upgrade_session', {
                session_id: deviceSessionId
            }, true);

            if (!completeResult || typeof completeResult.success !== 'boolean') {
                throw new Error('Firmware finalization returned an invalid response');
            }
            completionSettled = true;

            if (!completeResult.success) {
                throw new Error(`Failed to complete upgrade session: ${completeResult.error || 'Unknown error'}`);
            }

            // 更新最终状态
            setUpgradeSession(prev => prev ? {
                ...prev,
                status: 'completed',
                progress: 100,
                currentComponent: null
            } : null);

            onProgress?.({
                stage: 'uploadcompleted',
                progress: 100,
                message: 'Firmware upload completed!'
            });

        } catch (error) {
            const finalizationUncertain = completionIssued && !completionSettled;
            const reportedError = finalizationUncertain
                ? new FirmwareFinalizationUncertainError(error)
                : error;
            try {
                await abortFirmwareSessionIfSafe({
                    sessionId: activeSessionId,
                    completionIssued,
                    completionSettled,
                    abortSession: async (sessionId) => {
                        await sendDeviceRequest('abort_firmware_upgrade_session', {
                            session_id: sessionId
                        }, true);
                    },
                });
            } catch (abortError) {
                console.error('Failed to abort upgrade session:', abortError);
            }

            setUpgradeSession(prev => prev ? {
                ...prev,
                status: 'failed',
                error: reportedError instanceof Error ? reportedError.message : 'Unknown error'
            } : null);

            onProgress?.({
                stage: 'failed',
                progress: 0,
                message: `Upload failed: ${reportedError instanceof Error ? reportedError.message : 'Unknown error'}`
            });

            throw reportedError;
        }
    };

    const uploadCh585Firmware = async (
        image: Uint8Array,
        onProgress?: (progress: number) => void,
    ): Promise<void> => {
        if (image.length <= 0x1000 || (image.length & 3) !== 0) {
            throw new Error('CH585 image must be the aligned combined IAP + application BIN');
        }
        const sessionId = `ch585-${Date.now().toString(16)}`;
        const sha256 = await calculateSHA256(image);
        await sendDeviceRequest('ch585_update_begin', {
            session_id: sessionId,
            total_size: image.length,
            sha256,
        }, true);

        const chunkSize = 512;
        for (let offset = 0; offset < image.length; offset += chunkSize) {
            const chunk = image.slice(offset, Math.min(offset + chunkSize, image.length));
            const data = btoa(String.fromCharCode(...chunk));
            await sendDeviceRequest('ch585_update_chunk', {
                session_id: sessionId,
                offset,
                data,
            }, true);
            onProgress?.(Math.round(((offset + chunk.length) * 100) / image.length));
        }

        await sendDeviceRequest('ch585_update_complete', {
            session_id: sessionId,
        }, true);
        onProgress?.(100);
    };

    // 配置管理函数
    const setUpgradeConfig = (config: Partial<FirmwareUpgradeConfig>): void => {
        // 验证分片大小必须是4K或4K的倍数
        if (config.chunkSize !== undefined) {
            const CHUNK_SIZE_BASE = 4096; // 4KB基础单位

            if (config.chunkSize <= 0) {
                throw new Error('Chunk size must be greater than 0');
            }

            if (config.chunkSize % CHUNK_SIZE_BASE !== 0) {
                throw new Error(`Chunk size must be a multiple of 4KB (${CHUNK_SIZE_BASE} bytes), current value: ${config.chunkSize}`);
            }

            // 建议的最大分片大小为16KB，避免超过STM32的HTTP缓冲区限制
            const MAX_CHUNK_SIZE = 16384; // 16KB
            if (config.chunkSize > MAX_CHUNK_SIZE) {
                console.warn(`Chunk size ${config.chunkSize} exceeds the recommended maximum of ${MAX_CHUNK_SIZE}, which may lead to insufficient STM32 memory.`);
            }
        }

        setUpgradeConfigState(prevConfig => ({
            ...prevConfig,
            ...config
        }));
    };

    const getUpgradeConfig = (): FirmwareUpgradeConfig => {
        return upgradeConfig;
    };

    const getValidChunkSizes = (): number[] => {
        const CHUNK_SIZE_BASE = 4096; // 4KB基础单位
        const MAX_CHUNK_SIZE = 16384; // 16KB
        const validSizes: number[] = [];

        for (let size = CHUNK_SIZE_BASE; size <= MAX_CHUNK_SIZE; size += CHUNK_SIZE_BASE) {
            validSizes.push(size);
        }

        return validSizes;
    };

    const updateMarkingStatus = (status: StepInfo) => {
        setMarkingStatus(status);
    };

    // 立即发送队列中的特定命令
    const sendPendingCommandImmediately = useCallback((command: string): boolean => {
        return deviceClientRef.current?.sendPendingCommandImmediately(command) ?? false;
    }, []);

    // 快速清空队列
    const flushQueue = async (): Promise<void> => {
        if (deviceClient) {
            await deviceClient.flushQueue();
        }
    };

    // Device transport timeout configuration
    const getDeviceTransportConfig = () => {
        return transportConfig;
    };

    const updateDeviceTransportConfig = (config: Partial<DeviceTransportConfigType>) => {
        setTransportConfig(prevConfig => ({
            ...prevConfig,
            ...config
        }));
        console.log('设备传输配置已更新:', config);
    };

    // 按键索引映射到游戏控制器按钮或组合键
    const indexMapToGameControllerButtonOrCombination = (
        keyMapping: { [key in GameControllerButton]?: number[] },
        keyCombinations: KeyCombination[],
        inputMode: Platform
    ): { [key: number]: GameControllerButton | string } => {
        let labelMap = new Map<GameControllerButton, string>();
        switch (inputMode) {
            case Platform.XINPUT: labelMap = XInputButtonMap;
                break;
            case Platform.PS4: labelMap = PS4ButtonMap;
                break;
            case Platform.PS5: labelMap = PS4ButtonMap;
                break;
            case Platform.XBOX: labelMap = XInputButtonMap;
                break;
            case Platform.SWITCH: labelMap = SwitchButtonMap;
                break;
            default: labelMap = new Map<GameControllerButton, string>();
                break;
        }
        
        const map: { [key: number]: GameControllerButton | string } = {};
        
        // 处理普通游戏控制器按钮的按键映射
        if (keyMapping) {
            for(const [key, value] of Object.entries(keyMapping)) {
                for(const index of value) {
                    map[index] = labelMap.get(key as GameControllerButton) as GameControllerButton;
                }
            }
        }
        
        // 处理组合键的按键映射
        if (keyCombinations) {
            for(let i = 0; i < keyCombinations.length; i++) {
                const combination = keyCombinations[i];
                for(const index of combination.keyIndexes) {
                    map[index] = `COM${i + 1}`;
                }
            }
        }
        
        return map;
    };

    return (
        <GamepadConfigContext.Provider value={{
            contextJsReady,
            setContextJsReady,

            // WebHID connection state
            deviceConnected,
            showReconnect,
            deviceState,
            devicePhase,
            deviceError,
            deviceSession,
            connectDevice,
            reconnectDevice,
            disconnectDevice,
            getDeviceImageCatalog,
            readDeviceImage,
            uploadDeviceImage,
            deleteDeviceImage,

            globalConfig,
            screenControl,
            profileList,
            defaultProfile,
            hotkeysConfig,
            dataIsReady,
            setUserRebooting,
            firmwareUpdating,
            setFirmwareUpdating,

            fetchDefaultProfile,
            fetchProfileList,
            fetchHotkeysConfig,
            fetchGlobalConfig,
            updateGlobalConfig,
            fetchScreenControl,
            updateScreenControl,
            updateProfileDetails,
            getMacro,
            updateMacro,
            getProfileMacros,
            updateProfileMacros,
            resetProfileDetails,
            createProfile,
            deleteProfile,
            switchProfile,
            updateHotkeysConfig,
            isLoading,
            error,
            setError,
            rebootSystem,
            // 校准相关
            calibrationStatus,
            fetchCalibrationStatus,
            startManualCalibration,
            stopManualCalibration,
            clearManualCalibrationData,
            checkIsManualCalibrationCompleted,
            // ADC Mapping 相关
            defaultMappingId: defaultMappingId,
            markingStatus,
            mappingList,
            activeMapping,
            fetchMappingList,
            fetchMarkingStatus,
            updateDefaultMapping,
            fetchDefaultMapping,
            fetchActiveMapping,
            createMapping,
            deleteMapping,
            startMarking,
            stopMarking,
            stepMarking,
            renameMapping,
            // 按键监控相关
            buttonMonitoringActive: buttonMonitoringActive,
            startButtonMonitoring,
            stopButtonMonitoring,
            getButtonStates,
            // 按键性能监控相关
            startButtonPerformanceMonitoring,
            stopButtonPerformanceMonitoring,
            // LED 配置相关
            pushLedsConfig: pushLedsConfig,
            clearLedsPreview: clearLedsPreview,
            // 固件元数据相关
            firmwareInfo,
            fetchFirmwareMetadata,
            // 固件更新检查相关
            firmwareUpdateInfo,
            checkFirmwareUpdate,
            // 固件升级包下载和传输相关
            upgradeSession: upgradeSession,
            downloadFirmwarePackage: downloadFirmwarePackage,
            uploadFirmwareToDevice: uploadFirmwareToDevice,
            uploadCh585Firmware: uploadCh585Firmware,
            setUpgradeConfig: setUpgradeConfig,
            getUpgradeConfig: getUpgradeConfig,
            getValidChunkSizes: getValidChunkSizes,
            updateMarkingStatus: updateMarkingStatus,
            sendPendingCommandImmediately: sendPendingCommandImmediately,
            flushQueue: flushQueue,
            getDeviceTransportConfig: getDeviceTransportConfig,
            updateDeviceTransportConfig: updateDeviceTransportConfig,
            // 是否禁用完成配置按钮
            finishConfigDisabled: finishConfigDisabled,
            setFinishConfigDisabled: setFinishConfigDisabled,
            // 按键索引映射到游戏控制器按钮或组合键
            indexMapToGameControllerButtonOrCombination: indexMapToGameControllerButtonOrCombination,
            // 设备日志相关
            fetchDeviceLogsList: fetchDeviceLogsList,
            exportAllConfig: exportAllConfig,
            importAllConfig: importAllConfig,
            getHitboxLayout: getHitboxLayout,
            hitboxLayout: hitboxLayout,
        }}>
            {children}
        </GamepadConfigContext.Provider>
    );
}

export function useGamepadConfig() {
    const context = useContext(GamepadConfigContext);
    if (context === undefined) {
        throw new Error('useGamepadConfig must be used within a GamepadConfigProvider');
    }
    return context;
}
