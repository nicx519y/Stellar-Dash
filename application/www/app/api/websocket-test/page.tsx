'use client';

import { useState, useRef } from 'react';

// WebSocket 消息类型定义
interface WebSocketUpstreamMessage {
  cid: number;
  command: string;
  params?: Record<string, unknown>;
}

interface WebSocketDownstreamMessage {
  cid?: number;
  command: string;
  errNo: number;
  data?: Record<string, unknown>;
}

// 预定义的命令列表
const COMMANDS = [
  { value: 'ping', label: 'Ping' },
  { value: 'get_global_config', label: '获取全局配置' },
  { value: 'update_global_config', label: '更新全局配置' },
  { value: 'get_hotkeys_config', label: '获取快捷键配置' },
  { value: 'update_hotkeys_config', label: '更新快捷键配置' },
  { value: 'reboot', label: '重启系统' },
  { value: 'push_leds_config', label: '推送LED配置' },
  { value: 'clear_leds_preview', label: '清除LED预览' },
  { value: 'get_profile_list', label: '获取配置文件列表' },
  { value: 'get_default_profile', label: '获取默认配置文件' },
  { value: 'update_profile', label: '更新配置文件' },
  { value: 'create_profile', label: '创建配置文件' },
  { value: 'delete_profile', label: '删除配置文件' },
  { value: 'switch_default_profile', label: '切换默认配置文件' },
  { value: 'ms_get_list', label: '获取轴体映射列表' },
  { value: 'ms_get_mark_status', label: '获取映射标记状态' },
  { value: 'ms_set_default', label: '设置默认映射' },
  { value: 'ms_get_default', label: '获取默认映射' },
  { value: 'ms_create_mapping', label: '创建映射' },
  { value: 'ms_delete_mapping', label: '删除映射' },
  { value: 'ms_rename_mapping', label: '重命名映射' },
  { value: 'ms_mark_mapping_start', label: '开始映射标记' },
  { value: 'ms_mark_mapping_stop', label: '停止映射标记' },
  { value: 'ms_mark_mapping_step', label: '映射标记步骤' },
  { value: 'ms_get_mapping', label: '获取映射详情' },
  { value: 'start_manual_calibration', label: '开始手动校准' },
  { value: 'stop_manual_calibration', label: '停止手动校准' },
  { value: 'get_calibration_status', label: '获取校准状态' },
  { value: 'clear_manual_calibration_data', label: '清除校准数据' },
  { value: 'check_is_manual_calibration_completed', label: '检查校准是否完成' },
  { value: 'start_button_monitoring', label: '开始按键监控' },
  { value: 'stop_button_monitoring', label: '停止按键监控' },
  { value: 'start_button_performance_monitoring', label: '开始按键性能监控' },
  { value: 'stop_button_performance_monitoring', label: '停止按键性能监控' },
  { value: 'get_button_states', label: '获取按键状态' },
  { value: 'get_device_auth', label: '获取设备认证' },
  { value: 'get_firmware_metadata', label: '获取固件元数据' },
  { value: 'create_firmware_upgrade_session', label: '创建固件升级会话' },
  { value: 'upload_firmware_chunk', label: '上传固件分片' },
  { value: 'complete_firmware_upgrade_session', label: '完成固件升级会话' },
  { value: 'abort_firmware_upgrade_session', label: '中止固件升级会话' },
  { value: 'get_firmware_upgrade_status', label: '获取固件升级状态' },
  { value: 'cleanup_firmware_upgrade_session', label: '清理固件升级会话' },
];

export default function WebSocketTestPage() {
  const [ws, setWs] = useState<WebSocket | null>(null);
  const [isConnected, setIsConnected] = useState(false);
  const [selectedCommand, setSelectedCommand] = useState('ping');
  const [params, setParams] = useState('{}');
  const [messages, setMessages] = useState<string[]>([]);
  const [serverUrl, setServerUrl] = useState('ws://localhost:8081');
  const cidRef = useRef(1);

  const addMessage = (message: string) => {
    setMessages(prev => [...prev, `${new Date().toLocaleTimeString()}: ${message}`]);
  };

  const connect = () => {
    try {
      const websocket = new WebSocket(serverUrl);
      
      websocket.onopen = () => {
        setIsConnected(true);
        addMessage('WebSocket 连接已建立');
      };

      websocket.onmessage = (event) => {
        try {
          const response: WebSocketDownstreamMessage = JSON.parse(event.data);
          addMessage(`收到响应: ${JSON.stringify(response, null, 2)}`);
        } catch (error) {
          addMessage(`收到消息: ${event.data}`);
        }
      };

      websocket.onclose = () => {
        setIsConnected(false);
        addMessage('WebSocket 连接已关闭');
      };

      websocket.onerror = (error) => {
        addMessage(`WebSocket 错误: ${error}`);
      };

      setWs(websocket);
    } catch (error) {
      addMessage(`连接失败: ${error}`);
    }
  };

  const disconnect = () => {
    if (ws) {
      ws.close();
      setWs(null);
    }
  };

  const sendMessage = () => {
    if (!ws || !isConnected) {
      addMessage('WebSocket 未连接');
      return;
    }

    try {
      const message: WebSocketUpstreamMessage = {
        cid: cidRef.current++,
        command: selectedCommand,
        params: JSON.parse(params)
      };

      ws.send(JSON.stringify(message));
      addMessage(`发送消息: ${JSON.stringify(message, null, 2)}`);
    } catch (error) {
      addMessage(`发送失败: ${error}`);
    }
  };

  const clearMessages = () => {
    setMessages([]);
  };

  return (
    <div style={{ padding: '24px', maxWidth: '1200px', margin: '0 auto' }}>
      <h1 style={{ fontSize: '2rem', fontWeight: 'bold', marginBottom: '24px' }}>
        WebSocket 接口测试
      </h1>
      
      {/* 连接控制 */}
      <div style={{ padding: '16px', border: '1px solid #e2e8f0', borderRadius: '8px', marginBottom: '24px' }}>
        <h2 style={{ fontSize: '1.25rem', fontWeight: 'semibold', marginBottom: '16px' }}>连接设置</h2>
        <div style={{ display: 'flex', gap: '12px', alignItems: 'center' }}>
          <input
            type="text"
            value={serverUrl}
            onChange={(e) => setServerUrl(e.target.value)}
            placeholder="WebSocket 服务器地址"
            style={{ flex: 1, padding: '8px 12px', border: '1px solid #d1d5db', borderRadius: '4px' }}
          />
          <button
            onClick={isConnected ? disconnect : connect}
            style={{
              padding: '8px 16px',
              backgroundColor: isConnected ? '#dc2626' : '#059669',
              color: 'white',
              border: 'none',
              borderRadius: '4px',
              cursor: 'pointer'
            }}
          >
            {isConnected ? '断开连接' : '连接'}
          </button>
        </div>
        <p style={{ color: isConnected ? '#059669' : '#dc2626', marginTop: '8px' }}>
          状态: {isConnected ? '已连接' : '未连接'}
        </p>
      </div>

      {/* 命令发送 */}
      <div style={{ padding: '16px', border: '1px solid #e2e8f0', borderRadius: '8px', marginBottom: '24px' }}>
        <h2 style={{ fontSize: '1.25rem', fontWeight: 'semibold', marginBottom: '16px' }}>发送命令</h2>
        <div style={{ display: 'flex', gap: '16px' }}>
          <div style={{ flex: 1 }}>
            <select
              value={selectedCommand}
              onChange={(e) => setSelectedCommand(e.target.value)}
              style={{ width: '100%', padding: '8px 12px', border: '1px solid #d1d5db', borderRadius: '4px', marginBottom: '16px' }}
            >
              {COMMANDS.map(cmd => (
                <option key={cmd.value} value={cmd.value}>
                  {cmd.label}
                </option>
              ))}
            </select>
            <textarea
              value={params}
              onChange={(e) => setParams(e.target.value)}
              placeholder="参数 (JSON 格式)"
              rows={4}
              style={{ width: '100%', padding: '8px 12px', border: '1px solid #d1d5db', borderRadius: '4px', marginBottom: '16px' }}
            />
            <button
              onClick={sendMessage}
              disabled={!isConnected}
              style={{
                padding: '8px 16px',
                backgroundColor: isConnected ? '#2563eb' : '#9ca3af',
                color: 'white',
                border: 'none',
                borderRadius: '4px',
                cursor: isConnected ? 'pointer' : 'not-allowed'
              }}
            >
              发送命令
            </button>
          </div>
        </div>
      </div>

      {/* 消息日志 */}
      <div style={{ padding: '16px', border: '1px solid #e2e8f0', borderRadius: '8px', marginBottom: '24px' }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '16px' }}>
          <h2 style={{ fontSize: '1.25rem', fontWeight: 'semibold' }}>消息日志</h2>
          <button
            onClick={clearMessages}
            style={{
              padding: '4px 8px',
              backgroundColor: '#6b7280',
              color: 'white',
              border: 'none',
              borderRadius: '4px',
              cursor: 'pointer',
              fontSize: '0.875rem'
            }}
          >
            清空日志
          </button>
        </div>
        <div
          style={{
            height: '400px',
            width: '100%',
            border: '1px solid #d1d5db',
            borderRadius: '4px',
            padding: '16px',
            overflowY: 'auto',
            backgroundColor: '#f9fafb',
            fontFamily: 'monospace',
            fontSize: '0.875rem'
          }}
        >
          {messages.length === 0 ? (
            <p style={{ color: '#6b7280' }}>暂无消息</p>
          ) : (
            messages.map((msg, index) => (
              <p key={index} style={{ marginBottom: '8px', whiteSpace: 'pre-wrap' }}>
                {msg}
              </p>
            ))
          )}
        </div>
      </div>

      {/* 快速测试按钮 */}
      <div style={{ padding: '16px', border: '1px solid #e2e8f0', borderRadius: '8px' }}>
        <h2 style={{ fontSize: '1.25rem', fontWeight: 'semibold', marginBottom: '16px' }}>快速测试</h2>
        <div style={{ display: 'flex', gap: '8px', flexWrap: 'wrap' }}>
          <button
            onClick={() => {
              setSelectedCommand('ping');
              setParams('{}');
              sendMessage();
            }}
            disabled={!isConnected}
            style={{
              padding: '4px 8px',
              backgroundColor: isConnected ? '#059669' : '#9ca3af',
              color: 'white',
              border: 'none',
              borderRadius: '4px',
              cursor: isConnected ? 'pointer' : 'not-allowed',
              fontSize: '0.875rem'
            }}
          >
            Ping 测试
          </button>
          <button
            onClick={() => {
              setSelectedCommand('get_global_config');
              setParams('{}');
              sendMessage();
            }}
            disabled={!isConnected}
            style={{
              padding: '4px 8px',
              backgroundColor: isConnected ? '#059669' : '#9ca3af',
              color: 'white',
              border: 'none',
              borderRadius: '4px',
              cursor: isConnected ? 'pointer' : 'not-allowed',
              fontSize: '0.875rem'
            }}
          >
            获取全局配置
          </button>
          <button
            onClick={() => {
              setSelectedCommand('get_profile_list');
              setParams('{}');
              sendMessage();
            }}
            disabled={!isConnected}
            style={{
              padding: '4px 8px',
              backgroundColor: isConnected ? '#059669' : '#9ca3af',
              color: 'white',
              border: 'none',
              borderRadius: '4px',
              cursor: isConnected ? 'pointer' : 'not-allowed',
              fontSize: '0.875rem'
            }}
          >
            获取配置文件列表
          </button>
          <button
            onClick={() => {
              setSelectedCommand('get_button_states');
              setParams('{}');
              sendMessage();
            }}
            disabled={!isConnected}
            style={{
              padding: '4px 8px',
              backgroundColor: isConnected ? '#059669' : '#9ca3af',
              color: 'white',
              border: 'none',
              borderRadius: '4px',
              cursor: isConnected ? 'pointer' : 'not-allowed',
              fontSize: '0.875rem'
            }}
          >
            获取按键状态
          </button>
        </div>
      </div>
    </div>
  );
} 