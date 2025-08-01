/**
 * WebSocket队列管理器
 * 功能：
 * 1. 按command去重，只保留最后一个同command的消息
 * 2. 延迟发送，避免频繁请求
 * 3. 保证只有一个请求在运行，不并行发送
 * 4. 基于CID管理请求和响应
 */

import { DEFAULT_WEBSOCKET_CONFIG } from '@/contexts/gamepad-config-context';

// 通用参数类型
type MessageParams = Record<string, unknown>;

// 通用响应数据类型
type ResponseData = Record<string, unknown> | undefined;

// 队列状态
enum QueueState {
    IDLE = 'idle',    // 空闲状态，可以处理下一条消息
    BUSY = 'busy'     // 忙碌状态，正在处理消息
}

interface QueuedMessage {
    command: string;
    params: MessageParams;
    timestamp: number;
    canSendAt: number; // 可发送时间戳
    resolve: (value: ResponseData) => void;
    reject: (reason: Error) => void;
    paused: boolean; // 是否已暂停（被新请求覆盖）
}

export class WebSocketQueueManager {
    private messageQueue: Map<string, QueuedMessage> = new Map(); // 按command存储消息
    private queueState: QueueState = QueueState.IDLE; // 队列状态
    private pollTimer: NodeJS.Timeout | null = null; // 轮询定时器
    
    // 配置参数
    private readonly SEND_DELAY = DEFAULT_WEBSOCKET_CONFIG.sendDelay; // 延迟发送时间
    private readonly POLL_INTERVAL = DEFAULT_WEBSOCKET_CONFIG.pollInterval; // 轮询间隔
    
    // WebSocket发送函数，由外部设置 - 直接返回响应数据
    private sendFunction: ((command: string, params: MessageParams) => Promise<ResponseData>) | null = null;

    // 清空队列相关状态
    private isFlushingQueue: boolean = false; // 是否正在清空队列
    private flushPromise: Promise<void> | null = null; // 清空队列的Promise
    private flushResolve: (() => void) | null = null; // 清空完成的resolve函数
    private pendingFlushCount: number = 0; // 待清空的命令数量

    /**
     * 设置WebSocket发送函数
     */
    setSendFunction(sendFn: (command: string, params: MessageParams) => Promise<ResponseData>) {
        this.sendFunction = sendFn;
    }

    /**
     * 快速清空队列
     * 将所有待发送命令立即执行，等待所有命令完成后返回Promise
     * @returns Promise，当所有命令发送完毕后 resolve
     */
    flushQueue(): Promise<void> {
        if (this.isFlushingQueue) {
            return this.flushPromise!;
        }

        this.isFlushingQueue = true;
        this.flushPromise = new Promise<void>((resolve) => {
            this.flushResolve = resolve;
        });

        // 计算当前队列中活跃的命令数量
        this.pendingFlushCount = Array.from(this.messageQueue.values()).filter(msg => !msg.paused).length;

        if (this.pendingFlushCount === 0) {
            // 队列为空，立即完成
            this.isFlushingQueue = false;
            this.flushPromise = null;
            this.flushResolve = null;
            return Promise.resolve();
        }

        console.log(`[WebSocketQueue] Starting queue flush for ${this.pendingFlushCount} commands`);

        // 启动轮询以确保消息被处理
        this.startPolling();

        return this.flushPromise;
    }

    /**
     * 立即发送队列中的特定命令
     * @param command 命令名称
     * @returns 是否成功找到并修改了命令的发送时间
     */
    sendPendingCommandImmediately(command: string): boolean {
        const message = this.messageQueue.get(command);
        if (!message) {
            console.log(`[WebSocketQueue] Command ${command} not found in queue`);
            return false;
        }

        // 将发送时间设置为现在
        message.canSendAt = Date.now();
        message.paused = false; // 确保消息没有被暂停
        
        console.log(`[WebSocketQueue] Command ${command} will be sent immediately`);
        return true;
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

            // 如果队列中已有同command的消息，暂停旧请求的执行
            const existingMessage = this.messageQueue.get(command);
            if (existingMessage) {
                console.log(`[WebSocketQueue] Command ${command} already in queue, pausing old request`);
                // 标记旧消息为已暂停，这样它就不会被执行
                existingMessage.paused = true;
            }

            // 创建新的队列消息
            const queuedMessage: QueuedMessage = {
                command,
                params,
                timestamp: now,
                canSendAt,
                resolve,
                reject,
                paused: false
            };

            // 存储到队列（覆盖同command的旧消息）
            this.messageQueue.set(command, queuedMessage);

            console.log(`[WebSocketQueue] Enqueued command: ${command}, canSendAt: ${new Date(canSendAt).toISOString()}, immediate: ${immediate}`);

            // 启动轮询
            this.startPolling();
        });
    }

    /**
     * 启动轮询
     */
    private startPolling() {
        if (this.pollTimer) {
            return; // 已经在轮询中
        }

        console.log(`[WebSocketQueue] Starting polling`);
        this.pollTimer = setInterval(() => {
            this.poll();
        }, this.POLL_INTERVAL);
    }

    /**
     * 停止轮询
     */
    private stopPolling() {
        if (this.pollTimer) {
            clearInterval(this.pollTimer);
            this.pollTimer = null;
            console.log(`[WebSocketQueue] Stopped polling`);
        }
    }

    /**
     * 轮询处理队列
     */
    private poll() {
        // 只有在空闲状态时才处理消息
        if (this.queueState !== QueueState.IDLE) {
            return;
        }

        // 查找可发送的消息
        const now = Date.now();
        let nextMessage: QueuedMessage | null = null;

        // 从所有可发送的消息中选择发送时间最近的
        for (const message of this.messageQueue.values()) {
            // 跳过已暂停的消息
            if (message.paused) {
                continue;
            }
            
            // 如果正在清空队列，立即发送所有消息
            if (this.isFlushingQueue) {
                if (!nextMessage) {
                    nextMessage = message;
                }
            } else if (message.canSendAt <= now) {
                // 可以立即发送，选择发送时间最近的
                if (!nextMessage || message.canSendAt < nextMessage.canSendAt) {
                    nextMessage = message;
                }
            }
        }

        if (nextMessage) {
            // 设置为忙碌状态
            this.queueState = QueueState.BUSY;
            console.log(`[WebSocketQueue] Processing message: ${nextMessage.command}`);
            
            // 发送消息
            this.sendMessage(nextMessage);
        } else {
            // 没有可发送的消息，检查队列是否为空
            const activeMessages = Array.from(this.messageQueue.values()).filter(msg => !msg.paused);
            if (activeMessages.length === 0) {
                // 队列为空，停止轮询
                this.stopPolling();
                console.log(`[WebSocketQueue] Queue is empty, stopping polling`);
            }
        }
    }

    /**
     * 清理已暂停的消息
     */
    private cleanupPausedMessages(command: string) {
        const existingMessage = this.messageQueue.get(command);
        if (existingMessage && existingMessage.paused) {
            console.log(`[WebSocketQueue] Cleaning up paused message for command: ${command}`);
            // 对于已暂停的消息，我们用一个特殊的结果来resolve，表示请求被暂停
            existingMessage.resolve({ 
                _paused: true, 
                message: `Request ${command} was paused by newer request` 
            });
            this.messageQueue.delete(command);
        }
    }

    /**
     * 发送消息
     */
    private async sendMessage(message: QueuedMessage) {
        if (!this.sendFunction) {
            message.reject(new Error('WebSocket send function not set'));
            this.queueState = QueueState.IDLE;
            return;
        }

        try {
            console.log(`[WebSocketQueue] Sending command: ${message.command}`);
            
            // 从队列中移除消息
            this.messageQueue.delete(message.command);

            // 清理同命令的已暂停消息
            this.cleanupPausedMessages(message.command);

            // 直接调用发送函数并等待响应
            const responseData = await this.sendFunction(message.command, message.params);
            
            // 直接resolve响应数据
            message.resolve(responseData);
            
            console.log(`[WebSocketQueue] Command completed: ${message.command}`);

            // 如果是清空队列中的命令，减少计数
            if (this.isFlushingQueue) {
                this.pendingFlushCount--;
                console.log(`[WebSocketQueue] Flush command completed: ${message.command}, remaining: ${this.pendingFlushCount}`);
                
                // 检查是否所有清空命令都已完成
                if (this.pendingFlushCount <= 0) {
                    this.completeFlush();
                }
            }

        } catch (error) {
            console.error(`[WebSocketQueue] Failed to send message:`, error);
            message.reject(error instanceof Error ? error : new Error(String(error)));
            
            // 如果是清空队列中的命令，即使失败也要减少计数
            if (this.isFlushingQueue) {
                this.pendingFlushCount--;
                console.log(`[WebSocketQueue] Flush command failed: ${message.command}, remaining: ${this.pendingFlushCount}`);
                
                // 检查是否所有清空命令都已完成
                if (this.pendingFlushCount <= 0) {
                    this.completeFlush();
                }
            }
        } finally {
            // 设置为空闲状态，可以处理下一条消息
            this.queueState = QueueState.IDLE;
            console.log(`[WebSocketQueue] Queue state changed to IDLE`);
        }
    }

    /**
     * 完成清空队列
     */
    private completeFlush(): void {
        console.log(`[WebSocketQueue] All flush commands completed`);
        
        // 重置清空状态
        this.isFlushingQueue = false;
        this.flushPromise = null;
        
        // 调用resolve函数
        if (this.flushResolve) {
            this.flushResolve();
            this.flushResolve = null;
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

        // 停止轮询
        this.stopPolling();
        
        // 重置状态
        this.queueState = QueueState.IDLE;

        // 重置清空队列状态
        this.isFlushingQueue = false;
        this.flushPromise = null;
        this.flushResolve = null;
        this.pendingFlushCount = 0;

        console.log(`[WebSocketQueue] Queue cleared`);
    }

    /**
     * 获取队列状态信息
     */
    getStatus() {
        const pausedCommands = Array.from(this.messageQueue.values())
            .filter(msg => msg.paused)
            .map(msg => msg.command);
            
        return {
            queueSize: this.messageQueue.size,
            queueState: this.queueState,
            isPolling: this.pollTimer !== null,
            queuedCommands: Array.from(this.messageQueue.keys()),
            pausedCommands: pausedCommands,
            isFlushing: this.isFlushingQueue,
            pendingFlushCount: this.pendingFlushCount
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