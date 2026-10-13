import {
    DeferredConfigCoordinator
} from '@/lib/deferred-config-coordinator';
import {
    DeviceCommandClient,
    PerformanceTelemetryController
} from '@/lib/device-transport';
import { writeConfigResource } from '@/lib/device-transport/config-snapshot';
import { LedPreviewCoordinator } from '@/lib/led-preview-coordinator';
import { normalizeScreenControl } from '@/lib/screen-control-config';
import { SessionConfigStore, type ConfigDraftBackup } from '@/lib/session-config-store';
import {
    StepInfo
} from '@/types/adc';
import {
    GameProfile,
    GameProfileList, GlobalConfig,
    Hotkey,
    MacroConfig,
    ScreenControlConfig
} from '@/types/gamepad-config';
import {
    CalibrationStatus,
    LEDsConfig
} from '@/types/types';
import React from 'react';
import { calibrationStatusFrom, converProfileDetails, DeviceRequestSender } from './contract';

export interface ConfigurationActionsDependencies {
    configStoreRef: React.MutableRefObject<SessionConfigStore>;
    globalConfigRef: React.MutableRefObject<GlobalConfig>;
    setGlobalConfig: React.Dispatch<React.SetStateAction<GlobalConfig>>;
    screenControlRef: React.MutableRefObject<ScreenControlConfig>;
    setScreenControl: React.Dispatch<React.SetStateAction<ScreenControlConfig>>;
    setHotkeysConfig: React.Dispatch<React.SetStateAction<Hotkey[]>>;
    setProfileList: React.Dispatch<React.SetStateAction<GameProfileList>>;
    setDefaultProfile: React.Dispatch<React.SetStateAction<GameProfile>>;
    deferredConfigRef: React.MutableRefObject<DeferredConfigCoordinator | null>;
    configReadyRef: React.MutableRefObject<boolean>;
    recoveryRef: React.MutableRefObject<ConfigDraftBackup | null>;
    boundaryCountRef: React.MutableRefObject<number>;
    longDeviceActivityRef: React.MutableRefObject<boolean>;
    ledPreviewRef: React.MutableRefObject<LedPreviewCoordinator<LEDsConfig>>;
    sendDeviceRequest: DeviceRequestSender;
    screenControl: ScreenControlConfig;
    detachedDraftsRef: React.MutableRefObject<Map<string, ConfigDraftBackup>>;
    setConfigRecovery: React.Dispatch<React.SetStateAction<string[]>>;
    queueConfigTransaction: (afterFlush?: () => void | Promise<void>, resumeMonitoringAfterSuccess?: boolean, beforeFlush?: () => void | Promise<void>, all?: boolean) => Promise<void>;
    setButtonMonitoringActive: React.Dispatch<React.SetStateAction<boolean>>;
    performanceTelemetryRef: React.MutableRefObject<PerformanceTelemetryController | null>;
    setCalibrationStatus: React.Dispatch<React.SetStateAction<CalibrationStatus>>;
    setMarkingStatus: React.Dispatch<React.SetStateAction<StepInfo>>;
    deviceClientRef: React.MutableRefObject<DeviceCommandClient | null>;
}

export function useConfigurationActions(dependencies: ConfigurationActionsDependencies) {
    const publishConfig = () => {
        const store = dependencies.configStoreRef.current;
        const list = store.get<GameProfileList>('profile-list');
        if (!list) return;
        const id = store.get<string>('selected-profile');
        const profile = store.get<GameProfile>(`profile:${id}`);
        const global = store.get<GlobalConfig>('global');
        const screen = store.get<ScreenControlConfig>('screen-control');
        dependencies.globalConfigRef.current = global; dependencies.setGlobalConfig(global);
        dependencies.screenControlRef.current = screen; dependencies.setScreenControl(screen);
        dependencies.setHotkeysConfig(store.get<Hotkey[]>('hotkeys'));
        dependencies.setProfileList({ ...list, defaultId: id, items: list.items.map(item => ({ ...item, ...store.get<GameProfile>(`profile:${item.id}`) })) });
        dependencies.setDefaultProfile({ ...profile, keysConfig: { ...profile.keysConfig, macros: store.get<MacroConfig[]>(`macros:${id}`) } });
    };

    const updateSyncPause = () => dependencies.deferredConfigRef.current!.pause(
        !dependencies.configReadyRef.current || !!dependencies.recoveryRef.current || dependencies.boundaryCountRef.current > 0 || dependencies.longDeviceActivityRef.current,
    );

    const switchProfile = async (profileId: string): Promise<void> => {
        editConfig('selected-profile', profileId);
        const profile = dependencies.configStoreRef.current.get<GameProfile>(`profile:${profileId}`);
        if (profile?.ledsConfigs) dependencies.ledPreviewRef.current.replaceIfActive(profile.ledsConfigs);
    };

    const stageResource = (key: string): void => {
        const store = dependencies.configStoreRef.current;
        if (!store.dirty(key)) { dependencies.deferredConfigRef.current!.remove(key); return; }
        const generation = store.generation;
        dependencies.deferredConfigRef.current!.stage(key, async () => {
            if (!dependencies.configReadyRef.current || generation !== store.generation) throw new Error('Configuration session ended');
            if (!store.dirty(key)) return;
            const ticket = store.begin(key);
            try {
                const remote = await writeConfigResource(
                    (command, params) => dependencies.sendDeviceRequest(command, params, true), key, ticket.sent, converProfileDetails,
                );
                store.acknowledge(ticket, remote);
                if (generation === store.generation) publishConfig();
            } catch (error) { store.fail(ticket); throw error; }
        }, key === 'selected-profile' ? 30 : key.startsWith('macros:') ? 20 : 10);
    };

    const editConfig = (key: string, value: unknown, patch = false): void => {
        if (!dependencies.configReadyRef.current || dependencies.recoveryRef.current || dependencies.boundaryCountRef.current) return;
        const store = dependencies.configStoreRef.current;
        const previous = store.get(key);
        if (patch) store.patch(key, value as object); else store.set(key, value);
        if (previous === store.get(key)) return;
        publishConfig(); stageResource(key);
    };

    const stageDeferredProfileDetails = (id: string, profile: GameProfile): void => {
        const details = { ...profile, keysConfig: profile.keysConfig ? { ...profile.keysConfig, macros: undefined } : undefined };
        editConfig(`profile:${id}`, details, true);
    };

    const stageDeferredProfileMacros = (id: string, macros: MacroConfig[]): void => editConfig(`macros:${id}`, macros);

    const stageDeferredHotkeysConfig = (hotkeys: Hotkey[]): void => editConfig('hotkeys', hotkeys);

    const stageDeferredGlobalConfig = (global: GlobalConfig): void => editConfig('global', global, true);

    const stageDeferredScreenControl = (screen: ScreenControlConfig): void => {
        if (dependencies.screenControl.standbySupported === false) throw new Error('Update device firmware to change screen settings');
        editConfig('screen-control', screen);
    };

    const resolveConfigRecovery = (restore: boolean): void => {
        const backup = dependencies.recoveryRef.current;
        if (!backup) return;
        if (restore) dependencies.configStoreRef.current.restore(backup);
        dependencies.detachedDraftsRef.current.delete(backup.deviceId);
        dependencies.recoveryRef.current = null; dependencies.setConfigRecovery([]);
        publishConfig();
        dependencies.configStoreRef.current.dirtyKeys.forEach(stageResource);
        updateSyncPause();
    };

    const refreshDeviceConfig = async (): Promise<void> => {
        const generation = dependencies.configStoreRef.current.generation;
        const global = (await dependencies.sendDeviceRequest('get_global_config', {}, true))?.globalConfig;
        const screen = (await dependencies.sendDeviceRequest('get_screen_control_config', {}, true))?.screenControl;
        if (generation !== dependencies.configStoreRef.current.generation) return;
        if (!global || !screen) throw new Error('Device returned incomplete configuration');
        dependencies.configStoreRef.current.refresh('global', global);
        dependencies.configStoreRef.current.refresh('screen-control', normalizeScreenControl(screen, true));
        publishConfig();
    };

    const stopLongDeviceActivity = async <T,>(stop: () => Promise<T>): Promise<T> => {
        let result!: T;
        await dependencies.queueConfigTransaction(async () => { }, true, async () => {
            result = await stop();
            await refreshDeviceConfig();
        });
        return result;
    };

    const runConfigExclusive = async <T,>(operation: () => Promise<T>, refresh = false): Promise<T> => {
        let result!: T;
        await dependencies.queueConfigTransaction(async () => {
            result = await operation();
            if (refresh) await refreshDeviceConfig();
        }, true);
        return result;
    };

    const terminateWebConfigActivities = async (): Promise<void> => {
        const buttonData = await dependencies.sendDeviceRequest(
            'stop_button_monitoring',
            {},
            true,
        );
        dependencies.setButtonMonitoringActive(buttonData?.isActive ?? false);
        dependencies.performanceTelemetryRef.current?.resetMonitoringSession();

        const calibrationData = await dependencies.sendDeviceRequest(
            'get_calibration_status',
            {},
            true,
        );
        const currentCalibration = calibrationStatusFrom(
            calibrationData?.calibrationStatus,
        );
        if (!currentCalibration) {
            throw new Error('Device returned an invalid calibration status while finishing');
        }
        dependencies.setCalibrationStatus(currentCalibration);
        if (currentCalibration.isActive) {
            const data = await dependencies.sendDeviceRequest(
                'stop_manual_calibration',
                {},
                true,
            );
            const status = calibrationStatusFrom(data?.calibrationStatus);
            if (!status) {
                throw new Error('Device returned an invalid calibration status while finishing');
            }
            dependencies.setCalibrationStatus(status);
        }

        const markingData = await dependencies.sendDeviceRequest(
            'ms_get_mark_status',
            {},
            true,
        );
        const currentMarking = markingData?.status as StepInfo | undefined;
        if (currentMarking) dependencies.setMarkingStatus(currentMarking);
        if (currentMarking?.is_marking || currentMarking?.is_sampling) {
            const data = await dependencies.sendDeviceRequest(
                'ms_mark_mapping_stop',
                {},
                true,
            );
            if (data?.status) dependencies.setMarkingStatus(data.status as StepInfo);
        }

        // These commands are idempotent and discard only volatile preview or
        // multipart staging state. Neither command persists configuration.
        await dependencies.sendDeviceRequest('clear_leds_preview', {}, true);
        await dependencies.sendDeviceRequest('import_config_abort', {}, true);
        dependencies.longDeviceActivityRef.current = false;
        await dependencies.deviceClientRef.current?.flushQueue();
    };
    return {
        publishConfig, updateSyncPause, stageResource, editConfig, stageDeferredProfileDetails,
        stageDeferredProfileMacros, stageDeferredHotkeysConfig, stageDeferredGlobalConfig,
        stageDeferredScreenControl, resolveConfigRecovery, refreshDeviceConfig, stopLongDeviceActivity,
        runConfigExclusive, terminateWebConfigActivities, switchProfile,
    };
}
