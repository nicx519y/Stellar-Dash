import { useCallback, useEffect, useRef } from 'react';
import { eventBus, EVENTS } from '@/lib/event-manager';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import {
    parseButtonPerformanceMonitoringBinaryData,
    type ButtonPerformanceMonitoringBinaryData,
    type ButtonPerformanceData,
} from '@/lib/button-performance-binary-parser';

export type {
    ButtonPerformanceData,
    ButtonPerformanceMonitoringBinaryData as ButtonPerformanceMonitoringData,
};

export interface UseButtonPerformanceMonitorOptions {
    autoInitialize?: boolean;
    onError?: (error: Error) => void;
    onMonitoringStateChange?: (isActive: boolean) => void;
    onButtonPerformanceData?: (data: ButtonPerformanceMonitoringBinaryData) => void;
    /** 保留用于兼容；设备事件统一通过 eventBus 分发。 */
    useEventBus?: boolean;
}

export function useButtonPerformanceMonitor(options: UseButtonPerformanceMonitorOptions = {}) {
    const {
        autoInitialize = true,
        onError,
        onMonitoringStateChange,
        onButtonPerformanceData,
    } = options;
    const {
        startButtonPerformanceMonitoring,
        stopButtonPerformanceMonitoring,
    } = useGamepadConfig();

    const callbacksRef = useRef({
        onError,
        onMonitoringStateChange,
        onButtonPerformanceData,
    });
    callbacksRef.current = {
        onError,
        onMonitoringStateChange,
        onButtonPerformanceData,
    };
    const commandsRef = useRef({
        startButtonPerformanceMonitoring,
        stopButtonPerformanceMonitoring,
    });
    commandsRef.current = {
        startButtonPerformanceMonitoring,
        stopButtonPerformanceMonitoring,
    };

    const isActiveRef = useRef(false);
    const mountedRef = useRef(true);
    const unsubscribeRef = useRef<(() => void) | null>(null);
    const operationRef = useRef<Promise<void> | null>(null);

    const handleButtonPerformanceEvent = useCallback((data: unknown) => {
        try {
            const performanceData = data instanceof ArrayBuffer
                ? parseButtonPerformanceMonitoringBinaryData(data)
                : isPerformanceSnapshot(data)
                    ? data
                    : null;

            if (!performanceData) {
                console.warn('Failed to parse button performance monitoring data');
                return;
            }
            callbacksRef.current.onButtonPerformanceData?.(performanceData);
        } catch (error) {
            const normalized = error instanceof Error
                ? error
                : new Error('Failed to handle button performance monitoring event');
            console.error('Failed to handle button performance monitoring event:', normalized);
            callbacksRef.current.onError?.(normalized);
        }
    }, []);

    const subscribe = useCallback(() => {
        if (!unsubscribeRef.current) {
            unsubscribeRef.current = eventBus.on(
                EVENTS.BUTTON_PERFORMANCE_MONITORING,
                handleButtonPerformanceEvent,
            );
        }
    }, [handleButtonPerformanceEvent]);

    const unsubscribe = useCallback(() => {
        unsubscribeRef.current?.();
        unsubscribeRef.current = null;
    }, []);

    const startMonitoring = useCallback(async (): Promise<void> => {
        if (isActiveRef.current) return;
        if (operationRef.current) await operationRef.current;
        if (isActiveRef.current) return;

        // The first checkpoint/sample may arrive immediately after the ACK.
        subscribe();
        const operation = (async () => {
            try {
                await commandsRef.current.startButtonPerformanceMonitoring();
                if (!mountedRef.current) {
                    await commandsRef.current.stopButtonPerformanceMonitoring();
                    unsubscribe();
                    return;
                }
                isActiveRef.current = true;
                callbacksRef.current.onMonitoringStateChange?.(true);
                console.log('Button performance monitoring started successfully');
            } catch (error) {
                isActiveRef.current = false;
                unsubscribe();
                const normalized = error instanceof Error
                    ? error
                    : new Error('Failed to start button performance monitoring');
                callbacksRef.current.onError?.(normalized);
                console.error('Failed to start button performance monitoring:', normalized);
                throw normalized;
            }
        })();
        operationRef.current = operation;
        try {
            await operation;
        } finally {
            if (operationRef.current === operation) operationRef.current = null;
        }
    }, [subscribe, unsubscribe]);

    const stopMonitoring = useCallback(async (): Promise<void> => {
        if (operationRef.current) {
            try {
                await operationRef.current;
            } catch {
                return;
            }
        }
        if (!isActiveRef.current) {
            unsubscribe();
            return;
        }

        // A late packet must not repopulate a test view the user already left.
        isActiveRef.current = false;
        unsubscribe();
        const operation = (async () => {
            try {
                await commandsRef.current.stopButtonPerformanceMonitoring();
                callbacksRef.current.onMonitoringStateChange?.(false);
                console.log('Button performance monitoring stopped successfully');
            } catch (error) {
                const normalized = error instanceof Error
                    ? error
                    : new Error('Failed to stop button performance monitoring');
                callbacksRef.current.onError?.(normalized);
                console.error('Failed to stop button performance monitoring:', normalized);
                throw normalized;
            }
        })();
        operationRef.current = operation;
        try {
            await operation;
        } finally {
            if (operationRef.current === operation) operationRef.current = null;
        }
    }, [unsubscribe]);

    const getMonitoringState = useCallback(() => ({
        isMonitoring: isActiveRef.current,
    }), []);

    useEffect(() => {
        // autoInitialize historically initialized only the hook; user action
        // still controls whether a performance session starts.
        void autoInitialize;
        mountedRef.current = true;
        const removeDisconnectListener = eventBus.on(EVENTS.DEVICE_DISCONNECTED, () => {
            const wasActive = isActiveRef.current;
            isActiveRef.current = false;
            unsubscribe();
            if (wasActive) callbacksRef.current.onMonitoringStateChange?.(false);
        });
        return () => {
            mountedRef.current = false;
            removeDisconnectListener();
            unsubscribe();
            if (isActiveRef.current) {
                isActiveRef.current = false;
                void commandsRef.current.stopButtonPerformanceMonitoring().catch(() => undefined);
            }
        };
    }, [autoInitialize, unsubscribe]);

    return {
        startMonitoring,
        stopMonitoring,
        getMonitoringState,
        isActive: isActiveRef.current,
    };
}

function isPerformanceSnapshot(data: unknown): data is ButtonPerformanceMonitoringBinaryData {
    if (!data || typeof data !== 'object') return false;
    const value = data as Partial<ButtonPerformanceMonitoringBinaryData>;
    return value.command === 2 && Array.isArray(value.buttonData);
}

export function useGlobalButtonPerformanceMonitorManager() {
    const monitor = useButtonPerformanceMonitor();
    return {
        initializeManager: () => monitor,
        destroyManager: () => monitor.stopMonitoring(),
        getManager: () => monitor,
    };
}
