import { useCallback, useEffect, useRef } from 'react';
import { eventBus, EVENTS } from '@/lib/event-manager';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import {
    ButtonMonitorLifecycle,
    type SharedButtonMonitorLeaseToken,
} from '@/lib/button-monitor-lifecycle';

export interface UseButtonMonitorOptions {
    /** 是否自动初始化监控 */
    autoInitialize?: boolean;
    /** 错误回调 */
    onError?: (error: Error) => void;
    /** 状态变化回调 */
    onMonitoringStateChange?: (isActive: boolean) => void;
    /** 按键状态变化回调 */
    onButtonStatesChange?: (states: any) => void;
    /** 使用 eventBus 而不是直接监听（推荐） */
    useEventBus?: boolean;
    /** 是否控制 start/stop_button_monitoring 命令 */
    controlMonitoringCommand?: boolean;
}

export function useButtonMonitor(options: UseButtonMonitorOptions = {}) {
    const {
        autoInitialize = true,
        onError,
        onMonitoringStateChange,
        onButtonStatesChange,
        useEventBus: useEventBusOption = true, // 默认使用 eventBus
        controlMonitoringCommand = true,
    } = options;

    // 使用 gamepad-config-context 中的方法
    const { startButtonMonitoring, stopButtonMonitoring } = useGamepadConfig();

    const unsubscribeRef = useRef<(() => void) | null>(null);
    const deviceLeaseRef = useRef<SharedButtonMonitorLeaseToken | null>(null);
    const commandsRef = useRef({ startButtonMonitoring, stopButtonMonitoring });
    const optionsRef = useRef({
        controlMonitoringCommand,
        onError,
        onMonitoringStateChange,
        onButtonStatesChange,
        useEventBusOption,
    });
    commandsRef.current = { startButtonMonitoring, stopButtonMonitoring };
    optionsRef.current = {
        controlMonitoringCommand,
        onError,
        onMonitoringStateChange,
        onButtonStatesChange,
        useEventBusOption,
    };

    const handleButtonStateUpdate = (data: any) => {
        try {
            console.log('Received button state update:', data);
            const callback = optionsRef.current.onButtonStatesChange;

            if (data && data.buttonStates) {
                callback?.(data.buttonStates);
            } else if (data) {
                callback?.(data);
            }
        } catch (error) {
            console.error('Failed to handle button state update:', error);
            optionsRef.current.onError?.(
                error instanceof Error
                    ? error
                    : new Error('Failed to handle button state update'),
            );
        }
    };

    const lifecycleRef = useRef<ButtonMonitorLifecycle | null>(null);
    if (!lifecycleRef.current) {
        lifecycleRef.current = new ButtonMonitorLifecycle({
            startDevice: async () => {
                const controlsDevice = optionsRef.current.controlMonitoringCommand;
                if (!controlsDevice) return;

                deviceLeaseRef.current = await commandsRef.current.startButtonMonitoring();
            },
            stopDevice: async () => {
                const lease = deviceLeaseRef.current;
                deviceLeaseRef.current = null;
                if (lease) {
                    await commandsRef.current.stopButtonMonitoring(lease);
                }
            },
            onAcquired: () => {
                if (!unsubscribeRef.current) {
                    // Device events now share one event bus. Keep the legacy
                    // option for API compatibility without creating a second
                    // subscription path.
                    unsubscribeRef.current = eventBus.on(
                        EVENTS.BUTTON_STATE_CHANGED,
                        handleButtonStateUpdate,
                    );
                }
                optionsRef.current.onMonitoringStateChange?.(true);
                console.log('Button monitoring started successfully');
            },
            onReleased: () => {
                unsubscribeRef.current?.();
                unsubscribeRef.current = null;
                optionsRef.current.onMonitoringStateChange?.(false);
                console.log('Button monitoring stopped successfully');
            },
        });
    }
    const lifecycle = lifecycleRef.current;

    const reportOperationError = useCallback((operation: 'start' | 'stop', error: unknown): Error => {
        const normalized = error instanceof Error ? error : new Error('Unknown error');
        optionsRef.current.onError?.(normalized);
        console.error(`Failed to ${operation} button monitoring:`, normalized);
        return normalized;
    }, []);

    // 启动按键监控
    const startMonitoring = useCallback(async (): Promise<void> => {
        try {
            await lifecycle.start();
        } catch (error) {
            throw reportOperationError('start', error);
        }
    }, [lifecycle, reportOperationError]);

    // 停止按键监控
    const stopMonitoring = useCallback(async (): Promise<void> => {
        try {
            await lifecycle.stop();
        } catch (error) {
            throw reportOperationError('stop', error);
        }
    }, [lifecycle, reportOperationError]);

    // 获取当前状态
    const getMonitoringState = useCallback(() => {
        return {
            isMonitoring: lifecycle.ownsMonitor,
        };
    }, [lifecycle]);

    // autoInitialize is retained for source compatibility. Monitoring still
    // starts only when the owning component's ready-state effect requests it.
    void autoInitialize;

    useEffect(() => {
        lifecycle.activate();
        // 组件卸载时串行释放本 hook 成功获取的设备监控。
        return () => {
            void lifecycle.dispose().catch((error) => {
                console.error('Failed to dispose button monitoring:', error);
            });
        };
    }, [lifecycle]);

    return {
        startMonitoring,
        stopMonitoring,
        getMonitoringState,
        isActive: lifecycle.ownsMonitor,
    };
}

// 保留旧的接口以兼容现有代码
export function useGlobalButtonMonitorManager() {
    const monitor = useButtonMonitor();
    
    return {
        initializeManager: () => monitor,
        destroyManager: () => monitor.stopMonitoring(),
        getManager: () => monitor,
    };
} 
