#include "configs/calibration_command_handler.hpp"
#include "system_logger.h"
#include "adc_btns/adc_calibration.hpp"
#include "configs/webconfig_btns_manager.hpp"
#include "configs/websocket_server.hpp"

// 获取校准管理器实例
#define ADC_CALIBRATION_MANAGER ADCCalibrationManager::getInstance()
// 获取按键管理器实例  
#define WEBCONFIG_BTNS_MANAGER WebConfigBtnsManager::getInstance()

// ============================================================================
// CalibrationCommandHandler 单例实现
// ============================================================================

CalibrationCommandHandler& CalibrationCommandHandler::getInstance() {
    static CalibrationCommandHandler instance;
    
    // 设置校准状态变更回调，当状态变化时自动推送通知
    static bool callbackSet = false;
    if (!callbackSet) {
        ADC_CALIBRATION_MANAGER.setCalibrationStatusChangedCallback([]() {
            // 校准状态发生变化，发送推送通知
            CalibrationCommandHandler::getInstance().sendCalibrationStatusNotification();
        });
        callbackSet = true;
    }
    
    return instance;
}

// ============================================================================
// 推送功能实现
// ============================================================================

/**
 * @brief 推送校准状态变化通知
 */
void CalibrationCommandHandler::sendCalibrationStatusNotification() {
    // 获取WebSocket服务器实例
    WebSocketServer& server = WebSocketServer::getInstance();
    
    // 构建校准状态数据
    cJSON* notificationData = cJSON_CreateObject();
    cJSON* statusJSON = buildCalibrationStatusJSON();
    
    cJSON_AddItemToObject(notificationData, "calibrationStatus", statusJSON);
    cJSON_AddStringToObject(notificationData, "type", "calibration_update");
    cJSON_AddNumberToObject(notificationData, "timestamp", HAL_GetTick());
    
    // 创建通知消息（无CID的消息表示这是服务器主动推送）
    cJSON* notification = cJSON_CreateObject();
    cJSON_AddStringToObject(notification, "command", "calibration_update");
    cJSON_AddNumberToObject(notification, "errNo", 0);
    cJSON_AddItemToObject(notification, "data", notificationData);
    
    // 转换为JSON字符串
    char* notificationString = cJSON_PrintUnformatted(notification);
    if (notificationString) {
        // 广播给所有连接的客户端
        server.broadcast_text(std::string(notificationString));
        
        LOG_INFO("WebSocket", "Calibration status notification sent to all clients");
        
        // 释放JSON字符串内存
        free(notificationString);
    } else {
        LOG_ERROR("WebSocket", "Failed to serialize calibration notification");
    }
    
    // 清理JSON对象
    cJSON_Delete(notification);
}

// ============================================================================
// 校准相关命令实现
// ============================================================================

/**
 * @brief 开始手动校准
 * 对应HTTP接口: POST /api/start-manual-calibration
 * 
 * WebSocket命令格式:
 * {
 *   "cid": 1,
 *   "command": "start_manual_calibration",
 *   "params": {}
 * }
 * 
 * 响应格式:
 * {
 *   "cid": 1,
 *   "command": "start_manual_calibration",
 *   "errNo": 0,
 *   "data": {
 *     "message": "Manual calibration started",
 *     "calibrationStatus": {
 *       "isActive": true,
 *       "uncalibratedCount": 8,
 *       "activeCalibrationCount": 8,
 *       "allCalibrated": false
 *     }
 *   }
 * }
 */
WebSocketDownstreamMessage CalibrationCommandHandler::handleStartManualCalibration(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling start_manual_calibration command, cid: %d", request.getCid());
    
    // 开始手动校准
    ADCBtnsError error = ADC_CALIBRATION_MANAGER.startManualCalibration();
    if(error != ADCBtnsError::SUCCESS) {
        LOG_ERROR("WebSocket", "start_manual_calibration: Failed to start manual calibration");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to start manual calibration");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "message", "Manual calibration started");
    
    // 添加校准状态信息
    cJSON* statusJSON = cJSON_CreateObject();
    cJSON_AddBoolToObject(statusJSON, "isActive", ADC_CALIBRATION_MANAGER.isCalibrationActive());
    cJSON_AddNumberToObject(statusJSON, "uncalibratedCount", ADC_CALIBRATION_MANAGER.getUncalibratedButtonCount());
    cJSON_AddNumberToObject(statusJSON, "activeCalibrationCount", ADC_CALIBRATION_MANAGER.getActiveCalibrationButtonCount());
    cJSON_AddBoolToObject(statusJSON, "allCalibrated", ADC_CALIBRATION_MANAGER.isAllButtonsCalibrated());
    
    cJSON_AddItemToObject(dataJSON, "calibrationStatus", statusJSON);
    
    LOG_INFO("WebSocket", "start_manual_calibration command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

/**
 * @brief 结束手动校准
 * 对应HTTP接口: POST /api/stop-manual-calibration
 * 
 * WebSocket命令格式:
 * {
 *   "cid": 2,
 *   "command": "stop_manual_calibration",
 *   "params": {}
 * }
 * 
 * 响应格式:
 * {
 *   "cid": 2,
 *   "command": "stop_manual_calibration",
 *   "errNo": 0,
 *   "data": {
 *     "message": "Manual calibration stopped",
 *     "calibrationStatus": {
 *       "isActive": false,
 *       "uncalibratedCount": 3,
 *       "activeCalibrationCount": 0,
 *       "allCalibrated": false
 *     }
 *   }
 * }
 */
WebSocketDownstreamMessage CalibrationCommandHandler::handleStopManualCalibration(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling stop_manual_calibration command, cid: %d", request.getCid());
    
    // 停止手动校准
    ADCBtnsError error = ADC_CALIBRATION_MANAGER.stopCalibration();
    
    if(error != ADCBtnsError::SUCCESS) {
        LOG_ERROR("WebSocket", "stop_manual_calibration: Failed to stop manual calibration");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to stop manual calibration");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "message", "Manual calibration stopped");
    
    // 添加校准状态信息
    cJSON* statusJSON = cJSON_CreateObject();
    cJSON_AddBoolToObject(statusJSON, "isActive", ADC_CALIBRATION_MANAGER.isCalibrationActive());
    cJSON_AddNumberToObject(statusJSON, "uncalibratedCount", ADC_CALIBRATION_MANAGER.getUncalibratedButtonCount());
    cJSON_AddNumberToObject(statusJSON, "activeCalibrationCount", ADC_CALIBRATION_MANAGER.getActiveCalibrationButtonCount());
    cJSON_AddBoolToObject(statusJSON, "allCalibrated", ADC_CALIBRATION_MANAGER.isAllButtonsCalibrated());
    
    cJSON_AddItemToObject(dataJSON, "calibrationStatus", statusJSON);
    
    LOG_INFO("WebSocket", "stop_manual_calibration command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

/**
 * @brief 获取校准状态
 * 对应HTTP接口: GET /api/get-calibration-status
 * 
 * WebSocket命令格式:
 * {
 *   "cid": 3,
 *   "command": "get_calibration_status",
 *   "params": {}
 * }
 * 
 * 响应格式:
 * {
 *   "cid": 3,
 *   "command": "get_calibration_status",
 *   "errNo": 0,
 *   "data": {
 *     "calibrationStatus": {
 *       "isActive": true,
 *       "uncalibratedCount": 3,
 *       "activeCalibrationCount": 8,
 *       "allCalibrated": false,
 *       "buttons": [
 *         {
 *           "index": 0,
 *           "phase": "TOP_SAMPLING",
 *           "isCalibrated": false,
 *           "topValue": 0,
 *           "bottomValue": 0,
 *           "ledColor": "CYAN"
 *         }
 *       ]
 *     }
 *   }
 * }
 */
WebSocketDownstreamMessage CalibrationCommandHandler::handleGetCalibrationStatus(const WebSocketUpstreamMessage& request) {
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* statusJSON = buildCalibrationStatusJSON();
    
    cJSON_AddItemToObject(dataJSON, "calibrationStatus", statusJSON);
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

/**
 * @brief 清除手动校准数据
 * 对应HTTP接口: POST /api/clear-manual-calibration-data
 * 
 * WebSocket命令格式:
 * {
 *   "cid": 4,
 *   "command": "clear_manual_calibration_data",
 *   "params": {}
 * }
 * 
 * 响应格式:
 * {
 *   "cid": 4,
 *   "command": "clear_manual_calibration_data",
 *   "errNo": 0,
 *   "data": {
 *     "message": "Manual calibration data cleared successfully",
 *     "calibrationStatus": {
 *       "isActive": false,
 *       "uncalibratedCount": 8,
 *       "activeCalibrationCount": 0,
 *       "allCalibrated": false
 *     }
 *   }
 * }
 */
WebSocketDownstreamMessage CalibrationCommandHandler::handleClearManualCalibrationData(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling clear_manual_calibration_data command, cid: %d", request.getCid());
    
    // 清除所有手动校准数据
    ADCBtnsError error = ADC_CALIBRATION_MANAGER.resetAllCalibration();
    if(error != ADCBtnsError::SUCCESS) {
        LOG_ERROR("WebSocket", "clear_manual_calibration_data: Failed to clear manual calibration data");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to clear manual calibration data");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "message", "Manual calibration data cleared successfully");
    
    // 添加清除后的校准状态信息
    cJSON* statusJSON = cJSON_CreateObject();
    cJSON_AddBoolToObject(statusJSON, "isActive", ADC_CALIBRATION_MANAGER.isCalibrationActive());
    cJSON_AddNumberToObject(statusJSON, "uncalibratedCount", ADC_CALIBRATION_MANAGER.getUncalibratedButtonCount());
    cJSON_AddNumberToObject(statusJSON, "activeCalibrationCount", ADC_CALIBRATION_MANAGER.getActiveCalibrationButtonCount());
    cJSON_AddBoolToObject(statusJSON, "allCalibrated", ADC_CALIBRATION_MANAGER.isAllButtonsCalibrated());
    
    cJSON_AddItemToObject(dataJSON, "calibrationStatus", statusJSON);
    
    LOG_INFO("WebSocket", "clear_manual_calibration_data command completed successfully");

    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

// ============================================================================
// 按键监控相关命令实现
// ============================================================================

/**
 * @brief 开启按键功能
 * 对应HTTP接口: POST /api/start-button-monitoring
 * 
 * WebSocket命令格式:
 * {
 *   "cid": 5,
 *   "command": "start_button_monitoring",
 *   "params": {}
 * }
 * 
 * 响应格式:
 * {
 *   "cid": 5,
 *   "command": "start_button_monitoring",
 *   "errNo": 0,
 *   "data": {
 *     "message": "Button monitoring started successfully",
 *     "status": "active",
 *     "isActive": true
 *   }
 * }
 */
WebSocketDownstreamMessage CalibrationCommandHandler::handleStartButtonMonitoring(const WebSocketUpstreamMessage& request) {
    // 获取按键管理器实例并开始监控
    WebConfigBtnsManager& btnsManager = WEBCONFIG_BTNS_MANAGER;
    
    // 启动按键工作器
    btnsManager.startButtonWorkers();
    
    // 验证启动状态
    if (!btnsManager.isActive()) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to start button monitoring");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    if (!dataJSON) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to create JSON object");
    }
    
    cJSON_AddStringToObject(dataJSON, "message", "Button monitoring started successfully");
    cJSON_AddStringToObject(dataJSON, "status", "active");
    cJSON_AddBoolToObject(dataJSON, "isActive", btnsManager.isActive());
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

/**
 * @brief 关闭按键功能
 * 对应HTTP接口: POST /api/stop-button-monitoring
 * 
 * WebSocket命令格式:
 * {
 *   "cid": 6,
 *   "command": "stop_button_monitoring", 
 *   "params": {}
 * }
 * 
 * 响应格式:
 * {
 *   "cid": 6,
 *   "command": "stop_button_monitoring",
 *   "errNo": 0,
 *   "data": {
 *     "message": "Button monitoring stopped successfully",
 *     "status": "inactive",
 *     "isActive": false
 *   }
 * }
 */
WebSocketDownstreamMessage CalibrationCommandHandler::handleStopButtonMonitoring(const WebSocketUpstreamMessage& request) {
    // 获取按键管理器实例
    WebConfigBtnsManager& btnsManager = WEBCONFIG_BTNS_MANAGER;
    
    // 停止按键工作器（真正停止ADC和GPIO按键采样）
    btnsManager.stopButtonWorkers();
    
    // 验证停止状态
    if (btnsManager.isActive()) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to stop button monitoring");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    if (!dataJSON) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to create JSON object");
    }
    
    cJSON_AddStringToObject(dataJSON, "message", "Button monitoring stopped successfully");
    cJSON_AddStringToObject(dataJSON, "status", "inactive");
    cJSON_AddBoolToObject(dataJSON, "isActive", btnsManager.isActive());
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

/**
 * @brief 轮询获取按键状态
 * 对应HTTP接口: GET /api/get-button-states
 * 
 * WebSocket命令格式:
 * {
 *   "cid": 7,
 *   "command": "get_button_states",
 *   "params": {}
 * }
 * 
 * 响应格式:
 * {
 *   "cid": 7,
 *   "command": "get_button_states",
 *   "errNo": 0,
 *   "data": {
 *     "triggerMask": 5,
 *     "triggerBinary": "00000101",
 *     "totalButtons": 8,
 *     "timestamp": 1234567890
 *   }
 * }
 */
WebSocketDownstreamMessage CalibrationCommandHandler::handleGetButtonStates(const WebSocketUpstreamMessage& request) {
    // 获取按键管理器实例
    WebConfigBtnsManager& btnsManager = WEBCONFIG_BTNS_MANAGER;
    
    // 检查按键工作器是否活跃
    if (!btnsManager.isActive()) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Button monitoring is not active");
    }
    
    // 获取并清空触发掩码
    uint32_t triggerMask = btnsManager.getAndClearTriggerMask();
    uint8_t totalButtons = btnsManager.getTotalButtonCount();
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    if (!dataJSON) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to create JSON object");
    }
    
    // 添加触发掩码（十进制）
    cJSON_AddNumberToObject(dataJSON, "triggerMask", triggerMask);
    
    // 创建二进制字符串表示
    std::string binaryStr = "";
    for (int i = totalButtons - 1; i >= 0; i--) {
        binaryStr += (triggerMask & (1U << i)) ? "1" : "0";
    }
    cJSON_AddStringToObject(dataJSON, "triggerBinary", binaryStr.c_str());
    
    // 添加按键总数
    cJSON_AddNumberToObject(dataJSON, "totalButtons", totalButtons);
    
    // 添加时间戳
    cJSON_AddNumberToObject(dataJSON, "timestamp", HAL_GetTick());
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

// ============================================================================
// 命令路由处理
// ============================================================================

WebSocketDownstreamMessage CalibrationCommandHandler::handle(const WebSocketUpstreamMessage& request) {
    const std::string& command = request.getCommand();
    
    // 校准相关命令
    if (command == "start_manual_calibration") {
        return handleStartManualCalibration(request);
    } else if (command == "stop_manual_calibration") {
        return handleStopManualCalibration(request);
    } else if (command == "get_calibration_status") {
        return handleGetCalibrationStatus(request);
    } else if (command == "clear_manual_calibration_data") {
        return handleClearManualCalibrationData(request);
    }
    // 按键监控相关命令
    else if (command == "start_button_monitoring") {
        return handleStartButtonMonitoring(request);
    } else if (command == "stop_button_monitoring") {
        return handleStopButtonMonitoring(request);
    } else if (command == "get_button_states") {
        return handleGetButtonStates(request);
    }
    
    return create_error_response(request.getCid(), command, -1, "Unknown command");
}

// ============================================================================
// 辅助函数实现
// ============================================================================

/**
 * @brief 构建校准状态的JSON结构
 * @return cJSON* 校准状态JSON对象
 */
cJSON* CalibrationCommandHandler::buildCalibrationStatusJSON() {
    cJSON* statusJSON = cJSON_CreateObject();
    
    // 添加总体校准状态
    cJSON_AddBoolToObject(statusJSON, "isActive", ADC_CALIBRATION_MANAGER.isCalibrationActive());
    cJSON_AddNumberToObject(statusJSON, "uncalibratedCount", ADC_CALIBRATION_MANAGER.getUncalibratedButtonCount());
    cJSON_AddNumberToObject(statusJSON, "activeCalibrationCount", ADC_CALIBRATION_MANAGER.getActiveCalibrationButtonCount());
    cJSON_AddBoolToObject(statusJSON, "allCalibrated", ADC_CALIBRATION_MANAGER.isAllButtonsCalibrated());
    
    // 注意：即使所有按钮都校准完成，也不自动关闭校准模式
    // 只有用户手动调用停止校准接口才会关闭校准模式
    // 这样设计是为了让用户有机会确认校准结果
    
    // 添加每个按钮的详细状态
    cJSON* buttonsArray = cJSON_CreateArray();
    
    for(uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        cJSON* buttonJSON = cJSON_CreateObject();
        
        cJSON_AddNumberToObject(buttonJSON, "index", i);
        
        // 获取校准阶段
        CalibrationPhase phase = ADC_CALIBRATION_MANAGER.getButtonPhase(i);
        cJSON_AddStringToObject(buttonJSON, "phase", getPhaseString(phase));
        
        // 获取校准状态
        cJSON_AddBoolToObject(buttonJSON, "isCalibrated", ADC_CALIBRATION_MANAGER.isButtonCalibrated(i));
        
        // 获取校准值
        uint16_t topValue = 0, bottomValue = 0;
        ADC_CALIBRATION_MANAGER.getCalibrationValues(i, topValue, bottomValue);
        cJSON_AddNumberToObject(buttonJSON, "topValue", topValue);
        cJSON_AddNumberToObject(buttonJSON, "bottomValue", bottomValue);
        
        // 获取LED颜色
        CalibrationLEDColor ledColor = ADC_CALIBRATION_MANAGER.getButtonLEDColor(i);
        cJSON_AddStringToObject(buttonJSON, "ledColor", getLEDColorString(ledColor));
        
        cJSON_AddItemToArray(buttonsArray, buttonJSON);
    }
    
    cJSON_AddItemToObject(statusJSON, "buttons", buttonsArray);
    
    return statusJSON;
}

/**
 * @brief 构建按键状态的JSON结构
 * @return cJSON* 按键状态JSON对象
 */
cJSON* CalibrationCommandHandler::buildButtonStatesJSON() {
    // 此函数预留，当前在handleGetButtonStates中直接构建JSON
    // 如果需要复用可以在这里实现
    return cJSON_CreateObject();
}

/**
 * @brief 将校准阶段枚举转换为字符串
 * @param phase 校准阶段
 * @return 对应的字符串
 */
const char* CalibrationCommandHandler::getPhaseString(CalibrationPhase phase) {
    switch(phase) {
        case CalibrationPhase::IDLE: return "IDLE";
        case CalibrationPhase::TOP_SAMPLING: return "TOP_SAMPLING";
        case CalibrationPhase::BOTTOM_SAMPLING: return "BOTTOM_SAMPLING";
        case CalibrationPhase::COMPLETED: return "COMPLETED";
        case CalibrationPhase::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

/**
 * @brief 将LED颜色枚举转换为字符串
 * @param color LED颜色
 * @return 对应的字符串
 */
const char* CalibrationCommandHandler::getLEDColorString(CalibrationLEDColor color) {
    switch(color) {
        case CalibrationLEDColor::OFF: return "OFF";
        case CalibrationLEDColor::RED: return "RED";
        case CalibrationLEDColor::CYAN: return "CYAN";
        case CalibrationLEDColor::DARK_BLUE: return "DARK_BLUE";
        case CalibrationLEDColor::GREEN: return "GREEN";
        case CalibrationLEDColor::YELLOW: return "YELLOW";
        default: return "UNKNOWN";
    }
} 