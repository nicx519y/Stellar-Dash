# WebSocket服务器崩溃问题分析与解决方案

## 问题描述

当前端频繁发起WebSocket连接请求时，设备端会在几次连接后出现卡死崩溃现象。

## 根本原因分析

### 1. **连接管理问题**

#### 问题1: 连接数限制检查不充分
```cpp
// 原代码问题
if (!server->add_connection(conn)) {
    APP_ERR("WebSocket: Connection limit reached, rejecting new connection");
    delete conn;
    tcp_close(newpcb);
    return ERR_MEM;
}
```

**问题**: 
- 在`add_connection`之前没有检查连接数限制
- 可能导致连接数组越界
- 内存分配失败时处理不当

#### 问题2: 连接状态不一致
```cpp
void WebSocketServer::remove_connection(WebSocketConnection* conn) {
    // 直接删除连接对象，可能导致悬空指针
    delete conn;
}
```

**问题**:
- 在回调函数中直接删除对象
- 可能导致悬空指针访问
- 连接状态更新不及时

### 2. **内存管理问题**

#### 问题1: 缓冲区无限增长
```cpp
bool WebSocketConnection::ensure_buffer_capacity(size_t required_size) {
    size_t new_size = buffer_size;
    while (new_size < required_size) {
        new_size *= 2; // 可能导致内存分配过大
    }
}
```

**问题**:
- 没有限制最大缓冲区大小
- 可能导致内存耗尽
- 内存分配失败时没有处理

#### 问题2: 内存泄漏
```cpp
void WebSocketConnection::close() {
    if (pcb) {
        tcp_close(pcb); // 可能重复关闭
        pcb = nullptr;
    }
}
```

**问题**:
- 重复关闭TCP连接
- 缓冲区没有正确释放
- 回调函数中的内存泄漏

### 3. **TCP PCB状态管理问题**

#### 问题1: PCB状态不一致
```cpp
void WebSocketConnection::tcp_err_callback(void* arg, err_t err) {
    WebSocketConnection* conn = static_cast<WebSocketConnection*>(arg);
    if (conn) {
        conn->state = WS_STATE_CLOSED;
        conn->pcb = nullptr; // PCB已经被lwIP释放
    }
}
```

**问题**:
- PCB被lwIP释放后仍可能被访问
- 状态更新不及时
- 错误处理不完整

## 解决方案

### 1. **改进连接管理**

#### 解决方案1: 提前检查连接数限制
```cpp
err_t WebSocketServer::tcp_accept_callback(void* arg, struct tcp_pcb* newpcb, err_t err) {
    // 检查连接数限制
    if (server->connection_count >= server->max_connections) {
        APP_ERR("WebSocket: Connection limit reached (%zu/%zu), rejecting new connection", 
                server->connection_count, server->max_connections);
        tcp_close(newpcb);
        return ERR_MEM;
    }
    
    // 创建连接对象...
}
```

#### 解决方案2: 添加连接清理机制
```cpp
void WebSocketServer::cleanup_dead_connections() {
    if (!connections) return;
    
    for (size_t i = 0; i < max_connections; i++) {
        if (connections[i] && !connections[i]->is_connected()) {
            APP_DBG("WebSocket: Cleaning up dead connection");
            delete connections[i];
            connections[i] = nullptr;
            if (connection_count > 0) {
                connection_count--;
            }
        }
    }
}
```

### 2. **改进内存管理**

#### 解决方案1: 限制缓冲区大小
```cpp
#define MAX_BUFFER_SIZE 16384

bool WebSocketConnection::ensure_buffer_capacity(size_t required_size) {
    if (required_size > MAX_BUFFER_SIZE) {
        APP_ERR("WebSocket: Required buffer size %zu exceeds maximum %d", required_size, MAX_BUFFER_SIZE);
        return false;
    }
    
    // 限制最大分配大小
    size_t new_size = buffer_size;
    while (new_size < required_size && new_size < MAX_BUFFER_SIZE) {
        new_size *= 2;
    }
    
    if (new_size > MAX_BUFFER_SIZE) {
        new_size = MAX_BUFFER_SIZE;
    }
    
    char* new_buffer = (char*)realloc(buffer, new_size);
    if (!new_buffer) {
        APP_ERR("WebSocket: Failed to reallocate buffer");
        return false;
    }
    
    buffer = new_buffer;
    buffer_size = new_size;
    return true;
}
```

#### 解决方案2: 防止重复关闭
```cpp
class WebSocketConnection {
private:
    bool is_closing; // 防止重复关闭
    
public:
    void close() {
        if (is_closing) return; // 防止重复关闭
        
        is_closing = true;
        
        if (state != WS_STATE_CLOSED) {
            state = WS_STATE_CLOSING;
            
            // 发送关闭帧
            if (pcb && state == WS_STATE_OPEN) {
                std::string frame = create_frame(WS_OP_CLOSE, "");
                tcp_write(pcb, frame.c_str(), frame.length(), TCP_WRITE_FLAG_COPY);
                tcp_output(pcb);
            }
            
            // 触发断开回调
            if (on_disconnect) {
                on_disconnect(this);
            }
            
            // 关闭TCP连接
            if (pcb) {
                tcp_close(pcb);
                pcb = nullptr;
            }
            
            state = WS_STATE_CLOSED;
        }
    }
};
```

### 3. **改进错误处理**

#### 解决方案1: 增强发送数据检查
```cpp
void WebSocketConnection::send_raw_data(const char* data, size_t length) {
    if (!pcb || state != WS_STATE_OPEN || is_closing) {
        APP_WARN("WebSocket: Cannot send data, connection not ready");
        return;
    }
    
    err_t err = tcp_write(pcb, data, length, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        APP_ERR("WebSocket: Failed to send data: %d", err);
        return;
    }
    
    err = tcp_output(pcb);
    if (err != ERR_OK) {
        APP_ERR("WebSocket: Failed to output data: %d", err);
    }
}
```

#### 解决方案2: 改进数据接收处理
```cpp
void WebSocketConnection::handle_data(const uint8_t* data, size_t length) {
    if (!ensure_buffer_capacity(buffer_used + length + 1)) {
        APP_ERR("WebSocket: Buffer allocation failed");
        close();
        return;
    }
    
    // 处理数据...
}
```

## 实施建议

### 1. **立即修复**
- 替换现有的`websocket_server.cpp`为修复版本
- 更新头文件以支持新的成员变量和方法
- 重新编译并测试

### 2. **测试验证**
- 测试频繁连接/断开场景
- 监控内存使用情况
- 验证连接数限制是否生效
- 检查是否有内存泄漏

### 3. **长期改进**
- 添加连接超时机制
- 实现连接池管理
- 添加更详细的日志记录
- 实现连接状态监控

## 预期效果

### 1. **稳定性提升**
- 消除频繁连接导致的崩溃
- 防止内存泄漏
- 改善连接管理

### 2. **性能优化**
- 限制内存使用
- 提高连接处理效率
- 减少资源浪费

### 3. **可维护性**
- 更清晰的错误处理
- 更好的日志记录
- 更容易调试

## 注意事项

1. **向后兼容性**: 修复版本保持API兼容性
2. **性能影响**: 新增的检查可能略微影响性能，但可接受
3. **内存使用**: 限制缓冲区大小可能影响大消息处理
4. **测试覆盖**: 需要全面测试各种异常情况

通过这些修复，WebSocket服务器应该能够稳定处理频繁的连接请求，不再出现卡死崩溃的问题。 