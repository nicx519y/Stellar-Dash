import { useCallback } from 'react';
import { makeEmptyCalibrationStatus, makeEmptyMarkingStatus } from './contract';
import type { SessionLifecycleDependencies } from './session-lifecycle';

export function useSessionReset(dependencies: Pick<SessionLifecycleDependencies, 'postReadyRequestSchedulerRef' | 'buttonMonitorLeaseRef' | 'ledPreviewRef' | 'deferredConfigRef' | 'configReadyRef' | 'configStoreRef' | 'detachedDraftsRef' | 'recoveryRef' | 'setConfigRecovery' | 'feedbackSuspendedRef' | 'screenPreviewRef' | 'longDeviceActivityRef' | 'setDeferredConfigSaving' | 'calibrationCompletionRequestRef' | 'initializationGenerationRef' | 'setDefaultMappingId' | 'setMappingList' | 'setMarkingStatus' | 'setActiveMapping' | 'setCalibrationStatus' | 'setButtonMonitoringActive' | 'setFirmwareInfo' | 'setFirmwareUpdateInfo' | 'setUpgradeSession' | 'setHitboxLayout' | 'setDataIsReady' | 'setConfigReadProgress' | 'setFinishConfigDisabled' | 'setDeviceSession' | 'initialAutoConnectCancelRef' | 'foregroundReconnectCancelRef'>) {
    const resetDeviceSessionState = useCallback(() => {
        dependencies.postReadyRequestSchedulerRef.current?.endSession();
        dependencies.buttonMonitorLeaseRef.current?.endSession();
        dependencies.ledPreviewRef.current.reset();
        dependencies.deferredConfigRef.current?.clear();
        const wasReady = dependencies.configReadyRef.current;
        dependencies.configReadyRef.current = false;
        const backup = wasReady ? dependencies.configStoreRef.current.detach() : null;
        if (backup?.deviceId) dependencies.detachedDraftsRef.current.set(backup.deviceId, backup);
        dependencies.recoveryRef.current = null;
        dependencies.setConfigRecovery([]);
        dependencies.deferredConfigRef.current?.pause(true);
        dependencies.feedbackSuspendedRef.current = false;
        dependencies.screenPreviewRef.current = null;
        dependencies.longDeviceActivityRef.current = false;
        dependencies.setDeferredConfigSaving(false);
        dependencies.calibrationCompletionRequestRef.current = null;
        dependencies.initializationGenerationRef.current += 1;
        dependencies.setDefaultMappingId("");
        dependencies.setMappingList([]);
        dependencies.setMarkingStatus(makeEmptyMarkingStatus());
        dependencies.setActiveMapping(null);
        dependencies.setCalibrationStatus(makeEmptyCalibrationStatus());
        dependencies.setButtonMonitoringActive(false);
        dependencies.setFirmwareInfo(null);
        dependencies.setFirmwareUpdateInfo(null);
        dependencies.setUpgradeSession(null);
        dependencies.setHitboxLayout([]);
        dependencies.setDataIsReady(false);
        dependencies.setConfigReadProgress({ completed: 0, total: 0 });
        dependencies.setFinishConfigDisabled(false);
        dependencies.setDeviceSession(null);
    }, []);
    const cancelPendingAutomaticConnects = useCallback(() => {
        dependencies.initialAutoConnectCancelRef.current?.();
        dependencies.initialAutoConnectCancelRef.current = null;
        dependencies.foregroundReconnectCancelRef.current?.();
        dependencies.foregroundReconnectCancelRef.current = null;
    }, []);
    return { resetDeviceSessionState, cancelPendingAutomaticConnects };
}
