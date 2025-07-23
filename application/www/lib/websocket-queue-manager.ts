/**
 * WebSocket队列管理器
 * 功能：
 * 1. 按command去重，只保留最后一个同command的消息
 * 2. 延迟1秒发送，避免频繁请求
 * 3. 保证只有一个请求在运行，不并行发送
 * 4. 基于CID管理请求和响应
 */

// 通用参数类型
type MessageParams = Record<string, unknown>;

// 通用响应数据类型
type ResponseData = Record<string, unknown> | undefined;

interface QueuedMessage {
    command: string;
    params: MessageParams;
    timestamp: number;
    canSendAt: number; // 可发送时间戳
    resolve: (value: ResponseData) => void;
    reject: (reason: Error) => void;
}

export class WebSocketQueueManager {
    private messageQueue: Map<string, QueuedMessage> = new Map(); // 按command存储消息
    private isProcessing: boolean = false; // 是否正在处理队列
    private processingTimer: NodeJS.Timeout | null = null; // 处理队列的定时器
    
    // 配置参数
    private readonly SEND_DELAY = 1000; // 1秒延迟发送
    
    // WebSocket发送函数，由外部设置 - 直接返回响应数据
    private sendFunction: ((command: string, params: MessageParams) => Promise<ResponseData>) | null = null;

    /**
     * 设置WebSocket发送函数
     */
    setSendFunction(sendFn: (command: string, params: MessageParams) => Promise<ResponseData>) {
        this.sendFunction = sendFn;
    }

    /**
     * 推送消息到队列
     * @param command 命令
     * @param params 参数
     * @param immediate 是否立即发送，忽略延迟
     */
    async enqueue(command: string, params: MessageParams = {}, immediate: boolean = false): Promise<ResponseData> {
        return new Promise((resolve, reject) => {
            const now = Date.now();
            const canSendAt = immediate ? now : now + this.SEND_DELAY;

            // 如果队列中已有同command的消息，先拒绝旧的Promise
            const existingMessage = this.messageQueue.get(command);
            if (existingMessage) {
                existingMessage.reject(new Error(`Request cancelled by newer ${command} request`));
            }

            // 创建新的队列消息
            const queuedMessage: QueuedMessage = {
                command,
                params,
                timestamp: now,
                canSendAt,
                resolve,
                reject
            };

            // 存储到队列（覆盖同command的旧消息）
            this.messageQueue.set(command, queuedMessage);

            console.log(`[WebSocketQueue] Enqueued command: ${command}, canSendAt: ${new Date(canSendAt).toISOString()}, immediate: ${immediate}`);

            // 启动处理循环
            this.startProcessing();
        });
    }

    /**
     * 启动处理循环
     */
    private startProcessing() {
        if (this.isProcessing) {
            return;
        }

        this.isProcessing = true;
        this.processQueue();
    }

    /**
     * 处理队列
     */
    private processQueue() {
        // 查找可发送的消息
        const now = Date.now();
        let nextMessage: QueuedMessage | null = null;
        let nextSendTime = Infinity;

        for (const message of this.messageQueue.values()) {
            if (message.canSendAt <= now) {
                // 可以立即发送，选择最早的
                if (!nextMessage || message.timestamp < nextMessage.timestamp) {
                    nextMessage = message;
                }
            } else {    
                // 记录最早的可发送时间
                nextSendTime = Math.min(nextSendTime, message.canSendAt);
            }
        }

        if (nextMessage) {
            // 发送消息
            this.sendMessage(nextMessage);
        } else if (nextSendTime < Infinity) {
            // 设置定时器，等待下一个消息可发送
            const delay = nextSendTime - now;
            console.log(`[WebSocketQueue] Next message can send in ${delay}ms`);
            
            if (this.processingTimer) {
                clearTimeout(this.processingTimer);
            }
            
            this.processingTimer = setTimeout(() => {
                this.processingTimer = null;
                this.processQueue();
            }, delay);
        } else {
            // 队列为空，停止处理
            console.log(`[WebSocketQueue] Queue is empty, stopping processing`);
            this.isProcessing = false;
        }
    }

    /**
     * 发送消息
     */
    private async sendMessage(message: QueuedMessage) {
        if (!this.sendFunction) {
            message.reject(new Error('WebSocket send function not set'));
            return;
        }

        try {
            console.log(`[WebSocketQueue] Sending command: ${message.command}`);
            
            // 从队列中移除消息
            this.messageQueue.delete(message.command);

            // 直接调用发送函数并等待响应
            const responseData = await this.sendFunction(message.command, message.params);
            
            // 直接resolve响应数据
            message.resolve(responseData);
            
            console.log(`[WebSocketQueue] Command completed: ${message.command}`);

        } catch (error) {
            console.error(`[WebSocketQueue] Failed to send message:`, error);
            message.reject(error instanceof Error ? error : new Error(String(error)));
        } finally {
            // 继续处理队列
            this.processQueue();
        }
    }

    /**
     * 清空队列
     */
    clear() {
        // 拒绝所有等待中的消息
        for (const message of this.messageQueue.values()) {
            message.reject(new Error('Queue cleared'));
        }
        this.messageQueue.clear();

        // 停止处理
        if (this.processingTimer) {
            clearTimeout(this.processingTimer);
            this.processingTimer = null;
        }
        this.isProcessing = false;

        console.log(`[WebSocketQueue] Queue cleared`);
    }

    /**
     * 获取队列状态信息
     */
    getStatus() {
        return {
            queueSize: this.messageQueue.size,
            isProcessing: this.isProcessing,
            queuedCommands: Array.from(this.messageQueue.keys())
        };
    }

    /**
     * 销毁队列管理器
     */
    destroy() {
        this.clear();
        this.sendFunction = null;
    }
} 