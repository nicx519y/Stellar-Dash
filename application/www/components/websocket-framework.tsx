import { useState, useEffect, useRef, useCallback } from 'react';

// WebSocket消息类型定义
export interface WebSocketUpstreamMessage {
  cid: number;
  command: string;
  params?: Record<string, unknown>;
}

export interface WebSocketDownstreamMessage {
  cid: number;
  command: string;
  errNo: number;
  data?: Record<string, unknown>;
}

// WebSocket连接状态
export enum WebSocketState {
  DISCONNECTED = 'disconnected',
  CONNECTING = 'connecting',
  CONNECTED = 'connected',
  RECONNECTING = 'reconnecting',
  ERROR = 'error'
}

// 错误类型
export interface WebSocketError {
  type: 'connection' | 'timeout' | 'server' | 'parse';
  message: string;
  code?: number;
  timestamp: Date;
}

// 配置接口
export interface WebSocketConfig {
  url?: string;
  heartbeatInterval?: number;
  reconnectInterval?: number;
  maxReconnectAttempts?: number;
  timeout?: number;
  autoReconnect?: boolean;
}

// Hook选项接口
export interface UseWebSocketOptions extends WebSocketConfig {
  connectOnMount?: boolean;
}

// 回调函数类型
export type MessageHandler = (message: WebSocketDownstreamMessage) => void;
export type StateChangeHandler = (state: WebSocketState) => void;
export type ErrorHandler = (error: WebSocketError) => void;

// WebSocket框架主类
export class WebSocketFramework {
  private ws: WebSocket | null = null;
  private config: Required<WebSocketConfig>;
  private state: WebSocketState = WebSocketState.DISCONNECTED;
  private reconnectCount = 0;
  private heartbeatTimer: NodeJS.Timeout | null = null;
  private reconnectTimer: NodeJS.Timeout | null = null;
  private pendingRequests = new Map<number, {
    resolve: (value: WebSocketDownstreamMessage) => void;
    reject: (error: WebSocketError) => void;
    timer: NodeJS.Timeout;
    command: string;
  }>();
  private nextCid = 1;

  // 事件监听器
  private messageHandlers = new Set<MessageHandler>();
  private stateChangeHandlers = new Set<StateChangeHandler>();
  private errorHandlers = new Set<ErrorHandler>();

  constructor(config: WebSocketConfig = {}) {
    this.config = {
      url: config.url || `ws://${window.location.hostname}:81/ws`,
      heartbeatInterval: config.heartbeatInterval || 30000, // 30秒
      reconnectInterval: config.reconnectInterval || 5000,  // 5秒
      maxReconnectAttempts: config.maxReconnectAttempts || 10,
      timeout: config.timeout || 10000, // 10秒
      autoReconnect: config.autoReconnect !== false
    };
  }

  // 连接WebSocket
  public connect(): Promise<void> {
    return new Promise((resolve, reject) => {
      if (this.state === WebSocketState.CONNECTED || this.state === WebSocketState.CONNECTING) {
        resolve();
        return;
      }

      this.setState(WebSocketState.CONNECTING);
      
      try {
        this.ws = new WebSocket(this.config.url);
        
        // 连接超时处理
        const connectTimeout = setTimeout(() => {
          this.handleError({
            type: 'timeout',
            message: '连接超时',
            timestamp: new Date()
          });
          reject(new Error('连接超时'));
        }, this.config.timeout);

        this.ws.onopen = () => {
          clearTimeout(connectTimeout);
          this.setState(WebSocketState.CONNECTED);
          this.reconnectCount = 0;
          this.startHeartbeat();
          console.log('WebSocket连接已建立');
          resolve();
        };

        this.ws.onmessage = (event) => {
          this.handleMessage(event.data);
        };

        this.ws.onclose = () => {
          clearTimeout(connectTimeout);
          this.cleanup();
          console.log('WebSocket连接已关闭');
          
          if (this.config.autoReconnect && this.reconnectCount < this.config.maxReconnectAttempts) {
            this.scheduleReconnect();
          } else {
            this.setState(WebSocketState.DISCONNECTED);
          }
        };

        this.ws.onerror = () => {
          clearTimeout(connectTimeout);
          this.handleError({
            type: 'connection',
            message: '连接错误',
            timestamp: new Date()
          });
          reject(new Error('连接错误'));
        };

      } catch (error) {
        this.handleError({
          type: 'connection',
          message: `连接失败: ${error}`,
          timestamp: new Date()
        });
        reject(error);
      }
    });
  }

  // 断开连接
  public disconnect(): void {
    this.cleanup();
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
    this.setState(WebSocketState.DISCONNECTED);
  }

  // 发送消息（带响应）
  public async sendMessage(command: string, params: Record<string, unknown> = {}): Promise<WebSocketDownstreamMessage> {
    if (this.state !== WebSocketState.CONNECTED) {
      throw new Error('WebSocket未连接');
    }

    const cid = this.nextCid++;
    const message: WebSocketUpstreamMessage = { cid, command, params };

    return new Promise((resolve, reject) => {
      // 设置超时
      const timer = setTimeout(() => {
        this.pendingRequests.delete(cid);
        reject({
          type: 'timeout',
          message: `命令 ${command} 响应超时`,
          timestamp: new Date()
        });
      }, this.config.timeout);

      // 保存待处理请求
      this.pendingRequests.set(cid, { resolve, reject, timer, command });

      // 发送消息
      try {
        this.ws!.send(JSON.stringify(message));
        console.log('发送WebSocket消息:', message);
      } catch (error) {
        this.pendingRequests.delete(cid);
        clearTimeout(timer);
        reject({
          type: 'connection',
          message: `发送失败: ${error}`,
          timestamp: new Date()
        });
      }
    });
  }

  // 发送消息（不等待响应）
  public sendMessageNoResponse(command: string, params: Record<string, unknown> = {}): void {
    if (this.state !== WebSocketState.CONNECTED) {
      console.warn('WebSocket未连接，无法发送消息');
      return;
    }

    const cid = this.nextCid++;
    const message: WebSocketUpstreamMessage = { cid, command, params };

    try {
      this.ws!.send(JSON.stringify(message));
      console.log('发送WebSocket消息(无响应):', message);
    } catch (error) {
      this.handleError({
        type: 'connection',
        message: `发送失败: ${error}`,
        timestamp: new Date()
      });
    }
  }

  // 获取当前状态
  public getState(): WebSocketState {
    return this.state;
  }

  // 事件监听器管理
  public onMessage(handler: MessageHandler): () => void {
    this.messageHandlers.add(handler);
    return () => this.messageHandlers.delete(handler);
  }

  public onStateChange(handler: StateChangeHandler): () => void {
    this.stateChangeHandlers.add(handler);
    return () => this.stateChangeHandlers.delete(handler);
  }

  public onError(handler: ErrorHandler): () => void {
    this.errorHandlers.add(handler);
    return () => this.errorHandlers.delete(handler);
  }

  // 私有方法
  private setState(newState: WebSocketState): void {
    if (this.state !== newState) {
      this.state = newState;
      this.stateChangeHandlers.forEach(handler => handler(newState));
    }
  }

  private handleMessage(data: string): void {
    try {
      const message: WebSocketDownstreamMessage = JSON.parse(data);
      console.log('收到WebSocket消息:', message);

      // 处理待处理的请求响应
      const pending = this.pendingRequests.get(message.cid);
      if (pending) {
        clearTimeout(pending.timer);
        this.pendingRequests.delete(message.cid);
        
        if (message.errNo === 0) {
          pending.resolve(message);
        } else {
          pending.reject({
            type: 'server',
            message: `命令 ${pending.command} 执行失败`,
            code: message.errNo,
            timestamp: new Date()
          });
        }
        return;
      }

      // 分发消息给所有监听器
      this.messageHandlers.forEach(handler => handler(message));

    } catch (error) {
      this.handleError({
        type: 'parse',
        message: `消息解析失败: ${error}`,
        timestamp: new Date()
      });
    }
  }

  private handleError(error: WebSocketError): void {
    console.error('WebSocket错误:', error);
    this.setState(WebSocketState.ERROR);
    this.errorHandlers.forEach(handler => handler(error));
  }

  private startHeartbeat(): void {
    this.stopHeartbeat();
    this.heartbeatTimer = setInterval(() => {
      this.sendMessageNoResponse('ping', { type: 'heartbeat' });
    }, this.config.heartbeatInterval);
  }

  private stopHeartbeat(): void {
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer);
      this.heartbeatTimer = null;
    }
  }

  private scheduleReconnect(): void {
    this.setState(WebSocketState.RECONNECTING);
    this.reconnectCount++;
    
    this.reconnectTimer = setTimeout(() => {
      console.log(`尝试重连 (${this.reconnectCount}/${this.config.maxReconnectAttempts})`);
      this.connect().catch(() => {
        // 重连失败将在onclose中处理
      });
    }, this.config.reconnectInterval);
  }

  private cleanup(): void {
    this.stopHeartbeat();
    
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    
    // 清理所有待处理的请求
    this.pendingRequests.forEach(({ timer, reject }) => {
      clearTimeout(timer);
      reject({
        type: 'connection',
        message: '连接已断开',
        timestamp: new Date()
      });
    });
    this.pendingRequests.clear();
  }
}

export function useWebSocket(options: UseWebSocketOptions = {}) {
  const [state, setState] = useState<WebSocketState>(WebSocketState.DISCONNECTED);
  const [error, setError] = useState<WebSocketError | null>(null);
  const frameworkRef = useRef<WebSocketFramework | null>(null);

  // 稳定化options引用，避免不必要的重新创建
  const stableOptions = useRef(options);
  stableOptions.current = options;

  // 初始化框架
  useEffect(() => {
    frameworkRef.current = new WebSocketFramework(stableOptions.current);
    
    const unsubscribeState = frameworkRef.current.onStateChange(setState);
    const unsubscribeError = frameworkRef.current.onError(setError);

    return () => {
      unsubscribeState();
      unsubscribeError();
      frameworkRef.current?.disconnect();
    };
  }, []); // 空依赖数组，只在组件挂载时初始化一次

  // 自动连接
  useEffect(() => {
    if (stableOptions.current.connectOnMount !== false && frameworkRef.current) {
      frameworkRef.current.connect().catch(console.error);
    }
  }, []); // 只在组件挂载时检查一次

  const connect = useCallback(() => {
    return frameworkRef.current?.connect() || Promise.reject('Framework not initialized');
  }, []);

  const disconnect = useCallback(() => {
    frameworkRef.current?.disconnect();
  }, []);

  const sendMessage = useCallback((command: string, params: Record<string, unknown> = {}) => {
    return frameworkRef.current?.sendMessage(command, params) || Promise.reject('Framework not initialized');
  }, []);

  const sendMessageNoResponse = useCallback((command: string, params: Record<string, unknown> = {}) => {
    frameworkRef.current?.sendMessageNoResponse(command, params);
  }, []);

  const onMessage = useCallback((handler: MessageHandler): (() => void) => {
    return frameworkRef.current?.onMessage(handler) || (() => {});
  }, []);

  return {
    state,
    error,
    connect,
    disconnect,
    sendMessage,
    sendMessageNoResponse,
    onMessage,
    framework: frameworkRef.current
  };
}

// 导出工具函数
export const createWebSocketFramework = (config?: WebSocketConfig) => {
  return new WebSocketFramework(config);
};

export default WebSocketFramework; 