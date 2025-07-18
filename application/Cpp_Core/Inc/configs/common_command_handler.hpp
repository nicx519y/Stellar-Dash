#pragma once

#include "configs/websocket_command_handler.hpp"
#include "websocket_message.hpp"
#include "cJSON.h"

/**
 * @brief 通用命令处理器
 * 处理按键监控等通用功能的WebSocket命令
 */
class CommonCommandHandler : public WebSocketCommandHandler {
private:
    // 单例模式：私有构造函数
    CommonCommandHandler() = default;
    ~CommonCommandHandler() = default;
    
    // 禁用拷贝构造和赋值
    CommonCommandHandler(const CommonCommandHandler&) = delete;
    CommonCommandHandler& operator=(const CommonCommandHandler&) = delete;

public:
    // 单例模式：获取实例
    static CommonCommandHandler& getInstance();

    // 实现基类纯虚函数
    WebSocketDownstreamMessage handle(const WebSocketUpstreamMessage& request) override;

    // 按键监控相关命令处理方法
    
    /**
     * @brief 开启按键功能
     * WebSocket命令: start_button_monitoring
     * 对应HTTP接口: POST /api/start-button-monitoring
     */
    WebSocketDownstreamMessage handleStartButtonMonitoring(const WebSocketUpstreamMessage& request);
    
    /**
     * @brief 关闭按键功能
     * WebSocket命令: stop_button_monitoring
     * 对应HTTP接口: POST /api/stop-button-monitoring
     */
    WebSocketDownstreamMessage handleStopButtonMonitoring(const WebSocketUpstreamMessage& request);
    
    /**
     * @brief 获取按键状态（保留用于兼容，推荐使用推送模式）
     * WebSocket命令: get_button_states
     * 对应HTTP接口: GET /api/get-button-states
     */
    WebSocketDownstreamMessage handleGetButtonStates(const WebSocketUpstreamMessage& request);

    // 推送功能
    
    /**
     * @brief 推送按键状态变化通知
     * 当按键状态发生变化时自动调用此方法发送推送通知
     */
    void sendButtonStateNotification();

private:
    /**
     * @brief 构建按键状态的JSON对象
     * @return cJSON* 按键状态JSON对象
     */
    cJSON* buildButtonStatesJSON();
}; 