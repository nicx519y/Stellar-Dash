import React, { useState } from 'react';
import { useWebSocket } from './websocket-framework';
import { ReconnectModal } from './ui/reconnect-modal';
import { WebSocketState } from './websocket-framework';

export function WebSocketUsageExample() {
  const [isReconnecting, setIsReconnecting] = useState(false);
  
  const {
    state,
    error,
    isDisconnected,
    connect,
    disconnect,
    sendMessage,
    onMessage
  } = useWebSocket({
    url: 'ws://localhost:8081',
    heartbeatInterval: 30000,
    timeout: 15000,
    maxConnectAttempts: 3,
    connectOnMount: true
  });

  // 监听消息
  React.useEffect(() => {
    const unsubscribe = onMessage((message) => {
      console.log('收到消息:', message);
    });

    return unsubscribe;
  }, [onMessage]);

  // 处理重连
  const handleReconnect = async () => {
    setIsReconnecting(true);
    try {
      await connect();
      console.log('重连成功');
    } catch (error) {
      console.error('重连失败:', error);
    } finally {
      setIsReconnecting(false);
    }
  };

  return (
    <div>
      <h2>WebSocket 状态: {state}</h2>
      {error && <p>错误: {error.message}</p>}
      
      <button onClick={connect} disabled={state === WebSocketState.CONNECTING}>
        连接
      </button>
      <button onClick={disconnect} disabled={state === WebSocketState.DISCONNECTED}>
        断开
      </button>
      <button 
        onClick={() => sendMessage('test', { data: 'hello' })}
        disabled={state !== WebSocketState.CONNECTED}
      >
        发送测试消息
      </button>

      {/* 重连提示模态框 */}
      <ReconnectModal
        isOpen={isDisconnected}
        onReconnect={handleReconnect}
        isLoading={isReconnecting}
      />
    </div>
  );
} 