"use client";

import { useEffect, useRef, useState, useCallback } from "react";
import { useGamepadConfig } from "@/contexts/gamepad-config-context";
import { ButtonStates } from "@/contexts/gamepad-config-context";

// 按键事件类型定义
export interface ButtonEvent {
    type: 'button-press' | 'button-release' | 'button-change';
    buttonIndex: number;
    isPressed: boolean;
    triggerMask: number;
    triggerBinary: string;
    totalButtons: number;
    timestamp: number;
}

// 事件监听器类型
export type ButtonEventListener = (event: ButtonEvent) => void;

interface ButtonMonitorManagerProps {
    /** 是否自动开始监控（组件挂载时） */
    autoStart?: boolean;
    /** 轮询间隔，默认500ms */
    pollingInterval?: number;
    /** 是否在组件卸载时自动停止监控 */
    autoStopOnUnmount?: boolean;
    /** 子组件，可以通过 children 函数接收监控状态 */
    children?: (props: {
        isMonitoring: boolean;
        isPolling: boolean;
        lastButtonStates?: ButtonStates;
        startMonitoring: () => Promise<void>;
        stopMonitoring: () => Promise<void>;
        addEventListener: (listener: ButtonEventListener) => void;
        removeEventListener: (listener: ButtonEventListener) => void;
    }) => React.ReactNode;
    /** 状态变化回调 */
    onMonitoringStateChange?: (isActive: boolean) => void;
    /** 按键状态变化回调 */
    onButtonStatesChange?: (states: ButtonStates) => void;
    /** 错误回调 */
    onError?: (error: Error) => void;
}

export function ButtonMonitorManager({
    autoStart = false,
    pollingInterval = 500,
    autoStopOnUnmount = true,
    children,
    onMonitoringStateChange,
    onButtonStatesChange,
    onError,
}: ButtonMonitorManagerProps) {
    const { 
        buttonMonitoringActive, 
        startButtonMonitoring, 
        stopButtonMonitoring, 
        getButtonStates 
    } = useGamepadConfig();

    // 本地状态
    const [isPolling, setIsPolling] = useState(false);
    const [lastButtonStates, setLastButtonStates] = useState<ButtonStates>();
    const [previousTriggerMask, setPreviousTriggerMask] = useState<number>(0);
    const [localMonitoringActive, setLocalMonitoringActive] = useState(false); // 新增本地监控状态

    // 引用
    const pollingTimerRef = useRef<NodeJS.Timeout | null>(null);
    const eventListenersRef = useRef<Set<ButtonEventListener>>(new Set());
    const isMountedRef = useRef(true);
    const isManualOperationRef = useRef(false); // 跟踪是否为手动操作

    // 确保组件挂载时 isMountedRef 为 true
    useEffect(() => {
        isMountedRef.current = true;
        console.log('Component mounted, isMountedRef set to true');
    }, []);

    // 事件发布器
    const emitButtonEvent = useCallback((event: ButtonEvent) => {
        eventListenersRef.current.forEach(listener => {
            try {
                listener(event);
            } catch (error) {
                console.error('Button event listener error:', error);
            }
        });
    }, []);

    // 添加事件监听器
    const addEventListener = useCallback((listener: ButtonEventListener) => {
        eventListenersRef.current.add(listener);
    }, []);

    // 移除事件监听器
    const removeEventListener = useCallback((listener: ButtonEventListener) => {
        eventListenersRef.current.delete(listener);
    }, []);

    // 分析按键变化并发布事件
    const analyzeButtonChanges = useCallback((currentStates: ButtonStates) => {
        const currentMask = currentStates.triggerMask;
        const previousMask = previousTriggerMask;

        if (currentMask === previousMask) {
            return; // 没有变化
        }

        // 检查每个按键的状态变化
        for (let i = 0; i < currentStates.totalButtons; i++) {
            const buttonBit = 1 << i;
            const wasPressed = (previousMask & buttonBit) !== 0;
            const isPressed = (currentMask & buttonBit) !== 0;

            if (wasPressed !== isPressed) {
                // 按键状态发生变化
                const event: ButtonEvent = {
                    type: isPressed ? 'button-press' : 'button-release',
                    buttonIndex: i,
                    isPressed,
                    triggerMask: currentMask,
                    triggerBinary: currentStates.triggerBinary,
                    totalButtons: currentStates.totalButtons,
                    timestamp: currentStates.timestamp,
                };
                emitButtonEvent(event);
            }
        }

        // 发布通用变化事件
        if (currentMask !== previousMask) {
            const changeEvent: ButtonEvent = {
                type: 'button-change',
                buttonIndex: -1, // 表示整体变化
                isPressed: currentMask !== 0,
                triggerMask: currentMask,
                triggerBinary: currentStates.triggerBinary,
                totalButtons: currentStates.totalButtons,
                timestamp: currentStates.timestamp,
            };
            emitButtonEvent(changeEvent);
        }

        setPreviousTriggerMask(currentMask);
    }, [previousTriggerMask, emitButtonEvent]);

    // 轮询函数
    const pollButtonStates = useCallback(async () => {
        console.log('pollButtonStates called:', {
            isMounted: isMountedRef.current,
            localMonitoringActive
        });
        
        if (!isMountedRef.current || !localMonitoringActive) {
            console.log('pollButtonStates aborted:', {
                reason: !isMountedRef.current ? 'not mounted' : 'monitoring not active'
            });
            return;
        }

        try {
            console.log('Fetching button states...');
            const states = await getButtonStates();
            
            if (!isMountedRef.current) return;

            console.log('Button states received:', states);
            setLastButtonStates(states);
            analyzeButtonChanges(states);
            onButtonStatesChange?.(states);
        } catch (error) {
            if (!isMountedRef.current) return;
            
            const errorObj = error instanceof Error ? error : new Error(String(error));
            console.error('Failed to poll button states:', errorObj);
            onError?.(errorObj);
        }
    }, [localMonitoringActive, getButtonStates, analyzeButtonChanges, onButtonStatesChange, onError]);

    // 开始轮询
    const startPolling = useCallback(() => {
        console.log('startPolling called:', {
            hasTimer: !!pollingTimerRef.current,
            localMonitoringActive,
            pollingInterval
        });
        
        if (pollingTimerRef.current || !localMonitoringActive) {
            console.log('startPolling aborted:', {
                reason: pollingTimerRef.current ? 'already has timer' : 'monitoring not active'
            });
            return;
        }

        console.log('Starting polling timer...');
        setIsPolling(true);
        pollingTimerRef.current = setInterval(pollButtonStates, pollingInterval);
        console.log('Polling timer started, interval ID:', pollingTimerRef.current);
    }, [localMonitoringActive, pollButtonStates, pollingInterval]);

    // 停止轮询
    const stopPolling = useCallback(() => {
        if (pollingTimerRef.current) {
            clearInterval(pollingTimerRef.current);
            pollingTimerRef.current = null;
        }
        setIsPolling(false);
    }, []);

    // 开始监控
    const handleStartMonitoring = useCallback(async () => {
        try {
            isManualOperationRef.current = true; // 标记为手动操作
            setLocalMonitoringActive(true); // 先设置本地状态
            await startButtonMonitoring();
            onMonitoringStateChange?.(true);
        } catch (error) {
            setLocalMonitoringActive(false); // 出错时重置
            const errorObj = error instanceof Error ? error : new Error(String(error));
            console.error('Failed to start button monitoring:', errorObj);
            onError?.(errorObj);
            throw errorObj;
        } finally {
            isManualOperationRef.current = false; // 重置标记
        }
    }, [startButtonMonitoring, onMonitoringStateChange, onError]);

    // 停止监控
    const handleStopMonitoring = useCallback(async () => {
        try {
            isManualOperationRef.current = true; // 标记为手动操作
            setLocalMonitoringActive(false); // 先设置本地状态
            stopPolling();
            await stopButtonMonitoring();
            setPreviousTriggerMask(0);
            setLastButtonStates(undefined);
            onMonitoringStateChange?.(false);
        } catch (error) {
            const errorObj = error instanceof Error ? error : new Error(String(error));
            console.error('Failed to stop button monitoring:', errorObj);
            onError?.(errorObj);
            throw errorObj;
        } finally {
            isManualOperationRef.current = false; // 重置标记
        }
    }, [stopButtonMonitoring, stopPolling, onMonitoringStateChange, onError]);

    // 监听本地监控状态变化，控制轮询
    useEffect(() => {
        console.log('Local monitoring state changed:', localMonitoringActive);
        if (localMonitoringActive) {
            console.log('Attempting to start polling...');
            startPolling();
        } else {
            console.log('Stopping polling...');
            stopPolling();
        }
    }, [localMonitoringActive, startPolling, stopPolling]);

    // 同步context状态到本地状态，但避免在手动操作期间同步
    useEffect(() => {
        if (!isManualOperationRef.current) {
            setLocalMonitoringActive(buttonMonitoringActive);
        }
    }, [buttonMonitoringActive]);

    // 自动开始监控
    useEffect(() => {
        if (autoStart && !buttonMonitoringActive) {
            handleStartMonitoring().catch(console.error);
        }
    }, [autoStart, buttonMonitoringActive, handleStartMonitoring]);

    // 组件卸载时清理
    useEffect(() => {
        return () => {
            isMountedRef.current = false;
            stopPolling();
            
            // 只有在设置了自动停止且当前正在监控时才调用停止
            if (autoStopOnUnmount && buttonMonitoringActive) {
                // 异步调用，不等待结果
                stopButtonMonitoring().catch(console.error);
            }
        };
    }, []); // 移除依赖项，只在组件卸载时执行一次

    // 如果有子组件，渲染子组件
    if (children) {
        return (
            <>
                {children({
                    isMonitoring: localMonitoringActive,
                    isPolling,
                    lastButtonStates,
                    startMonitoring: handleStartMonitoring,
                    stopMonitoring: handleStopMonitoring,
                    addEventListener,
                    removeEventListener,
                })}
            </>
        );
    }

    // 如果没有子组件，返回 null（作为无UI的服务组件）
    return null;
}

// 导出hook用于更简单的使用
export function useButtonMonitor(options: {
    autoStart?: boolean;
    pollingInterval?: number;
    autoStopOnUnmount?: boolean;
} = {}) {
    const [manager, setManager] = useState<{
        isMonitoring: boolean;
        isPolling: boolean;
        lastButtonStates?: ButtonStates;
        startMonitoring: () => Promise<void>;
        stopMonitoring: () => Promise<void>;
        addEventListener: (listener: ButtonEventListener) => void;
        removeEventListener: (listener: ButtonEventListener) => void;
    }>();

    return {
        manager,
        ButtonMonitorComponent: useCallback(
            ({ children }: { children?: React.ReactNode }) => (
                <ButtonMonitorManager {...options}>
                    {(managerProps) => {
                        setManager(managerProps);
                        return children || null;
                    }}
                </ButtonMonitorManager>
            ),
            [options]
        ),
    };
} 