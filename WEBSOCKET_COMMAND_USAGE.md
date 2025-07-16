# WebSocket 命令处理器使用指南

## get_global_config 命令

### HTTP 接口迁移
原HTTP接口：`GET /api/global-config`  
新WebSocket命令：`get_global_config`

### 使用方法

**1. 客户端发送请求：**
```json
{
  "cid": 1,
  "command": "get_global_config",
  "params": {}
}
```

**2. 服务器响应：**
```json
{
  "cid": 1,
  "command": "get_global_config",
  "errNo": 0,
  "data": {
    "globalConfig": {
      "inputMode": "XINPUT",
      "autoCalibrationEnabled": true,
      "manualCalibrationActive": false
    }
  }
}
```

### 错误响应示例
```json
{
  "cid": 1,
  "command": "get_global_config",
  "errNo": -1,
  "data": {
    "errorMessage": "Internal error"
  }
}
```

### JavaScript 客户端示例
```javascript
// 连接WebSocket
const ws = new WebSocket('ws://192.168.7.1:8081');

// 发送获取全局配置请求
const request = {
  cid: 1,
  command: "get_global_config",
  params: {}
};

ws.send(JSON.stringify(request));

// 处理响应
ws.onmessage = function(event) {
  const response = JSON.parse(event.data);
  if (response.cid === 1 && response.command === "get_global_config") {
    if (response.errNo === 0) {
      console.log("Global config:", response.data.globalConfig);
    } else {
      console.error("Error:", response.data.errorMessage);
    }
  }
};
```

### Python 客户端示例
```python
import asyncio
import websockets
import json

async def get_global_config():
    uri = "ws://192.168.7.1:8081"
    async with websockets.connect(uri) as websocket:
        # 发送请求
        request = {
            "cid": 1,
            "command": "get_global_config",
            "params": {}
        }
        await websocket.send(json.dumps(request))
        
        # 接收响应
        response = await websocket.recv()
        data = json.loads(response)
        
        if data["errNo"] == 0:
            print("Global config:", data["data"]["globalConfig"])
        else:
            print("Error:", data["data"]["errorMessage"])

# 运行
asyncio.run(get_global_config())
```

## 架构说明

### 文件结构
```
application/Cpp_Core/
├── Inc/configs/
│   ├── websocket_commands.hpp      # 命令处理器声明
│   ├── websocket_message.hpp       # 消息类定义
│   └── websocket_message_queue.hpp # 消息队列定义
└── Src/configs/
    ├── websocket_commands.cpp      # 命令处理器实现
    ├── websocket_message.cpp       # 消息类实现
    ├── websocket_message_queue.cpp # 消息队列实现
    └── webconfig.cpp              # 主要配置和路由
```

### 优势
✅ **模块化设计**：命令处理器独立于主文件  
✅ **类型安全**：统一的消息结构和错误处理  
✅ **易于扩展**：添加新命令只需实现函数并注册  
✅ **一致性**：与现有HTTP接口保持数据格式一致  
✅ **实时性**：WebSocket提供双向实时通信

### 添加新命令的步骤
1. 在`websocket_commands.hpp`中声明新的处理函数
2. 在`websocket_commands.cpp`中实现处理逻辑  
3. 在`webconfig.cpp`的`websocket_command_handlers`映射表中注册
4. 更新文档说明使用方法 