import { BUTTON_STATE_CHANGED_CMD, type ButtonStateBinaryData } from '@/lib/button-binary-parser';
import {
    SharedButtonMonitorLease
} from '@/lib/button-monitor-lifecycle';
import {
    DeferredConfigCoordinator
} from '@/lib/deferred-config-coordinator';
import {
    configuredTransportMode,
    createDeviceCommandClient,
    DeviceCommandClient,
    DeviceCommandMessage,
    DeviceConnectionError,
    DeviceConnectionPhase,
    DeviceRequestOptions,
    DeviceResponse,
    DeviceSession,
    DeviceTransportConfig,
    DeviceTransportState,
    PerformanceTelemetryController,
    PostReadyRequestScheduler,
    registerDeviceArrivalReconnect,
    registerDevicePageLifecycle,
    registerDeviceVisibilityLifecycle,
    scheduleInitialDeviceAutoConnect,
    WebHidTransport
} from '@/lib/device-transport';
import { readDeviceConfigSnapshot, type ConfigSyncProgress, type ConfigSyncResult } from '@/lib/device-transport/config-sync';
import { createDeviceConnectionWatchdog } from '@/lib/device-transport/device-connection-watchdog';
import { initializeDeviceSession } from '@/lib/device-transport/device-initialization';
import { eventBus, EVENTS } from '@/lib/event-manager';
import { LedPreviewCoordinator } from '@/lib/led-preview-coordinator';
import {
    configDifferences, restoredConfigDraft, SessionConfigStore, type ConfigDraftBackup,
    type ConfigResources,
} from '@/lib/session-config-store';
import {
    ADCValuesMapping,
    StepInfo
} from '@/types/adc';
import {
    GlobalConfig,
    ScreenControlConfig
} from '@/types/gamepad-config';
import {
    CalibrationStatus,
    DeviceFirmwareInfo,
    FirmwareUpdateCheckResponse,
    FirmwareUpgradeSession,
    LEDsConfig
} from '@/types/types';
import React, { useEffect } from 'react';
import { calibrationStatusFrom, converProfileDetails, DeviceRequestSender, HitboxLayoutItem } from './contract';

export interface SessionLifecycleDependencies {
    globalConfigRef: React.MutableRefObject<GlobalConfig>;
    globalConfig: GlobalConfig;
    screenControlRef: React.MutableRefObject<ScreenControlConfig>;
    screenControl: ScreenControlConfig;
    postReadyRequestSchedulerRef: React.MutableRefObject<PostReadyRequestScheduler | null>;
    buttonMonitorLeaseRef: React.MutableRefObject<SharedButtonMonitorLease | null>;
    ledPreviewRef: React.MutableRefObject<LedPreviewCoordinator<LEDsConfig>>;
    deferredConfigRef: React.MutableRefObject<DeferredConfigCoordinator | null>;
    configReadyRef: React.MutableRefObject<boolean>;
    configStoreRef: React.MutableRefObject<SessionConfigStore>;
    detachedDraftsRef: React.MutableRefObject<Map<string, ConfigDraftBackup>>;
    recoveryRef: React.MutableRefObject<ConfigDraftBackup | null>;
    setConfigRecovery: React.Dispatch<React.SetStateAction<string[]>>;
    feedbackSuspendedRef: React.MutableRefObject<boolean>;
    screenPreviewRef: React.MutableRefObject<number | null>;
    longDeviceActivityRef: React.MutableRefObject<boolean>;
    setDeferredConfigSaving: React.Dispatch<React.SetStateAction<boolean>>;
    calibrationCompletionRequestRef: React.MutableRefObject<{ generation: number; promise: Promise<boolean>; } | null>;
    initializationGenerationRef: React.MutableRefObject<number>;
    setDefaultMappingId: React.Dispatch<React.SetStateAction<string>>;
    setMappingList: React.Dispatch<React.SetStateAction<{ id: string; name: string; }[]>>;
    setMarkingStatus: React.Dispatch<React.SetStateAction<StepInfo>>;
    setActiveMapping: React.Dispatch<React.SetStateAction<ADCValuesMapping | null>>;
    setCalibrationStatus: React.Dispatch<React.SetStateAction<CalibrationStatus>>;
    setButtonMonitoringActive: React.Dispatch<React.SetStateAction<boolean>>;
    setFirmwareInfo: React.Dispatch<React.SetStateAction<DeviceFirmwareInfo | null>>;
    setFirmwareUpdateInfo: React.Dispatch<React.SetStateAction<FirmwareUpdateCheckResponse | null>>;
    setUpgradeSession: React.Dispatch<React.SetStateAction<FirmwareUpgradeSession | null>>;
    setHitboxLayout: React.Dispatch<React.SetStateAction<HitboxLayoutItem[]>>;
    setDataIsReady: React.Dispatch<React.SetStateAction<boolean>>;
    setConfigReadProgress: React.Dispatch<React.SetStateAction<ConfigSyncProgress>>;
    setFinishConfigDisabled: React.Dispatch<React.SetStateAction<boolean>>;
    setDeviceSession: React.Dispatch<React.SetStateAction<DeviceSession | null>>;
    initialAutoConnectCancelRef: React.MutableRefObject<(() => void) | null>;
    foregroundReconnectCancelRef: React.MutableRefObject<(() => void) | null>;
    transportConfig: DeviceTransportConfig;
    deviceClientRef: React.MutableRefObject<DeviceCommandClient | null>;
    rfBindingConnectionEpoch: React.MutableRefObject<number>;
    setDeviceState: React.Dispatch<React.SetStateAction<DeviceTransportState>>;
    setDeviceConnected: React.Dispatch<React.SetStateAction<boolean>>;
    setDevicePhase: React.Dispatch<React.SetStateAction<DeviceConnectionPhase>>;
    setDeviceError: React.Dispatch<React.SetStateAction<DeviceConnectionError | null>>;
    performanceTelemetryRef: React.MutableRefObject<PerformanceTelemetryController | null>;
    pageHiddenRef: React.MutableRefObject<boolean>;
    initialAutoConnectClientRef: React.MutableRefObject<DeviceCommandClient | null>;
    setShowReconnect: React.Dispatch<React.SetStateAction<boolean>>;
    setDeviceClient: React.Dispatch<React.SetStateAction<DeviceCommandClient | null>>;
    devicePhase: DeviceConnectionPhase;
    deviceClient: DeviceCommandClient | null;
    deviceState: DeviceTransportState;
    userRebooting: boolean;
    firmwareUpdating: boolean;
    deferredConfigDirty: boolean;
    deferredConfigSaving: boolean;
    deviceConnected: boolean;
    configRecovery: string[];
    dataIsReady: boolean;
    fetchFirmwareMetadata: (immediate?: boolean, requester?: DeviceRequestSender) => Promise<void>;
    sendInitializationDeviceRequest: DeviceRequestSender;
    getHitboxLayout: (immediate?: boolean, requester?: DeviceRequestSender) => Promise<HitboxLayoutItem[]>;
    publishConfig: () => void;
    updateSyncPause: () => void;
    setError: React.Dispatch<React.SetStateAction<string | null>>;
    reconnectDevice: () => Promise<void>;
    connectionWatchdogPausedRef: React.MutableRefObject<boolean>;
    boundaryCountRef: React.MutableRefObject<number>;
}

export function useSessionLifecycle(dependencies: SessionLifecycleDependencies, callbacks: { resetDeviceSessionState: () => void; cancelPendingAutomaticConnects: () => void }) {
    const { resetDeviceSessionState, cancelPendingAutomaticConnects } = callbacks;
    const deviceClient = dependencies.deviceClient;
    useEffect(() => {
        dependencies.globalConfigRef.current = dependencies.globalConfig;
    }, [dependencies.globalConfig]);

    useEffect(() => {
        dependencies.screenControlRef.current = dependencies.screenControl;
    }, [dependencies.screenControl]);

    const handleNotificationMessage = (message: DeviceCommandMessage): void => {
        const { command, data } = message;

        switch (command) {
            case 'calibration_update':
                {
                    const status = calibrationStatusFrom(data?.calibrationStatus);
                    if (status) dependencies.setCalibrationStatus(status);
                }
                eventBus.emit(EVENTS.CALIBRATION_UPDATE, data);
                break;
            case 'marking_status_update':
                if (data?.status) {
                    dependencies.setMarkingStatus(data.status as StepInfo);
                }
                eventBus.emit(EVENTS.MARKING_STATUS_UPDATE, data);
                break;
            case 'button.state': {
                if (dependencies.feedbackSuspendedRef.current || dependencies.configStoreRef.current.get('selected-profile') !== dependencies.configStoreRef.current.confirmed['selected-profile']) break;
                const payload = data ?? {};
                const buttonState: ButtonStateBinaryData = {
                    command: BUTTON_STATE_CHANGED_CMD,
                    isActive: payload.isActive === true,
                    triggerMask: Number(payload.triggerMask ?? 0) >>> 0,
                    totalButtons: Number(payload.totalButtons ?? 0) & 0xff,
                    eventSequence: Number(payload.eventSequence ?? 0) >>> 0,
                    droppedSnapshots: Number(payload.droppedSnapshots ?? 0) & 0xffff,
                };
                eventBus.emit(EVENTS.BUTTON_STATE_CHANGED, buttonState);
                break;
            }
            case 'performance.sample':
            case 'performance.edge':
            case 'performance.checkpoint':
            case 'transport.sequence-gap':
                // PerformanceTelemetryController consumes these typed events.
                break;
            default:
                console.log('收到未知通知消息:', message);
        }
    };

    useEffect(() => {
        const client = createDeviceCommandClient({
            mode: configuredTransportMode(),
            config: dependencies.transportConfig,
        });
        dependencies.deviceClientRef.current = client;

        const unsubscribeState = client.onStateChange((state) => {
            if (state !== DeviceTransportState.CONNECTED) dependencies.rfBindingConnectionEpoch.current++;
            dependencies.setDeviceState(state);
            dependencies.setDeviceConnected(state === DeviceTransportState.CONNECTED);
            dependencies.setDeviceSession(
                state === DeviceTransportState.CONNECTED
                    ? client.transport.session
                    : null,
            );
        });
        const unsubscribePhase = client.onPhaseChange(dependencies.setDevicePhase);
        const unsubscribeError = client.onError((error) => {
            dependencies.setDeviceError(error);
            console.error(
                '设备传输错误:',
                `type=${error.type}`,
                `transport=${error.transportCode ?? 'none'}`,
                `phase=${error.phase ?? 'none'}`,
                `command=${error.command ?? 'none'}`,
                error.message,
            );
        });
        const unsubscribeMessage = client.onMessage(handleNotificationMessage);
        const unsubscribeDisconnect = client.onDisconnect(() => {
            console.log('设备传输连接断开，触发全局断开事件');
            eventBus.emit(EVENTS.DEVICE_DISCONNECTED);
        });

        const performanceTelemetry = new PerformanceTelemetryController(client.transport, {
            requester: {
                request: async <T = Record<string, unknown> | undefined>(
                    command: string,
                    params: Record<string, unknown> = {},
                    options: DeviceRequestOptions = {},
                ): Promise<DeviceResponse<T>> => {
                    const data = await dependencies.postReadyRequestSchedulerRef.current!.schedule(
                        () => client.request(command, params, options),
                    );
                    return {
                        transactionId: 0,
                        data: data as T,
                    };
                },
            },
        });
        dependencies.performanceTelemetryRef.current = performanceTelemetry;
        const unsubscribePerformance = performanceTelemetry.subscribe((snapshot) => {
            eventBus.emit(EVENTS.BUTTON_PERFORMANCE_MONITORING, snapshot);
        });
        const restoreForegroundConnection = () => {
            dependencies.pageHiddenRef.current = false;
            cancelPendingAutomaticConnects();
            if (client.getState() === DeviceTransportState.CONNECTED) {
                performanceTelemetry.startClockSync();
                return;
            }
            dependencies.initialAutoConnectClientRef.current = client;
            resetDeviceSessionState();
            dependencies.setShowReconnect(false);
            const cancel = scheduleInitialDeviceAutoConnect(
                client.transport.kind,
                dependencies.transportConfig.closeTimeoutMs,
                () => {
                    dependencies.foregroundReconnectCancelRef.current = null;
                    return client.connect(false);
                },
                (error) => {
                    dependencies.foregroundReconnectCancelRef.current = null;
                    console.error('前台 WebHID 自动重连失败:', error);
                    if (!dependencies.pageHiddenRef.current) dependencies.setShowReconnect(true);
                },
            );
            dependencies.foregroundReconnectCancelRef.current = cancel;
        };
        const removeVisibilityLifecycle = registerDeviceVisibilityLifecycle({
            pauseBackgroundActivity: () => {
                dependencies.pageHiddenRef.current = true;
                cancelPendingAutomaticConnects();
                dependencies.setShowReconnect(false);
                // Do not abort a request that has already reached WebHID. Just
                // stop the optional 10-second clock probes from creating new
                // writes while Chromium may throttle or freeze this document.
                performanceTelemetry.pauseClockSync();
            },
            restoreForegroundActivity: restoreForegroundConnection,
        });
        const removePageLifecycle = registerDevicePageLifecycle({
            suspendForBfcache: () => {
                dependencies.pageHiddenRef.current = true;
                cancelPendingAutomaticConnects();
                // Physical close only. The live JS client is retained so the
                // same bfcache document can explicitly reconnect on pageshow.
                client.disconnect();
            },
            destroyDocument: () => {
                dependencies.pageHiddenRef.current = true;
                cancelPendingAutomaticConnects();
                client.dispose();
            },
            restoreFromBfcache: () => {
                restoreForegroundConnection();
            },
        });
        dependencies.setDeviceClient(client);

        return () => {
            if (dependencies.deviceClientRef.current === client) {
                dependencies.deviceClientRef.current = null;
            }
            cancelPendingAutomaticConnects();
            removeVisibilityLifecycle();
            removePageLifecycle();
            unsubscribeState();
            unsubscribePhase();
            unsubscribeError();
            unsubscribeMessage();
            unsubscribeDisconnect();
            unsubscribePerformance();
            performanceTelemetry.stop();
            dependencies.performanceTelemetryRef.current = null;
            client.dispose();
        };
    }, []);

    useEffect(() => {
        const telemetry = dependencies.performanceTelemetryRef.current;
        if (!telemetry) return;
        if (dependencies.devicePhase === DeviceConnectionPhase.READY) {
            telemetry.start({ deferClockSync: true });
            dependencies.postReadyRequestSchedulerRef.current?.releaseInitialBatchWhenIdle(
                () => {
                    if (!dependencies.pageHiddenRef.current) telemetry.startClockSync();
                },
            );
        } else {
            dependencies.postReadyRequestSchedulerRef.current?.endSession();
            telemetry.stop();
        }
    }, [dependencies.devicePhase]);

    useEffect(() => {
        if (dependencies.pageHiddenRef.current) return;
        if (deviceClient && dependencies.deviceState === DeviceTransportState.DISCONNECTED) {
            if (dependencies.initialAutoConnectClientRef.current !== deviceClient) {
                resetDeviceSessionState();
                const cancel = scheduleInitialDeviceAutoConnect(
                    deviceClient.transport.kind,
                    dependencies.transportConfig.closeTimeoutMs,
                    () => {
                        dependencies.initialAutoConnectCancelRef.current = null;
                        dependencies.initialAutoConnectClientRef.current = deviceClient;
                        return deviceClient.connect(false);
                    },
                    (error) => {
                        console.error('首次 WebHID 连接失败:', error);
                        dependencies.setShowReconnect(true);
                    },
                );
                dependencies.initialAutoConnectCancelRef.current = cancel;
                return () => {
                    if (dependencies.initialAutoConnectCancelRef.current === cancel) {
                        dependencies.initialAutoConnectCancelRef.current = null;
                    }
                    cancel();
                };
            } else {
                if (!dependencies.userRebooting && !dependencies.firmwareUpdating) {
                    dependencies.setShowReconnect(true);
                }
            }
        }
    }, [
        deviceClient,
        dependencies.deviceState,
        dependencies.userRebooting,
        dependencies.firmwareUpdating,
        resetDeviceSessionState,
        dependencies.transportConfig.closeTimeoutMs,
    ]);

    useEffect(() => {
        if (dependencies.deviceState === DeviceTransportState.DISCONNECTED || dependencies.deviceState === DeviceTransportState.ERROR) {
            resetDeviceSessionState();
        }
    }, [dependencies.deviceState, resetDeviceSessionState]);

    useEffect(() => {
        if ((!dependencies.deferredConfigDirty && !dependencies.deferredConfigSaving && !dependencies.detachedDraftsRef.current.size) || typeof window === 'undefined') return;
        const warnBeforeUnload = (event: BeforeUnloadEvent) => {
            event.preventDefault();
            event.returnValue = '';
        };
        window.addEventListener('beforeunload', warnBeforeUnload);
        return () => window.removeEventListener('beforeunload', warnBeforeUnload);
    }, [dependencies.deferredConfigDirty, dependencies.deferredConfigSaving, dependencies.deviceConnected, dependencies.configRecovery]);

    useEffect(() => {
        // Reconnect clears readiness before the new connect lifecycle settles.
        // A previous CONNECTED render must not read configuration while the
        // current client is still discovering/opening its new session.
        if (deviceClient && dependencies.deviceConnected && dependencies.deviceState === DeviceTransportState.CONNECTED && !dependencies.dataIsReady &&
            dependencies.devicePhase === DeviceConnectionPhase.INITIALIZING && deviceClient.getPhase() === DeviceConnectionPhase.INITIALIZING) {
            const client = deviceClient;
            const controller = new AbortController();
            dependencies.setShowReconnect(false);
            const generation = dependencies.initializationGenerationRef.current;
            let resources: ConfigResources;
            let syncResult: ConfigSyncResult;
            void initializeDeviceSession({
                loaders: {
                    configuration: async () => {
                        syncResult = await readDeviceConfigSnapshot(
                            (command, params) => client.requestInitialization(command, params, { signal: controller.signal }),
                            converProfileDetails,
                            (progress) => { if (generation === dependencies.initializationGenerationRef.current && !controller.signal.aborted) dependencies.setConfigReadProgress(progress); },
                            () => generation === dependencies.initializationGenerationRef.current && !controller.signal.aborted,
                        );
                        resources = syncResult.resources;
                    },
                    globalConfig: async () => { }, screenControl: async () => { },
                    profileList: async () => { }, hotkeys: async () => { },
                    firmwareMetadata: () => dependencies.fetchFirmwareMetadata(true, dependencies.sendInitializationDeviceRequest),
                    hitboxLayout: () => dependencies.getHitboxLayout(true, dependencies.sendInitializationDeviceRequest),
                },
                isCurrent: () => generation === dependencies.initializationGenerationRef.current,
                signal: controller.signal,
                timeoutMs: dependencies.transportConfig.startupTimeoutMs,
                deadlineAtMs: client.getStartupDeadlineMs() ?? undefined,
                onStage: (stage, status) => {
                    if (status === 'started') client.setInitializationStage(stage);
                },
                onReady: (layout) => {
                    if (!client.markReady()) return;
                    dependencies.postReadyRequestSchedulerRef.current?.beginSession();
                    dependencies.buttonMonitorLeaseRef.current?.beginSession();
                    const identity = syncResult.manifest
                        ? `${client.transport.kind}:${syncResult.manifest.deviceCacheKey}`
                        : client.transport.session?.deviceId ?? (configuredTransportMode() === 'mock' ? 'mock' : '');
                    dependencies.configStoreRef.current.hydrate(identity, resources);
                    dependencies.publishConfig();
                    const backup = identity ? dependencies.detachedDraftsRef.current.get(identity) : undefined;
                    if (backup) {
                        dependencies.recoveryRef.current = backup;
                        dependencies.setConfigRecovery(configDifferences(resources, restoredConfigDraft(backup, resources)));
                        // An uncertain in-flight write still requires a decision even if values match.
                        if (!configDifferences(resources, restoredConfigDraft(backup, resources)).length) dependencies.setConfigRecovery(['Device confirmation / 设备写入结果确认']);
                    }
                    dependencies.configReadyRef.current = true;
                    dependencies.updateSyncPause();
                    dependencies.setHitboxLayout(layout);
                    dependencies.setDataIsReady(true);
                },
                onFailure: (initializationError) => {
                    const message = initializationError instanceof Error
                        ? initializationError.message
                        : String(initializationError);
                    dependencies.setError(`设备数据初始化失败: ${message}`);
                    dependencies.setShowReconnect(true);
                    client.disconnect();
                },
            });
            return () => controller.abort();
        }
    }, [dependencies.deviceConnected, dependencies.deviceState, dependencies.devicePhase, dependencies.dataIsReady, deviceClient]);

    useEffect(() => {
        if (!deviceClient || dependencies.firmwareUpdating || !(deviceClient.transport instanceof WebHidTransport)) return;
        const transport = deviceClient.transport;
        return registerDeviceArrivalReconnect(
            handler => transport.onAvailable(handler),
            () => !dependencies.pageHiddenRef.current && (
                deviceClient.getState() === DeviceTransportState.DISCONNECTED ||
                deviceClient.getState() === DeviceTransportState.ERROR
            ),
            dependencies.reconnectDevice,
            error => {
                console.error('设备重新出现后自动重连失败:', error);
                dependencies.setShowReconnect(true);
            },
            dependencies.transportConfig.closeTimeoutMs,
        );
    }, [deviceClient, dependencies.firmwareUpdating, dependencies.reconnectDevice, dependencies.transportConfig.closeTimeoutMs]);

    useEffect(() => {
        if (!deviceClient || !(deviceClient.transport instanceof WebHidTransport)) return;
        let sampleId = 0;
        return createDeviceConnectionWatchdog({
            enabled: () => !dependencies.pageHiddenRef.current && !dependencies.connectionWatchdogPausedRef.current &&
                dependencies.configReadyRef.current && !dependencies.boundaryCountRef.current && !dependencies.longDeviceActivityRef.current &&
                deviceClient.getPhase() === DeviceConnectionPhase.READY &&
                deviceClient.getState() === DeviceTransportState.CONNECTED,
            generation: () => dependencies.initializationGenerationRef.current,
            probe: () => deviceClient.request('performance.clock-sync', { sampleId: ++sampleId }, { timeoutMs: 2000 }),
            unavailable: () => deviceClient.disconnect(),
        });
    }, [deviceClient]);

}
