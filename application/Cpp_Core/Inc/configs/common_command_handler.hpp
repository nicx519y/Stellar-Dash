#pragma once

#include "configs/websocket_command_handler.hpp"
#include "websocket_message.hpp"
#include "configs/webconfig_btns_manager.hpp"  // 为ADCBtnTestEvent定义
#include "cJSON.h"
#include <cstdint>
#include <string>
#include <vector>  // 为std::vector

// ============================================================================
// 二进制数据结构定义
// ============================================================================

// 按键状态推送的二进制数据结构
#pragma pack(push, 1)  // 确保结构体紧密排列，避免字节对齐问题
struct ButtonStateBinaryData {
    uint8_t command;        // 命令号：1 表示按键状态变化
    uint8_t isActive;       // 布尔值：1=活跃，0=非活跃
    uint32_t triggerMask;   // 32位按键触发掩码
    uint8_t totalButtons;   // 总按键数量
    uint8_t reserved[2];    // 保留字节，用于将来扩展，保持8字节对齐
};

// ADC按键测试事件推送的二进制数据结构
struct ADCBtnTestEventBinaryData {
    uint8_t command;        // 命令号：2 表示ADC按键测试事件
    uint8_t eventCount;     // 本次推送的事件数量
    uint32_t timestamp;     // 时间戳
    uint8_t reserved[2];    // 保留字节，用于将来扩展，保持8字节对齐
    // 后面跟着 eventCount 个 ADCBtnTestEventItem
};

// 单个ADC按键测试事件项
struct ADCBtnTestEventItem {
    uint8_t buttonIndex;    // 按键索引
    uint8_t virtualPin;     // 虚拟引脚
    uint16_t adcValue;      // 触发时的ADC值
    float triggerDistance;  // 触发时的物理行程（mm）
    float limitValueDistance; // limitValue对应的物理行程（mm）
    uint16_t limitValue;    // limitValue值
    uint8_t isPressEvent;   // 是否为按下事件（1=按下，0=释放）
};
#pragma pack(pop)

// 命令号定义
#define BUTTON_STATE_CHANGED_CMD    1
#define ADC_BTN_TEST_EVENT_CMD      2

// ============================================================================
// CommonCommandHandler 类定义
// ============================================================================

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

    /**
     * @brief 推送ADC按键测试事件通知
     * 当ADC按键在测试模式下触发时调用此方法发送推送通知
     */
    void sendADCBtnTestEventNotification(const std::vector<ADCBtnTestEvent>& testEvents);

private:
    /**
     * @brief 构建按键状态的JSON对象
     * @return cJSON* 按键状态JSON对象
     */
    cJSON* buildButtonStatesJSON();

    // 构建按键状态的二进制数据
    ButtonStateBinaryData buildButtonStateBinaryData();

    // 构建ADC按键测试事件的二进制数据
    std::vector<uint8_t> buildADCBtnTestEventBinaryData(const std::vector<ADCBtnTestEvent>& testEvents);
}; 