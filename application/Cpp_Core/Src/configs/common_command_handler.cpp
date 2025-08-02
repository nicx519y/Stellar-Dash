#include "configs/common_command_handler.hpp"
#include "system_logger.h"
#include "configs/webconfig_btns_manager.hpp"
#include "configs/websocket_server.hpp"

// 获取按键管理器实例  
#define WEBCONFIG_BTNS_MANAGER WebConfigBtnsManager::getInstance()

// ============================================================================
// CommonCommandHandler 单例实现
// ============================================================================

CommonCommandHandler& CommonCommandHandler::getInstance() {
    static CommonCommandHandler instance;
    
    // 设置按键状态变更回调，当状态变化时自动推送通知
    static bool callbackSet = false;
    if (!callbackSet) {
        WEBCONFIG_BTNS_MANAGER.setButtonStateChangedCallback([]() {
            // 按键状态发生变化，发送推送通知
            CommonCommandHandler::getInstance().sendButtonStateNotification();
        });
        
        WEBCONFIG_BTNS_MANAGER.setButtonPerformanceMonitoringCallback([]() {
            // 按键性能监控发生变化，发送推送通知
            CommonCommandHandler::getInstance().sendButtonPerformanceMonitoringNotification();
        });
        
        callbackSet = true;
    }
    
    return instance;
}

// ============================================================================
// 推送功能实现
// ============================================================================

/**
 * @brief 推送按键状态变化通知（二进制格式）
 */
void CommonCommandHandler::sendButtonStateNotification() {
    // 获取WebSocket服务器实例
    WebSocketServer& server = WebSocketServer::getInstance();
    
    // 构建二进制按键状态数据
    ButtonStateBinaryData binaryData = buildButtonStateBinaryData();
    
    // 发送二进制数据到所有连接的客户端
    server.broadcast_binary(reinterpret_cast<const uint8_t*>(&binaryData), sizeof(ButtonStateBinaryData));
    
    APP_DBG("Button state binary notification sent to all clients (cmd=%d, active=%d, mask=0x%08X, total=%d)", 
            binaryData.command, binaryData.isActive, binaryData.triggerMask, binaryData.totalButtons);
}

/**
 * @brief 推送按键性能监控通知（二进制格式）
 */
void CommonCommandHandler::sendButtonPerformanceMonitoringNotification() {
    // 获取WebSocket服务器实例
    WebSocketServer& server = WebSocketServer::getInstance();
    
    // 构建完整的二进制数据
    std::vector<uint8_t> binaryData = buildButtonPerformanceMonitoringBinaryData();
    
    // 发送二进制数据到所有连接的客户端
    server.broadcast_binary(binaryData.data(), binaryData.size());
    
    APP_DBG("Button performance monitoring binary notification sent to all clients (size=%d bytes)", binaryData.size());
}


/**
 * @brief 构建按键状态的二进制数据
 * @return 按键状态二进制数据结构
 */
ButtonStateBinaryData CommonCommandHandler::buildButtonStateBinaryData() {
    ButtonStateBinaryData data = {};
    
    // 设置命令号
    data.command = BUTTON_STATE_CHANGED_CMD;
    
    // 获取按键管理器的状态
    bool isManagerActive = WEBCONFIG_BTNS_MANAGER.isActive();
    data.isActive = isManagerActive ? 1 : 0;
    
    // 获取当前按键触发掩码
    data.triggerMask = WEBCONFIG_BTNS_MANAGER.getCurrentMask(); // 这里直接调用update获取最新状态
    
    // 获取总按键数量
    data.totalButtons = WEBCONFIG_BTNS_MANAGER.getTotalButtonCount();
    
    // 保留字节清零
    data.reserved[0] = 0;
    data.reserved[1] = 0;
    
    return data;
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
WebSocketDownstreamMessage CommonCommandHandler::handleStartButtonMonitoring(const WebSocketUpstreamMessage& request) {
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
WebSocketDownstreamMessage CommonCommandHandler::handleStopButtonMonitoring(const WebSocketUpstreamMessage& request) {
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

// ============================================================================
// 命令路由处理
// ============================================================================

WebSocketDownstreamMessage CommonCommandHandler::handle(const WebSocketUpstreamMessage& request) {
    const std::string& command = request.getCommand();
    
    APP_DBG("CommonCommandHandler::handle command: %s", command.c_str());

    // 按键监控相关命令
    if (command == "start_button_monitoring") {
        return handleStartButtonMonitoring(request);
    } else if (command == "stop_button_monitoring") {
        return handleStopButtonMonitoring(request);
    } else if (command == "start_button_performance_monitoring") {
        return handleStartButtonPerformanceMonitoring(request);
    } else if (command == "stop_button_performance_monitoring") {
        return handleStopButtonPerformanceMonitoring(request);
    }
    
    return create_error_response(request.getCid(), command, -1, "Unknown common command");
}

/**
 * @brief 构建按键性能监控的二进制数据
 * @return 完整的二进制数据
 */
std::vector<uint8_t> CommonCommandHandler::buildButtonPerformanceMonitoringBinaryData() {
    // 收集所有ADC按键的状态
    std::vector<ButtonPerformanceData> buttonDataList;
    
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        // 获取按钮详细信息
        uint16_t currentAdcValue, pressStartValue, releaseStartValue;
        float triggerDistance, pressStartValueDistance, releaseStartValueDistance;
        
        if (ADC_BTNS_WORKER.getButtonDetails(i, currentAdcValue, triggerDistance, pressStartValue, releaseStartValue, pressStartValueDistance, releaseStartValueDistance)) {
            // 获取虚拟引脚
            uint8_t virtualPin = ADC_BTNS_WORKER.getButtonVirtualPin(i);
            if (virtualPin != 0xFF) {
                // 检查按键是否按下
                bool isPressed = (WEBCONFIG_BTNS_MANAGER.getCurrentMask() & (1U << virtualPin)) != 0;
                
                // 构造按键性能数据
                ButtonPerformanceData buttonData;
                buttonData.buttonIndex = i;
                buttonData.virtualPin = virtualPin;
                buttonData.adcValue = currentAdcValue;
                buttonData.triggerDistance = triggerDistance;
                buttonData.pressStartValue = pressStartValue;
                buttonData.releaseStartValue = releaseStartValue;
                buttonData.pressStartValueDistance = pressStartValueDistance;
                buttonData.releaseStartValueDistance = releaseStartValueDistance;
                buttonData.isPressed = isPressed ? 1 : 0;
                buttonData.reserved = 0;
                
                buttonDataList.push_back(buttonData);
            }
        }
    }
    
    // 计算总数据大小：头部 + 按键数据数组
    size_t totalSize = sizeof(ButtonPerformanceMonitoringBinaryData) + 
                      buttonDataList.size() * sizeof(ButtonPerformanceData);
    
    // 创建完整的数据缓冲区
    std::vector<uint8_t> buffer(totalSize);
    uint8_t* dataPtr = buffer.data();
    
    // 构建头部数据
    ButtonPerformanceMonitoringBinaryData header;
    header.command = BUTTON_PERFORMANCE_MONITORING_CMD;
    header.isActive = WEBCONFIG_BTNS_MANAGER.isActive() ? 1 : 0;
    header.buttonCount = static_cast<uint8_t>(buttonDataList.size());
    header.reserved = 0;
    header.timestamp = HAL_GetTick();
    
    // 复制头部数据
    memcpy(dataPtr, &header, sizeof(ButtonPerformanceMonitoringBinaryData));
    dataPtr += sizeof(ButtonPerformanceMonitoringBinaryData);
    
    // 复制按键数据
    for (const auto& buttonData : buttonDataList) {
        memcpy(dataPtr, &buttonData, sizeof(ButtonPerformanceData));
        dataPtr += sizeof(ButtonPerformanceData);
    }
    
    return buffer;
}

/**
 * @brief 启动按键性能监控（包含测试模式）
 * WebSocket命令: start_button_performance_monitoring
 * 
 * WebSocket命令格式:
 * {
 *   "cid": 9,
 *   "command": "start_button_performance_monitoring",
 *   "params": {}
 * }
 * 
 * 响应格式:
 * {
 *   "cid": 9,
 *   "command": "start_button_performance_monitoring",
 *   "errNo": 0,
 *   "data": {
 *     "message": "Button performance monitoring started successfully",
 *     "status": "active",
 *     "isActive": true,
 *     "isTestModeEnabled": true
 *   }
 * }
 */
WebSocketDownstreamMessage CommonCommandHandler::handleStartButtonPerformanceMonitoring(const WebSocketUpstreamMessage& request) {
    // 获取按键管理器实例
    WebConfigBtnsManager& btnsManager = WEBCONFIG_BTNS_MANAGER;
    
    // 启动按键工作器
    btnsManager.startButtonWorkers();
    
    // 启用测试模式
    btnsManager.enableTestMode(true);
    
    // 验证启动状态
    if (!btnsManager.isActive()) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to start button performance monitoring");
    }
    
    // 验证测试模式状态
    if (!btnsManager.isTestModeEnabled()) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to enable test mode");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    if (!dataJSON) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to create JSON object");
    }
    
    cJSON_AddStringToObject(dataJSON, "message", "Button performance monitoring started successfully");
    cJSON_AddStringToObject(dataJSON, "status", "active");
    cJSON_AddBoolToObject(dataJSON, "isActive", btnsManager.isActive());
    cJSON_AddBoolToObject(dataJSON, "isTestModeEnabled", btnsManager.isTestModeEnabled());
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

/**
 * @brief 停止按键性能监控
 * WebSocket命令: stop_button_performance_monitoring
 * 
 * WebSocket命令格式:
 * {
 *   "cid": 10,
 *   "command": "stop_button_performance_monitoring",
 *   "params": {}
 * }
 * 
 * 响应格式:
 * {
 *   "cid": 10,
 *   "command": "stop_button_performance_monitoring",
 *   "errNo": 0,
 *   "data": {
 *     "message": "Button performance monitoring stopped successfully",
 *     "status": "inactive",
 *     "isActive": false,
 *     "isTestModeEnabled": false
 *   }
 * }
 */
WebSocketDownstreamMessage CommonCommandHandler::handleStopButtonPerformanceMonitoring(const WebSocketUpstreamMessage& request) {
    // 获取按键管理器实例
    WebConfigBtnsManager& btnsManager = WEBCONFIG_BTNS_MANAGER;
    
    // 禁用测试模式
    btnsManager.enableTestMode(false);
    
    // 停止按键工作器
    btnsManager.stopButtonWorkers();
    
    // 验证停止状态
    if (btnsManager.isActive()) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to stop button performance monitoring");
    }
    
    // 验证测试模式状态
    if (btnsManager.isTestModeEnabled()) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to disable test mode");
    }
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    if (!dataJSON) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to create JSON object");
    }
    
    cJSON_AddStringToObject(dataJSON, "message", "Button performance monitoring stopped successfully");
    cJSON_AddStringToObject(dataJSON, "status", "inactive");
    cJSON_AddBoolToObject(dataJSON, "isActive", btnsManager.isActive());
    cJSON_AddBoolToObject(dataJSON, "isTestModeEnabled", btnsManager.isTestModeEnabled());
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}
