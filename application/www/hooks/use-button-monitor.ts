import { useEffect, useRef } from 'react';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { 
    ButtonMonitorManager, 
    ButtonMonitorConfig,
    createGlobalButtonMonitorManager,
    getGlobalButtonMonitorManager,
    destroyGlobalButtonMonitorManager
} from '@/components/button-monitor-manager';

export interface UseButtonMonitorOptions extends Omit<ButtonMonitorConfig, 'onError' | 'onMonitoringStateChange' | 'onButtonStatesChange'> {
    /** 是否自动初始化全局管理器 */
    autoInitialize?: boolean;
    /** 错误回调 */
    onError?: (error: Error) => void;
    /** 状态变化回调 */
    onMonitoringStateChange?: (isActive: boolean) => void;
    /** 按键状态变化回调 */
    onButtonStatesChange?: (states: any) => void;
}

export function useButtonMonitor(options: UseButtonMonitorOptions = {}) {
    const {
        autoInitialize = true,
        pollingInterval = 500,
        onError,
        onMonitoringStateChange,
        onButtonStatesChange,
    } = options;

    const { 
        startButtonMonitoring, 
        stopButtonMonitoring, 
        getButtonStates 
    } = useGamepadConfig();

    const managerRef = useRef<ButtonMonitorManager | null>(null);

    // 初始化全局管理器
    useEffect(() => {
        if (!autoInitialize) return;

        // 检查是否已有全局实例
        let manager = getGlobalButtonMonitorManager();
        
        if (!manager) {
            // 创建新的全局实例
            manager = createGlobalButtonMonitorManager(
                {
                    pollingInterval,
                    onError,
                    onMonitoringStateChange,
                    onButtonStatesChange,
                },
                {
                    startButtonMonitoring,
                    stopButtonMonitoring,
                    getButtonStates,
                }
            );
        }

        managerRef.current = manager;

        // 组件卸载时不销毁全局实例，让其他组件继续使用
        return () => {
            managerRef.current = null;
        };
    }, [
        autoInitialize,
        pollingInterval,
        onError,
        onMonitoringStateChange,
        onButtonStatesChange,
        startButtonMonitoring,
        stopButtonMonitoring,
        getButtonStates,
    ]);

    // 获取管理器实例
    const getManager = (): ButtonMonitorManager | null => {
        return managerRef.current || getGlobalButtonMonitorManager();
    };

    // 开始监控
    const startMonitoring = async (): Promise<void> => {
        const manager = getManager();
        if (manager) {
            await manager.startMonitoring();
        }
    };

    // 停止监控
    const stopMonitoring = async (): Promise<void> => {
        const manager = getManager();
        if (manager) {
            await manager.stopMonitoring();
        }
    };

    // 获取状态
    const getState = () => {
        const manager = getManager();
        return manager ? manager.getState() : {
            isMonitoring: false,
            isPolling: false,
            lastButtonStates: undefined,
        };
    };

    // 添加事件监听器
    const addEventListener = (listener: any): void => {
        const manager = getManager();
        if (manager) {
            manager.addEventListener(listener);
        }
    };

    // 移除事件监听器
    const removeEventListener = (listener: any): void => {
        const manager = getManager();
        if (manager) {
            manager.removeEventListener(listener);
        }
    };

    return {
        startMonitoring,
        stopMonitoring,
        getState,
        addEventListener,
        removeEventListener,
        manager: getManager(),
    };
}

// 用于应用级别的管理器初始化和清理
export function useGlobalButtonMonitorManager() {
    const { 
        startButtonMonitoring, 
        stopButtonMonitoring, 
        getButtonStates 
    } = useGamepadConfig();

    // 初始化全局管理器
    const initializeManager = (config: ButtonMonitorConfig = {}) => {
        return createGlobalButtonMonitorManager(
            config,
            {
                startButtonMonitoring,
                stopButtonMonitoring,
                getButtonStates,
            }
        );
    };

    // 销毁全局管理器
    const destroyManager = () => {
        destroyGlobalButtonMonitorManager();
    };

    // 获取全局管理器
    const getManager = () => {
        return getGlobalButtonMonitorManager();
    };

    return {
        initializeManager,
        destroyManager,
        getManager,
    };
} 