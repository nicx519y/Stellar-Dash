import { useEffect, useRef } from 'react';
import { eventBus, EVENTS } from '@/lib/event-manager';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { parseButtonPerformanceMonitoringBinaryData, type ButtonPerformanceMonitoringBinaryData, type ButtonPerformanceData } from '@/lib/button-performance-binary-parser';

// 重新导出数据结构
export type { ButtonPerformanceData, ButtonPerformanceMonitoringBinaryData as ButtonPerformanceMonitoringData };

export interface UseButtonPerformanceMonitorOptions {
    /** 是否自动初始化监控 */
    autoInitialize?: boolean;
    /** 错误回调 */
    onError?: (error: Error) => void;
    /** 状态变化回调 */
    onMonitoringStateChange?: (isActive: boolean) => void;
    /** 按键性能监控数据回调 */
    onButtonPerformanceData?: (data: ButtonPerformanceMonitoringBinaryData) => void;
    /** 使用 eventBus 而不是直接监听（推荐） */
    useEventBus?: boolean;
}

export function useButtonPerformanceMonitor(options: UseButtonPerformanceMonitorOptions = {}) {
    const {
        autoInitialize = true,
        onError,
        onMonitoringStateChange,
        onButtonPerformanceData,
        useEventBus: useEventBusOption = true, // 默认使用 eventBus
    } = options;

    // 使用 gamepad-config-context 中的方法
    const { startButtonPerformanceMonitoring, stopButtonPerformanceMonitoring } = useGamepadConfig();

    const isActiveRef = useRef<boolean>(false);
    const unsubscribeRef = useRef<(() => void) | null>(null);

    // 启动按键性能监控
    const startMonitoring = async (): Promise<void> => {
        try {
            // 使用专门的性能监控命令，自动启用测试模式
            await startButtonPerformanceMonitoring();
            
            isActiveRef.current = true;
            
            // 开始监听设备事件推送
            if (!unsubscribeRef.current) {
                            if (useEventBusOption) {
                // 使用 eventBus 监听（推荐方式）
                unsubscribeRef.current = eventBus.on(EVENTS.BUTTON_PERFORMANCE_MONITORING, handleButtonPerformanceEvent);
            } else {
                // 注意：由于 device event service 使用统一事件总线，这里只支持 eventBus 方式
                unsubscribeRef.current = eventBus.on(EVENTS.BUTTON_PERFORMANCE_MONITORING, handleButtonPerformanceEvent);
            }
            }
            
            onMonitoringStateChange?.(true);
            console.log('Button performance monitoring started successfully');
        } catch (error) {
            const errorMsg = error instanceof Error ? error : new Error('Unknown error');
            onError?.(errorMsg);
            console.error('Failed to start button performance monitoring:', errorMsg);
        }
    };

    // 停止按键性能监控
    const stopMonitoring = async (): Promise<void> => {
        try {
            // 使用专门的性能监控命令，自动禁用测试模式
            await stopButtonPerformanceMonitoring();
            
            isActiveRef.current = false;
            
            // 停止监听设备事件推送
            if (unsubscribeRef.current) {
                unsubscribeRef.current();
                unsubscribeRef.current = null;
            }
            
            onMonitoringStateChange?.(false);
            console.log('Button performance monitoring stopped successfully');
        } catch (error) {
            const errorMsg = error instanceof Error ? error : new Error('Unknown error');
            onError?.(errorMsg);
            console.error('Failed to stop button performance monitoring:', errorMsg);
        }
    };

    // 处理设备推送的按键性能监控事件
    const handleButtonPerformanceEvent = (data: unknown) => {
        try {
            
            // WebHID typed telemetry emits the same
            // semantic snapshot after merging SAMPLE/EDGE/CHECKPOINT packets.
            const performanceData = data instanceof ArrayBuffer
                ? parseButtonPerformanceMonitoringBinaryData(data)
                : isPerformanceSnapshot(data)
                    ? data
                    : null;
            
            if (performanceData) {
                onButtonPerformanceData?.(performanceData);
            } else {
                console.warn('Failed to parse button performance monitoring binary data');
            }
        } catch (error) {
            console.error('Failed to handle button performance monitoring event:', error);
            onError?.(error instanceof Error ? error : new Error('Failed to handle button performance monitoring event'));
        }
    };

    // 获取当前状态
    const getMonitoringState = () => {
        return {
            isMonitoring: isActiveRef.current,
        };
    };

    // 自动初始化
    useEffect(() => {
        if (autoInitialize) {
            // 组件挂载时不自动启动，由用户手动启动
        }

        // 组件卸载时清理
        return () => {
            if (unsubscribeRef.current) {
                unsubscribeRef.current();
                unsubscribeRef.current = null;
            }
        };
    }, [autoInitialize]);

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

// 保留旧的接口以兼容现有代码
export function useGlobalButtonPerformanceMonitorManager() {
    const monitor = useButtonPerformanceMonitor();
    
    return {
        initializeManager: () => monitor,
        destroyManager: () => monitor.stopMonitoring(),
        getManager: () => monitor,
    };
}
