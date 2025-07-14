'use client';

import { createContext, useContext, useState, useEffect, useMemo } from 'react';
import JSZip from 'jszip';
import { GameProfile, 
        LedsEffectStyle, 
        AroundLedsEffectStyle,
        Platform, GameSocdMode, 
        GameControllerButton, Hotkey, RapidTriggerConfig, GameProfileList, GlobalConfig } from '@/types/gamepad-config';
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

import DeviceAuthManager from '@/utils/deviceAuth';

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
    calibrationStatus: CalibrationStatus;
    startManualCalibration: () => Promise<void>;
    stopManualCalibration: () => Promise<void>;
    fetchCalibrationStatus: () => Promise<void>;
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
    const [calibrationStatus, setCalibrationStatus] = useState<CalibrationStatus>({
        isActive: false,
        uncalibratedCount: 0,
        activeCalibrationCount: 0,
        allCalibrated: false,
        buttons: []
    });
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

    useEffect(() => {
        fetchGlobalConfig();
        fetchProfileList();
        fetchHotkeysConfig();
    }, []);

    useEffect(() => {
        if (profileList.defaultId !== "") {
            fetchDefaultProfile();
        }
    }, [profileList]);

    const setContextJsReady = (ready: boolean) => {
        setJsReady(ready);
    }

    const fetchDefaultProfile = async (): Promise<void> => {
        try {
            setIsLoading(true);
            const response = await fetchWithKeepAlive('/api/default-profile', {
                method: 'GET'
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to fetch default profile"));
            };
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
            const response = await fetchWithKeepAlive('/api/profile-list', {
                method: 'GET'
            }); 
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to fetch profile list"))    ;
            };
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
            const response = await fetchWithKeepAlive('/api/hotkeys-config', {
                method: 'GET'
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to fetch hotkeys config"));
            };
            setHotkeysConfig(data.hotkeysConfig);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch hotkeys config"));
        } finally {
            setIsLoading(false);
        }
    }

    const updateProfileDetails = async (profileId: string, profileDetails: GameProfile): Promise<void> => {
        try {
            setIsLoading(true);
            const response = await fetchWithKeepAlive('/api/update-profile', {
                method: 'POST',
                body: JSON.stringify({ profileId, profileDetails }),
            });

            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to update profile details"));
            };

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
            const response = await fetchWithKeepAlive('/api/create-profile', {
                method: 'POST',
                body: JSON.stringify({ profileName }),
            });

            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to create profile"));
            };
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
            const response = await fetchWithKeepAlive('/api/delete-profile', {
                method: 'POST',
                body: JSON.stringify({ profileId }),
            });

            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to delete profile"));
            };
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
            const response = await fetchWithKeepAlive('/api/switch-default-profile', {
                method: 'POST',
                body: JSON.stringify({ profileId }),
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to switch profile"));
            };
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
            const response = await fetchWithKeepAlive('/api/update-hotkeys-config', {
                method: 'POST',
                body: JSON.stringify({ hotkeysConfig }),
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to update hotkeys config"));
            }
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
            const response = await fetchWithKeepAlive('/api/reboot', {
                method: 'POST',
            });

            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to reboot system"));
            }
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
            const response = await fetchWithKeepAlive('/api/ms-get-list', {
                method: 'GET'
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to fetch mapping list"));
            }
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
            const response = await fetchWithKeepAlive('/api/ms-get-default', {
                method: 'GET'
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to fetch default mapping"));
            }
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
            const response = await fetchWithKeepAlive('/api/ms-create-mapping', {
                method: 'POST',
                body: JSON.stringify({ name, length, step }),
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to create mapping"));
            }
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
            const response = await fetchWithKeepAlive('/api/ms-delete-mapping', {
                method: 'POST',
                body: JSON.stringify({ id }),
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to delete mapping"));
            }
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
            const response = await fetchWithKeepAlive('/api/ms-set-default', {
                method: 'POST',
                body: JSON.stringify({ id }),
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to set default mapping"));
            }
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
            const response = await fetchWithKeepAlive('/api/ms-mark-mapping-start', {
                method: 'POST',
                body: JSON.stringify({ id }),
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to start marking"));
            }

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
            const response = await fetchWithKeepAlive('/api/ms-mark-mapping-stop', {
                method: 'GET'
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to stop marking"));
            }

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
            const response = await fetchWithKeepAlive('/api/ms-mark-mapping-step', {
                method: 'GET'
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to step marking"));
            }

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
            const response = await fetchWithKeepAlive('/api/ms-get-mark-status', {
                method: 'GET'
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to fetch marking status"));
            }

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
            const response = await fetchWithKeepAlive('/api/ms-get-mapping', {
                method: 'POST',
                body: JSON.stringify({ id }),
            });

            const data = await processResponse(response, setError);

            if (!data) {
                return Promise.reject(new Error("Failed to fetch mapping"));
            }

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
            const response = await fetchWithKeepAlive('/api/ms-rename-mapping', {
                method: 'POST',
                body: JSON.stringify({ id, name }),
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to rename mapping"));
            }
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
            const response = await fetchWithKeepAlive('/api/global-config', {
                method: 'GET'
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to fetch global config"));
            }
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
            const response = await fetchWithKeepAlive('/api/update-global-config', {
                method: 'POST',
                body: JSON.stringify({ globalConfig }),
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to update global config"));
            }
            setGlobalConfig(data.globalConfig);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to update global config"));
        } finally {
            setIsLoading(false);
        }
    };

    const startManualCalibration = async (): Promise<void> => {
        try {
            setIsLoading(true);
            const response = await fetchWithKeepAlive('/api/start-manual-calibration', {
                method: 'GET'
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to start manual calibration"));
            }
            setCalibrationStatus(data.calibrationStatus);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to start manual calibration"));
        } finally {
            setIsLoading(false);
        }
    };

    const stopManualCalibration = async (): Promise<void> => {
        try {
            setIsLoading(true);
            const response = await fetchWithKeepAlive('/api/stop-manual-calibration', {
                method: 'GET'
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to stop manual calibration"));
            }
            setCalibrationStatus(data.calibrationStatus);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to stop manual calibration"));
        } finally {
            setIsLoading(false);
        }
    };

    const fetchCalibrationStatus = async (): Promise<void> => {
        try {
            const response = await fetchWithKeepAlive('/api/get-calibration-status', {
                method: 'GET'
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to fetch calibration status"));
            }
            setCalibrationStatus(data.calibrationStatus);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch calibration status"));
        }
    };

    const clearManualCalibrationData = async (): Promise<void> => {
        try {
            setIsLoading(true);
            const response = await fetchWithKeepAlive('/api/clear-manual-calibration-data', {
                method: 'GET'
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to clear manual calibration data"));
            }
            setCalibrationStatus(data.calibrationStatus);
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
            setIsLoading(true);
            const response = await fetchWithKeepAlive('/api/start-button-monitoring', {
                method: 'GET'
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to start button monitoring"));
            }
            setButtonMonitoringActive(data.isActive ?? true);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to start button monitoring"));
        } finally {
            setIsLoading(false);
        }
    };

    const stopButtonMonitoring = async (): Promise<void> => {
        try {
            setIsLoading(true);
            const response = await fetchWithKeepAlive('/api/stop-button-monitoring', {
                method: 'GET'
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to stop button monitoring"));
            }
            setButtonMonitoringActive(data.isActive ?? false);
            setError(null);
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to stop button monitoring"));
        } finally {
            setIsLoading(false);
        }
    };

    const getButtonStates = async (): Promise<ButtonStates> => {
        setError(null);
        try {
            const response = await fetchWithKeepAlive('/api/get-button-states');
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to get button states"));
            }
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
        // setIsLoading(true);
        try {
            const response = await fetchWithKeepAlive('/api/push-leds-config', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify(ledsConfig),
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to push LED configuration"));
            }
            return Promise.resolve();
        } catch (error) {
            setError(error instanceof Error ? error.message : 'An error occurred');
            return Promise.reject(new Error("Failed to push LED configuration"));
        } finally {
            // setIsLoading(false);
        }
    };

    const clearLedsPreview = async (): Promise<void> => {
        setError(null);
        try {
            const response = await fetchWithKeepAlive('/api/clear-leds-preview', {
                method: 'GET'
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to clear LED preview"));
            }
            return Promise.resolve();
        } catch (error) {
            setError(error instanceof Error ? error.message : 'An error occurred');
            return Promise.reject(new Error("Failed to clear LED preview"));
        }
    };
    
    const fetchFirmwareMetadata = async (): Promise<void> => {
        try {
            // setIsLoading(true);
            const response = await fetchWithKeepAlive('/api/firmware-metadata', {
                method: 'GET'
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to fetch firmware metadata"));
            }
            setFirmwareInfo({
                firmware: data
            });
            return Promise.resolve();
        } catch (err) {
            // setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch firmware metadata"));
        } finally {
            // setIsLoading(false);
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
            // setIsLoading(true);
            
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
        } finally {
            // setIsLoading(false);
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
            // setIsLoading(true);
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
        } finally {
            // setIsLoading(false);
        }
    };

    // 上传固件到设备
    const uploadFirmwareToDevice = async (firmwarePackage: FirmwarePackage, onProgress?: (progress: FirmwarePackageDownloadProgress) => void): Promise<void> => {
        try {
            // 创建升级会话
            const sessionId = generateSessionId();
            const sessionResponse = await fetchWithKeepAlive('/api/firmware-upgrade', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    action: 'create',
                    session_id: sessionId,
                    manifest: firmwarePackage.manifest
                })
            });

            if (!sessionResponse.ok) {
                throw new Error(`Failed to create upgrade session: ${sessionResponse.statusText}`);
            }

            const sessionData = await sessionResponse.json();
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

                    // 创建FormData进行二进制传输
                    const formData = new FormData();
                    
                    // 添加元数据作为JSON字符串
                    const metadata = {
                        session_id: deviceSessionId,
                        component_name: componentName,
                        chunk_index: chunkIndex,
                        total_chunks: totalChunks,
                        target_address: `0x${targetAddress.toString(16).toUpperCase()}`,
                        chunk_size: chunkSize,
                        chunk_offset: chunkOffset,
                        checksum: checksum
                    };
                    
                    // 调试输出metadata对象
                    console.log('Metadata object before JSON.stringify:', metadata);
                    console.log('Checksum type:', typeof metadata.checksum);
                    console.log('Checksum length:', metadata.checksum.length);
                    
                    const metadataJsonString = JSON.stringify(metadata);
                    console.log('Metadata JSON string:', metadataJsonString);
                    
                    formData.append('metadata', metadataJsonString);
                    
                    // 添加二进制数据
                    const blob = new Blob([chunkData], { type: 'application/octet-stream' });
                    formData.append('data', blob, 'chunk.bin');

                    // 重试机制
                    let retryCount = 0;
                    let success = false;
                    let sessionRecreated = false; // 标记是否已重新创建会话

                    while (retryCount <= upgradeConfig.maxRetries && !success) {
                        try {
                            const chunkResponse = await fetchWithKeepAlive('/api/firmware-upgrade/chunk', {
                                method: 'POST',
                                body: formData
                            });

                            if (!chunkResponse.ok) {
                                throw new Error(`Chunk upload failed: ${chunkResponse.statusText}`);
                            }

                            const chunkResult = await chunkResponse.json();
                            if (chunkResult.errNo !== 0) {
                                // 检查是否是会话不存在的错误
                                if (chunkResult.errNo === 2 && chunkResult.errorCode === 'SESSION_NOT_FOUND' && !sessionRecreated) {
                                    console.warn('Session lost, attempting to recreate session...');
                                    
                                    // 重新创建会话
                                    const recreateResponse = await fetchWithKeepAlive('/api/firmware-upgrade', {
                                        method: 'POST',
                                        headers: {
                                            'Content-Type': 'application/json',
                                        },
                                        body: JSON.stringify({
                                            action: 'create',
                                            session_id: deviceSessionId,
                                            manifest: firmwarePackage.manifest
                                        })
                                    });

                                    if (recreateResponse.ok) {
                                        sessionRecreated = true;
                                        console.log('Session recreated successfully, retrying chunk upload...');
                                        // 不增加重试计数，直接重试
                                        continue;
                                    } else {
                                        throw new Error(`Failed to recreate session: ${recreateResponse.statusText}`);
                                    }
                                } else {
                                    throw new Error(`Chunk verification failed: ${chunkResult.errorMessage || 'Unknown error'}`);
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
            const completeResponse = await fetchWithKeepAlive('/api/firmware-upgrade-complete', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({
                    action: 'complete',
                    session_id: deviceSessionId
                })
            });

            if (!completeResponse.ok) {
                throw new Error(`Failed to complete upgrade session: ${completeResponse.statusText}`);
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
                    await fetchWithKeepAlive('/api/firmware-upgrade-abort', {
                        method: 'POST',
                        headers: {
                            'Content-Type': 'application/json',
                        },
                        body: JSON.stringify({
                            action: 'abort',
                            session_id: upgradeSession.sessionId
                        })
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


    return (
        <GamepadConfigContext.Provider value={{
            contextJsReady,
            setContextJsReady,
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
            calibrationStatus,
            startManualCalibration,
            stopManualCalibration,
            fetchCalibrationStatus,
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