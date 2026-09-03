#include "configs/common_command_handler.hpp"
#include "board_cfg.h"
#include "system_logger.h"
#include "adc_btns/adc_calibration.hpp"
#include "configs/webconfig_btns_manager.hpp"
#include "config_transport_sink.hpp"

// 获取按键管理器实例  
#define WEBCONFIG_BTNS_MANAGER WebConfigBtnsManager::getInstance()
// 获取校准管理器实例
#define ADC_CALIBRATION_MANAGER ADCCalibrationManager::getInstance()

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
 * @brief 推送按键状态变化通知（二进制格式）
 */
void CommonCommandHandler::sendButtonStateNotification() {
    // 构建二进制按键状态数据
    ButtonStateBinaryData binaryData = buildButtonStateBinaryData();
    
    ConfigTransport_PublishBinary(
        reinterpret_cast<const uint8_t*>(&binaryData),
        sizeof(ButtonStateBinaryData));
    
    APP_DBG("Button state binary notification sent to all clients (cmd=%d, active=%d, mask=0x%08X, total=%d)", 
            binaryData.command, binaryData.isActive, binaryData.triggerMask, binaryData.totalButtons);
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
 * DeviceCommand命令格式:
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
DeviceCommandResponse CommonCommandHandler::handleStartButtonMonitoring(const DeviceCommandRequest& request) {
    // 获取按键管理器实例并开始监控
    WebConfigBtnsManager& btnsManager = WEBCONFIG_BTNS_MANAGER;

    if (ADC_CALIBRATION_MANAGER.isCalibrationActive()) {
        return create_error_response(request.getCid(), request.getCommand(), 2, "Calibration is active, button monitoring is not allowed");
    }

    if (!ADC_CALIBRATION_MANAGER.isAllButtonsCalibrated(false)) {
        return create_error_response(request.getCid(), request.getCommand(), 2, "Manual calibration is not completed, button monitoring is not allowed");
    }

    btnsManager.enableTestMode(false);
    
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
 * DeviceCommand命令格式:
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
DeviceCommandResponse CommonCommandHandler::handleStopButtonMonitoring(const DeviceCommandRequest& request) {
    // 获取按键管理器实例
    WebConfigBtnsManager& btnsManager = WEBCONFIG_BTNS_MANAGER;

    btnsManager.enableTestMode(false);
    
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

DeviceCommandResponse CommonCommandHandler::handleGetButtonStates(
    const DeviceCommandRequest& request)
{
    const ButtonStateBinaryData snapshot = buildButtonStateBinaryData();
    cJSON *dataJSON = cJSON_CreateObject();
    if (dataJSON == nullptr) {
        return create_error_response(
            request.getCid(),
            request.getCommand(),
            1,
            "Failed to create button-state snapshot");
    }
    cJSON_AddBoolToObject(
        dataJSON, "isActive", snapshot.isActive != 0u);
    cJSON_AddNumberToObject(
        dataJSON, "triggerMask", snapshot.triggerMask);
    cJSON_AddNumberToObject(
        dataJSON, "totalButtons", snapshot.totalButtons);
    return create_success_response(
        request.getCid(), request.getCommand(), dataJSON);
}

// ============================================================================
// 命令路由处理
// ============================================================================

DeviceCommandResponse CommonCommandHandler::handle(const DeviceCommandRequest& request) {
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
    } else if (command == "get_device_logs_list") {
        return handleGetDeviceLogsList(request);
    } else if (command == "get_hitbox_layout") {
        return handleGetHitboxLayout(request);
    } else if (command == "get_button_states") {
        return handleGetButtonStates(request);
    }
    
    return create_error_response(request.getCid(), command, -1, "Unknown common command");
}



/**
 * @brief 启动按键性能监控（包含测试模式）
 * DeviceCommand命令: start_button_performance_monitoring
 * 
 * DeviceCommand命令格式:
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
DeviceCommandResponse CommonCommandHandler::handleStartButtonPerformanceMonitoring(const DeviceCommandRequest& request) {
    // 获取按键管理器实例
    WebConfigBtnsManager& btnsManager = WEBCONFIG_BTNS_MANAGER;

    if (ADC_CALIBRATION_MANAGER.isCalibrationActive()) {
        return create_error_response(request.getCid(), request.getCommand(), 2, "Calibration is active, button performance monitoring is not allowed");
    }

    if (!ADC_CALIBRATION_MANAGER.isAllButtonsCalibrated(false)) {
        return create_error_response(request.getCid(), request.getCommand(), 2, "Manual calibration is not completed, button performance monitoring is not allowed");
    }
    
    // 启动按键工作器
    if (!btnsManager.startButtonWorkers()) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to start button performance monitoring");
    }
    
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
        btnsManager.enableTestMode(false);
        btnsManager.stopButtonWorkers();
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to create JSON object");
    }
    
    cJSON_AddStringToObject(dataJSON, "message", "Button performance monitoring started successfully");
    cJSON_AddStringToObject(dataJSON, "status", "active");
    cJSON_AddBoolToObject(dataJSON, "isActive", btnsManager.isActive());
    cJSON_AddBoolToObject(dataJSON, "isTestModeEnabled", btnsManager.isTestModeEnabled());
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

/**
 * @brief 获取设备日志列表（从Flash读取，按时间倒序，限制条数）
 * DeviceCommand命令: get_device_logs_list
 *
 * 请求示例:
 * {
 *   "cid": 21,
 *   "command": "get_device_logs_list",
 *   "params": { "limit": 200 }
 * }
 *
 * 响应示例:
 * {
 *   "cid": 21,
 *   "command": "get_device_logs_list",
 *   "errNo": 0,
 *   "data": {
 *     "count": 120,
 *     "items": ["[...]"]
 *   }
 * }
 */
DeviceCommandResponse CommonCommandHandler::handleGetDeviceLogsList(const DeviceCommandRequest& request) {
    // 固定仅返回最近50条日志
    uint32_t limit = 50;

    // 先尝试刷新缓冲到Flash，确保包含最近日志
    (void)Logger_Flush();

    uint32_t sectorArray[LOG_FLASH_SECTOR_COUNT] = {0};
    uint32_t sectorCount = 0;
    LogResult r = Logger_GetSortedSectors(sectorArray, &sectorCount);
    if (r != LOG_RESULT_SUCCESS) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to enumerate log sectors");
    }

    cJSON* dataJSON = cJSON_CreateObject();
    if (!dataJSON) {
        return create_error_response(request.getCid(), request.getCommand(), 2, "Failed to create JSON object");
    }
    cJSON* itemsArray = cJSON_CreateArray();
    if (!itemsArray) {
        cJSON_Delete(dataJSON);
        return create_error_response(request.getCid(), request.getCommand(), 2, "Failed to create JSON array");
    }

    uint32_t collected = 0;
    // 从最新的扇区开始向前收集，按条目倒序以保证整体新→旧
    for (int i = static_cast<int>(sectorCount) - 1; i >= 0 && collected < limit; --i) {
        uint32_t sectorIndex = sectorArray[i];
        // 改为使用堆内存，避免因大数组导致的栈溢出
        LogEntry* entries = (LogEntry*)malloc(LOG_ENTRIES_PER_SECTOR * sizeof(LogEntry));
        if (!entries) {
            // 无法分配足够内存，直接跳过该扇区
            continue;
        }
        uint32_t entryCount = 0;
        LogResult lr = Logger_GetSectorLogs(sectorIndex, entries, &entryCount);
        if (lr != LOG_RESULT_SUCCESS) {
            free(entries);
            continue; // 跳过异常扇区
        }
        for (int j = static_cast<int>(entryCount) - 1; j >= 0 && collected < limit; --j) {
            // 仅返回 WARN 和 ERROR 级别的日志
            const char* entry = entries[j];
            if (entry && (
                strstr(entry, "[WARN]") != nullptr ||
                strstr(entry, "[ERROR]") != nullptr ||
                strstr(entry, "[FATAL]") != nullptr
            )) {
                cJSON_AddItemToArray(itemsArray, cJSON_CreateString(entry));
                collected++;
            }
        }
        free(entries);
    }

    cJSON_AddItemToObject(dataJSON, "items", itemsArray);
    cJSON_AddNumberToObject(dataJSON, "count", static_cast<double>(collected));

    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

/**
 * @brief 停止按键性能监控
 * DeviceCommand命令: stop_button_performance_monitoring
 * 
 * DeviceCommand命令格式:
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
DeviceCommandResponse CommonCommandHandler::handleStopButtonPerformanceMonitoring(const DeviceCommandRequest& request) {
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

/**
 * @brief 获取Hitbox按键布局
 * DeviceCommand命令: get_hitbox_layout
 *
 * 响应格式:
 * {
 *   "cid": xx,
 *   "command": "get_hitbox_layout",
 *   "errNo": 0,
 *   "data": [
 *     { "x": 125.10, "y": 103.10, "r": 26.00 },
 *     ...
 *   ]
 * }
 */
DeviceCommandResponse CommonCommandHandler::handleGetHitboxLayout(const DeviceCommandRequest& request) {
    cJSON* dataJSON = cJSON_CreateArray();
    if (!dataJSON) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to create JSON array");
    }

    size_t count = sizeof(HITBOX_BUTTON_POS_LIST) / sizeof(HITBOX_BUTTON_POS_LIST[0]);
    for (size_t i = 0; i < count; i++) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "x", HITBOX_BUTTON_POS_LIST[i].x);
        cJSON_AddNumberToObject(item, "y", HITBOX_BUTTON_POS_LIST[i].y);
        cJSON_AddNumberToObject(item, "r", HITBOX_BUTTON_POS_LIST[i].r);
        cJSON_AddItemToArray(dataJSON, item);
    }

    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}
