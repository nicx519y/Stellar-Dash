import { useEffect, useRef } from 'react';
import { eventBus, EVENTS } from '@/lib/event-manager';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';

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
}

export function useButtonMonitor(options: UseButtonMonitorOptions = {}) {
    const {
        autoInitialize = true,
        onError,
        onMonitoringStateChange,
        onButtonStatesChange,
        useEventBus: useEventBusOption = true, // 默认使用 eventBus
    } = options;

    // 使用 gamepad-config-context 中的方法
    const { startButtonMonitoring, stopButtonMonitoring } = useGamepadConfig();

    const isActiveRef = useRef<boolean>(false);
    const unsubscribeRef = useRef<(() => void) | null>(null);

    // 启动按键监控
    const startMonitoring = async (): Promise<void> => {
        try {
            // 使用 gamepad-config-context 中的方法
            await startButtonMonitoring();
            
            isActiveRef.current = true;
            
            // 开始监听WebSocket推送消息
            if (!unsubscribeRef.current) {
                if (useEventBusOption) {
                    // 使用 eventBus 监听（推荐方式）
                    unsubscribeRef.current = eventBus.on(EVENTS.BUTTON_STATE_CHANGED, handleButtonStateUpdate);
                } else {
                    // 注意：由于 webSocketService 已简化，这里只支持 eventBus 方式
                    unsubscribeRef.current = eventBus.on(EVENTS.BUTTON_STATE_CHANGED, handleButtonStateUpdate);
                }
            }
            
            onMonitoringStateChange?.(true);
            console.log('Button monitoring started successfully');
        } catch (error) {
            const errorMsg = error instanceof Error ? error : new Error('Unknown error');
            onError?.(errorMsg);
            console.error('Failed to start button monitoring:', errorMsg);
        }
    };

    // 停止按键监控
    const stopMonitoring = async (): Promise<void> => {
        try {
            // 使用 gamepad-config-context 中的方法
            await stopButtonMonitoring();
            
            isActiveRef.current = false;
            
            // 停止监听WebSocket推送消息
            if (unsubscribeRef.current) {
                unsubscribeRef.current();
                unsubscribeRef.current = null;
            }
            
            onMonitoringStateChange?.(false);
            console.log('Button monitoring stopped successfully');
        } catch (error) {
            const errorMsg = error instanceof Error ? error : new Error('Unknown error');
            onError?.(errorMsg);
            console.error('Failed to stop button monitoring:', errorMsg);
        }
    };

    // 处理WebSocket推送的按键状态更新
    const handleButtonStateUpdate = (data: any) => {
        try {
            console.log('Received button state update:', data);
            
            // 解析推送数据
            if (data && data.buttonStates) {
                onButtonStatesChange?.(data.buttonStates);
            } else if (data) {
                // 如果直接收到按键状态数据
                onButtonStatesChange?.(data);
            }
        } catch (error) {
            console.error('Failed to handle button state update:', error);
            onError?.(error instanceof Error ? error : new Error('Failed to handle button state update'));
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

// 保留旧的接口以兼容现有代码
export function useGlobalButtonMonitorManager() {
    const monitor = useButtonMonitor();
    
    return {
        initializeManager: () => monitor,
        destroyManager: () => monitor.stopMonitoring(),
        getManager: () => monitor,
    };
} 