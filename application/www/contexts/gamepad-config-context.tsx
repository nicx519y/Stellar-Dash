'use client';

import React, { createContext, useContext, useState, useEffect, useRef, useMemo } from 'react';
import { GameProfile, 
        LedsEffectStyle, 
        AroundLedsEffectStyle,
        Platform, GameSocdMode, 
        GameControllerButton, Hotkey, GameProfileList, GlobalConfig } from '@/types/gamepad-config';
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

import { 
    DEFAULT_FIRMWARE_PACKAGE_CHUNK_SIZE, 
    DEFAULT_FIRMWARE_UPGRADE_MAX_RETRIES, 
    DEFAULT_FIRMWARE_UPGRADE_TIMEOUT,
    DEFAULT_FIRMWARE_SERVER_HOST
} from '@/types/gamepad-config';

import DeviceAuthManager from '@/contexts/deviceAuth';

// 导入WebSocket框架
import { 
    WebSocketFramework, 
    WebSocketState, 
    WebSocketDownstreamMessage,
    WebSocketError 
} from '@/components/websocket-framework';

// 导入事件总线
import { eventBus, EVENTS } from '@/lib/event-manager';
import { parseButtonStateBinaryData, BUTTON_STATE_CHANGED_CMD, type ButtonStateBinaryData } from '@/lib/button-binary-parser';

// 导入固件工具函数
import { calculateSHA256, extractFirmwarePackage } from '@/lib/firmware-utils';

// 导入WebSocket队列管理器
import { WebSocketQueueManager } from '@/lib/websocket-queue-manager';
import { BUTTON_PERFORMANCE_MONITORING_CMD, parseButtonPerformanceMonitoringBinaryData } from '@/lib/button-performance-binary-parser';

// 固件服务器配置
const FIRMWARE_SERVER_CONFIG = {
    // 默认固件服务器地址，可通过环境变量覆盖
    defaultHost: process.env.NEXT_PUBLIC_FIRMWARE_SERVER_HOST || DEFAULT_FIRMWARE_SERVER_HOST,
    // API 端点
    endpoints: {
        checkUpdate: '/api/firmware-check-update'
    }
};

// WebSocket配置类型
export interface WebSocketConfigType {
    // WebSocket连接配置
    url: string;
    heartbeatInterval: number; // 心跳间隔（毫秒）
    timeout: number; // 超时时间（毫秒）
    
    // 队列管理器配置
    sendDelay: number; // 延迟发送时间（毫秒） 
    pollInterval: number; // 轮询间隔（毫秒）
}

// 默认WebSocket配置
export const DEFAULT_WEBSOCKET_CONFIG: WebSocketConfigType = {
    url: `ws://${typeof window !== 'undefined' ? window.location.hostname : 'localhost'}:8081`,
    heartbeatInterval: 30000, // 30秒心跳间隔
    timeout: 15000, // 15秒超时
    sendDelay: 3000, // 500ms延迟发送
    pollInterval: 50, // 轮询间隔50ms
} as const;

interface GamepadConfigContextType {
    contextJsReady: boolean;
    setContextJsReady: (ready: boolean) => void;
    
    // WebSocket 连接状态
    wsConnected: boolean;
    wsState: WebSocketState;
    showReconnect: boolean;
    wsError: WebSocketError | null;
    connectWebSocket: () => Promise<void>;
    disconnectWebSocket: () => void;
    
    profileList: GameProfileList;
    defaultProfile: GameProfile;
    hotkeysConfig: Hotkey[];
    globalConfig: GlobalConfig;
    dataIsReady: boolean;
    setUserRebooting: (rebooting: boolean) => void;
    firmwareUpdating: boolean;
    setFirmwareUpdating: (updating: boolean) => void;
    
    fetchGlobalConfig: () => Promise<void>;
    updateGlobalConfig: (globalConfig: GlobalConfig) => Promise<void>;
    fetchDefaultProfile: () => Promise<void>;
    fetchProfileList: () => Promise<void>;
    fetchHotkeysConfig: () => Promise<void>;
    updateProfileDetails: (profileId: string, profileDetails: GameProfile) => Promise<void>;
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
    startManualCalibration: () => Promise<CalibrationStatus>;
    stopManualCalibration: () => Promise<CalibrationStatus>;
    clearManualCalibrationData: () => Promise<void>;
    // ADC Mapping 相关
    defaultMappingId: string;
    markingStatus: StepInfo;
    mappingList: { id: string, name: string }[];
    activeMapping: ADCValuesMapping | null;
    fetchMappingList: () => Promise<void>;
    fetchDefaultMapping: () => Promise<void>;
    fetchActiveMapping: (id: string) => Promise<void>;
    createMapping: (name: string, length: number, step: number) => Promise<void>;
    deleteMapping: (id: string) => Promise<void>;
    updateDefaultMapping: (id: string) => Promise<void>;
    startMarking: (id: string) => Promise<void>;
    stopMarking: () => Promise<void>;
    stepMarking: () => Promise<void>;
    // fetchMarkingStatus: () => Promise<void>;
    renameMapping: (id: string, name: string) => Promise<void>;
    // 按键监控相关
    buttonMonitoringActive: boolean;
    startButtonMonitoring: () => Promise<void>;
    stopButtonMonitoring: () => Promise<void>;
    getButtonStates: () => Promise<ButtonStates>;
    // 按键性能监控相关
    startButtonPerformanceMonitoring: () => Promise<void>;
    stopButtonPerformanceMonitoring: () => Promise<void>;
    // LED 配置相关
    pushLedsConfig: (ledsConfig: LEDsConfig) => Promise<void>;
    clearLedsPreview: () => Promise<void>;
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
    setUpgradeConfig: (config: Partial<FirmwareUpgradeConfig>) => void;
    getUpgradeConfig: () => FirmwareUpgradeConfig;
    getValidChunkSizes: () => number[];
    updateMarkingStatus: (status: StepInfo) => void;
    // 立即发送队列中的特定命令
    sendPendingCommandImmediately: (command: string) => boolean;
    // 快速清空队列
    flushQueue: () => Promise<void>;
    // WebSocket配置管理
    getWebSocketConfig: () => WebSocketConfigType;
    updateWebSocketConfig: (config: Partial<WebSocketConfigType>) => void;
}

const GamepadConfigContext = createContext<GamepadConfigContextType | undefined>(undefined);

/**
 * convert profile details
 * @param profile - GameProfile
 * @returns 
 */
const converProfileDetails = (profile: any) => {
    const newProfile: GameProfile = {
        ...profile,
        keysConfig: {
            inputMode: profile.keysConfig?.inputMode as Platform ?? Platform.XINPUT,
            socdMode: profile.keysConfig?.socdMode as GameSocdMode ?? GameSocdMode.SOCD_MODE_UP_PRIORITY,
            invertXAxis: profile.keysConfig?.invertXAxis as boolean ?? false,
            invertYAxis: profile.keysConfig?.invertYAxis as boolean ?? false,
            fourWayMode: profile.keysConfig?.fourWayMode as boolean ?? false,
            keyMapping: profile.keysConfig?.keyMapping as { [key in GameControllerButton]?: number[] } ?? {},
            keysEnableTag: profile.keysConfig?.keysEnableTag as boolean[] ?? [],
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
    if(data.errNo) {
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
    const [profileList, setProfileList] = useState<GameProfileList>({ defaultId: "", maxNumProfiles: 0, items: [] });
    const [defaultProfile, setDefaultProfile] = useState<GameProfile>({ id: "", name: "" });
    const [isLoading, setIsLoading] = useState(true);
    const [error, setError] = useState<string | null>(null);
    const [hotkeysConfig, setHotkeysConfig] = useState<Hotkey[]>([]);
    const [jsReady, setJsReady] = useState(false);
    
    // WebSocket 状态
    const [wsConnected, setWsConnected] = useState(false);
    const [wsState, setWsState] = useState<WebSocketState>(WebSocketState.DISCONNECTED);
    const [wsError, setWsError] = useState<WebSocketError | null>(null);
    const [wsFramework, setWsFramework] = useState<WebSocketFramework | null>(null);
    const [showReconnect, setShowReconnect] = useState(false);  // 是否显示websocket重连窗口
    
    // WebSocket 队列管理器
    // const wsQueueManager = useRef<WebSocketQueueManager | null>(null);
    
    const [defaultMappingId, setDefaultMappingId] = useState<string>("");
    const [mappingList, setMappingList] = useState<{ id: string, name: string }[]>([]);
    const [markingStatus, setMarkingStatus] = useState<StepInfo>({
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
    });
    const [activeMapping, setActiveMapping] = useState<ADCValuesMapping | null>(null);
    const [buttonMonitoringActive, setButtonMonitoringActive] = useState<boolean>(false);
    const [firmwareInfo, setFirmwareInfo] = useState<DeviceFirmwareInfo | null>(null);
    const [firmwareUpdateInfo, setFirmwareUpdateInfo] = useState<FirmwareUpdateCheckResponse | null>(null);
    const [firmwareServerHost, _setFirmwareServerHostState] = useState<string>(FIRMWARE_SERVER_CONFIG.defaultHost);
    const [upgradeSession, setUpgradeSession] = useState<FirmwareUpgradeSession | null>(null);
    const [upgradeConfig, setUpgradeConfigState] = useState<FirmwareUpgradeConfig>({
        chunkSize: DEFAULT_FIRMWARE_PACKAGE_CHUNK_SIZE, // 4KB默认分片大小
        maxRetries: DEFAULT_FIRMWARE_UPGRADE_MAX_RETRIES,
        timeout: DEFAULT_FIRMWARE_UPGRADE_TIMEOUT // 30秒超时
    });

    // WebSocket配置状态管理
    const [websocketConfig, setWebsocketConfig] = useState<WebSocketConfigType>(DEFAULT_WEBSOCKET_CONFIG);

    const contextJsReady = useMemo(() => jsReady, [jsReady]);

    const [globalConfigIsReady, setGlobalConfigIsReady] = useState(false);
    const [profileListIsReady, setProfileListIsReady] = useState(false);
    const [hotkeysConfigIsReady, setHotkeysConfigIsReady] = useState(false);
    const [firmwareInfoIsReady, setFirmwareInfoIsReady] = useState(false);
    const [dataIsReady, setDataIsReady] = useState(false);
    const [userRebooting, setUserRebooting] = useState(false); // 是否是用户手动重启
    const [firmwareUpdating, setFirmwareUpdating] = useState(false); // 是否正在固件升级


    // 处理通知消息
    const handleNotificationMessage = (message: WebSocketDownstreamMessage): void => {
        const { command, data } = message;

        switch (command) {
            case 'welcome':
                console.log('收到欢迎消息:', data);
                break;
            case 'calibration_update':
                // 校准状态更新 - 只发布事件，让具体组件处理
                console.log('收到校准状态推送更新:', data);
                eventBus.emit(EVENTS.CALIBRATION_UPDATE, data);
                break;
            case 'marking_status_update':
                // 发布标记状态更新事件，让具体组件订阅处理
                eventBus.emit(EVENTS.MARKING_STATUS_UPDATE, data);
                break;
            // 注意：button_state_changed 现在使用二进制格式推送，不再使用JSON
            default:
                console.log('收到未知通知消息:', message);
        }
    };

    // 处理二进制消息
    const handleBinaryMessage = (data: ArrayBuffer): void => {
        try {
            // 先检查数据长度
            if (data.byteLength < 1) {
                console.warn('二进制消息长度不足，至少需要1字节包含CMD字段');
                return;
            }

            // 读取第一个字节作为CMD字段
            const dataView = new DataView(data);
            const cmd = dataView.getUint8(0);

            // 根据CMD字段分发处理
            switch (cmd) {
                case BUTTON_STATE_CHANGED_CMD: {
                    // 按键状态变化消息
                    const buttonStateData = parseButtonStateBinaryData(new Uint8Array(data));
                    if (buttonStateData) {
                        eventBus.emit(EVENTS.BUTTON_STATE_CHANGED, buttonStateData);
                    }
                    break;
                }
                case BUTTON_PERFORMANCE_MONITORING_CMD: {
                    // 按键性能监控消息
                    const performanceData = parseButtonPerformanceMonitoringBinaryData(new Uint8Array(data));
                    if (performanceData) {
                        eventBus.emit(EVENTS.BUTTON_PERFORMANCE_MONITORING, performanceData);
                    }
                    break;
                }
                default:
                    console.warn(`收到未知的二进制消息命令: ${cmd}`);
                    break;
            }
        } catch (e) {
            console.error('Failed to parse binary message:', e);
        }
    };

    // 初始化WebSocket框架
    useEffect(() => {
        const framework = new WebSocketFramework({
            url: websocketConfig.url,
            heartbeatInterval: websocketConfig.heartbeatInterval,
            timeout: websocketConfig.timeout
        });

        // 设置事件监听器
        const unsubscribeState = framework.onStateChange((state) => {
            setWsState(state);
            setWsConnected(state === WebSocketState.CONNECTED);
        });

        const unsubscribeError = framework.onError((error) => {
            setWsError(error);
            console.error('WebSocket错误:', error);
        });

        /**
         * 处理通知消息 JSON数据推送
         */
        const unsubscribeMessage = framework.onMessage((message: WebSocketDownstreamMessage) => {
            // 现在只处理通知消息（没有CID的消息），响应消息由WebSocket框架内部处理
            if (message.cid === undefined) {
                handleNotificationMessage(message);
            }
            // 响应消息由WebSocket框架内部处理，不再需要队列管理器参与
        });

        /**
         * 处理二进制消息推送
         */
        const unsubscribeBinary = framework.onBinaryMessage((data: ArrayBuffer) => {
            handleBinaryMessage(data);
        });

        const unsubscribeDisconnect = framework.onDisconnect(() => {
            console.log('WebSocket连接断开，触发全局断开事件');
            // 这里可以触发全局事件，让layout组件知道连接断开了
            eventBus.emit(EVENTS.WEBSOCKET_DISCONNECTED);
        });

        setWsFramework(framework);

        // 清理函数
        return () => {
            unsubscribeState();
            unsubscribeError();
            unsubscribeMessage();
            unsubscribeBinary();
            unsubscribeDisconnect();
            framework.disconnect();
        };
    }, []);

    const isFirstConnectRef = useRef(true);

    // 自动连接WebSocket
    useEffect(() => {
        if (wsFramework && wsState === WebSocketState.DISCONNECTED) {
            if (isFirstConnectRef.current) { // 只有第一次连接时会自动连接，并且显示loading
                setIsLoading(true);
                wsFramework.connect().catch((error) => {
                    console.error('首次连接失败:', error);
                    setIsLoading(false);
                    setShowReconnect(true); // 首次连接失败也显示重连窗口
                });
                isFirstConnectRef.current = false;
            } else {
                setIsLoading(false);
                if(!userRebooting && !firmwareUpdating) { // 不是用户主动重启导致的断连，并且不是固件升级导致的断连
                    setShowReconnect(true); // 显示重连窗口
                }
            }
        }
    }, [wsFramework, wsState]);

    // 当WebSocket连接成功后，初始化数据
    useEffect(() => {
        if (wsConnected && wsState === WebSocketState.CONNECTED) {
            // 隐藏重连窗口
            setShowReconnect(false);
            
            // 设置DeviceAuthManager的WebSocket发送函数
            const authManager = DeviceAuthManager.getInstance();
            authManager.setWebSocketSendFunction(sendWebSocketRequest);
            
            fetchGlobalConfig().then(() => {
                setGlobalConfigIsReady(true);
            }).catch(console.error);
            fetchProfileList().then(() => {
                setProfileListIsReady(true);
            }).catch(console.error);
            fetchHotkeysConfig().then(() => {
                setHotkeysConfigIsReady(true);
            }).catch(console.error);
            fetchFirmwareMetadata().then(() => {
                setFirmwareInfoIsReady(true);
            }).catch(console.error);

        }
    }, [wsConnected, wsState]);

    useEffect(() => {
        if(globalConfigIsReady && profileListIsReady && hotkeysConfigIsReady && firmwareInfoIsReady) {
            setDataIsReady(true);
            setIsLoading(false);
        }
    }, [globalConfigIsReady, profileListIsReady, hotkeysConfigIsReady, firmwareInfoIsReady]);

    // useEffect(() => {
    //     if (profileList.defaultId !== "") {
    //         fetchDefaultProfile();
    //     }
    // }, [profileList]);

    const setContextJsReady = (ready: boolean) => {
        setJsReady(ready);
    };

    // WebSocket连接管理
    const connectWebSocket = async (): Promise<void> => {
        if (wsFramework) {
            return wsFramework.connect();
        }
        throw new Error('WebSocket框架未初始化');
    };

    const disconnectWebSocket = (): void => {
        if (wsFramework) {
            wsFramework.disconnect();
        }
    };

    /**
     * 发送WebSocket请求
     * @param command 命令
     * @param params 参数
     * @param immediate 是否立即发送，忽略延迟 true: 立即发送 false: 延迟发送
     * @returns 
     */
    const sendWebSocketRequest = async (command: string, params: Record<string, unknown> = {}, immediate: boolean = false): Promise<any> => {
        if (!wsFramework) {
            return Promise.reject(new Error('WebSocket框架未初始化'));
        }
        if (wsState !== WebSocketState.CONNECTED) {
            throw new Error('WebSocket未连接');
        }
        
        try {
            // 将请求推入队列，队列管理器会处理延迟、去重和顺序发送
            return await wsFramework.enqueue(command, params, immediate);
        } catch (error) {
            if (error instanceof Error) {
                throw error;
            }
            throw new Error(`WebSocket请求失败: ${error}`);
        }
    };

    const fetchDefaultProfile = async (immediate: boolean = true): Promise<void> => {
        try {
            // setIsLoading(true);
            const data = await sendWebSocketRequest('get_default_profile', {}, immediate);
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
            const data = await sendWebSocketRequest('get_profile_list', {}, immediate);
            if (data && 'profileList' in data) {
                setProfileList(data.profileList as GameProfileList);
            }

            if(data && 'defaultProfileDetails' in data) {
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
            const data = await sendWebSocketRequest('get_hotkeys_config', {}, immediate);
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

    const updateProfileDetails = async (profileId: string, profileDetails: GameProfile, immediate: boolean = false): Promise<void> => {
        try {
            // setIsLoading(true);
            const data = await sendWebSocketRequest('update_profile', { profileId, profileDetails }, immediate);

            // 如果更新的是 profile 的 name， 或者更新的profile不是defaultProfile，则需要重新获取 profile list
            if(profileDetails.name != undefined && profileDetails.name !== defaultProfile.name || profileDetails.id !== defaultProfile.id) {
                fetchProfileList();
            } else if (data && 'defaultProfileDetails' in data) {
                // 否则更新 default profile
                setDefaultProfile(converProfileDetails(data.defaultProfileDetails) ?? {});
            }
            // setError(null);
            return Promise.resolve();
        } catch (err) {
            // setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to update profile details"));
        } finally {
            // setIsLoading(false);
        }
    };

    const resetProfileDetails = async (immediate: boolean = true): Promise<void> => {
        await fetchDefaultProfile();
    };

    const createProfile = async (profileName: string, immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('create_profile', { profileName }, immediate);
            if (data && 'profileList' in data) {
                setProfileList(data.profileList as GameProfileList);
            }
            if(data && 'defaultProfileDetails' in data) {
                setDefaultProfile(converProfileDetails(data.defaultProfileDetails) ?? {});
            }
            setError(null);
            return Promise.resolve();
        } catch (err) {
            // setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to create profile"));
        } finally {
            setIsLoading(false);
        }
    };

    const deleteProfile = async (profileId: string, immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('delete_profile', { profileId }, immediate);
            if (data && 'profileList' in data) {
                setProfileList(data.profileList as GameProfileList);
            }
            if(data && 'defaultProfileDetails' in data) {
                setDefaultProfile(converProfileDetails(data.defaultProfileDetails) ?? {});
            }
            setError(null);
            return Promise.resolve();
        } catch (err) {
            // setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to delete profile"));
        } finally {
            setIsLoading(false);
        }
    };

    const switchProfile = async (profileId: string, immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('switch_default_profile', { profileId }, immediate);
            if (data && 'profileList' in data) {
                setProfileList(data.profileList as GameProfileList);
            }
            if(data && 'defaultProfileDetails' in data) {
                setDefaultProfile(converProfileDetails(data.defaultProfileDetails) ?? {});
            }
            setError(null);
            return Promise.resolve();
        } catch (err) {
            // setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to switch profile"));
        } finally {
            setIsLoading(false);
        }
    };

    const updateHotkeysConfig = async (hotkeysConfig: Hotkey[], immediate: boolean = false): Promise<void> => {
        try {
            const data = await sendWebSocketRequest('update_hotkeys_config', { hotkeysConfig }, immediate);
            if(data) {
                setHotkeysConfig(data.hotkeysConfig as Hotkey[]);
            }
            return Promise.resolve();
        } catch (err) {
            fetchHotkeysConfig(true); // 如果更新失败，则重新拉取最新的hotkeys配置
            return Promise.reject(new Error("Failed to update hotkeys config"));
        } finally {
        }
    };

    const rebootSystem = async (immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            await sendWebSocketRequest('reboot', {}, immediate);
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
            const data = await sendWebSocketRequest('ms_get_list', {}, immediate);
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
            const data = await sendWebSocketRequest('ms_get_default', {}, immediate);
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

    const createMapping = async (name: string, length: number, step: number, immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('ms_create_mapping', { name, length, step }, immediate);
            if (data && 'mappingList' in data && 'defaultMappingId' in data) {
                setMappingList(data.mappingList as { id: string, name: string }[]);
                setDefaultMappingId(data.defaultMappingId as string);
            }
            setError(null);
            return Promise.resolve();
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
            const data = await sendWebSocketRequest('ms_delete_mapping', { id }, immediate);
            if (data && 'mappingList' in data && 'defaultMappingId' in data) {
                setMappingList(data.mappingList as { id: string, name: string }[]);
                setDefaultMappingId(data.defaultMappingId as string);
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
            const data = await sendWebSocketRequest('ms_set_default', { id }, immediate);
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
            const data = await sendWebSocketRequest('ms_mark_mapping_start', { id }, immediate);
            if(data.status) {
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
            const data = await sendWebSocketRequest('ms_mark_mapping_stop', {}, immediate);
            if(data.status) {
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
            const data = await sendWebSocketRequest('ms_mark_mapping_step', {}, immediate);
            if(data.status) {
                setMarkingStatus(data.status);
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

    // const fetchMarkingStatus = async (): Promise<void> => {
    //     try {
    //         const data = await sendWebSocketRequest('ms_get_mark_status');
    //         if(data.status) {
    //             setMarkingStatus(data.status);
    //         }
    //         setError(null);
    //         return Promise.resolve();
    //     } catch (err) {
    //         setError(err instanceof Error ? err.message : 'An error occurred');
    //         return Promise.reject(new Error("Failed to fetch marking status"));
    //     }
    // };

    const fetchActiveMapping = async (id: string, immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('ms_get_mapping', { id }, immediate);
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
            const data = await sendWebSocketRequest('ms_rename_mapping', { id, name }, immediate);
            setMappingList(data.mappingList);
            setDefaultMappingId(data.defaultMappingId);
            setActiveMapping({...(activeMapping as ADCValuesMapping), name: name});
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
            const data = await sendWebSocketRequest('get_global_config', {}, immediate);
            console.log('fetchGlobalConfig', data);
            setGlobalConfig(data.globalConfig);
            return Promise.resolve();
        } catch (err) {
            // setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch global config"));
        } finally {
            // setIsLoading(false);
        }
    };

    const updateGlobalConfig = async (globalConfig: GlobalConfig, immediate: boolean = true): Promise<void> => {
        try {
            // setIsLoading(true);
            setGlobalConfig(globalConfig);
            const data = await sendWebSocketRequest('update_global_config', { globalConfig }, immediate);
            return Promise.resolve();
        } catch (err) {
            setError('Failed to update global config');
            return Promise.reject(new Error("Failed to update global config"));
        } finally {
            // setIsLoading(false);
        }
    };

    const startManualCalibration = async (immediate: boolean = true): Promise<CalibrationStatus> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('start_manual_calibration', {}, immediate);
            setError(null);
            return Promise.resolve(data.calibrationStatus);
        } catch (err) {
            // setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to start manual calibration"));
        } finally {
            setIsLoading(false);
        }
    };

    const stopManualCalibration = async (immediate: boolean = true): Promise<CalibrationStatus> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('stop_manual_calibration', {}, immediate);
            setError(null);
            return Promise.resolve(data.calibrationStatus);
        } catch (err) {
            // setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to stop manual calibration"));
        } finally {
            setIsLoading(false);
        }
    };

    const clearManualCalibrationData = async (immediate: boolean = true): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('clear_manual_calibration_data', {}, immediate);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            // setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to clear manual calibration data"));
        } finally {
            setIsLoading(false);
        }
    };

    const startButtonMonitoring = async (immediate: boolean = true): Promise<void> => {
        try {
            // setIsLoading(true);
            const data = await sendWebSocketRequest('start_button_monitoring', {}, immediate);
            setButtonMonitoringActive(data.isActive ?? true);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            // setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to start button monitoring"));
        } finally {
            // setIsLoading(false);
        }
    };

    const stopButtonMonitoring = async (immediate: boolean = true): Promise<void> => {
        try {
            // setIsLoading(true);
            const data = await sendWebSocketRequest('stop_button_monitoring', {}, immediate);
            setButtonMonitoringActive(data.isActive ?? false);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            // setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to stop button monitoring"));
        } finally {
            // setIsLoading(false);
        }
    };

    /**
     * @deprecated 已废弃：现在使用服务器推送模式，请监听 EVENTS.BUTTON_STATE_CHANGED 事件
     * 保留此方法仅用于兼容性，推荐使用推送模式获取按键状态变化
     */
    const getButtonStates = async (): Promise<ButtonStates> => {
        setError(null);
        try {
            const data = await sendWebSocketRequest('get_button_states');
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
            const data = await sendWebSocketRequest('start_button_performance_monitoring', {}, immediate);
            setButtonMonitoringActive(data.isActive ?? false);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            return Promise.reject(new Error("Failed to start button performance monitoring"));
        }
    };

    const stopButtonPerformanceMonitoring = async (immediate: boolean = true): Promise<void> => {
        try {
            const data = await sendWebSocketRequest('stop_button_performance_monitoring', {}, immediate);
            setButtonMonitoringActive(data.isActive ?? false);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            return Promise.reject(new Error("Failed to stop button performance monitoring"));
        }
    };

    // LED 配置相关
    const pushLedsConfig = async (ledsConfig: LEDsConfig, immediate: boolean = true): Promise<void> => {
        setError(null);
        try {
            await sendWebSocketRequest('push_leds_config', ledsConfig as unknown as Record<string, unknown>, immediate);
            return Promise.resolve();
        } catch (error) {
            setError(error instanceof Error ? error.message : 'An error occurred');
            return Promise.reject(new Error("Failed to push LED configuration"));
        }
    };

    const clearLedsPreview = async (immediate: boolean = true): Promise<void> => {
        setError(null);
        try {
            await sendWebSocketRequest('clear_leds_preview', {}, immediate);
            return Promise.resolve();
        } catch (error) {
            setError(error instanceof Error ? error.message : 'An error occurred');
            return Promise.reject(new Error("Failed to clear LED preview"));
        }
    };
    
    const fetchFirmwareMetadata = async (immediate: boolean = true): Promise<void> => {
        try {
            const data = await sendWebSocketRequest('get_firmware_metadata', {}, immediate);
            setFirmwareInfo({
                firmware: data
            });
            return Promise.resolve();
        } catch (err) {
            return Promise.reject(new Error("Failed to fetch firmware metadata"));
        }
    };

    // 生成默认的固件更新信息，主要用于请求固件更新信息失败时返回，显示固件无需更新
    const makeDefaultFirmwareUpdateInfo = (): FirmwareUpdateCheckResponse => {
        return {
            currentVersion: firmwareInfo?.firmware?.version || '',
            updateAvailable: false,
            updateCount: 0,
            checkTime: new Date().toISOString(),
            latestVersion: firmwareInfo?.firmware?.version || '',
            latestFirmware: {
                id: '',
                name: '',
                version: '',
                desc: '',
                createTime: '',
                updateTime: '',
                slotA: null,
                slotB: null,
            },
            availableUpdates: []
        };
    }

    const checkFirmwareUpdate = async (currentVersion: string, customServerHost?: string): Promise<void> => {
        try {
            // 构建请求数据
            const requestData: FirmwareUpdateCheckRequest = {
                currentVersion: currentVersion.trim()
            };
            
            // 确定服务器地址
            const serverHost = customServerHost || firmwareServerHost || FIRMWARE_SERVER_CONFIG.defaultHost;
            const url = `${serverHost}${FIRMWARE_SERVER_CONFIG.endpoints.checkUpdate}`;
            
            // 获取设备认证管理器
            const authManager = DeviceAuthManager.getInstance();
            
            // 重试逻辑：最多重试2次
            const maxRetries = 2;
            let attempt = 0;
            let lastError: any = null;
            
            while (attempt <= maxRetries) {
                try {
                    // 获取设备认证信息
                    const authInfo = await authManager.getValidAuth();
                    
                    if (!authInfo) {
                        throw new Error('无法获取设备认证信息');
                    }
                    
                    console.log(`🚀 开始固件更新检查 (尝试 ${attempt + 1}/${maxRetries + 1})`);
                    
                    // 直接请求服务器，认证信息放在body中
                    const response = await fetch(url, {
                        method: 'POST',
                        headers: {
                            'Content-Type': 'application/json',
                        },
                        body: JSON.stringify({
                            ...requestData,
                            deviceAuth: authInfo
                        })
                    });

                    // 检查HTTP状态
                    if (!response.ok) {
                        throw new Error(`HTTP error: ${response.status} ${response.statusText}`);
                    }
                    
                    const responseData = await response.json();
                    
                    // 检查服务器返回的错误
                    if (responseData.errNo && responseData.errNo !== 0) {
                        // 检查是否是认证相关错误
                        const authErrorCodes = [
                            'AUTH_MISSING', 'AUTH_INVALID_FORMAT', 'AUTH_INCOMPLETE',
                            'DEVICE_NOT_REGISTERED', 'INVALID_SIGNATURE', 'CHALLENGE_REUSED',
                            'AUTH_SERVER_ERROR', 'CHALLENGE_EXPIRED'
                        ];
                        
                        if (authErrorCodes.includes(responseData.errorCode)) {
                            console.log(`🔄 检测到认证错误: ${responseData.errorCode}，尝试重新获取认证信息`);
                            
                            // 处理认证错误并重新获取认证信息
                            await authManager.handleAuthError(responseData);
                            
                            // 如果不是最后一次尝试，继续重试
                            if (attempt < maxRetries) {
                                attempt++;
                                console.log(`🔁 认证错误重试 ${attempt}/${maxRetries}`);
                                continue;
                            }
                        }
                        
                        throw new Error(`Server error: ${responseData.errorMessage || 'Unknown error'}`);
                    }
                    
                    // 请求成功，设置更新信息
                    console.log('✅ 固件更新检查成功');
                    setFirmwareUpdateInfo(responseData.data);
                    return Promise.resolve();
                    
                } catch (error) {
                    console.error(`❌ 固件更新检查失败 (尝试 ${attempt + 1}):`, error);
                    lastError = error;
                    
                    // 如果是认证相关错误，尝试重新获取认证信息
                    if (error instanceof Error && 
                        (error.message.includes('认证') || 
                         error.message.includes('auth') || 
                         error.message.includes('AUTH'))) {
                        
                        console.log('🔄 检测到认证错误，尝试重新获取认证信息');
                        await authManager.handleAuthError(error);
                        
                        // 如果不是最后一次尝试，继续重试
                        if (attempt < maxRetries) {
                            attempt++;
                            console.log(`🔁 认证错误重试 ${attempt}/${maxRetries}`);
                            continue;
                        }
                    }
                    
                    // 如果不是认证错误，或者已经是最后一次尝试，跳出循环
                    break;
                }
            }
            
            // 如果所有重试都失败了，返回默认的固件更新信息
            console.log('❌ 所有重试都失败，返回默认固件更新信息');
            setFirmwareUpdateInfo(makeDefaultFirmwareUpdateInfo());
            return Promise.resolve();
            
        } catch (err) {
            console.error('❌ 固件更新检查异常:', err);
            setFirmwareUpdateInfo(makeDefaultFirmwareUpdateInfo());
            return Promise.resolve();
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
            const response = await fetch(downloadUrl);
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
        try {
            // 生成会话ID
            const sessionId = generateSessionId();

            // 创建升级会话
            const sessionData = await sendWebSocketRequest('create_firmware_upgrade_session', {
                session_id: sessionId,
                manifest: firmwarePackage.manifest
            }, true);

            const deviceSessionId = sessionData.session_id || sessionId;

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
                const totalChunks = Math.ceil(componentData.length / upgradeConfig.chunkSize);

                // 解析组件基地址（支持十六进制格式）
                let baseAddress: number;
                
                if(component.address.toString().startsWith('0x')){
                    baseAddress = parseInt(component.address.toString(), 16);
                }else if(component.address.toString().startsWith('0X')){
                    baseAddress = parseInt(component.address.toString(), 16);
                }else{
                    baseAddress = parseInt(component.address.toString(), 10);
                }

                // 分片传输
                for (let chunkIndex = 0; chunkIndex < totalChunks; chunkIndex++) {
                    const start = chunkIndex * upgradeConfig.chunkSize;
                    const end = Math.min(start + upgradeConfig.chunkSize, componentData.length);
                    const chunkData = componentData.slice(start, end);
                    
                    // 计算当前chunk的精确写入地址和偏移
                    const chunkOffset = parseInt(start.toString(), 10);
                    const targetAddress = baseAddress + chunkOffset;
                    const chunkSize = chunkData.length;

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

                    // 重试机制
                    let retryCount = 0;
                    let success = false;
                    let sessionRecreated = false; // 标记是否已重新创建会话

                    while (retryCount <= upgradeConfig.maxRetries && !success) {
                        try {
                            // 尝试使用二进制传输（如果WebSocket框架支持）
                            let chunkResult: any;
                            
                            console.log('WebSocket框架检查:', {
                                wsFramework: !!wsFramework,
                                sendBinaryMessage: typeof wsFramework?.sendBinaryMessage,
                                onBinaryMessage: typeof wsFramework?.onBinaryMessage
                            });
                            
                            if (wsFramework && typeof wsFramework.sendBinaryMessage === 'function') {
                                // 使用二进制传输
                                console.log('使用二进制传输模式上传固件分片');
                                chunkResult = await sendBinaryFirmwareChunk(
                                    deviceSessionId,
                                    componentName,
                                    chunkIndex,
                                    totalChunks,
                                    chunkSize,
                                    chunkOffset,
                                    targetAddress,
                                    checksum,
                                    chunkData
                                );
                            } else {
                                // 降级到JSON+Base64传输
                                console.log('降级到JSON+Base64传输模式');
                                const base64Data = btoa(String.fromCharCode(...chunkData));
                                
                                // 准备WebSocket请求参数
                                const chunkParams = {
                                    session_id: deviceSessionId,
                                    component_name: componentName,
                                    chunk_index: chunkIndex,
                                    total_chunks: totalChunks,
                                    target_address: `0x${targetAddress.toString(16).toUpperCase()}`,
                                    chunk_size: chunkSize,
                                    chunk_offset: chunkOffset,
                                    checksum: checksum,
                                    data: base64Data
                                };
                                
                                chunkResult = await sendWebSocketRequest('upload_firmware_chunk', chunkParams, true);
                            }
                            
                            if (!chunkResult.success) {
                                // 检查是否是会话不存在的错误
                                if (chunkResult.error && chunkResult.error.includes('session') && chunkResult.error.includes('not found') && !sessionRecreated) {
                                    console.warn('Session lost, attempting to recreate session...');
                                    
                                    // 重新创建会话
                                    const recreateResult = await sendWebSocketRequest('create_firmware_upgrade_session', {
                                        session_id: deviceSessionId,
                                        manifest: firmwarePackage.manifest
                                    }, true);

                                    if (recreateResult.success) {
                                        sessionRecreated = true;
                                        console.log('Session recreated successfully, retrying chunk upload...');
                                        // 不增加重试计数，直接重试
                                        continue;
                                    } else {
                                        throw new Error(`Failed to recreate session: ${recreateResult.error || 'Unknown error'}`);
                                    }
                                } else {
                                    throw new Error(`Chunk verification failed: ${chunkResult.error || 'Unknown error'}`);
                                }
                            }

                            success = true;
                        } catch (error) {
                            retryCount++;
                            if (retryCount <= upgradeConfig.maxRetries) {
                                // 递增延迟重试
                                const delay = Math.min(1000 * Math.pow(2, retryCount - 1), 5000);
                                await new Promise(resolve => setTimeout(resolve, delay));
                                console.warn(`Chunk ${chunkIndex} upload failed, retrying (${retryCount}/${upgradeConfig.maxRetries})...`);
                            } else {
                                throw new Error(`Chunk ${chunkIndex} upload failed after ${upgradeConfig.maxRetries} retries: ${error instanceof Error ? error.message : 'Unknown error'}`);
                            }
                        }
                    }

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
            const completeResult = await sendWebSocketRequest('complete_firmware_upgrade_session', {
                session_id: deviceSessionId
            }, true);

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
            // 错误处理：尝试中止会话
            if (upgradeSession?.sessionId) {
                try {
                    await sendWebSocketRequest('abort_firmware_upgrade_session', {
                        session_id: upgradeSession.sessionId
                    }, true);
                } catch (abortError) {
                    console.error('Failed to abort upgrade session:', abortError);
                }
            }

            setUpgradeSession(prev => prev ? {
                ...prev,
                status: 'failed',
                error: error instanceof Error ? error.message : 'Unknown error'
            } : null);

            onProgress?.({
                stage: 'failed',
                progress: 0,
                message: `Upload failed: ${error instanceof Error ? error.message : 'Unknown error'}`
            });

            throw error;
        }
    };

    // 二进制固件分片传输函数
    const sendBinaryFirmwareChunk = async (
        sessionId: string,
        componentName: string,
        chunkIndex: number,
        totalChunks: number,
        chunkSize: number,
        chunkOffset: number,
        targetAddress: number,
        checksum: string,
        chunkData: Uint8Array
    ): Promise<any> => {
        console.log('sendBinaryFirmwareChunk called:', {
            sessionId,
            componentName,
            chunkIndex,
            totalChunks,
            chunkSize,
            wsFramework: !!wsFramework,
            wsFrameworkMethods: wsFramework ? Object.getOwnPropertyNames(wsFramework) : 'null'
        });
        
        if (!wsFramework) {
            throw new Error('WebSocket framework not available');
        }

        // 构建二进制消息头部（82字节固定大小）
        const BINARY_CMD_UPLOAD_FIRMWARE_CHUNK = 0x01;
        const headerSize = 82; // 修正头部大小：1+1+2+32+2+16+4+4+4+4+4+8 = 82字节
        const header = new ArrayBuffer(headerSize);
        const headerView = new DataView(header);
        const headerBytes = new Uint8Array(header);

        // 填充头部数据
        let offset = 0;
        
        // command (1 byte)
        headerView.setUint8(offset, BINARY_CMD_UPLOAD_FIRMWARE_CHUNK);
        offset += 1;
        
        // reserved1 (1 byte)
        headerView.setUint8(offset, 0);
        offset += 1;
        
        // session_id_len (2 bytes, little-endian)
        const sessionIdBytes = new TextEncoder().encode(sessionId);
        const sessionIdLen = Math.min(sessionIdBytes.length, 31); // 最多31字节，保留1字节给null terminator
        headerView.setUint16(offset, sessionIdLen, true);
        offset += 2;
        
        // session_id (32 bytes)
        headerBytes.set(sessionIdBytes.slice(0, sessionIdLen), offset);
        offset += 32;
        
        // component_name_len (2 bytes, little-endian)
        const componentNameBytes = new TextEncoder().encode(componentName);
        const componentNameLen = Math.min(componentNameBytes.length, 15); // 最多15字节，保留1字节给null terminator
        headerView.setUint16(offset, componentNameLen, true);
        offset += 2;
        
        // component_name (16 bytes)
        headerBytes.set(componentNameBytes.slice(0, componentNameLen), offset);
        offset += 16;
        
        // chunk_index (4 bytes, little-endian)
        headerView.setUint32(offset, chunkIndex, true);
        offset += 4;
        
        // total_chunks (4 bytes, little-endian)
        headerView.setUint32(offset, totalChunks, true);
        offset += 4;
        
        // chunk_size (4 bytes, little-endian)
        headerView.setUint32(offset, chunkSize, true);
        offset += 4;
        
        // chunk_offset (4 bytes, little-endian)
        headerView.setUint32(offset, chunkOffset, true);
        offset += 4;
        
        // target_address (4 bytes, little-endian)
        headerView.setUint32(offset, targetAddress, true);
        offset += 4;
        
        // checksum (8 bytes) - SHA256的前8字节
        const checksumBytes = new Uint8Array(8);
        for (let i = 0; i < 8 && i * 2 < checksum.length; i++) {
            checksumBytes[i] = parseInt(checksum.substr(i * 2, 2), 16);
        }
        headerBytes.set(checksumBytes, offset);

        // 合并头部和数据
        const totalSize = headerSize + chunkData.length;
        const binaryMessage = new Uint8Array(totalSize);
        binaryMessage.set(headerBytes, 0);
        binaryMessage.set(chunkData, headerSize);

        // 发送二进制消息
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error('Binary chunk upload timeout'));
            }, upgradeConfig.timeout);

            // 监听二进制响应
            const handleBinaryResponse = (data: ArrayBuffer) => {
                clearTimeout(timeout);
                
                try {
                    // 解析二进制响应
                    const responseView = new DataView(data);
                    const responseCommand = responseView.getUint8(0);
                    
                    if (responseCommand === 0x81) { // 响应命令
                        const success = responseView.getUint8(1) === 1;
                        const responseChunkIndex = responseView.getUint32(2, true);
                        const progress = responseView.getUint32(6, true);
                        const errorLen = responseView.getUint8(10);
                        
                        let errorMessage = '';
                        if (!success && errorLen > 0) {
                            const errorBytes = new Uint8Array(data, 11, errorLen);
                            errorMessage = new TextDecoder().decode(errorBytes);
                        }
                        
                        resolve({
                            success,
                            chunk_index: responseChunkIndex,
                            progress,
                            error: success ? null : errorMessage
                        });
                    } else {
                        reject(new Error('Invalid binary response command'));
                    }
                } catch (error) {
                    reject(new Error(`Failed to parse binary response: ${error}`));
                }
            };

            // 注册一次性二进制消息监听器
            if (typeof wsFramework.onBinaryMessage === 'function') {
                console.log('二进制消息监听器注册成功');
                const unsubscribe = wsFramework.onBinaryMessage(handleBinaryResponse);
                
                // 发送二进制消息
                try {
                    console.log('发送二进制消息:', {
                        messageSize: binaryMessage.length,
                        headerSize,
                        chunkDataSize: chunkData.length,
                        wsFrameworkState: wsFramework.getState ? wsFramework.getState() : 'unknown'
                    });
                    wsFramework.sendBinaryMessage(binaryMessage);
                    console.log('二进制消息发送成功');
                } catch (error) {
                    console.error('二进制消息发送失败:', error);
                    clearTimeout(timeout);
                    if (unsubscribe) unsubscribe();
                    reject(error);
                }
                
                // 确保在响应后取消监听
                const originalResolve = resolve;
                const originalReject = reject;
                resolve = (value: any) => {
                    if (unsubscribe) unsubscribe();
                    originalResolve(value);
                };
                reject = (reason: any) => {
                    if (unsubscribe) unsubscribe();
                    originalReject(reason);
                };
            } else {
                clearTimeout(timeout);
                reject(new Error('Binary message not supported by WebSocket framework'));
            }
        });
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
    const sendPendingCommandImmediately = (command: string): boolean => {
        if (wsFramework) {
            return wsFramework.sendPendingCommandImmediately(command);
        }
        return false;
    };

    // 快速清空队列
    const flushQueue = async (): Promise<void> => {
        if (wsFramework) {
            await wsFramework.flushQueue();
        }
    };

    // WebSocket配置管理
    const getWebSocketConfig = () => {
        return websocketConfig;
    };

    const updateWebSocketConfig = (config: Partial<WebSocketConfigType>) => {
        setWebsocketConfig(prevConfig => ({
            ...prevConfig,
            ...config
        }));
        console.log('WebSocket配置已更新:', config);
    };


    return (
        <GamepadConfigContext.Provider value={{
            contextJsReady,
            setContextJsReady,

            // WebSocket 状态
            wsConnected,
            showReconnect,
            wsState,
            wsError,
            connectWebSocket,
            disconnectWebSocket,
            
            globalConfig,
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
            updateProfileDetails,
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
            startManualCalibration,
            stopManualCalibration,
            clearManualCalibrationData,
            // ADC Mapping 相关
            defaultMappingId: defaultMappingId,
            markingStatus,
            mappingList,
            activeMapping,
            fetchMappingList,
            // fetchMarkingStatus,
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
            setUpgradeConfig: setUpgradeConfig,
            getUpgradeConfig: getUpgradeConfig,
            getValidChunkSizes: getValidChunkSizes,
            updateMarkingStatus: updateMarkingStatus,
            sendPendingCommandImmediately: sendPendingCommandImmediately,
            flushQueue: flushQueue,
            getWebSocketConfig: getWebSocketConfig,
            updateWebSocketConfig: updateWebSocketConfig,
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