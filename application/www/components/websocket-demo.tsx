import React, { useState, useEffect } from 'react';
import { 
  useWebSocket, 
  WebSocketState, 
  WebSocketDownstreamMessage 
} from './websocket-framework';

const WebSocketDemo: React.FC = () => {
  const [messages, setMessages] = useState<string[]>([]);
  const [responseData, setResponseData] = useState<Record<string, unknown> | null>(null);
  const [loading, setLoading] = useState(false);

  // 使用WebSocket框架
  const {
    state,
    error,
    connect,
    disconnect,
    sendMessage,
    sendMessageNoResponse,
    onMessage
  } = useWebSocket({
    url: `ws://${window.location.hostname}:8081`,
    heartbeatInterval: 30000,
    reconnectInterval: 5000,
    maxReconnectAttempts: 5,
    timeout: 10000,
    autoReconnect: true,
    connectOnMount: true
  });

  // 监听消息
  useEffect(() => {
    const unsubscribe = onMessage((message: WebSocketDownstreamMessage) => {
      // 处理服务器主动推送的消息（非响应类型）
      if (message.command === 'welcome' || message.command === 'notification') {
        setMessages(prev => [...prev, `收到推送: ${message.command} - ${JSON.stringify(message.data)}`]);
      }
    });

    return unsubscribe;
  }, [onMessage]);

  // 错误处理
  useEffect(() => {
    if (error) {
      setMessages(prev => [...prev, `错误: ${error.message}`]);
    }
  }, [error]);

  // 状态变化处理
  useEffect(() => {
    setMessages(prev => [...prev, `状态变更: ${state}`]);
  }, [state]);

  // 获取全局配置
  const handleGetGlobalConfig = async () => {
    setLoading(true);
    try {
      const response = await sendMessage('get_global_config', {});
      setResponseData(response.data || {});
      setMessages(prev => [...prev, `获取全局配置成功: ${JSON.stringify(response.data)}`]);
    } catch (error: unknown) {
      const errorMessage = error instanceof Error ? error.message : String(error);
      setMessages(prev => [...prev, `获取全局配置失败: ${errorMessage}`]);
    } finally {
      setLoading(false);
    }
  };

  // 发送Ping
  const handlePing = async () => {
    setLoading(true);
    try {
      const response = await sendMessage('ping', { timestamp: Date.now() });
      setMessages(prev => [...prev, `Ping响应: ${JSON.stringify(response.data)}`]);
    } catch (error: unknown) {
      const errorMessage = error instanceof Error ? error.message : String(error);
      setMessages(prev => [...prev, `Ping失败: ${errorMessage}`]);
    } finally {
      setLoading(false);
    }
  };

  // 发送心跳（无响应）
  const handleHeartbeat = () => {
    sendMessageNoResponse('ping', { type: 'heartbeat' });
    setMessages(prev => [...prev, '发送心跳包']);
  };

  // 测试不存在的命令
  const handleTestInvalidCommand = async () => {
    setLoading(true);
    try {
      const response = await sendMessage('invalid_command', { test: true });
      setMessages(prev => [...prev, `无效命令响应: ${JSON.stringify(response)}`]);
    } catch (error: unknown) {
      const errorMessage = error instanceof Error ? error.message : String(error);
      setMessages(prev => [...prev, `无效命令错误: ${errorMessage}`]);
    } finally {
      setLoading(false);
    }
  };

  // 清空消息
  const handleClearMessages = () => {
    setMessages([]);
    setResponseData(null);
  };

  // 获取状态颜色
  const getStateColor = (state: WebSocketState) => {
    switch (state) {
      case WebSocketState.CONNECTED:
        return 'text-green-600';
      case WebSocketState.CONNECTING:
      case WebSocketState.RECONNECTING:
        return 'text-yellow-600';
      case WebSocketState.ERROR:
        return 'text-red-600';
      case WebSocketState.DISCONNECTED:
        return 'text-gray-600';
      default:
        return 'text-gray-600';
    }
  };

  return (
    <div className="p-6 max-w-4xl mx-auto">
      <h1 className="text-2xl font-bold mb-6">WebSocket 演示</h1>
      
      {/* 连接状态 */}
      <div className="mb-6 p-4 bg-gray-100 rounded-lg">
        <h2 className="text-lg font-semibold mb-2">连接状态</h2>
        <p className={`font-medium ${getStateColor(state)}`}>
          状态: {state}
        </p>
        {error && (
          <p className="text-red-600 mt-2">
            错误: {error.message}
          </p>
        )}
      </div>

      {/* 控制按钮 */}
      <div className="mb-6 space-y-2">
        <h2 className="text-lg font-semibold mb-2">控制操作</h2>
        <div className="space-x-2">
          <button
            onClick={connect}
            disabled={state === WebSocketState.CONNECTED || state === WebSocketState.CONNECTING}
            className="px-4 py-2 bg-blue-500 text-white rounded disabled:bg-gray-400"
          >
            连接
          </button>
          <button
            onClick={disconnect}
            disabled={state === WebSocketState.DISCONNECTED}
            className="px-4 py-2 bg-red-500 text-white rounded disabled:bg-gray-400"
          >
            断开连接
          </button>
          <button
            onClick={handleClearMessages}
            className="px-4 py-2 bg-gray-500 text-white rounded"
          >
            清空消息
          </button>
        </div>
      </div>

      {/* 测试按钮 */}
      <div className="mb-6 space-y-2">
        <h2 className="text-lg font-semibold mb-2">测试功能</h2>
        <div className="space-x-2">
          <button
            onClick={handleGetGlobalConfig}
            disabled={loading || state !== WebSocketState.CONNECTED}
            className="px-4 py-2 bg-green-500 text-white rounded disabled:bg-gray-400"
          >
            获取全局配置
          </button>
          <button
            onClick={handlePing}
            disabled={loading || state !== WebSocketState.CONNECTED}
            className="px-4 py-2 bg-yellow-500 text-white rounded disabled:bg-gray-400"
          >
            发送Ping
          </button>
          <button
            onClick={handleHeartbeat}
            disabled={state !== WebSocketState.CONNECTED}
            className="px-4 py-2 bg-purple-500 text-white rounded disabled:bg-gray-400"
          >
            发送心跳
          </button>
          <button
            onClick={handleTestInvalidCommand}
            disabled={loading || state !== WebSocketState.CONNECTED}
            className="px-4 py-2 bg-orange-500 text-white rounded disabled:bg-gray-400"
          >
            测试无效命令
          </button>
        </div>
      </div>

      {/* 响应数据 */}
      {responseData && (
        <div className="mb-6">
          <h2 className="text-lg font-semibold mb-2">最新响应数据</h2>
          <pre className="bg-gray-100 p-4 rounded-lg text-sm overflow-x-auto">
            {JSON.stringify(responseData, null, 2)}
          </pre>
        </div>
      )}

      {/* 消息日志 */}
      <div>
        <h2 className="text-lg font-semibold mb-2">消息日志</h2>
        <div className="bg-black text-green-400 p-4 rounded-lg h-96 overflow-y-auto font-mono text-sm">
          {messages.map((message, index) => (
            <div key={index} className="mb-1">
              [{new Date().toLocaleTimeString()}] {message}
            </div>
          ))}
          {loading && (
            <div className="text-yellow-400">
              [处理中...] 请求正在处理，请稍候...
            </div>
          )}
        </div>
      </div>
    </div>
  );
};

export default WebSocketDemo; 