'use client';
import { useCalibrationState } from './gamepad-config/calibration-state';
import { useConfigurationActions } from './gamepad-config/configuration-actions';
import { useConfigurationState } from './gamepad-config/configuration-state';
import { createConfigTransactions } from './gamepad-config/configuration-transactions';
import { GamepadConfigContextType } from './gamepad-config/contract';
import { useFeedbackActions } from './gamepad-config/feedback-actions';
import { useFeedbackState } from './gamepad-config/feedback-state';
import { useFirmwareActions } from './gamepad-config/firmware-actions';
import { useFirmwareState } from './gamepad-config/firmware-state';
import { useMappingActions } from './gamepad-config/mapping-actions';
import { createResourceActions } from './gamepad-config/resource-actions';
import { createSelectionContext } from './gamepad-config/selection-context';
import { SelectionStore } from './gamepad-config/selection-store';
import { useSessionActions } from './gamepad-config/session-actions';
import { useSessionLifecycle } from './gamepad-config/session-lifecycle';
import { useSessionReset } from './gamepad-config/session-reset';
import { useSessionState } from './gamepad-config/session-state';

import React, { useLayoutEffect, useMemo, useRef } from 'react';

import {
    DeviceConnectionPhase
} from '@/lib/device-transport';

/**
 * GamepadConfigProvider
 * @param children - React.ReactNode
 * @returns 
 */
const { Context: GamepadConfigContext, useFields } = createSelectionContext<GamepadConfigContextType>();
export const useGamepadFields = useFields;

export function GamepadConfigProvider({ children }: { children: React.ReactNode }) {
    const {
        operationLoading, setIsLoading, rfBindingBusy, setRfBindingBusy, rfBindingBusyRef,
        rfBindingConnectionEpoch, rfBindingOwnerRef, error, setError, jsReady, setJsReady,
        deviceConnected, setDeviceConnected, deviceState, setDeviceState, devicePhase, setDevicePhase,
        deviceError, setDeviceError, deviceClient, setDeviceClient, deviceClientRef, deviceSession,
        setDeviceSession, showReconnect, setShowReconnect, transportConfig, setTransportConfig,
        userRebooting, setUserRebooting, connectionWatchdogPausedRef, hitboxLayout, setHitboxLayout,
        initializationGenerationRef, postReadyRequestSchedulerRef, performanceTelemetryRef,
        initialAutoConnectClientRef, initialAutoConnectCancelRef, foregroundReconnectCancelRef,
        pageHiddenRef,
    } = useSessionState();
    const {
        globalConfig, setGlobalConfig, globalConfigRef, screenControl, setScreenControl,
        screenControlRef, profileList, setProfileList, defaultProfile, setDefaultProfile, hotkeysConfig,
        setHotkeysConfig, dataIsReady, setDataIsReady, finishConfigDisabled, setFinishConfigDisabled,
        deferredConfigDirty, setDeferredConfigDirty, deferredConfigSaving, setDeferredConfigSaving,
        configStoreRef, detachedDraftsRef, configReadyRef, configRecovery, setConfigRecovery,
        recoveryRef, configReadProgress, setConfigReadProgress, configSyncState, setConfigSyncState,
        configBoundaryBusy, setConfigBoundaryBusy, boundaryCountRef, autoFlushRef, deferredConfigRef,
        deferredFlushTailRef,
    } = useConfigurationState();
    const {
        defaultMappingId, setDefaultMappingId, mappingList, setMappingList, mappingStorageMode,
        setMappingStorageMode, mappingSource, setMappingSource, markingStatus, setMarkingStatus,
        activeMapping, setActiveMapping, calibrationStatus, setCalibrationStatus,
        calibrationCompletionRequestRef,
    } = useCalibrationState();
    const {
        firmwareInfo, setFirmwareInfo, firmwareUpdateInfo, setFirmwareUpdateInfo, firmwareServerHost,
        _setFirmwareServerHostState, upgradeSession, setUpgradeSession, upgradeConfig,
        setUpgradeConfigState, firmwareUpdating, setFirmwareUpdating, releaseActivityRef,
        releaseInventoryRequestRef,
    } = useFirmwareState();
    const {
        buttonMonitoringActive, setButtonMonitoringActive, buttonMonitorLeaseRef, ledPreviewRef,
        screenPreviewRef, feedbackSuspendedRef, longDeviceActivityRef,
    } = useFeedbackState();
    const sessionDependencies = {
        get globalConfigRef() { return globalConfigRef; },
        get globalConfig() { return globalConfig; },
        get screenControlRef() { return screenControlRef; },
        get screenControl() { return screenControl; },
        get postReadyRequestSchedulerRef() { return postReadyRequestSchedulerRef; },
        get buttonMonitorLeaseRef() { return buttonMonitorLeaseRef; },
        get ledPreviewRef() { return ledPreviewRef; },
        get deferredConfigRef() { return deferredConfigRef; },
        get configReadyRef() { return configReadyRef; },
        get configStoreRef() { return configStoreRef; },
        get detachedDraftsRef() { return detachedDraftsRef; },
        get recoveryRef() { return recoveryRef; },
        get setConfigRecovery() { return setConfigRecovery; },
        get feedbackSuspendedRef() { return feedbackSuspendedRef; },
        get screenPreviewRef() { return screenPreviewRef; },
        get longDeviceActivityRef() { return longDeviceActivityRef; },
        get setDeferredConfigSaving() { return setDeferredConfigSaving; },
        get calibrationCompletionRequestRef() { return calibrationCompletionRequestRef; },
        get initializationGenerationRef() { return initializationGenerationRef; },
        get setDefaultMappingId() { return setDefaultMappingId; },
        get setMappingList() { return setMappingList; },
        get setMarkingStatus() { return setMarkingStatus; },
        get setActiveMapping() { return setActiveMapping; },
        get setCalibrationStatus() { return setCalibrationStatus; },
        get setButtonMonitoringActive() { return setButtonMonitoringActive; },
        get setFirmwareInfo() { return setFirmwareInfo; },
        get setFirmwareUpdateInfo() { return setFirmwareUpdateInfo; },
        get setUpgradeSession() { return setUpgradeSession; },
        get setHitboxLayout() { return setHitboxLayout; },
        get setDataIsReady() { return setDataIsReady; },
        get setConfigReadProgress() { return setConfigReadProgress; },
        get setFinishConfigDisabled() { return setFinishConfigDisabled; },
        get setDeviceSession() { return setDeviceSession; },
        get initialAutoConnectCancelRef() { return initialAutoConnectCancelRef; },
        get foregroundReconnectCancelRef() { return foregroundReconnectCancelRef; },
        get transportConfig() { return transportConfig; },
        get deviceClientRef() { return deviceClientRef; },
        get rfBindingConnectionEpoch() { return rfBindingConnectionEpoch; },
        get setDeviceState() { return setDeviceState; },
        get setDeviceConnected() { return setDeviceConnected; },
        get setDevicePhase() { return setDevicePhase; },
        get setDeviceError() { return setDeviceError; },
        get performanceTelemetryRef() { return performanceTelemetryRef; },
        get pageHiddenRef() { return pageHiddenRef; },
        get initialAutoConnectClientRef() { return initialAutoConnectClientRef; },
        get setShowReconnect() { return setShowReconnect; },
        get setDeviceClient() { return setDeviceClient; },
        get devicePhase() { return devicePhase; },
        get deviceClient() { return deviceClient; },
        get deviceState() { return deviceState; },
        get userRebooting() { return userRebooting; },
        get setUserRebooting() { return setUserRebooting; },
        get firmwareUpdating() { return firmwareUpdating; },
        get deferredConfigDirty() { return deferredConfigDirty; },
        get deferredConfigSaving() { return deferredConfigSaving; },
        get deviceConnected() { return deviceConnected; },
        get configRecovery() { return configRecovery; },
        get dataIsReady() { return dataIsReady; },
        get fetchFirmwareMetadata() { return fetchFirmwareMetadata; },
        get sendInitializationDeviceRequest() { return sendInitializationDeviceRequest; },
        get getHitboxLayout() { return getHitboxLayout; },
        get publishConfig() { return publishConfig; },
        get updateSyncPause() { return updateSyncPause; },
        get setError() { return setError; },
        get reconnectDevice() { return reconnectDevice; },
        get connectionWatchdogPausedRef() { return connectionWatchdogPausedRef; },
        get boundaryCountRef() { return boundaryCountRef; },
    };
    const { resetDeviceSessionState, cancelPendingAutomaticConnects } = useSessionReset(sessionDependencies);

    const contextJsReady = useMemo(() => jsReady, [jsReady]);
    const connectionLoading = devicePhase !== DeviceConnectionPhase.IDLE
        && devicePhase !== DeviceConnectionPhase.READY
        && devicePhase !== DeviceConnectionPhase.ERROR;
    const isLoading = connectionLoading || operationLoading || rfBindingBusy;
    connectionWatchdogPausedRef.current = firmwareUpdating || userRebooting;
    const configEditingBlocked = !dataIsReady || !deviceConnected || configBoundaryBusy || firmwareUpdating || configRecovery.length > 0;
    const deviceBackgroundReadsPaused = configBoundaryBusy || userRebooting;
    const {
        publishConfig, updateSyncPause, stageResource, editConfig, stageDeferredProfileDetails,
        stageDeferredProfileMacros, stageDeferredHotkeysConfig, stageDeferredGlobalConfig,
        stageDeferredScreenControl, resolveConfigRecovery, refreshDeviceConfig, stopLongDeviceActivity,
        runConfigExclusive, terminateWebConfigActivities, switchProfile,
    } = useConfigurationActions({
        get configStoreRef() { return configStoreRef; },
        get globalConfigRef() { return globalConfigRef; },
        get setGlobalConfig() { return setGlobalConfig; },
        get screenControlRef() { return screenControlRef; },
        get setScreenControl() { return setScreenControl; },
        get setHotkeysConfig() { return setHotkeysConfig; },
        get setProfileList() { return setProfileList; },
        get setDefaultProfile() { return setDefaultProfile; },
        get deferredConfigRef() { return deferredConfigRef; },
        get configReadyRef() { return configReadyRef; },
        get recoveryRef() { return recoveryRef; },
        get boundaryCountRef() { return boundaryCountRef; },
        get longDeviceActivityRef() { return longDeviceActivityRef; },
        get ledPreviewRef() { return ledPreviewRef; },
        get sendDeviceRequest() { return sendDeviceRequest; },
        get screenControl() { return screenControl; },
        get detachedDraftsRef() { return detachedDraftsRef; },
        get setConfigRecovery() { return setConfigRecovery; },
        get queueConfigTransaction() { return queueConfigTransaction; },
        get setButtonMonitoringActive() { return setButtonMonitoringActive; },
        get performanceTelemetryRef() { return performanceTelemetryRef; },
        get setCalibrationStatus() { return setCalibrationStatus; },
        get setMarkingStatus() { return setMarkingStatus; },
        get deviceClientRef() { return deviceClientRef; },
    });
    const {
        setContextJsReady, connectDevice, reconnectDevice, disconnectDevice, getDeviceImageCatalog,
        readDeviceImage, uploadDeviceImage, deleteDeviceImage, rfBindingRequest, runRfBindingOperation,
        sendDeviceRequest, diagnosticsPolicy, getProfileMacros, fetchDeviceLogsList, fetchDeviceLogsPage, clearDeviceLogs, sendInitializationDeviceRequest,
        getHitboxLayout, rebootSystem, exitWebConfig, sendPendingCommandImmediately, flushQueue,
        getDeviceTransportConfig, updateDeviceTransportConfig,
        indexMapToGameControllerButtonOrCombination,
    } = useSessionActions({
        get setJsReady() { return setJsReady; },
        get deviceClient() { return deviceClient; },
        get setUserRebooting() { return setUserRebooting; },
        get cancelPendingAutomaticConnects() { return cancelPendingAutomaticConnects; },
        get initialAutoConnectClientRef() { return initialAutoConnectClientRef; },
        get resetDeviceSessionState() { return resetDeviceSessionState; },
        get setError() { return setError; },
        get setDeviceError() { return setDeviceError; },
        get deviceClientRef() { return deviceClientRef; },
        get boundaryCountRef() { return boundaryCountRef; },
        get rfBindingBusyRef() { return rfBindingBusyRef; },
        get rfBindingOwnerRef() { return rfBindingOwnerRef; },
        get rfBindingConnectionEpoch() { return rfBindingConnectionEpoch; },
        get operationLoading() { return operationLoading; },
        get setRfBindingBusy() { return setRfBindingBusy; },
        get configStoreRef() { return configStoreRef; },
        get setIsLoading() { return setIsLoading; },
        get transportConfig() { return transportConfig; },
        get setTransportConfig() { return setTransportConfig; },
    });
    const {
        fetchMappingList, fetchDefaultMapping, createMapping, deleteMapping, updateDefaultMapping,
        startMarking, stopMarking, stepMarking, fetchMarkingStatus, syncMarkingProgress,
        fetchActiveMapping, renameMapping, readSwitchMappingEnvelope, fetchSwitchMappingResponse,
        fetchDeviceAuthorizedResource, fetchSwitchMappingCatalog, fetchSwitchMappingDetail,
        fetchSwitchMappingImage, uploadSwitchMappingImage, updateSwitchMappingMetadata,
        updateSwitchMappingCurve, deleteSwitchMapping, installCanonicalMapping, installSwitchMapping,
        clearInstalledSwitchMapping, createSwitchMappingFromCurrent, beginMappingDraft,
        publishMappingDraft, updateMarkingStatus,
    } = useMappingActions({
        get setIsLoading() { return setIsLoading; },
        get sendDeviceRequest() { return sendDeviceRequest; },
        get setMappingList() { return setMappingList; },
        get setDefaultMappingId() { return setDefaultMappingId; },
        get setMappingStorageMode() { return setMappingStorageMode; },
        get setMappingSource() { return setMappingSource; },
        get setError() { return setError; },
        get activeMapping() { return activeMapping; },
        get setActiveMapping() { return setActiveMapping; },
        get longDeviceActivityRef() { return longDeviceActivityRef; },
        get updateSyncPause() { return updateSyncPause; },
        get setMarkingStatus() { return setMarkingStatus; },
        get markingStatus() { return markingStatus; },
        get deviceClient() { return deviceClient; },
    });
    const {
        previewScreenBrightness, fetchCalibrationStatus, startManualCalibration, stopManualCalibration,
        clearManualCalibrationData, checkIsManualCalibrationCompleted, startButtonMonitoring,
        stopButtonMonitoring, getButtonStates, startButtonPerformanceMonitoring,
        stopButtonPerformanceMonitoring, pushLedsConfig, clearLedsPreview,
    } = useFeedbackActions({
        get screenPreviewRef() { return screenPreviewRef; },
        get feedbackSuspendedRef() { return feedbackSuspendedRef; },
        get boundaryCountRef() { return boundaryCountRef; },
        get sendDeviceRequest() { return sendDeviceRequest; },
        get setCalibrationStatus() { return setCalibrationStatus; },
        get setError() { return setError; },
        get longDeviceActivityRef() { return longDeviceActivityRef; },
        get updateSyncPause() { return updateSyncPause; },
        get setIsLoading() { return setIsLoading; },
        get initializationGenerationRef() { return initializationGenerationRef; },
        get calibrationCompletionRequestRef() { return calibrationCompletionRequestRef; },
        get postReadyRequestSchedulerRef() { return postReadyRequestSchedulerRef; },
        get buttonMonitorLeaseRef() { return buttonMonitorLeaseRef; },
        get setButtonMonitoringActive() { return setButtonMonitoringActive; },
        get performanceTelemetryRef() { return performanceTelemetryRef; },
        get ledPreviewRef() { return ledPreviewRef; },
        get configStoreRef() { return configStoreRef; },
    });

    const {
        getReleaseInventory, downloadSelectedRelease, installSelectedRelease, releaseInstallAction,
        fetchFirmwareMetadata, checkFirmwareUpdate, generateSessionId, downloadFirmwarePackage,
        uploadFirmwareToDevice, uploadCh585Firmware, setUpgradeConfig, getUpgradeConfig,
        getValidChunkSizes,
    } = useFirmwareActions({
        get deviceClient() { return deviceClient; },
        get releaseInventoryRequestRef() { return releaseInventoryRequestRef; },
        get releaseActivityRef() { return releaseActivityRef; },
        get boundaryCountRef() { return boundaryCountRef; },
        get setFirmwareUpdating() { return setFirmwareUpdating; },
        get longDeviceActivityRef() { return longDeviceActivityRef; },
        get updateSyncPause() { return updateSyncPause; },
        get queueConfigTransaction() { return queueConfigTransaction; },
        get terminateWebConfigActivities() { return terminateWebConfigActivities; },
        get sendDeviceRequest() { return sendDeviceRequest; },
        get setFirmwareInfo() { return setFirmwareInfo; },
        get deviceState() { return deviceState; },
        get firmwareServerHost() { return firmwareServerHost; },
        get setFirmwareUpdateInfo() { return setFirmwareUpdateInfo; },
        get setError() { return setError; },
        get setUpgradeSession() { return setUpgradeSession; },
        get upgradeConfig() { return upgradeConfig; },
        get setUpgradeConfigState() { return setUpgradeConfigState; },
    });

    const { performDeferredConfigFlush, queueConfigTransaction, flushDeferredConfig } = createConfigTransactions({
        get initializationGenerationRef() { return initializationGenerationRef; },
        get configReadyRef() { return configReadyRef; },
        get deferredConfigRef() { return deferredConfigRef; },
        get configStoreRef() { return configStoreRef; },
        get setDeferredConfigSaving() { return setDeferredConfigSaving; },
        get ledPreviewRef() { return ledPreviewRef; },
        get sendDeviceRequest() { return sendDeviceRequest; },
        get buttonMonitorLeaseRef() { return buttonMonitorLeaseRef; },
        get feedbackSuspendedRef() { return feedbackSuspendedRef; },
        get checkIsManualCalibrationCompleted() { return checkIsManualCalibrationCompleted; },
        get setButtonMonitoringActive() { return setButtonMonitoringActive; },
        get postReadyRequestSchedulerRef() { return postReadyRequestSchedulerRef; },
        get longDeviceActivityRef() { return longDeviceActivityRef; },
        get deviceClientRef() { return deviceClientRef; },
        get setError() { return setError; },
        get screenPreviewRef() { return screenPreviewRef; },
        get boundaryCountRef() { return boundaryCountRef; },
        get setConfigBoundaryBusy() { return setConfigBoundaryBusy; },
        get updateSyncPause() { return updateSyncPause; },
        get recoveryRef() { return recoveryRef; },
        get deferredFlushTailRef() { return deferredFlushTailRef; },
    });

    autoFlushRef.current = () => queueConfigTransaction(undefined, true, undefined, false);

    useSessionLifecycle(sessionDependencies, { resetDeviceSessionState, cancelPendingAutomaticConnects });

    const { resourceCommand, resourceTransaction, syncLightingResourceReferences, installMappingResource } = createResourceActions({
        sendDeviceRequest, runConfigExclusive, configStoreRef, publishConfig, installCanonicalMapping,
    });

    const value: GamepadConfigContextType = {
        contextJsReady,
        setContextJsReady,

        // WebHID connection state
        rfBindingBusy, rfBindingRequest, runRfBindingOperation: (action) => runConfigExclusive(() => runRfBindingOperation(action)),
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
        uploadDeviceImage: (...args) => runConfigExclusive(() => uploadDeviceImage(...args), true),
        deleteDeviceImage: (...args) => runConfigExclusive(() => deleteDeviceImage(...args), true),
        fetchDeviceAuthorizedResource,

        resourceCommand, resourceTransaction, syncLightingResourceReferences,
        globalConfig,
        screenControl,
        profileList,
        defaultProfile,
        hotkeysConfig,
        dataIsReady,
        setUserRebooting,
        deviceBackgroundReadsPaused,
        firmwareUpdating,
        getReleaseInventory, downloadSelectedRelease, installSelectedRelease, releaseInstallAction,
        setFirmwareUpdating,

        previewScreenBrightness,
        getProfileMacros,
        stageDeferredProfileDetails,
        stageDeferredProfileMacros,
        stageDeferredHotkeysConfig,
        stageDeferredGlobalConfig,
        stageDeferredScreenControl,
        flushDeferredConfig,
        terminateWebConfigActivities,
        deferredConfigDirty,
        deferredConfigSaving,
        configSyncState, configReadProgress, configEditingBlocked, configRecovery, resolveConfigRecovery,
        retrySync: () => deferredConfigRef.current!.retry(),
        flushAndWait: () => flushDeferredConfig(undefined, true),
        switchProfile,
        isLoading,
        error,
        setError,
        rebootSystem: (...args) => runConfigExclusive(() => rebootSystem(...args)),
        exitWebConfig,
        // 校准相关
        calibrationStatus,
        fetchCalibrationStatus,
        startManualCalibration: (...args) => runConfigExclusive(() => startManualCalibration(...args), true),
        stopManualCalibration: () => stopLongDeviceActivity(stopManualCalibration),
        clearManualCalibrationData: (...args) => runConfigExclusive(() => clearManualCalibrationData(...args), true),
        checkIsManualCalibrationCompleted,
        // ADC Mapping 相关
        defaultMappingId: defaultMappingId,
        markingStatus,
        mappingList,
        activeMapping,
        mappingStorageMode,
        mappingSource,
        fetchMappingList,
        fetchMarkingStatus,
        updateDefaultMapping: (...args) => runConfigExclusive(() => updateDefaultMapping(...args), true),
        fetchDefaultMapping,
        fetchActiveMapping,
        createMapping: (...args) => runConfigExclusive(() => createMapping(...args)),
        deleteMapping: (...args) => runConfigExclusive(() => deleteMapping(...args)),
        startMarking: (...args) => runConfigExclusive(() => startMarking(...args)),
        stopMarking: () => stopLongDeviceActivity(stopMarking),
        stepMarking,
        syncMarkingProgress,
        renameMapping: (...args) => runConfigExclusive(() => renameMapping(...args)),
        fetchSwitchMappingCatalog,
        fetchSwitchMappingDetail,
        fetchSwitchMappingImage,
        uploadSwitchMappingImage,
        updateSwitchMappingMetadata,
        updateSwitchMappingCurve,
        deleteSwitchMapping,
        installMappingResource,
        installSwitchMapping: (...args) => runConfigExclusive(() => installSwitchMapping(...args), true),
        clearInstalledSwitchMapping: (...args) => runConfigExclusive(() => clearInstalledSwitchMapping(...args), true),
        createSwitchMappingFromCurrent,
        beginMappingDraft: (...args) => runConfigExclusive(() => beginMappingDraft(...args)),
        publishMappingDraft: (...args) => runConfigExclusive(() => publishMappingDraft(...args)),
        // 按键监控相关
        buttonMonitoringActive: buttonMonitoringActive,
        startButtonMonitoring,
        stopButtonMonitoring,
        getButtonStates,
        // 按键性能监控相关
        startButtonPerformanceMonitoring,
        stopButtonPerformanceMonitoring: () => stopLongDeviceActivity(stopButtonPerformanceMonitoring),
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
        uploadFirmwareToDevice: (...args) => runConfigExclusive(() => uploadFirmwareToDevice(...args)),
        uploadCh585Firmware: (...args) => runConfigExclusive(() => uploadCh585Firmware(...args)),
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
        diagnosticsPolicy: (enabled) => runConfigExclusive(() => diagnosticsPolicy(enabled)),
        fetchDeviceLogsPage: fetchDeviceLogsPage,
        clearDeviceLogs,
        getHitboxLayout: getHitboxLayout,
        hitboxLayout: hitboxLayout,
    };
    const storeRef = useRef<SelectionStore<GamepadConfigContextType> | null>(null);
    if (!storeRef.current) storeRef.current = new SelectionStore(value);
    const store = storeRef.current;
    useLayoutEffect(() => { store.publish(value); });
    return <GamepadConfigContext.Provider value={store}>{children}</GamepadConfigContext.Provider>;
}

export type { DeviceTransportConfigType, HitboxLayoutItem } from "./gamepad-config/contract";
