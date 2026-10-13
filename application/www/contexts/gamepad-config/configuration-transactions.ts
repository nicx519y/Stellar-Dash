import {
    SharedButtonMonitorLease
} from '@/lib/button-monitor-lifecycle';
import {
    DeferredConfigCoordinator
} from '@/lib/deferred-config-coordinator';
import {
    DeviceCommandClient,
    PostReadyRequestScheduler
} from '@/lib/device-transport';
import { LedPreviewCoordinator } from '@/lib/led-preview-coordinator';
import { SessionConfigStore, type ConfigDraftBackup } from '@/lib/session-config-store';
import {
    LEDsConfig
} from '@/types/types';
import React from 'react';
import { DeviceRequestSender } from './contract';

export interface ConfigTransactionsDependencies {
    initializationGenerationRef: React.MutableRefObject<number>;
    configReadyRef: React.MutableRefObject<boolean>;
    deferredConfigRef: React.MutableRefObject<DeferredConfigCoordinator | null>;
    configStoreRef: React.MutableRefObject<SessionConfigStore>;
    setDeferredConfigSaving: React.Dispatch<React.SetStateAction<boolean>>;
    ledPreviewRef: React.MutableRefObject<LedPreviewCoordinator<LEDsConfig>>;
    sendDeviceRequest: DeviceRequestSender;
    buttonMonitorLeaseRef: React.MutableRefObject<SharedButtonMonitorLease | null>;
    feedbackSuspendedRef: React.MutableRefObject<boolean>;
    checkIsManualCalibrationCompleted: (immediate?: boolean) => Promise<boolean>;
    setButtonMonitoringActive: React.Dispatch<React.SetStateAction<boolean>>;
    postReadyRequestSchedulerRef: React.MutableRefObject<PostReadyRequestScheduler | null>;
    longDeviceActivityRef: React.MutableRefObject<boolean>;
    deviceClientRef: React.MutableRefObject<DeviceCommandClient | null>;
    setError: React.Dispatch<React.SetStateAction<string | null>>;
    screenPreviewRef: React.MutableRefObject<number | null>;
    boundaryCountRef: React.MutableRefObject<number>;
    setConfigBoundaryBusy: React.Dispatch<React.SetStateAction<boolean>>;
    updateSyncPause: () => void;
    recoveryRef: React.MutableRefObject<ConfigDraftBackup | null>;
    deferredFlushTailRef: React.MutableRefObject<Promise<void>>;
}

export function createConfigTransactions(dependencies: ConfigTransactionsDependencies) {
    const performDeferredConfigFlush = async (
        afterFlush?: () => void | Promise<void>,
        resumeMonitoringAfterSuccess: boolean = false,
        beforeFlush?: () => void | Promise<void>,
        all = true,
    ): Promise<void> => {
        const generation = dependencies.initializationGenerationRef.current;
        const current = () => generation === dependencies.initializationGenerationRef.current && dependencies.configReadyRef.current;
        if (!current()) throw new Error("Device configuration is not ready");
        const hasDeferredWrites = dependencies.deferredConfigRef.current!.dirty;
        if (!hasDeferredWrites && !afterFlush) return;

        // Ordinary autosave keeps the device's input workers and LED DMA alive.
        // Only explicit device operations below own the stop/clear boundary.
        if (!all) {
            const previousProfile = dependencies.configStoreRef.current.confirmed['selected-profile'];
            dependencies.setDeferredConfigSaving(true);
            try {
                await dependencies.deferredConfigRef.current!.flush(false);
                if (!current()) throw new Error('Configuration session ended');
            } finally {
                if (current()) {
                    dependencies.setDeferredConfigSaving(false);
                    // Profile previews requested before the switch ACK were
                    // retained locally. Replay the latest one after confirmation.
                    if (previousProfile !== dependencies.configStoreRef.current.confirmed['selected-profile']) {
                        await dependencies.ledPreviewRef.current.replay(async config => {
                            if (dependencies.configStoreRef.current.get('selected-profile') !== dependencies.configStoreRef.current.confirmed['selected-profile']) return;
                            await dependencies.sendDeviceRequest('push_leds_config', config as unknown as Record<string, unknown>, false);
                        }).catch(error => console.warn('LED preview refresh failed:', error));
                    }
                }
            }
            return;
        }

        const lease = dependencies.buttonMonitorLeaseRef.current!;
        const preview = dependencies.ledPreviewRef.current;
        // Block new previews synchronously before stopping the workers. A
        // queued push is drained before stop; later pushes only replace the
        // snapshot and cannot restart sampling in the middle of a save.
        const previewSuspension = preview.suspend();
        dependencies.feedbackSuspendedRef.current = true;
        let completed = false;
        let suspended = false;
        if (hasDeferredWrites) dependencies.setDeferredConfigSaving(true);
        const resumeButtonMonitor = async () => {
            await lease.resume(async () => {
                const completed = await dependencies.checkIsManualCalibrationCompleted(true);
                if (!completed) { dependencies.setButtonMonitoringActive(false); throw new Error('Manual calibration not completed'); }
                const data = await dependencies.postReadyRequestSchedulerRef.current!.schedule(
                    () => dependencies.sendDeviceRequest('start_button_monitoring', {}, true),
                );
                dependencies.setButtonMonitoringActive(data.isActive ?? true);
            });
        };
        try {
            suspended = await lease.suspend(async () => {
                const data = await dependencies.postReadyRequestSchedulerRef.current!.schedule(
                    () => dependencies.sendDeviceRequest('stop_button_monitoring', {}, true),
                );
                dependencies.setButtonMonitoringActive(data.isActive ?? false);
            });

            if (preview.active) {
                // Preserve the desired snapshot for resume. The public clear
                // API instead releases preview ownership on page teardown.
                await dependencies.sendDeviceRequest('clear_leds_preview', {}, true);
            }

            await beforeFlush?.();
            if (dependencies.longDeviceActivityRef.current) throw new Error("Stop calibration or performance monitoring before this device operation");
            if (!current()) throw new Error("Configuration session ended");
            await dependencies.deferredConfigRef.current!.flush(all);
            await dependencies.deviceClientRef.current?.flushQueue();
            if (!current()) throw new Error("Configuration session ended");
            await afterFlush?.();
            if (suspended && current()) {
                if (resumeMonitoringAfterSuccess && !dependencies.longDeviceActivityRef.current && dependencies.configStoreRef.current.get('selected-profile') === dependencies.configStoreRef.current.confirmed['selected-profile']) {
                    try { await resumeButtonMonitor(); }
                    catch (error) { console.warn('Configuration saved; monitoring remains paused:', error); }
                } else if (!resumeMonitoringAfterSuccess) {
                    lease.finalizeSuspension();
                }
            }
            completed = true;
            dependencies.setError(null);
        } catch (error) {
            if (suspended && current() && !dependencies.longDeviceActivityRef.current && dependencies.configStoreRef.current.get('selected-profile') === dependencies.configStoreRef.current.confirmed['selected-profile']) {
                try {
                    await resumeButtonMonitor();
                } catch (resumeError) {
                    console.error('恢复按键监控失败:', resumeError);
                }
            }
            const normalized = error instanceof Error
                ? error
                : new Error('Failed to flush deferred configuration');
            throw normalized;
        } finally {
            const canResume = current() && !dependencies.longDeviceActivityRef.current && (!completed || resumeMonitoringAfterSuccess)
                && dependencies.configStoreRef.current.get('selected-profile') === dependencies.configStoreRef.current.confirmed['selected-profile'];
            if (current()) dependencies.feedbackSuspendedRef.current = !canResume;
            try {
                if (current()) await preview.resume(
                    previewSuspension,
                    async (config) => {
                        if (dependencies.configStoreRef.current.get('selected-profile') !== dependencies.configStoreRef.current.confirmed['selected-profile']) return;
                        await dependencies.sendDeviceRequest('push_leds_config', config as unknown as Record<string, unknown>, true);
                    },
                    canResume,
                );
            } catch (previewError) {
                // A preview failure must not turn an acknowledged profile
                // switch into a reported persistence failure.
                console.error('恢复灯光预览失败:', previewError);
                dependencies.setError(previewError instanceof Error ? previewError.message : 'Failed to restore LED preview');
            }
            if (current() && canResume && dependencies.screenPreviewRef.current !== null) {
                await dependencies.sendDeviceRequest('preview_screen_brightness', { brightness: dependencies.screenPreviewRef.current }, true).catch(() => undefined);
            }
            if (current() && hasDeferredWrites) dependencies.setDeferredConfigSaving(false);
        }
    };

    const queueConfigTransaction = (
        afterFlush?: () => void | Promise<void>,
        resumeMonitoringAfterSuccess = true,
        beforeFlush?: () => void | Promise<void>,
        all = true,
    ): Promise<void> => {
        const generation = dependencies.initializationGenerationRef.current;
        if (all) { dependencies.boundaryCountRef.current++; dependencies.setConfigBoundaryBusy(true); dependencies.updateSyncPause(); }
        const run = async () => {
            if (generation !== dependencies.initializationGenerationRef.current || !dependencies.configReadyRef.current || dependencies.recoveryRef.current) throw new Error('Configuration session is not ready');
            if (!all && (dependencies.boundaryCountRef.current || dependencies.longDeviceActivityRef.current)) return;
            if (all && dependencies.longDeviceActivityRef.current && !beforeFlush) throw new Error('Stop calibration or performance monitoring before this device operation');
            await performDeferredConfigFlush(afterFlush, resumeMonitoringAfterSuccess, beforeFlush, all);
        };
        const operation = dependencies.deferredFlushTailRef.current.then(run, run).finally(() => {
            if (all) {
                dependencies.boundaryCountRef.current--;
                dependencies.setConfigBoundaryBusy(dependencies.boundaryCountRef.current > 0);
                dependencies.updateSyncPause();
            }
        });
        dependencies.deferredFlushTailRef.current = operation.catch(() => undefined);
        return operation;
    };

    const flushDeferredConfig = (
        afterFlush?: () => void | Promise<void>,
        resumeMonitoringAfterSuccess = false,
        beforeFlush?: () => void | Promise<void>,
    ) => queueConfigTransaction(afterFlush, resumeMonitoringAfterSuccess, beforeFlush);
    return { performDeferredConfigFlush, queueConfigTransaction, flushDeferredConfig };
}
