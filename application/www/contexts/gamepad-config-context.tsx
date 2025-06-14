'use client';

import { createContext, useContext, useState, useEffect, useMemo } from 'react';
import JSZip from 'jszip';
import { GameProfile, 
        LedsEffectStyle, 
        Platform, GameSocdMode, 
        GameControllerButton, Hotkey, RapidTriggerConfig, GameProfileList, GlobalConfig } from '@/types/gamepad-config';
import { StepInfo, ADCValuesMapping } from '@/types/adc';
import { ButtonStates, CalibrationStatus, DeviceFirmwareInfo, FirmwareComponent, FirmwareManifest, FirmwareUpgradeConfig, FirmwareUpgradeSession, FirmwarePackage, FirmwareUpdateCheckResponse, LEDsConfig, FirmwarePackageDownloadProgress, FirmwareUpdateCheckRequest, ChunkTransferRequest } from '@/types/types';

// 固件服务器配置
const FIRMWARE_SERVER_CONFIG = {
    // 默认固件服务器地址，可通过环境变量覆盖
    defaultHost: process.env.NEXT_PUBLIC_FIRMWARE_SERVER_HOST || 'http://localhost:3000',
    // API 端点
    endpoints: {
        checkUpdate: '/api/firmware-check-update'
    }
};



// 工具函数：计算数据的SHA256校验和
const calculateSHA256 = async (data: Uint8Array): Promise<string> => {
    const hashBuffer = await crypto.subtle.digest('SHA-256', data);
    const hashArray = Array.from(new Uint8Array(hashBuffer));
    return hashArray.map(b => b.toString(16).padStart(2, '0')).join('');
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
            const calculatedHash = await calculateSHA256(componentData);
            if (calculatedHash !== comp.sha256) {
                console.warn(`component ${comp.name} SHA256 checksum mismatch: expected ${comp.sha256}, actual ${calculatedHash}`);
                // 在生产环境中可能需要抛出错误，这里先警告
                // throw new Error(`component ${comp.name} SHA256 checksum mismatch`);
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
    // 配置选项
    const defaultOptions: RequestInit = {
        keepalive: true,
        headers: {
            'Content-Type': 'application/json',
            'Connection': 'keep-alive'
        }
    };

    return async (url: string, options: RequestInit = {}): Promise<Response> => {
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
    firmwarePackage: FirmwarePackage | null;
    upgradeSession: FirmwareUpgradeSession | null;
    downloadFirmwarePackage: (downloadUrl: string, onProgress?: (progress: FirmwarePackageDownloadProgress) => void) => Promise<void>;
    uploadFirmwareToDevice: (firmwarePackage: FirmwarePackage, onProgress?: (progress: FirmwarePackageDownloadProgress) => void) => Promise<void>;
    setUpgradeConfig: (config: Partial<FirmwareUpgradeConfig>) => void;
    getUpgradeConfig: () => FirmwareUpgradeConfig;
    clearFirmwarePackage: () => void;
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
        },
        ledsConfigs: {
            ledEnabled: profile.ledsConfigs?.ledEnabled as boolean ?? false,
            ledsEffectStyle: profile.ledsConfigs?.ledsEffectStyle as LedsEffectStyle ?? LedsEffectStyle.STATIC,
            ledColors: profile.ledsConfigs?.ledColors as string[] ?? ["#000000", "#000000", "#000000"],
            ledBrightness: profile.ledsConfigs?.ledBrightness as number ?? 100,
            ledAnimationSpeed: profile.ledsConfigs?.ledAnimationSpeed as number ?? 1,
        },
        hotkeys: profile.hotkeys as Hotkey[] ?? [],
        triggerConfigs: profile.triggerConfigs as { [key: number]: RapidTriggerConfig } ?? {},
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
    const [firmwarePackage, setFirmwarePackage] = useState<FirmwarePackage | null>(null);
    const [upgradeSession, setUpgradeSession] = useState<FirmwareUpgradeSession | null>(null);
    const [upgradeConfig, setUpgradeConfigState] = useState<FirmwareUpgradeConfig>({
        chunkSize: 4096, // 4KB默认分片大小
        maxRetries: 3,
        timeout: 30000 // 30秒超时
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
            setIsLoading(true);
            const response = await fetchWithKeepAlive('/api/firmware-metadata', {
                method: 'GET'
            });
            const data = await processResponse(response, setError);
            if (!data) {
                return Promise.reject(new Error("Failed to fetch firmware metadata"));
            }
            setFirmwareInfo({
                device_id: data.device_id,
                firmware: data.firmware
            });
            return Promise.resolve();
        } catch (err) {
            setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to fetch firmware metadata"));
        } finally {
            setIsLoading(false);
        }
    };

    const checkFirmwareUpdate = async (currentVersion: string, customServerHost?: string): Promise<void> => {
        try {
            setIsLoading(true);
            
            // 使用传入的服务器地址、状态中的地址或默认配置
            const serverHost = customServerHost || firmwareServerHost || FIRMWARE_SERVER_CONFIG.defaultHost;
            const updateCheckUrl = `${serverHost}${FIRMWARE_SERVER_CONFIG.endpoints.checkUpdate}`;
            
            // 构建请求数据
            const requestData: FirmwareUpdateCheckRequest = {
                currentVersion: currentVersion.trim()
            };
            
            // 直接请求固件服务器
            const response = await fetch(updateCheckUrl, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify(requestData),
            });
            
            if (!response.ok) {
                throw new Error(`Firmware Server Error: ${response.status} ${response.statusText}`);
            }
            
            const responseData = await response.json();
            
            // 检查服务器返回的错误
            if (responseData.errNo && responseData.errNo !== 0) {
                throw new Error(responseData.errorMessage || 'Firmware update check failed');
            }
            
            // 设置更新信息
            setFirmwareUpdateInfo(responseData.data);
            setError(null);
            return Promise.resolve();
            
        } catch (err) {
            const errorMessage = err instanceof Error ? err.message : 'Firmware update check failed';
            setError(errorMessage);
            return Promise.reject(new Error(errorMessage));
        } finally {
            setIsLoading(false);
        }
    };

    const setFirmwareServerHost = (host: string): void => {
        setFirmwareServerHostState(host);
    };

    const getFirmwareServerHost = (): string => {
        return firmwareServerHost;
    };

    // 工具函数：Base64编码
    const arrayBufferToBase64 = (buffer: Uint8Array): string => {
        return btoa(String.fromCharCode(...buffer));
    };

    // 工具函数：生成会话ID
    const generateSessionId = (): string => {
        return 'session_' + Date.now() + '_' + Math.random().toString(36).substr(2, 9);
    };

    // 下载固件包
    const downloadFirmwarePackage = async (downloadUrl: string, onProgress?: (progress: FirmwarePackageDownloadProgress) => void): Promise<void> => {
        try {
            // setIsLoading(true);
            setError(null);

            // 初始化进度
            const initialProgress: FirmwarePackageDownloadProgress = {
                stage: 'downloading',
                progress: 0,
                message: '开始下载固件包...'
            };
            onProgress?.(initialProgress);

            // 1. 下载固件包 (进度 0% - 30%)
            const response = await fetch(downloadUrl);
            if (!response.ok) {
                throw new Error(`下载失败: ${response.status} ${response.statusText}`);
            }

            const contentLength = parseInt(response.headers.get('content-length') || '0', 10);
            if (contentLength === 0) {
                throw new Error('无法获取文件大小');
            }

            const reader = response.body?.getReader();
            if (!reader) {
                throw new Error('无法读取响应数据');
            }

            const chunks: Uint8Array[] = [];
            let receivedLength = 0;

            while (true) {
                const { done, value } = await reader.read();
                if (done) break;

                chunks.push(value);
                receivedLength += value.length;

                // 下载进度占总进度的30%
                const downloadProgress = (receivedLength / contentLength) * 30;
                onProgress?.({
                    stage: 'downloading',
                    progress: downloadProgress,
                    message: `downloading firmware package... ${Math.round(receivedLength / 1024)}KB/${Math.round(contentLength / 1024)}KB`,
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
                progress: 30,
                message: 'extracting firmware package...'
            });

            // 2. 解压和验证包 (进度 30% - 40%)
            const { manifest, components } = await extractFirmwarePackage(packageData);

            // 3. 计算总大小
            onProgress?.({
                stage: 'extracting',
                progress: 35,
                message: 'preparing firmware package...'
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

            // 5. 存储到内存中
            setFirmwarePackage(firmwarePackage);

            onProgress?.({
                stage: 'downcompleted',
                progress: 40,
                message: `firmware package download completed! total size: ${Math.round(totalSize / 1024)}KB`
            });

            setError(null);
            return Promise.resolve();

        } catch (err) {
            const errorMessage = err instanceof Error ? err.message : 'failed to download firmware package: unknown error';
            setError(errorMessage);
            
            onProgress?.({
                stage: 'failed',
                progress: 0,
                message: 'download failed',
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
            // setIsLoading(true);
            setError(null);

            const { manifest, components, totalSize } = firmwarePackage;
            let transferredBytes = 0;

            // 生成会话ID
            const sessionId = generateSessionId();

            // 创建升级会话
            const session: FirmwareUpgradeSession = {
                sessionId,
                isActive: true,
                progress: {
                    stage: 'uploading',
                    progress: 0,
                    message: 'begin to upload firmware...'
                },
                config: upgradeConfig
            };
            setUpgradeSession(session);

            onProgress?.({
                stage: 'uploading',
                progress: 0,
                message: 'begin to start firmware upgrade session...'
            });

            // 1. 开始升级会话
            const sessionResponse = await fetchWithKeepAlive('/api/firmware-upgrade', {
                method: 'POST',
                body: JSON.stringify({
                    action: 'start_session',
                    manifest: manifest,
                    total_size: totalSize
                })
            });

            const sessionData = await processResponse(sessionResponse, setError);
            if (!sessionData) {
                throw new Error('failed to start firmware upgrade session');
            }

            const deviceSessionId = sessionData.session_id || sessionId;

            try {
                // 2. 按组件顺序传输 (进度 0% - 100%)
                for (const [componentName, component] of Object.entries(components)) {
                    if (!component.data) {
                        throw new Error(`component ${componentName} data is missing`);
                    }

                    onProgress?.({
                        stage: 'uploading',
                        progress: (transferredBytes / totalSize) * 100,
                        message: `uploading ${componentName}...`,
                        current_component: componentName,
                        bytes_transferred: transferredBytes,
                        total_bytes: totalSize
                    });

                    // 分片传输单个组件
                    const componentData = component.data;
                    const totalChunks = Math.ceil(componentData.length / upgradeConfig.chunkSize);

                    for (let chunkIndex = 0; chunkIndex < totalChunks; chunkIndex++) {
                        const start = chunkIndex * upgradeConfig.chunkSize;
                        const end = Math.min(start + upgradeConfig.chunkSize, componentData.length);
                        const chunkData = componentData.slice(start, end);

                        // 计算分片校验和
                        const chunkChecksum = await calculateSHA256(chunkData);

                        // 转换为Base64传输
                        const base64Chunk = arrayBufferToBase64(chunkData);

                        // 构建分片传输请求
                        const chunkRequest: ChunkTransferRequest = {
                            session_id: deviceSessionId,
                            component_name: componentName,
                            chunk_index: chunkIndex,
                            total_chunks: totalChunks,
                            target_address: component.address,
                            data: base64Chunk,
                            checksum: chunkChecksum
                        };

                        // 发送分片数据，支持重试
                        let retryCount = 0;
                        let chunkSuccess = false;

                        while (retryCount <= upgradeConfig.maxRetries && !chunkSuccess) {
                            try {
                                const chunkResponse = await fetchWithKeepAlive('/api/firmware-upgrade/chunk', {
                                    method: 'POST',
                                    body: JSON.stringify(chunkRequest)
                                });

                                const chunkResponseData = await processResponse(chunkResponse, setError);
                                if (!chunkResponseData) {
                                    throw new Error('invalid chunk transfer response');
                                }

                                chunkSuccess = true;
                                transferredBytes += chunkData.length;

                                // 更新进度
                                const overallProgress = (transferredBytes / totalSize) * 100;
                                const chunkProgress = (chunkIndex + 1) / totalChunks;

                                onProgress?.({
                                    stage: 'uploading',
                                    progress: overallProgress,
                                    message: `uploading ${componentName}... ${chunkIndex + 1}/${totalChunks}`,
                                    current_component: componentName,
                                    chunk_progress: chunkProgress,
                                    bytes_transferred: transferredBytes,
                                    total_bytes: totalSize
                                });

                                // 避免过快传输导致设备缓冲区溢出
                                await new Promise(resolve => setTimeout(resolve, 10));

                            } catch (chunkError) {
                                retryCount++;
                                if (retryCount <= upgradeConfig.maxRetries) {
                                    console.warn(`chunk transfer failed, retrying (${retryCount}/${upgradeConfig.maxRetries}):`, chunkError);
                                    await new Promise(resolve => setTimeout(resolve, 1000 * retryCount)); // 递增延迟
                                } else {
                                    throw new Error(`chunk transfer failed, reached max retry times: ${chunkError instanceof Error ? chunkError.message : 'unknown error'}`);
                                }
                            }
                        }
                    }
                }

                // 3. 完成升级
                onProgress?.({
                    stage: 'uploading',
                    progress: 95,
                    message: 'completing firmware upgrade...'
                });

                const completeResponse = await fetchWithKeepAlive('/api/firmware-upgrade', {
                    method: 'POST',
                    body: JSON.stringify({
                        action: 'complete_session',
                        session_id: deviceSessionId
                    })
                });

                const completeData = await processResponse(completeResponse, setError);
                if (!completeData) {
                    throw new Error('failed to complete firmware upgrade');
                }

                // 更新会话状态
                setUpgradeSession({
                    ...session,
                    isActive: false,
                    progress: {
                        stage: 'uploadcompleted',
                        progress: 100,
                        message: 'firmware upgrade completed successfully'
                    }
                });

                onProgress?.({
                    stage: 'uploadcompleted',
                    progress: 100,
                    message: 'firmware upgrade completed successfully, will restart from new slot'
                });

                setError(null);
                return Promise.resolve();

            } catch (uploadError) {
                // 出错时清理会话
                try {
                    await fetchWithKeepAlive('/api/firmware-upgrade', {
                        method: 'POST',
                        body: JSON.stringify({
                            action: 'abort_session',
                            session_id: deviceSessionId
                        })
                    });
                } catch (abortError) {
                    console.warn('清理升级会话失败:', abortError);
                }
                throw uploadError;
            }

        } catch (err) {
            const errorMessage = err instanceof Error ? err.message : 'failed to upload firmware: unknown error';
            setError(errorMessage);

            // 更新会话状态
            if (upgradeSession) {
                setUpgradeSession({
                    ...upgradeSession,
                    isActive: false,
                    progress: {
                        stage: 'failed',
                        progress: 0,
                        message: 'upload failed',
                        error: errorMessage
                    }
                });
            }

            onProgress?.({
                stage: 'failed',
                progress: 0,
                message: 'upload failed',
                error: errorMessage
            });

            return Promise.reject(new Error(errorMessage));
        } finally {
            // setIsLoading(false);
        }
    };

    // 配置管理函数
    const setUpgradeConfig = (config: Partial<FirmwareUpgradeConfig>): void => {
        setUpgradeConfigState(prevConfig => ({
            ...prevConfig,
            ...config
        }));
    };

    const getUpgradeConfig = (): FirmwareUpgradeConfig => {
        return upgradeConfig;
    };

    const clearFirmwarePackage = (): void => {
        setFirmwarePackage(null);
        setUpgradeSession(null);
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
            firmwarePackage: firmwarePackage,
            upgradeSession: upgradeSession,
            downloadFirmwarePackage: downloadFirmwarePackage,
            uploadFirmwareToDevice: uploadFirmwareToDevice,
            setUpgradeConfig: setUpgradeConfig,
            getUpgradeConfig: getUpgradeConfig,
            clearFirmwarePackage: clearFirmwarePackage,
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