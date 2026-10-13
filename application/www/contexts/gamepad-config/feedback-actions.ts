import {
    SharedButtonMonitorLease,
    type SharedButtonMonitorLeaseToken,
} from '@/lib/button-monitor-lifecycle';
import {
    PerformanceTelemetryController,
    PostReadyRequestScheduler
} from '@/lib/device-transport';
import { LedPreviewCoordinator } from '@/lib/led-preview-coordinator';
import { SessionConfigStore } from '@/lib/session-config-store';
import {
    ButtonStates,
    CalibrationStatus,
    LEDsConfig
} from '@/types/types';
import React from 'react';
import { calibrationStatusFrom, DeviceRequestSender } from './contract';

export interface FeedbackActionsDependencies {
    screenPreviewRef: React.MutableRefObject<number | null>;
    feedbackSuspendedRef: React.MutableRefObject<boolean>;
    boundaryCountRef: React.MutableRefObject<number>;
    sendDeviceRequest: DeviceRequestSender;
    setCalibrationStatus: React.Dispatch<React.SetStateAction<CalibrationStatus>>;
    setError: React.Dispatch<React.SetStateAction<string | null>>;
    longDeviceActivityRef: React.MutableRefObject<boolean>;
    updateSyncPause: () => void;
    setIsLoading: React.Dispatch<React.SetStateAction<boolean>>;
    initializationGenerationRef: React.MutableRefObject<number>;
    calibrationCompletionRequestRef: React.MutableRefObject<{ generation: number; promise: Promise<boolean>; } | null>;
    postReadyRequestSchedulerRef: React.MutableRefObject<PostReadyRequestScheduler | null>;
    buttonMonitorLeaseRef: React.MutableRefObject<SharedButtonMonitorLease | null>;
    setButtonMonitoringActive: React.Dispatch<React.SetStateAction<boolean>>;
    performanceTelemetryRef: React.MutableRefObject<PerformanceTelemetryController | null>;
    ledPreviewRef: React.MutableRefObject<LedPreviewCoordinator<LEDsConfig>>;
    configStoreRef: React.MutableRefObject<SessionConfigStore>;
}

export function useFeedbackActions(dependencies: FeedbackActionsDependencies) {
    const previewScreenBrightness = async (brightness: number): Promise<void> => {
        const value = Math.max(0, Math.min(100, brightness | 0));
        dependencies.screenPreviewRef.current = value;
        if (!dependencies.feedbackSuspendedRef.current && !dependencies.boundaryCountRef.current) await dependencies.sendDeviceRequest('preview_screen_brightness', { brightness: value }, false);
    };

    const fetchCalibrationStatus = async (immediate: boolean = true): Promise<CalibrationStatus> => {
        try {
            const data = await dependencies.sendDeviceRequest('get_calibration_status', {}, immediate);
            const status = calibrationStatusFrom(data?.calibrationStatus);
            if (!status) throw new Error('Device returned an invalid calibration status');
            dependencies.setCalibrationStatus(status);
            dependencies.setError(null);
            return status;
        } catch (err) {
            const error = err instanceof Error ? err : new Error('Failed to fetch calibration status');
            dependencies.setError(error.message);
            throw error;
        }
    };

    const startManualCalibration = async (immediate: boolean = true): Promise<CalibrationStatus> => {
        dependencies.longDeviceActivityRef.current = true; dependencies.updateSyncPause();
        try {
            dependencies.setIsLoading(true);
            const data = await dependencies.sendDeviceRequest('start_manual_calibration', {}, immediate);
            const status = calibrationStatusFrom(data?.calibrationStatus);
            if (!status) throw new Error('Device returned an invalid calibration status');
            dependencies.setCalibrationStatus(status);
            dependencies.setError(null);
            return status;
        } catch (err) {
            dependencies.longDeviceActivityRef.current = false; dependencies.updateSyncPause();
            dependencies.setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to start manual calibration"));
        } finally {
            dependencies.setIsLoading(false);
        }
    };

    const stopManualCalibration = async (immediate: boolean = true): Promise<CalibrationStatus> => {
        try {
            dependencies.setIsLoading(true);
            const data = await dependencies.sendDeviceRequest('stop_manual_calibration', {}, immediate);
            const status = calibrationStatusFrom(data?.calibrationStatus);
            if (!status || status.isActive) throw new Error('Device did not confirm calibration stopped');
            dependencies.longDeviceActivityRef.current = false; dependencies.updateSyncPause();
            dependencies.setCalibrationStatus(status);
            dependencies.setError(null);
            return status;
        } catch (err) {
            dependencies.setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to stop manual calibration"));
        } finally {
            dependencies.setIsLoading(false);
        }
    };

    const clearManualCalibrationData = async (immediate: boolean = true): Promise<void> => {
        try {
            dependencies.setIsLoading(true);
            const data = await dependencies.sendDeviceRequest('clear_manual_calibration_data', {}, immediate);
            const status = calibrationStatusFrom(data?.calibrationStatus);
            if (!status) throw new Error('Device returned an invalid calibration status');
            dependencies.setCalibrationStatus(status);
            dependencies.setError(null);
            return Promise.resolve();
        } catch (err) {
            // setError(err instanceof Error ? err.message : 'An error occurred');
            return Promise.reject(new Error("Failed to clear manual calibration data"));
        } finally {
            dependencies.setIsLoading(false);
        }
    };

    const checkIsManualCalibrationCompleted = (immediate: boolean = true): Promise<boolean> => {
        const generation = dependencies.initializationGenerationRef.current;
        const existing = dependencies.calibrationCompletionRequestRef.current;
        if (existing?.generation === generation) {
            return existing.promise;
        }

        const promise = dependencies.postReadyRequestSchedulerRef.current!.schedule(async () => {
            try {
                const data = await dependencies.sendDeviceRequest(
                    'check_is_manual_calibration_completed',
                    {},
                    immediate,
                );
                return data.isCompleted === true;
            } catch {
                throw new Error("Failed to check if manual calibration is completed");
            }
        });
        dependencies.calibrationCompletionRequestRef.current = { generation, promise };
        void promise.finally(() => {
            if (dependencies.calibrationCompletionRequestRef.current?.promise === promise) {
                dependencies.calibrationCompletionRequestRef.current = null;
            }
        }).catch(() => undefined);
        return promise;
    };

    const startButtonMonitoring = (
        immediate: boolean = true,
    ): Promise<SharedButtonMonitorLeaseToken> => {
        const promise = dependencies.buttonMonitorLeaseRef.current!.acquire(async () => {
            try {
                const completed = await checkIsManualCalibrationCompleted(immediate);
                if (!completed) {
                    dependencies.setButtonMonitoringActive(false);
                    throw new Error("Manual calibration not completed");
                }

                const data = await dependencies.postReadyRequestSchedulerRef.current!.schedule(
                    () => dependencies.sendDeviceRequest('start_button_monitoring', {}, immediate),
                );
                dependencies.setButtonMonitoringActive(data.isActive ?? true);
                dependencies.setError(null);
            } catch {
                throw new Error("Failed to start button monitoring");
            }
        });
        dependencies.postReadyRequestSchedulerRef.current!.track(promise);
        return promise;
    };

    const stopButtonMonitoring = async (
        lease: SharedButtonMonitorLeaseToken,
        immediate: boolean = true,
    ): Promise<void> => {
        try {
            await dependencies.buttonMonitorLeaseRef.current!.release(
                lease,
                async () => {
                    const data = await dependencies.postReadyRequestSchedulerRef.current!.schedule(
                        () => dependencies.sendDeviceRequest('stop_button_monitoring', {}, immediate),
                    );
                    dependencies.setButtonMonitoringActive(data.isActive ?? false);
                    dependencies.setError(null);
                },
            );
        } catch (err) {
            return Promise.reject(new Error("Failed to stop button monitoring"));
        }
    };

    const getButtonStates = async (): Promise<ButtonStates> => {
        dependencies.setError(null);
        try {
            const data = await dependencies.sendDeviceRequest('get_button_states');
            return Promise.resolve(data as ButtonStates);
        } catch (error) {
            console.error('获取按键状态失败:', error);
            dependencies.setError(error instanceof Error ? error.message : '获取按键状态失败');
            throw error;
        }
    };

    const startButtonPerformanceMonitoring = async (immediate: boolean = true): Promise<void> => {
        dependencies.performanceTelemetryRef.current?.resetMonitoringSession();
        dependencies.longDeviceActivityRef.current = true; dependencies.updateSyncPause();
        try {
            const data = await dependencies.sendDeviceRequest('start_button_performance_monitoring', {}, immediate);
            if (data?.isActive !== true || data?.isTestModeEnabled !== true) {
                throw new Error('Device did not enter button performance monitoring mode');
            }
            dependencies.setButtonMonitoringActive(true);
            dependencies.setError(null);
            return;
        } catch (err) {
            dependencies.longDeviceActivityRef.current = false; dependencies.updateSyncPause();
            const error = err instanceof Error
                ? err
                : new Error('Failed to start button performance monitoring');
            dependencies.setButtonMonitoringActive(false);
            dependencies.setError(error.message);
            throw error;
        }
    };

    const stopButtonPerformanceMonitoring = async (immediate: boolean = true): Promise<void> => {
        try {
            const data = await dependencies.sendDeviceRequest('stop_button_performance_monitoring', {}, immediate);
            if (data?.isActive !== false || data?.isTestModeEnabled !== false) {
                throw new Error('Device did not leave button performance monitoring mode');
            }
            dependencies.longDeviceActivityRef.current = false; dependencies.updateSyncPause();
            dependencies.setButtonMonitoringActive(false);
            dependencies.performanceTelemetryRef.current?.resetMonitoringSession();
            dependencies.setError(null);
            return;
        } catch (err) {
            const error = err instanceof Error
                ? err
                : new Error('Failed to stop button performance monitoring');
            dependencies.setError(error.message);
            throw error;
        }
    };

    const pushLedsConfig = async (ledsConfig: LEDsConfig, immediate: boolean = false): Promise<void> => {
        dependencies.setError(null);
        try {
            await dependencies.ledPreviewRef.current.push(ledsConfig, async (config) => {
                if (dependencies.feedbackSuspendedRef.current || dependencies.configStoreRef.current.get('selected-profile') !== dependencies.configStoreRef.current.confirmed['selected-profile']) return;
                await dependencies.sendDeviceRequest('push_leds_config', config as unknown as Record<string, unknown>, immediate);
            });
            return Promise.resolve();
        } catch (error) {
            const normalized = error instanceof Error
                ? error
                : new Error("Failed to push LED configuration");
            dependencies.setError(normalized.message);
            return Promise.reject(normalized);
        }
    };

    const clearLedsPreview = async (immediate: boolean = true): Promise<void> => {
        dependencies.setError(null);
        try {
            await dependencies.ledPreviewRef.current.clear(async () => {
                await dependencies.sendDeviceRequest('clear_leds_preview', {}, immediate);
            });
            return Promise.resolve();
        } catch (error) {
            const normalized = error instanceof Error
                ? error
                : new Error("Failed to clear LED preview");
            dependencies.setError(normalized.message);
            return Promise.reject(normalized);
        }
    };
    return {
        previewScreenBrightness, fetchCalibrationStatus, startManualCalibration, stopManualCalibration,
        clearManualCalibrationData, checkIsManualCalibrationCompleted, startButtonMonitoring,
        stopButtonMonitoring, getButtonStates, startButtonPerformanceMonitoring,
        stopButtonPerformanceMonitoring, pushLedsConfig, clearLedsPreview,
    };
}
