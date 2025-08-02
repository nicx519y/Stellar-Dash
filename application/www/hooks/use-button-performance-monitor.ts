import { useEffect, useRef } from 'react';
import { eventBus, EVENTS } from '@/lib/event-manager';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { createWebSocketFramework } from '@/components/websocket-framework';

// ADC按键测试事件数据结构
export interface ButtonPerformanceData {
    buttonIndex: number;
    virtualPin: number;
    triggerValue: number;
    triggerDistance: number;
    pressStartValue: number;
    releaseStartValue: number;
    pressStartValueDistance: number;
    releaseStartValueDistance: number;
    isPressed: boolean;
}

export interface ButtonPerformanceMonitoringData {
    command: number;
    isActive: boolean;
    buttonCount: number;
    timestamp: number;
    buttonData: ButtonPerformanceData[];
}

export interface UseButtonPerformanceMonitorOptions {
    /** 是否自动初始化监控 */
    autoInitialize?: boolean;
    /** 错误回调 */
    onError?: (error: Error) => void;
    /** 状态变化回调 */
    onMonitoringStateChange?: (isActive: boolean) => void;
    /** 按键性能监控数据回调 */
    onButtonPerformanceData?: (data: ButtonPerformanceMonitoringData) => void;
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
            
            // 开始监听WebSocket推送消息
            if (!unsubscribeRef.current) {
                            if (useEventBusOption) {
                // 使用 eventBus 监听（推荐方式）
                unsubscribeRef.current = eventBus.on(EVENTS.BUTTON_PERFORMANCE_MONITORING, handleButtonPerformanceEvent);
            } else {
                // 注意：由于 webSocketService 已简化，这里只支持 eventBus 方式
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
            
            // 停止监听WebSocket推送消息
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

    // 处理WebSocket推送的按键性能监控事件
    const handleButtonPerformanceEvent = (data: unknown) => {
        try {
            console.log('Received button performance monitoring event:', data);
            
            // 检查数据类型
            if (!(data instanceof ArrayBuffer)) {
                console.warn('Received non-ArrayBuffer data for button performance monitoring event');
                return;
            }
            
            // 解析二进制数据
            const view = new DataView(data);
            const command = view.getUint8(0);
            
            if (command === 2) { // 按键性能监控命令号
                const isActive = view.getUint8(1);
                const buttonCount = view.getUint8(2);
                const reserved = view.getUint8(3); // 添加reserved字段解析
                const timestamp = view.getUint32(4, true); // little-endian
                
                const buttonData: ButtonPerformanceData[] = [];
                // 解析每个按键数据
                let offset = 8; // 跳过头部 (1+1+1+1+4 = 8字节)
                for (let i = 0; i < buttonCount; i++) {
                    const event: ButtonPerformanceData = {
                        buttonIndex: view.getUint8(offset),
                        virtualPin: view.getUint8(offset + 1),
                        triggerValue: view.getUint16(offset + 2, true),
                        triggerDistance: view.getFloat32(offset + 4, true),
                        pressStartValue: view.getUint16(offset + 8, true),
                        releaseStartValue: view.getUint16(offset + 10, true),
                        pressStartValueDistance: view.getFloat32(offset + 12, true),
                        releaseStartValueDistance: view.getFloat32(offset + 16, true),
                        isPressed: view.getUint8(offset + 20) === 1,
                    };
                    buttonData.push(event);
                    offset += 22; // ButtonPerformanceData结构体大小：1+1+2+4+2+2+4+4+1+1 = 22字节
                }
                
                const performanceData: ButtonPerformanceMonitoringData = {
                    command,
                    isActive: isActive === 1,
                    buttonCount,
                    timestamp,
                    buttonData,
                };
                
                onButtonPerformanceData?.(performanceData);
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

// 保留旧的接口以兼容现有代码
export function useGlobalButtonPerformanceMonitorManager() {
    const monitor = useButtonPerformanceMonitor();
    
    return {
        initializeManager: () => monitor,
        destroyManager: () => monitor.stopMonitoring(),
        getManager: () => monitor,
    };
} 