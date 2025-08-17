#ifndef CALIBRATION_COMMAND_HANDLER_HPP
#define CALIBRATION_COMMAND_HANDLER_HPP

#include "configs/websocket_command_handler.hpp"
#include "configs/websocket_message.hpp"
#include "adc_btns/adc_calibration.hpp"
#include "configs/webconfig_btns_manager.hpp"
#include "cJSON.h"

/**
 * @brief 校准和按键监控命令处理器
 * 
 * 负责处理所有与按键校准和监控相关的WebSocket命令，包括：
 * - 手动校准相关命令（开始、停止、清除、获取状态）
 * - 按键监控相关命令（开启、关闭、获取状态）
 */
class CalibrationCommandHandler : public WebSocketCommandHandler {
public:
    static CalibrationCommandHandler& getInstance();
    
    // 校准相关命令
    WebSocketDownstreamMessage handleStartManualCalibration(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleStopManualCalibration(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleGetCalibrationStatus(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleClearManualCalibrationData(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleCheckIsManualCalibrationCompleted(const WebSocketUpstreamMessage& request);
    
    // 按键监控相关命令
    WebSocketDownstreamMessage handleStartButtonMonitoring(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleStopButtonMonitoring(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleGetButtonStates(const WebSocketUpstreamMessage& request);
    
    // WebSocketCommandHandler接口实现
    WebSocketDownstreamMessage handle(const WebSocketUpstreamMessage& request) override;
    
    // 推送校准状态变化通知
    void sendCalibrationStatusNotification();

private:
    CalibrationCommandHandler() = default;
    
    // 辅助函数
    cJSON* buildCalibrationStatusJSON();
    cJSON* buildButtonStatesJSON();
    const char* getPhaseString(CalibrationPhase phase);
    const char* getLEDColorString(CalibrationLEDColor color);
};

#endif // CALIBRATION_COMMAND_HANDLER_HPP 