# 按键监控 WebSocket 命令文档

本文档描述了按键监控相关的 WebSocket 命令接口，包括按键监控启停和实时状态推送功能。

## 命令列表

### 1. 开始按键监控 (start_button_monitoring)

启动按键监控功能，开始检测按键状态变化。

**请求格式：**
```json
{
  "cid": 1,
  "command": "start_button_monitoring",
  "params": {}
}
```

**响应格式：**
```json
{
  "cid": 1,
  "command": "start_button_monitoring",
  "errNo": 0,
  "data": {
    "message": "Button monitoring started successfully",
    "status": "active",
    "isActive": true
  }
}
```

### 2. 停止按键监控 (stop_button_monitoring)

停止按键监控功能，停止检测按键状态变化。

**请求格式：**
```json
{
  "cid": 2,
  "command": "stop_button_monitoring",
  "params": {}
}
```

**响应格式：**
```json
{
  "cid": 2,
  "command": "stop_button_monitoring",
  "errNo": 0,
  "data": {
    "message": "Button monitoring stopped successfully",
    "status": "inactive",
    "isActive": false
  }
}
```

## 服务器推送功能

### 按键状态变化推送 (button_state_changed)

当按键状态发生变化时，服务器会自动推送通知给所有连接的客户端。

**推送消息格式：**
```json
{
  "command": "button_state_changed",
  "errNo": 0,
  "data": {
    "type": "button_state_update",
    "timestamp": 1234567890,
    "buttonStates": {
      "triggerMask": 5,
      "triggerBinary": "00000101",
      "totalButtons": 8,
      "timestamp": 1234567890,
      "isActive": true
    }
  }
}
```

**字段说明：**
- `triggerMask`: 触发掩码（十进制），表示本次变化中被触发的按键
- `triggerBinary`: 触发掩码的二进制字符串表示
- `totalButtons`: 总按键数量
- `timestamp`: 时间戳
- `isActive`: 按键监控是否处于活跃状态

## 技术特点

### 推送模式优势

1. **实时性**：按键状态变化立即推送，无需客户端轮询
2. **效率高**：减少网络请求，只有变化时才发送数据
3. **节省资源**：降低服务器和客户端的CPU和网络开销
4. **可靠性**：基于WebSocket连接，确保消息及时到达

### 工作原理

1. **状态检测**：WebConfigBtnsManager实时监控按键状态
2. **变化检测**：检测按键从0到1的跳变（按下瞬间）
3. **回调触发**：触发已注册的按键状态变化回调
4. **消息广播**：通过WebSocket服务器广播给所有连接的客户端
5. **事件分发**：客户端接收消息后通过事件总线分发给具体组件 