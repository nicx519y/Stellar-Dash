#ifndef CALIBRATION_COMMAND_HANDLER_HPP
#define CALIBRATION_COMMAND_HANDLER_HPP

#include "configs/device_command_handler.hpp"
#include "configs/device_command_message.hpp"
#include "adc_btns/adc_calibration.hpp"
#include "configs/webconfig_btns_manager.hpp"
#include "cJSON.h"

/**
 * @brief 校准和按键监控命令处理器
 * 
 * 负责处理所有与按键校准和监控相关的DeviceCommand命令，包括：
 * - 手动校准相关命令（开始、停止、清除、获取状态）
 * - 按键监控相关命令（开启、关闭、获取状态）
 */
class CalibrationCommandHandler : public DeviceCommandHandler {
public:
    static CalibrationCommandHandler& getInstance();
    
    // 校准相关命令
    DeviceCommandResponse handleStartManualCalibration(const DeviceCommandRequest& request);
    DeviceCommandResponse handleStopManualCalibration(const DeviceCommandRequest& request);
    DeviceCommandResponse handleGetCalibrationStatus(const DeviceCommandRequest& request);
    DeviceCommandResponse handleClearManualCalibrationData(const DeviceCommandRequest& request);
    DeviceCommandResponse handleCheckIsManualCalibrationCompleted(const DeviceCommandRequest& request);
    
    // 按键监控相关命令
    DeviceCommandResponse handleStartButtonMonitoring(const DeviceCommandRequest& request);
    DeviceCommandResponse handleStopButtonMonitoring(const DeviceCommandRequest& request);
    DeviceCommandResponse handleGetButtonStates(const DeviceCommandRequest& request);
    
    // DeviceCommandHandler接口实现
    DeviceCommandResponse handle(const DeviceCommandRequest& request) override;
    
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