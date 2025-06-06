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

// 按键监控管理器配置
export interface ButtonMonitorConfig {
    /** 轮询间隔，默认500ms */
    pollingInterval?: number;
    /** 错误回调 */
    onError?: (error: Error) => void;
    /** 状态变化回调 */
    onMonitoringStateChange?: (isActive: boolean) => void;
    /** 按键状态变化回调 */
    onButtonStatesChange?: (states: ButtonStates) => void;
}

// 按键监控管理器类
export class ButtonMonitorManager {
    private pollingInterval: number;
    private onError?: (error: Error) => void;
    private onMonitoringStateChange?: (isActive: boolean) => void;
    private onButtonStatesChange?: (states: ButtonStates) => void;

    // 状态
    private isMonitoring: boolean = false;
    private isPolling: boolean = false;
    private lastButtonStates?: ButtonStates;
    private previousTriggerMask: number = 0;

    // 引用
    private pollingTimer: NodeJS.Timeout | null = null;
    private eventListeners: Set<ButtonEventListener> = new Set();

    // 外部依赖注入
    private startButtonMonitoring: () => Promise<void>;
    private stopButtonMonitoring: () => Promise<void>;
    private getButtonStates: () => Promise<ButtonStates>;

    constructor(
        config: ButtonMonitorConfig,
        dependencies: {
            startButtonMonitoring: () => Promise<void>;
            stopButtonMonitoring: () => Promise<void>;
            getButtonStates: () => Promise<ButtonStates>;
        }
    ) {
        this.pollingInterval = config.pollingInterval || 500;
        this.onError = config.onError;
        this.onMonitoringStateChange = config.onMonitoringStateChange;
        this.onButtonStatesChange = config.onButtonStatesChange;

        this.startButtonMonitoring = dependencies.startButtonMonitoring;
        this.stopButtonMonitoring = dependencies.stopButtonMonitoring;
        this.getButtonStates = dependencies.getButtonStates;
    }

    // 获取当前状态
    public getState() {
        return {
            isMonitoring: this.isMonitoring,
            isPolling: this.isPolling,
            lastButtonStates: this.lastButtonStates,
        };
    }

    // 添加事件监听器
    public addEventListener(listener: ButtonEventListener): void {
        this.eventListeners.add(listener);
    }

    // 移除事件监听器
    public removeEventListener(listener: ButtonEventListener): void {
        this.eventListeners.delete(listener);
    }

    // 发布事件
    private emitButtonEvent(event: ButtonEvent): void {
        // 发布给内部监听器
        this.eventListeners.forEach(listener => {
            try {
                listener(event);
            } catch (error) {
                console.error('Button event listener error:', error);
            }
        });

        // 发布全局自定义事件
        const customEvent = new CustomEvent('device-button-event', {
            detail: event
        });
        window.dispatchEvent(customEvent);
    }

    // 分析按键变化并发布事件
    private analyzeButtonChanges(currentStates: ButtonStates): void {
        const currentMask = currentStates.triggerMask;
        const previousMask = this.previousTriggerMask;

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
                
                this.emitButtonEvent(event);
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
            
            this.emitButtonEvent(changeEvent);
        }

        this.previousTriggerMask = currentMask;
    }

    // 轮询函数
    private async pollButtonStates(): Promise<void> {
        if (!this.isMonitoring) {
            return;
        }

        try {
            const states = await this.getButtonStates();
            
            // 只在按键状态真正发生变化时才更新 lastButtonStates
            if (states.triggerMask !== this.previousTriggerMask) {
                this.lastButtonStates = states;
            }
            
            this.analyzeButtonChanges(states);
            this.onButtonStatesChange?.(states);
        } catch (error) {
            const errorObj = error instanceof Error ? error : new Error(String(error));
            console.error('Failed to poll button states:', errorObj);
            this.onError?.(errorObj);
        }
    }

    // 开始轮询
    private startPolling(): void {
        if (this.pollingTimer || !this.isMonitoring) {
            return;
        }

        this.isPolling = true;
        this.pollingTimer = setInterval(() => {
            this.pollButtonStates();
        }, this.pollingInterval);
    }

    // 停止轮询
    private stopPolling(): void {
        if (this.pollingTimer) {
            clearInterval(this.pollingTimer);
            this.pollingTimer = null;
        }
        this.isPolling = false;
    }

    // 开始监控
    public async startMonitoring(): Promise<void> {
        if (this.isMonitoring) {
            return;
        }

        try {
            await this.startButtonMonitoring();
            this.isMonitoring = true;
            this.startPolling();
            this.onMonitoringStateChange?.(true);
        } catch (error) {
            this.isMonitoring = false;
            const errorObj = error instanceof Error ? error : new Error(String(error));
            console.error('Failed to start button monitoring:', errorObj);
            this.onError?.(errorObj);
            throw errorObj;
        }
    }

    // 停止监控
    public async stopMonitoring(): Promise<void> {
        if (!this.isMonitoring) {
            return;
        }

        try {
            this.isMonitoring = false;
            this.stopPolling();
            await this.stopButtonMonitoring();
            this.previousTriggerMask = 0;
            this.lastButtonStates = undefined;
            this.onMonitoringStateChange?.(false);
        } catch (error) {
            const errorObj = error instanceof Error ? error : new Error(String(error));
            console.error('Failed to stop button monitoring:', errorObj);
            this.onError?.(errorObj);
            throw errorObj;
        }
    }

    // 销毁管理器
    public destroy(): void {
        this.stopPolling();
        this.eventListeners.clear();
        
        // 如果正在监控，尝试停止（不等待结果）
        if (this.isMonitoring) {
            this.stopButtonMonitoring().catch(console.error);
        }
    }
}

// 全局单例实例
let globalButtonMonitorManager: ButtonMonitorManager | null = null;

// 创建全局管理器实例
export function createGlobalButtonMonitorManager(
    config: ButtonMonitorConfig,
    dependencies: {
        startButtonMonitoring: () => Promise<void>;
        stopButtonMonitoring: () => Promise<void>;
        getButtonStates: () => Promise<ButtonStates>;
    }
): ButtonMonitorManager {
    // 如果已存在实例，先销毁
    if (globalButtonMonitorManager) {
        globalButtonMonitorManager.destroy();
    }
    
    globalButtonMonitorManager = new ButtonMonitorManager(config, dependencies);
    return globalButtonMonitorManager;
}

// 获取全局管理器实例
export function getGlobalButtonMonitorManager(): ButtonMonitorManager | null {
    return globalButtonMonitorManager;
}

// 销毁全局管理器实例
export function destroyGlobalButtonMonitorManager(): void {
    if (globalButtonMonitorManager) {
        globalButtonMonitorManager.destroy();
        globalButtonMonitorManager = null;
    }
} 