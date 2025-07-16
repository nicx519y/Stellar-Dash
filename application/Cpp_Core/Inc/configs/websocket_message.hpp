#pragma once

#include <string>
#include <cstdint>
#include "cJSON.h"
#include "configs/websocket_server.hpp"

/**
 * @brief WebSocket 上行消息结构
 * 用于封装从客户端发送到服务器的消息
 * 消息格式：{"cid": 2, "command": "ping", "params": {...}}
 */
class WebSocketUpstreamMessage {
private:
    uint32_t cid_;                    // 命令ID
    std::string command_;             // 命令名称
    cJSON* params_;                   // 参数对象
    WebSocketConnection* connection_; // 连接对象
    
public:
    WebSocketUpstreamMessage();
    ~WebSocketUpstreamMessage();
    
    // 禁用拷贝构造和赋值，只允许移动
    WebSocketUpstreamMessage(const WebSocketUpstreamMessage&) = delete;
    WebSocketUpstreamMessage& operator=(const WebSocketUpstreamMessage&) = delete;
    
    // 移动构造和赋值
    WebSocketUpstreamMessage(WebSocketUpstreamMessage&& other) noexcept;
    WebSocketUpstreamMessage& operator=(WebSocketUpstreamMessage&& other) noexcept;
    
    // Getter方法
    uint32_t getCid() const { return cid_; }
    const std::string& getCommand() const { return command_; }
    cJSON* getParams() const { return params_; }
    WebSocketConnection* getConnection() const { return connection_; }
    
    // Setter方法
    void setCid(uint32_t cid) { cid_ = cid; }
    void setCommand(const std::string& command) { command_ = command; }
    void setParams(cJSON* params);
    void setConnection(WebSocketConnection* connection) { connection_ = connection; }
};

/**
 * @brief WebSocket 下行消息结构
 * 用于封装从服务器发送到客户端的响应消息
 * 消息格式：{"cid": 2, "command": "ping", "errNo": 0, "data": {...}}
 */
class WebSocketDownstreamMessage {
private:
    uint32_t cid_;        // 命令ID，与上行消息保持一致
    std::string command_; // 命令名称
    int errNo_;           // 错误码 (0表示成功)
    cJSON* data_;         // 响应数据对象
    
public:
    WebSocketDownstreamMessage();
    ~WebSocketDownstreamMessage();
    
    // 禁用拷贝构造和赋值，只允许移动
    WebSocketDownstreamMessage(const WebSocketDownstreamMessage&) = delete;
    WebSocketDownstreamMessage& operator=(const WebSocketDownstreamMessage&) = delete;
    
    // 移动构造和赋值
    WebSocketDownstreamMessage(WebSocketDownstreamMessage&& other) noexcept;
    WebSocketDownstreamMessage& operator=(WebSocketDownstreamMessage&& other) noexcept;
    
    // Getter方法
    uint32_t getCid() const { return cid_; }
    const std::string& getCommand() const { return command_; }
    int getErrNo() const { return errNo_; }
    cJSON* getData() const { return data_; }
    
    // Setter方法
    void setCid(uint32_t cid) { cid_ = cid; }
    void setCommand(const std::string& command) { command_ = command; }
    void setErrNo(int errNo) { errNo_ = errNo; }
    void setData(cJSON* data);
};

// ==================== 工具函数声明 ====================

/**
 * @brief 创建WebSocket响应消息
 * @param cid 命令ID
 * @param command 命令名称
 * @param errNo 错误码
 * @param data 响应数据（可选）
 * @param errorMessage 错误消息（可选）
 * @return WebSocket下行消息对象
 */
WebSocketDownstreamMessage create_websocket_response(uint32_t cid, const std::string& command, 
                                                    int errNo, cJSON* data = nullptr, 
                                                    const std::string& errorMessage = "");

/**
 * @brief 发送WebSocket响应消息
 * @param conn WebSocket连接对象
 * @param response 要发送的响应消息（移动语义）
 */
void send_websocket_response(WebSocketConnection* conn, WebSocketDownstreamMessage&& response); 