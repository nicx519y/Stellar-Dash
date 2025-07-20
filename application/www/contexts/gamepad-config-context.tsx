'use client';

import React, { createContext, useContext, useState, useEffect, useRef, useMemo } from 'react';
import JSZip from 'jszip';
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
    FirmwareComponent, 
    FirmwareManifest, 
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

// 固件服务器配置
const FIRMWARE_SERVER_CONFIG = {
    // 默认固件服务器地址，可通过环境变量覆盖
    defaultHost: process.env.NEXT_PUBLIC_FIRMWARE_SERVER_HOST || DEFAULT_FIRMWARE_SERVER_HOST,
    // API 端点
    endpoints: {
        checkUpdate: '/api/firmware-check-update'
    }
};

// 工具函数：计算数据的SHA256校验和
const calculateSHA256 = async (data: Uint8Array): Promise<string> => {
    // 检查Web Crypto API是否可用
    if (typeof crypto === 'undefined' || 
        typeof crypto.subtle === 'undefined' || 
        typeof crypto.subtle.digest !== 'function') {
        console.warn('Web Crypto API not available, falling back to JS implementation');
        return calculateSHA256JS(data);
    }

    // 检查是否在安全上下文中（HTTPS或localhost）
    if (typeof window !== 'undefined' && 
        window.location && 
        window.location.protocol !== 'https:' && 
        !window.location.hostname.match(/^(localhost|127\.0\.0\.1|::1)$/)) {
        console.warn('Web Crypto API requires secure context (HTTPS), falling back to JS implementation');
        return calculateSHA256JS(data);
    }

    try {
        const hashBuffer = await crypto.subtle.digest('SHA-256', data);
        const hashArray = Array.from(new Uint8Array(hashBuffer));
        return hashArray.map(b => b.toString(16).padStart(2, '0')).join('');
    } catch (error) {
        console.error('Web Crypto API SHA256 calculation failed:', error);
        console.warn('Falling back to JS implementation');
        // 回退到JS实现
        return calculateSHA256JS(data);
    }
};

// 纯JavaScript SHA256实现 - 修复版本
const calculateSHA256JS = (data: Uint8Array): string => {
    // SHA-256 constants
    const K = [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    ];

    // Helper functions
    const rightRotate = (value: number, amount: number): number => {
        return ((value >>> amount) | (value << (32 - amount))) >>> 0;
    };

    const safeAdd = (...nums: number[]): number => {
        let result = 0;
        for (const num of nums) {
            result = (result + (num >>> 0)) >>> 0;
        }
        return result;
    };

    // Pre-processing: adding padding bits
    const msgLength = data.length;
    const bitLength = msgLength * 8;
    
    // Calculate padded length - 必须为64字节的倍数
    const paddedLength = Math.ceil((msgLength + 9) / 64) * 64;
    const padded = new Uint8Array(paddedLength);
    
    // Copy message
    padded.set(data);
    
    // Add '1' bit (plus zero padding to make it a byte)
    padded[msgLength] = 0x80;
    
    // Add length in bits as 64-bit big-endian integer to the end
    // 修复：正确处理64位长度编码
    const bitLengthHigh = Math.floor(bitLength / 0x100000000); // 高32位
    const bitLengthLow = bitLength >>> 0; // 低32位
    
    // 写入高32位（位56-63）
    padded[paddedLength - 8] = (bitLengthHigh >>> 24) & 0xff;
    padded[paddedLength - 7] = (bitLengthHigh >>> 16) & 0xff;
    padded[paddedLength - 6] = (bitLengthHigh >>> 8) & 0xff;
    padded[paddedLength - 5] = bitLengthHigh & 0xff;
    
    // 写入低32位（位0-31）
    padded[paddedLength - 4] = (bitLengthLow >>> 24) & 0xff;
    padded[paddedLength - 3] = (bitLengthLow >>> 16) & 0xff;
    padded[paddedLength - 2] = (bitLengthLow >>> 8) & 0xff;
    padded[paddedLength - 1] = bitLengthLow & 0xff;
    
    // Initialize hash values (first 32 bits of fractional parts of square roots of first 8 primes)
    let h0 = 0x6a09e667;
    let h1 = 0xbb67ae85;
    let h2 = 0x3c6ef372;
    let h3 = 0xa54ff53a;
    let h4 = 0x510e527f;
    let h5 = 0x9b05688c;
    let h6 = 0x1f83d9ab;
    let h7 = 0x5be0cd19;

    // Process message in 512-bit chunks
    for (let chunkStart = 0; chunkStart < paddedLength; chunkStart += 64) {
        const w = new Array(64);

        // Break chunk into sixteen 32-bit big-endian words
        for (let i = 0; i < 16; i++) {
            const j = chunkStart + i * 4;
            w[i] = (padded[j] << 24) | (padded[j + 1] << 16) | (padded[j + 2] << 8) | padded[j + 3];
            w[i] = w[i] >>> 0; // Ensure unsigned 32-bit
        }

        // Extend the sixteen 32-bit words into sixty-four 32-bit words
        for (let i = 16; i < 64; i++) {
            const s0 = rightRotate(w[i - 15], 7) ^ rightRotate(w[i - 15], 18) ^ (w[i - 15] >>> 3);
            const s1 = rightRotate(w[i - 2], 17) ^ rightRotate(w[i - 2], 19) ^ (w[i - 2] >>> 10);
            w[i] = safeAdd(w[i - 16], s0, w[i - 7], s1);
        }

        // Initialize working variables for this chunk
        let a = h0, b = h1, c = h2, d = h3, e = h4, f = h5, g = h6, h = h7;

        // Main loop
        for (let i = 0; i < 64; i++) {
            const S1 = rightRotate(e, 6) ^ rightRotate(e, 11) ^ rightRotate(e, 25);
            const ch = (e & f) ^ ((~e) & g);
            const temp1 = safeAdd(h, S1, ch, K[i], w[i]);
            const S0 = rightRotate(a, 2) ^ rightRotate(a, 13) ^ rightRotate(a, 22);
            const maj = (a & b) ^ (a & c) ^ (b & c);
            const temp2 = safeAdd(S0, maj);

            h = g;
            g = f;
            f = e;
            e = safeAdd(d, temp1);
            d = c;
            c = b;
            b = a;
            a = safeAdd(temp1, temp2);
        }

        // Add this chunk's hash to result so far
        h0 = safeAdd(h0, a);
        h1 = safeAdd(h1, b);
        h2 = safeAdd(h2, c);
        h3 = safeAdd(h3, d);
        h4 = safeAdd(h4, e);
        h5 = safeAdd(h5, f);
        h6 = safeAdd(h6, g);
        h7 = safeAdd(h7, h);
    }

    // Produce the final hash value
    return [h0, h1, h2, h3, h4, h5, h6, h7]
        .map(h => (h >>> 0).toString(16).padStart(8, '0'))
        .join('');
};

// 工具函数：解压固件包
const extractFirmwarePackage = async (data: Uint8Array): Promise<{ manifest: FirmwareManifest, components: { [key: string]: FirmwareComponent } }> => {
    try {
        // 使用JSZip解压ZIP文件
        const zip = await JSZip.loadAsync(data);
        
        // 1. 读取manifest.json
        const manifestFile = zip.file('manifest.json');
        if (!manifestFile) {
            throw new Error('firmware package is missing manifest.json file');
        }
        
        const manifestContent = await manifestFile.async('string');
        const manifest: FirmwareManifest = JSON.parse(manifestContent);
        
        // 验证manifest结构
        if (!manifest.version || !manifest.slot || !manifest.components || !Array.isArray(manifest.components)) {
            throw new Error('manifest.json format is invalid');
        }
        
        // 2. 读取所有组件文件
        const components: { [key: string]: FirmwareComponent } = {};
        
        for (const comp of manifest.components) {
            if (!comp.name || !comp.file || !comp.address || !comp.size || !comp.sha256) {
                throw new Error(`component ${comp.name || 'unknown'} config is incomplete`);
            }
            
            // 查找组件文件
            const componentFile = zip.file(comp.file);
            if (!componentFile) {
                throw new Error(`firmware package is missing component file: ${comp.file}`);
            }
            
            // 读取组件数据
            const componentData = await componentFile.async('uint8array');
            
            // 验证文件大小
            if (componentData.length !== comp.size) {
                console.warn(`component ${comp.name} file size mismatch: expected ${comp.size}, actual ${componentData.length}`);
            }
            
            // 验证SHA256校验和
            try {
                const calculatedHash = await calculateSHA256(componentData);
                if (calculatedHash !== comp.sha256) {
                    console.warn(`component ${comp.name} SHA256 checksum mismatch: expected ${comp.sha256}, actual ${calculatedHash}`);
                    // 在开发环境中可能需要抛出错误，这里先警告
                    // throw new Error(`component ${comp.name} SHA256 checksum mismatch`);
                }
            } catch (checksumError) {
                console.warn(`component ${comp.name} SHA256 checksum calculation failed:`, checksumError);
                // 继续处理，不因为校验和计算失败而中断
            }
            
            // 创建组件对象
            components[comp.name] = {
                ...comp,
                data: componentData
            };
        }
        
        return { manifest, components };
        
    } catch (error) {
        if (error instanceof Error) {
            throw new Error(`failed to extract firmware package: ${error.message}`);
        } else {
            throw new Error('failed to extract firmware package: unknown error');
        }
    }
};

// 创建自定义fetch函数来支持Keep-Alive
const createFetchWithKeepAlive = () => {
    return async (url: string, options: RequestInit = {}): Promise<Response> => {
        // 默认配置
        const defaultOptions: RequestInit = {
            keepalive: true,
            headers: {
                'Connection': 'keep-alive'
            }
        };

        // 如果body不是FormData，则添加Content-Type: application/json
        if (!(options.body instanceof FormData)) {
            (defaultOptions.headers as Record<string, string>)['Content-Type'] = 'application/json';
        }

        // 合并默认选项和传入的选项
        const mergedOptions: RequestInit = {
            ...defaultOptions,
            ...options,
            headers: {
                ...defaultOptions.headers,
                ...options.headers
            }
        };

        return fetch(url, mergedOptions);
    };
};

// 创建全局的fetch实例
const fetchWithKeepAlive = createFetchWithKeepAlive();


interface GamepadConfigContextType {
    contextJsReady: boolean;
    setContextJsReady: (ready: boolean) => void;
    
    // WebSocket 连接状态
    wsConnected: boolean;
    wsState: WebSocketState;
    wsError: WebSocketError | null;
    connectWebSocket: () => Promise<void>;
    disconnectWebSocket: () => void;
    
    profileList: GameProfileList;
    defaultProfile: GameProfile;
    hotkeysConfig: Hotkey[];
    globalConfig: GlobalConfig;
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
    fetchMarkingStatus: () => Promise<void>;
    renameMapping: (id: string, name: string) => Promise<void>;
    // 按键监控相关
    buttonMonitoringActive: boolean;
    startButtonMonitoring: () => Promise<void>;
    stopButtonMonitoring: () => Promise<void>;
    getButtonStates: () => Promise<ButtonStates>;
    // LED 配置相关
    pushLedsConfig: (ledsConfig: LEDsConfig) => Promise<void>;
    clearLedsPreview: () => Promise<void>;
    // 固件元数据相关
    firmwareInfo: DeviceFirmwareInfo | null;
    fetchFirmwareMetadata: () => Promise<void>;
    // 固件更新检查相关
    firmwareUpdateInfo: FirmwareUpdateCheckResponse | null;
    checkFirmwareUpdate: (currentVersion: string, customServerHost?: string) => Promise<void>;
    setFirmwareServerHost: (host: string) => void;
    getFirmwareServerHost: () => string;
    // 固件升级包下载和传输相关
    upgradeSession: FirmwareUpgradeSession | null;
    downloadFirmwarePackage: (downloadUrl: string, onProgress?: (progress: FirmwarePackageDownloadProgress) => void) => Promise<FirmwarePackage>;
    uploadFirmwareToDevice: (firmwarePackage: FirmwarePackage, onProgress?: (progress: FirmwarePackageDownloadProgress) => void) => Promise<void>;
    setUpgradeConfig: (config: Partial<FirmwareUpgradeConfig>) => void;
    getUpgradeConfig: () => FirmwareUpgradeConfig;
    getValidChunkSizes: () => number[];
    updateMarkingStatus: (status: StepInfo) => void;
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
    const [firmwareServerHost, setFirmwareServerHostState] = useState<string>(FIRMWARE_SERVER_CONFIG.defaultHost);
    const [upgradeSession, setUpgradeSession] = useState<FirmwareUpgradeSession | null>(null);
    const [upgradeConfig, setUpgradeConfigState] = useState<FirmwareUpgradeConfig>({
        chunkSize: DEFAULT_FIRMWARE_PACKAGE_CHUNK_SIZE, // 4KB默认分片大小
        maxRetries: DEFAULT_FIRMWARE_UPGRADE_MAX_RETRIES,
        timeout: DEFAULT_FIRMWARE_UPGRADE_TIMEOUT // 30秒超时
    });

    const contextJsReady = useMemo(() => {
        return jsReady;
    }, [jsReady]);

    // 处理通知消息
    const handleNotificationMessage = (message: WebSocketDownstreamMessage): void => {
        const { command, data } = message;

        switch (command) {
            case 'welcome':
                console.log('收到欢迎消息:', data);
                break;
            case 'notification':
                console.log('收到通知:', data);
                break;
            case 'config_changed':
                // 配置变更，重新获取数据
                fetchGlobalConfig();
                fetchProfileList();
                // 同时发布事件
                eventBus.emit(EVENTS.CONFIG_CHANGED, data);
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
            url: `ws://${window.location.hostname}:8081`,
            heartbeatInterval: 30000,
            reconnectInterval: 5000,
            maxReconnectAttempts: 10,
            timeout: 15000,
            autoReconnect: true
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
            // 只处理通知消息（没有CID的消息）
            if (message.cid === undefined) {
                handleNotificationMessage(message);
            }
            // 响应消息由WebSocketFramework内部处理
        });

        /**
         * 处理二进制消息推送
         */
        const unsubscribeBinary = framework.onBinaryMessage((data: ArrayBuffer) => {
            handleBinaryMessage(data);
        });

        setWsFramework(framework);

        // 清理函数
        return () => {
            unsubscribeState();
            unsubscribeError();
            unsubscribeMessage();
            unsubscribeBinary();
            framework.disconnect();
        };
    }, []);

    // 自动连接WebSocket
    useEffect(() => {
        if (wsFramework && wsState === WebSocketState.DISCONNECTED) {
            wsFramework.connect().catch(console.error);
        }
    }, [wsFramework, wsState]);

    // 当WebSocket连接成功后，初始化数据
    useEffect(() => {
        if (wsConnected && wsState === WebSocketState.CONNECTED) {
            // 设置DeviceAuthManager的WebSocket发送函数
            const authManager = DeviceAuthManager.getInstance();
            authManager.setWebSocketSendFunction(sendWebSocketRequest);
            
            fetchGlobalConfig();
            fetchProfileList();
            fetchHotkeysConfig();
        }
    }, [wsConnected, wsState]);

    useEffect(() => {
        if (profileList.defaultId !== "") {
            fetchDefaultProfile();
        }
    }, [profileList]);

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

    // 使用WebSocket发送请求的辅助函数
    const sendWebSocketRequest = async (command: string, params: any = {}): Promise<any> => {
        if(!wsFramework) {
            return Promise.reject(new Error('WebSocket框架未初始化'));
        }
        if (wsState !== WebSocketState.CONNECTED) {
            throw new Error('WebSocket未连接');
        }
        
        try {
            // WebSocket框架已经处理了错误，直接返回数据
            return await wsFramework.sendMessage(command, params);
        } catch (error) {
            if (error instanceof Error) {
                throw error;
            }
            throw new Error(`WebSocket请求失败: ${error}`);
        }
    };

    const fetchDefaultProfile = async (): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('get_default_profile');
            setDefaultProfile(converProfileDetails(data.profileDetails) ?? {});
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch default profile"));
        } finally {
            setIsLoading(false);
        }
    };

    const fetchProfileList = async (): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('get_profile_list');
            setProfileList(data.profileList);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch profile list"));
        } finally {
            setIsLoading(false);
        }
    };

    const fetchHotkeysConfig = async (): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('get_hotkeys_config');
            setHotkeysConfig(data.hotkeysConfig);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch hotkeys config"));
        } finally {
            setIsLoading(false);
        }
    };

    const updateProfileDetails = async (profileId: string, profileDetails: GameProfile): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('update_profile', { profileId, profileDetails });

            // 如果更新的是 profile 的 name，则需要重新获取 profile list
            if(profileDetails.hasOwnProperty("name") || profileDetails.hasOwnProperty("id")) {
                fetchProfileList();
            } else { // 否则更新 default profile
                setDefaultProfile(converProfileDetails(data.profileDetails) ?? {});
            }
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to update profile details"));
        } finally {
            setIsLoading(false);
        }
    };

    const resetProfileDetails = async (): Promise<void> => {
        await fetchDefaultProfile();
    };

    const createProfile = async (profileName: string): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('create_profile', { profileName });
            setProfileList(data.profileList);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to create profile"));
        } finally {
            setIsLoading(false);
        }
    };

    const deleteProfile = async (profileId: string): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('delete_profile', { profileId });
            setProfileList(data.profileList);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to delete profile"));
        } finally {
            setIsLoading(false);
        }
    };

    const switchProfile = async (profileId: string): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('switch_default_profile', { profileId });
            setProfileList(data.profileList);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to switch profile"));
        } finally {
            setIsLoading(false);
        }
    };

    const updateHotkeysConfig = async (hotkeysConfig: Hotkey[]): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('update_hotkeys_config', { hotkeysConfig });
            setHotkeysConfig(data.hotkeysConfig);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to update hotkeys config"));
        } finally {
            setIsLoading(false);
        }
    };

    const rebootSystem = async (): Promise<void> => {
        try {
            setIsLoading(true);
            await sendWebSocketRequest('reboot');
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to reboot system"));
        } finally {
            setIsLoading(false);
        }
    };

    const fetchMappingList = async (): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('ms_get_list');
            setMappingList(data.mappingList);
            setDefaultMappingId(data.defaultMappingId);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch mapping list"));
        } finally {
            setIsLoading(false);
        }
    };

    const fetchDefaultMapping = async (): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('ms_get_default');
            setDefaultMappingId(data.id ?? "");
            return Promise.resolve(data.id);
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch default mapping"));
        } finally {
            setIsLoading(false);
        }
    };

    const createMapping = async (name: string, length: number, step: number): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('ms_create_mapping', { name, length, step });
            setMappingList(data.mappingList);
            setDefaultMappingId(data.defaultMappingId);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to create mapping"));
        } finally {
            setIsLoading(false);
        }
    };

    const deleteMapping = async (id: string): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('ms_delete_mapping', { id });
            setMappingList(data.mappingList);
            setDefaultMappingId(data.defaultMappingId);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to delete mapping"));
        } finally {
            setIsLoading(false);
        }
    };

    const updateDefaultMapping = async (id: string): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('ms_set_default', { id });
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

    const startMarking = async (id: string): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('ms_mark_mapping_start', { id });
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

    const stopMarking = async (): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('ms_mark_mapping_stop');
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

    const stepMarking = async (): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('ms_mark_mapping_step');
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

    const fetchMarkingStatus = async (): Promise<void> => {
        try {
            const data = await sendWebSocketRequest('ms_get_mark_status');
            if(data.status) {
                setMarkingStatus(data.status);
            }
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch marking status"));
        }
    };

    const fetchActiveMapping = async (id: string): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('ms_get_mapping', { id });
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

    const renameMapping = async (id: string, name: string): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('ms_rename_mapping', { id, name });
            setMappingList(data.mappingList);
            setDefaultMappingId(data.defaultMappingId);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to rename mapping"));
        } finally {
            setIsLoading(false);
        }
    };

    const fetchGlobalConfig = async (): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('get_global_config');
            console.log('fetchGlobalConfig', data);
            setGlobalConfig(data.globalConfig);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch global config"));
        } finally {
            setIsLoading(false);
        }
    };

    const updateGlobalConfig = async (globalConfig: GlobalConfig): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('update_global_config', { globalConfig });
            setGlobalConfig(data.globalConfig);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to update global config"));
        } finally {
            setIsLoading(false);
        }
    };

    const startManualCalibration = async (): Promise<CalibrationStatus> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('start_manual_calibration');
            setError(null);
            return Promise.resolve(data.calibrationStatus);
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to start manual calibration"));
        } finally {
            setIsLoading(false);
        }
    };

    const stopManualCalibration = async (): Promise<CalibrationStatus> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('stop_manual_calibration');
            setError(null);
            return Promise.resolve(data.calibrationStatus);
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to stop manual calibration"));
        } finally {
            setIsLoading(false);
        }
    };

    const clearManualCalibrationData = async (): Promise<void> => {
        try {
            setIsLoading(true);
            const data = await sendWebSocketRequest('clear_manual_calibration_data');
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to clear manual calibration data"));
        } finally {
            setIsLoading(false);
        }
    };

    const startButtonMonitoring = async (): Promise<void> => {
        try {
            // setIsLoading(true);
            const data = await sendWebSocketRequest('start_button_monitoring');
            setButtonMonitoringActive(data.isActive ?? true);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to start button monitoring"));
        } finally {
            // setIsLoading(false);
        }
    };

    const stopButtonMonitoring = async (): Promise<void> => {
        try {
            // setIsLoading(true);
            const data = await sendWebSocketRequest('stop_button_monitoring');
            setButtonMonitoringActive(data.isActive ?? false);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
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

    // LED 配置相关
    const pushLedsConfig = async (ledsConfig: LEDsConfig): Promise<void> => {
        setError(null);
        try {
            await sendWebSocketRequest('push_leds_config', ledsConfig);
            return Promise.resolve();
        } catch (error) {
            setError(error instanceof Error ? error.message : 'An error occurred');
            return Promise.reject(new Error("Failed to push LED configuration"));
        }
    };

    const clearLedsPreview = async (): Promise<void> => {
        setError(null);
        try {
            await sendWebSocketRequest('clear_leds_preview');
            return Promise.resolve();
        } catch (error) {
            setError(error instanceof Error ? error.message : 'An error occurred');
            return Promise.reject(new Error("Failed to clear LED preview"));
        }
    };
    
    const fetchFirmwareMetadata = async (): Promise<void> => {
        try {
            const data = await sendWebSocketRequest('get_firmware_metadata');
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

    const setFirmwareServerHost = (host: string): void => {
        setFirmwareServerHostState(host);
    };

    const getFirmwareServerHost = (): string => {
        return firmwareServerHost;
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
            });

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
                                
                                chunkResult = await sendWebSocketRequest('upload_firmware_chunk', chunkParams);
                            }
                            
                            if (!chunkResult.success) {
                                // 检查是否是会话不存在的错误
                                if (chunkResult.error && chunkResult.error.includes('session') && chunkResult.error.includes('not found') && !sessionRecreated) {
                                    console.warn('Session lost, attempting to recreate session...');
                                    
                                    // 重新创建会话
                                    const recreateResult = await sendWebSocketRequest('create_firmware_upgrade_session', {
                                        session_id: deviceSessionId,
                                        manifest: firmwarePackage.manifest
                                    });

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
            });

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
                    });
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


    return (
        <GamepadConfigContext.Provider value={{
            contextJsReady,
            setContextJsReady,
            
            // WebSocket 状态
            wsConnected,
            wsState,
            wsError,
            connectWebSocket,
            disconnectWebSocket,
            
            globalConfig,
            profileList,
            defaultProfile,
            hotkeysConfig,
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
            // LED 配置相关
            pushLedsConfig: pushLedsConfig,
            clearLedsPreview: clearLedsPreview,
            // 固件元数据相关
            firmwareInfo,
            fetchFirmwareMetadata,
            // 固件更新检查相关
            firmwareUpdateInfo,
            checkFirmwareUpdate,
            setFirmwareServerHost,
            getFirmwareServerHost,
            // 固件升级包下载和传输相关
            upgradeSession: upgradeSession,
            downloadFirmwarePackage: downloadFirmwarePackage,
            uploadFirmwareToDevice: uploadFirmwareToDevice,
            setUpgradeConfig: setUpgradeConfig,
            getUpgradeConfig: getUpgradeConfig,
            getValidChunkSizes: getValidChunkSizes,
            updateMarkingStatus: updateMarkingStatus,
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