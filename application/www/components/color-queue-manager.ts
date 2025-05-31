/**
 * 颜色队列管理器
 * 用于管理用户最近使用的颜色，支持 localStorage 持久化存储
 */
export class ColorQueueManager {
    private static readonly COLOR_QUEUE_KEY = 'led-color-swatches';
    private static readonly MAX_COLOR_QUEUE_SIZE = 10;
    
    // 初始颜色队列：红橙黄绿青蓝紫黑白灰
    private static readonly DEFAULT_COLOR_QUEUE = [
        "#ff0000", // 红
        "#ff8000", // 橙
        "#ffff00", // 黄
        "#00ff00", // 绿
        "#00ffff", // 青
        "#0000ff", // 蓝
        "#8000ff", // 紫
        "#000000", // 黑
        "#ffffff", // 白
        "#808080", // 灰
    ];

    /**
     * 检查是否在浏览器环境中
     */
    private static isClient(): boolean {
        return typeof window !== 'undefined';
    }

    /**
     * 从 localStorage 获取颜色队列
     */
    public static getColorQueue(): string[] {
        if (!this.isClient()) {
            return [...this.DEFAULT_COLOR_QUEUE];
        }
        
        try {
            const stored = localStorage.getItem(this.COLOR_QUEUE_KEY);
            if (stored) {
                const parsed = JSON.parse(stored);
                if (Array.isArray(parsed) && parsed.length > 0) {
                    return parsed;
                }
            }
        } catch (error) {
            console.warn('Failed to parse color queue from localStorage:', error);
        }
        
        return [...this.DEFAULT_COLOR_QUEUE];
    }

    /**
     * 保存颜色队列到 localStorage
     */
    private static saveColorQueue(colors: string[]): void {
        if (!this.isClient()) {
            return;
        }
        
        try {
            localStorage.setItem(this.COLOR_QUEUE_KEY, JSON.stringify(colors));
        } catch (error) {
            console.warn('Failed to save color queue to localStorage:', error);
        }
    }

    /**
     * 更新颜色队列（先进先出）
     * @param newColor 新选择的颜色
     * @returns 更新后的颜色队列
     */
    public static updateColorQueue(newColor: string): string[] {
        const currentQueue = this.getColorQueue();
        
        // 如果新颜色和队列第一个颜色相同，不做任何操作
        if (currentQueue[0] === newColor) {
            return currentQueue;
        }
        
        // 移除队列中已存在的相同颜色
        const filteredQueue = currentQueue.filter(color => color !== newColor);
        
        // 将新颜色添加到队列头部
        const newQueue = [newColor, ...filteredQueue];
        
        // 限制队列长度
        const trimmedQueue = newQueue.slice(0, this.MAX_COLOR_QUEUE_SIZE);
        
        // 保存到 localStorage
        this.saveColorQueue(trimmedQueue);
        
        return trimmedQueue;
    }

    /**
     * 重置颜色队列为默认值
     */
    public static resetColorQueue(): string[] {
        const defaultQueue = [...this.DEFAULT_COLOR_QUEUE];
        this.saveColorQueue(defaultQueue);
        return defaultQueue;
    }

    /**
     * 清空颜色队列
     */
    public static clearColorQueue(): string[] {
        const emptyQueue: string[] = [];
        this.saveColorQueue(emptyQueue);
        return emptyQueue;
    }

    /**
     * 获取队列最大长度
     */
    public static getMaxQueueSize(): number {
        return this.MAX_COLOR_QUEUE_SIZE;
    }

    /**
     * 获取默认颜色队列
     */
    public static getDefaultColorQueue(): string[] {
        return [...this.DEFAULT_COLOR_QUEUE];
    }

    /**
     * 检查颜色是否在队列中
     */
    public static isColorInQueue(color: string): boolean {
        const queue = this.getColorQueue();
        return queue.includes(color);
    }

    /**
     * 移除队列中的特定颜色
     */
    public static removeColorFromQueue(colorToRemove: string): string[] {
        const currentQueue = this.getColorQueue();
        const filteredQueue = currentQueue.filter(color => color !== colorToRemove);
        this.saveColorQueue(filteredQueue);
        return filteredQueue;
    }
} 
 