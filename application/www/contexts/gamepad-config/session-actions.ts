import { parseDiagnosticsPolicy } from "@/lib/diagnostics-policy";
import { FaultLogQuery, LogSource, parseFaultLogPage } from "@/lib/device-fault-logs";
import {
    DeviceCommandClient,
    DeviceConnectionError,
    DeviceImageCatalog,
    DeviceImageTarget,
    DeviceImageUploadRequest,
    DeviceTransportConfig,
    DeviceTransportState
} from '@/lib/device-transport';
import { BindingArgs, BindingSnapshot, checkBinding } from '@/lib/device-transport/rf-binding';
import { cloneConfig, SessionConfigStore } from '@/lib/session-config-store';
import {
    GameControllerButton,
    KeyCombination,
    MacroConfig,
    Platform,
    PS4ButtonMap, SwitchButtonMap,
    XInputButtonMap
} from '@/types/gamepad-config';
import React, { useCallback } from 'react';
import { DeviceRequestSender, DeviceTransportConfigType, EXIT_WEB_CONFIG_TIMEOUT_MS, HitboxLayoutItem } from './contract';

export interface SessionActionsDependencies {
    setJsReady: React.Dispatch<React.SetStateAction<boolean>>;
    deviceClient: DeviceCommandClient | null;
    setUserRebooting: React.Dispatch<React.SetStateAction<boolean>>;
    cancelPendingAutomaticConnects: () => void;
    initialAutoConnectClientRef: React.MutableRefObject<DeviceCommandClient | null>;
    resetDeviceSessionState: () => void;
    setError: React.Dispatch<React.SetStateAction<string | null>>;
    setDeviceError: React.Dispatch<React.SetStateAction<DeviceConnectionError | null>>;
    deviceClientRef: React.MutableRefObject<DeviceCommandClient | null>;
    boundaryCountRef: React.MutableRefObject<number>;
    rfBindingBusyRef: React.MutableRefObject<boolean>;
    rfBindingOwnerRef: React.MutableRefObject<{ client: DeviceCommandClient; epoch: number; } | null>;
    rfBindingConnectionEpoch: React.MutableRefObject<number>;
    operationLoading: boolean;
    setRfBindingBusy: React.Dispatch<React.SetStateAction<boolean>>;
    configStoreRef: React.MutableRefObject<SessionConfigStore>;
    setIsLoading: React.Dispatch<React.SetStateAction<boolean>>;
    transportConfig: DeviceTransportConfig;
    setTransportConfig: React.Dispatch<React.SetStateAction<DeviceTransportConfig>>;
}

export function useSessionActions(dependencies: SessionActionsDependencies) {
    const setContextJsReady = (ready: boolean) => {
        dependencies.setJsReady(ready);
    };

    const connectDevice = useCallback(async (): Promise<void> => {
        if (dependencies.deviceClient) {
            // 该入口由用户点击连接按钮触发，允许打开 WebHID chooser。
            dependencies.setUserRebooting(false);
            dependencies.cancelPendingAutomaticConnects();
            dependencies.initialAutoConnectClientRef.current = dependencies.deviceClient;
            dependencies.resetDeviceSessionState();
            dependencies.setError(null);
            dependencies.setDeviceError(null);
            return dependencies.deviceClient.connect(true);
        }
        throw new Error('设备传输层未初始化');
    }, [dependencies.deviceClient, dependencies.resetDeviceSessionState, dependencies.cancelPendingAutomaticConnects]);

    const reconnectDevice = useCallback(async (): Promise<void> => {
        if (dependencies.deviceClient) {
            // 程序化重连只能使用浏览器已授权的设备，不能打开 chooser。
            dependencies.setUserRebooting(false);
            dependencies.cancelPendingAutomaticConnects();
            dependencies.initialAutoConnectClientRef.current = dependencies.deviceClient;
            dependencies.resetDeviceSessionState();
            dependencies.setError(null);
            dependencies.setDeviceError(null);
            return dependencies.deviceClient.connect(false);
        }
        throw new Error('设备传输层未初始化');
    }, [dependencies.deviceClient, dependencies.resetDeviceSessionState, dependencies.cancelPendingAutomaticConnects]);

    const disconnectDevice = (): void => {
        if (dependencies.deviceClient) {
            dependencies.deviceClient.disconnect();
        }
    };

    const getDeviceImageCatalog = useCallback(async (): Promise<DeviceImageCatalog> => {
        if (!dependencies.deviceClient) throw new Error('设备命令客户端未初始化');
        return dependencies.deviceClient.getImageCatalog();
    }, [dependencies.deviceClient]);

    const readDeviceImage = useCallback(async (
        target: DeviceImageTarget,
        totalSize: number,
    ): Promise<Uint8Array> => {
        if (!dependencies.deviceClient) throw new Error('设备命令客户端未初始化');
        return dependencies.deviceClient.readImage(target, totalSize);
    }, [dependencies.deviceClient]);

    const uploadDeviceImage = useCallback(async (
        request: DeviceImageUploadRequest,
    ): Promise<void> => {
        if (!dependencies.deviceClient) throw new Error('设备命令客户端未初始化');
        const result = await dependencies.deviceClient.uploadImage(request);
        if (!result.success) {
            throw new Error(`Device rejected image upload: ${result.error || 'Unknown error'}`);
        }
    }, [dependencies.deviceClient]);

    const deleteDeviceImage = useCallback(async (): Promise<void> => {
        if (!dependencies.deviceClient) throw new Error('设备命令客户端未初始化');
        const result = await dependencies.deviceClient.deleteImage();
        if (!result.success) {
            throw new Error(`Device rejected image deletion: ${result.error || 'Unknown error'}`);
        }
    }, [dependencies.deviceClient]);

    const rfBindingRequest = useCallback(async (op: number, args: BindingArgs = {}): Promise<BindingSnapshot> => {
        const client = dependencies.deviceClientRef.current;
        if (!client || client.getState() !== DeviceTransportState.CONNECTED) throw new Error('BINDING_DISCONNECTED');
        if (dependencies.boundaryCountRef.current > 0 && !dependencies.rfBindingBusyRef.current) throw new Error('BINDING_BUSY');
        const owner = dependencies.rfBindingOwnerRef.current;
        if (owner && (owner.client !== client || owner.epoch !== dependencies.rfBindingConnectionEpoch.current)) throw new Error('BINDING_DISCONNECTED');
        const commands = ['', 'get_rf_binding', 'get_rf_binding', 'prepare_rf_binding', 'commit_rf_binding', 'abort_rf_binding'];
        if (!commands[op]) throw new Error('BINDING_INVALID');
        const reply = await client.enqueue(commands[op], { ...args, pending: op === 2 }, true);
        return checkBinding(reply);
    }, []);

    const runRfBindingOperation = async <T,>(action: () => Promise<T>): Promise<T> => {
        if (dependencies.rfBindingBusyRef.current || dependencies.operationLoading) throw new Error('BINDING_BUSY');
        const client = dependencies.deviceClientRef.current;
        if (!client || client.getState() !== DeviceTransportState.CONNECTED) throw new Error('BINDING_DISCONNECTED');
        dependencies.rfBindingOwnerRef.current = { client, epoch: dependencies.rfBindingConnectionEpoch.current };
        dependencies.rfBindingBusyRef.current = true; dependencies.setRfBindingBusy(true);
        try { return await action(); }
        finally { dependencies.rfBindingOwnerRef.current = null; dependencies.rfBindingBusyRef.current = false; dependencies.setRfBindingBusy(false); }
    };

    const sendDeviceRequest: DeviceRequestSender = async (command, params = {}, immediate = false, options = {}) => {
        if (dependencies.rfBindingBusyRef.current && !command.startsWith('get_')) throw new Error('BINDING_BUSY');
        if (!dependencies.deviceClient) {
            return Promise.reject(new Error('设备命令客户端未初始化'));
        }
        // Read the concrete client's synchronous state. React state is only a
        // rendering projection and can remain CONNECTED for one render after a
        // physical disconnect or failed native write.
        if (dependencies.deviceClient.getState() !== DeviceTransportState.CONNECTED) {
            throw new Error('设备尚未连接');
        }

        try {
            // DeviceCommandClient owns the only RPC lane and the centralized
            // command policy decides whether this operation is coalesced.
            return await dependencies.deviceClient.enqueue(command, params, immediate, options);
        } catch (error) {
            if (error instanceof Error) {
                throw error;
            }
            throw new Error(`设备请求失败: ${error}`);
        }
    };

    const getProfileMacros = useCallback(async (profileId: string): Promise<MacroConfig[]> => {
        return cloneConfig(dependencies.configStoreRef.current.get<MacroConfig[]>(`macros:${profileId}`) ?? []);
    }, []);

    const diagnosticsPolicy = async (enabled?: import("@/lib/diagnostics-policy").DiagnosticsSettings) => parseDiagnosticsPolicy(await sendDeviceRequest(
        enabled === undefined ? 'get_diagnostics_policy' : 'set_diagnostics_policy',
        enabled === undefined ? {} : enabled, true));
    const fetchDeviceLogsPage = async (query: FaultLogQuery) => parseFaultLogPage(
        await sendDeviceRequest("get_device_logs_page", { ...query }, true), query.source);
    const clearDeviceLogs = async (source: LogSource) => {
        const reply = await sendDeviceRequest('clear_device_logs', { source, confirm: true }, true, { timeoutMs: 30000 });
        if (reply?.success !== true || reply?.source !== source) throw new Error('The device did not confirm clearing the logs');
    };

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

    const sendInitializationDeviceRequest: DeviceRequestSender = async (
        command,
        params = {},
    ) => {
        if (!dependencies.deviceClient) {
            throw new Error('设备命令客户端未初始化');
        }
        return dependencies.deviceClient.requestInitialization(command, params);
    };

    const getHitboxLayout = async (
        immediate: boolean = true,
        requester: DeviceRequestSender = sendDeviceRequest,
    ): Promise<HitboxLayoutItem[]> => {
        try {
            const data = await requester('get_hitbox_layout', {}, immediate);
            return Promise.resolve(data as HitboxLayoutItem[]);
        } catch (err) {
            // setError(err instanceof Error ? err.message : '获取Hitbox布局失败');
            return Promise.reject(new Error("Failed to get XORA layout"));
        }
    };

    const rebootSystem = async (immediate: boolean = true): Promise<void> => {
        try {
            dependencies.setIsLoading(true);
            await sendDeviceRequest('reboot', {}, immediate);
            dependencies.setError(null);
            return Promise.resolve();
        } catch (err) {
            // setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to reboot system"));
        } finally {
            dependencies.setIsLoading(false);
        }
    };

    const exitWebConfig = async (): Promise<void> => {
        try {
            await sendDeviceRequest('exit_webconfig', {}, true, {
                timeoutMs: EXIT_WEB_CONFIG_TIMEOUT_MS,
            });
            // The TX HID interface can remain enumerated in game mode. End
            // this configuration session as soon as exit is acknowledged.
            dependencies.deviceClientRef.current?.disconnect();
            dependencies.setError(null);
        } catch (err) {
            const error = err instanceof Error
                ? err
                : new Error('Failed to exit WebConfig mode');
            return Promise.reject(error);
        }
    };

    const sendPendingCommandImmediately = useCallback((command: string): boolean => {
        return dependencies.deviceClientRef.current?.sendPendingCommandImmediately(command) ?? false;
    }, []);

    const flushQueue = async (): Promise<void> => {
        if (dependencies.deviceClient) {
            await dependencies.deviceClient.flushQueue();
        }
    };

    const getDeviceTransportConfig = () => {
        return dependencies.transportConfig;
    };

    const updateDeviceTransportConfig = (config: Partial<DeviceTransportConfigType>) => {
        dependencies.setTransportConfig(prevConfig => ({
            ...prevConfig,
            ...config
        }));
        console.log('设备传输配置已更新:', config);
    };

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
            for (const [key, value] of Object.entries(keyMapping)) {
                for (const index of value) {
                    map[index] = labelMap.get(key as GameControllerButton) as GameControllerButton;
                }
            }
        }

        // 处理组合键的按键映射
        if (keyCombinations) {
            for (let i = 0; i < keyCombinations.length; i++) {
                const combination = keyCombinations[i];
                for (const index of combination.keyIndexes) {
                    map[index] = `COM${i + 1}`;
                }
            }
        }

        return map;
    };
    return {
        setContextJsReady, connectDevice, reconnectDevice, disconnectDevice, getDeviceImageCatalog,
        readDeviceImage, uploadDeviceImage, deleteDeviceImage, rfBindingRequest, runRfBindingOperation,
        sendDeviceRequest, diagnosticsPolicy, getProfileMacros, fetchDeviceLogsList, fetchDeviceLogsPage, clearDeviceLogs, sendInitializationDeviceRequest,
        getHitboxLayout, rebootSystem, exitWebConfig, sendPendingCommandImmediately, flushQueue,
        getDeviceTransportConfig, updateDeviceTransportConfig,
        indexMapToGameControllerButtonOrCombination,
    };
}
