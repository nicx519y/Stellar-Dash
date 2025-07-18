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
        callbackSet = true;
    }
    
    return instance;
}

// ============================================================================
// 推送功能实现
// ============================================================================

/**
 * @brief 推送按键状态变化通知
 */
void CommonCommandHandler::sendButtonStateNotification() {
    // 获取WebSocket服务器实例
    WebSocketServer& server = WebSocketServer::getInstance();
    
    // 构建按键状态数据
    cJSON* notificationData = cJSON_CreateObject();
    cJSON* buttonStatesJSON = buildButtonStatesJSON();
    
    cJSON_AddItemToObject(notificationData, "buttonStates", buttonStatesJSON);
    cJSON_AddStringToObject(notificationData, "type", "button_state_update");
    cJSON_AddNumberToObject(notificationData, "timestamp", HAL_GetTick());
    
    // 创建通知消息（无CID的消息表示这是服务器主动推送）
    cJSON* notification = cJSON_CreateObject();
    cJSON_AddStringToObject(notification, "command", "button_state_changed");
    cJSON_AddNumberToObject(notification, "errNo", 0);
    cJSON_AddItemToObject(notification, "data", notificationData);
    
    // 转换为JSON字符串
    char* notificationString = cJSON_PrintUnformatted(notification);
    if (notificationString) {
        // 广播给所有连接的客户端
        server.broadcast_text(std::string(notificationString));
        
        // APP_DBG("Button state notification sent to all clients");
        
        // 释放JSON字符串内存
        free(notificationString);
    } else {
        LOG_ERROR("WebSocket", "Failed to serialize button state notification");
    }
    
    // 清理JSON对象
    cJSON_Delete(notification);
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

/**
 * @brief 获取按键状态（保留用于兼容，推荐使用推送模式）
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
WebSocketDownstreamMessage CommonCommandHandler::handleGetButtonStates(const WebSocketUpstreamMessage& request) {
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

WebSocketDownstreamMessage CommonCommandHandler::handle(const WebSocketUpstreamMessage& request) {
    const std::string& command = request.getCommand();
    
    // 按键监控相关命令
    if (command == "start_button_monitoring") {
        return handleStartButtonMonitoring(request);
    } else if (command == "stop_button_monitoring") {
        return handleStopButtonMonitoring(request);
    } else if (command == "get_button_states") {
        return handleGetButtonStates(request);
    }
    
    return create_error_response(request.getCid(), command, -1, "Unknown common command");
}

// ============================================================================
// 辅助函数实现
// ============================================================================

/**
 * @brief 构建按键状态的JSON对象
 * @return cJSON* 按键状态JSON对象
 */
cJSON* CommonCommandHandler::buildButtonStatesJSON() {
    // 获取按键管理器实例
    WebConfigBtnsManager& btnsManager = WEBCONFIG_BTNS_MANAGER;
    
    // 检查按键工作器是否活跃
    if (!btnsManager.isActive()) {
        // 返回空状态
        cJSON* dataJSON = cJSON_CreateObject();
        if (dataJSON) {
            cJSON_AddNumberToObject(dataJSON, "triggerMask", 0);
            cJSON_AddStringToObject(dataJSON, "triggerBinary", "00000000");
            cJSON_AddNumberToObject(dataJSON, "totalButtons", btnsManager.getTotalButtonCount());
            cJSON_AddNumberToObject(dataJSON, "timestamp", HAL_GetTick());
            cJSON_AddBoolToObject(dataJSON, "isActive", false);
        }
        return dataJSON;
    }
    
    // 获取并清空触发掩码
    uint32_t triggerMask = btnsManager.getAndClearTriggerMask();
    uint8_t totalButtons = btnsManager.getTotalButtonCount();
    
    // 创建按键状态数据
    cJSON* dataJSON = cJSON_CreateObject();
    if (!dataJSON) {
        return nullptr;
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
    
    // 添加活跃状态
    cJSON_AddBoolToObject(dataJSON, "isActive", true);
    
    return dataJSON;
} 