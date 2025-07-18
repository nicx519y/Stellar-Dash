#include "configs/websocket_command_handler.hpp"
#include "storagemanager.hpp"
#include "system_logger.h"
#include "configs/calibration_command_handler.hpp"
#include "configs/firmware_command_handler.hpp"
#include "configs/common_command_handler.hpp"
#include <map>

// ============================================================================
// WebSocketCommandHandler 静态成员变量定义
// ============================================================================
bool WebSocketCommandHandler::needReboot = false;
uint32_t WebSocketCommandHandler::rebootTick = 0;

// ============================================================================
// WebSocketCommandHandler 基类实现
// ============================================================================

WebSocketDownstreamMessage WebSocketCommandHandler::create_error_response(
    int cid, const std::string& command, int errNo, const std::string& errorMessage) {
    return create_websocket_response(cid, command, errNo, nullptr, errorMessage.c_str());
}

WebSocketDownstreamMessage WebSocketCommandHandler::create_success_response(
    int cid, const std::string& command, cJSON* data) {
    return create_websocket_response(cid, command, 0, data);
}

// ============================================================================
// WebSocketCommandManager 实现
// ============================================================================

WebSocketCommandManager& WebSocketCommandManager::getInstance() {
    static WebSocketCommandManager instance;
    return instance;
}

void WebSocketCommandManager::registerHandler(const std::string& command, WebSocketCommandHandler* handler) {
    handlers[command] = handler;
}

WebSocketDownstreamMessage WebSocketCommandManager::processCommand(const WebSocketUpstreamMessage& request) {
    const std::string& command = request.getCommand();
    
    auto it = handlers.find(command);
    if (it != handlers.end()) {
        return it->second->handle(request);
    }
    
    // 未找到处理器，返回错误响应
    LOG_WARN("processCommand:WebSocket", "Unknown command: %s", command.c_str());
    APP_DBG("processCommand:WebSocket: Unknown command: %s", command.c_str());
    return create_websocket_response(request.getCid(), command, -1, nullptr, "Unknown command");
}

void WebSocketCommandManager::initializeHandlers() {
    // 获取处理器实例
    GlobalConfigCommandHandler& globalHandler = GlobalConfigCommandHandler::getInstance();
    ProfileCommandHandler& profileHandler = ProfileCommandHandler::getInstance();
    MSMarkCommandHandler& msMarkHandler = MSMarkCommandHandler::getInstance();
    CalibrationCommandHandler& calibrationHandler = CalibrationCommandHandler::getInstance();
    FirmwareCommandHandler& firmwareHandler = FirmwareCommandHandler::getInstance();
    CommonCommandHandler& commonHandler = CommonCommandHandler::getInstance();
    
    // 注册全局配置相关命令
    registerHandler("get_global_config", &globalHandler);
    registerHandler("update_global_config", &globalHandler);
    registerHandler("get_hotkeys_config", &globalHandler);
    registerHandler("update_hotkeys_config", &globalHandler);
    registerHandler("reboot", &globalHandler);
    
    // 注册LED配置相关命令
    registerHandler("push_leds_config", &globalHandler);
    registerHandler("clear_leds_preview", &globalHandler);
    
    // 注册配置文件相关命令
    registerHandler("get_profile_list", &profileHandler);
    registerHandler("get_default_profile", &profileHandler);
    registerHandler("update_profile", &profileHandler);
    registerHandler("create_profile", &profileHandler);
    registerHandler("delete_profile", &profileHandler);
    registerHandler("switch_default_profile", &profileHandler);
    
    // 注册轴体映射相关命令
    registerHandler("ms_get_list", &msMarkHandler);
    registerHandler("ms_get_mark_status", &msMarkHandler);
    registerHandler("ms_set_default", &msMarkHandler);
    registerHandler("ms_get_default", &msMarkHandler);
    registerHandler("ms_create_mapping", &msMarkHandler);
    registerHandler("ms_delete_mapping", &msMarkHandler);
    registerHandler("ms_rename_mapping", &msMarkHandler);
    registerHandler("ms_mark_mapping_start", &msMarkHandler);
    registerHandler("ms_mark_mapping_stop", &msMarkHandler);
    registerHandler("ms_mark_mapping_step", &msMarkHandler);
    registerHandler("ms_get_mapping", &msMarkHandler);
    
    // 注册校准相关命令
    registerHandler("start_manual_calibration", &calibrationHandler);
    registerHandler("stop_manual_calibration", &calibrationHandler);
    registerHandler("get_calibration_status", &calibrationHandler);
    registerHandler("clear_manual_calibration_data", &calibrationHandler);
    
    // 注册按键监控相关命令
    registerHandler("start_button_monitoring", &commonHandler);
    registerHandler("stop_button_monitoring", &commonHandler);
    registerHandler("get_button_states", &commonHandler);
    
    // 注册固件相关命令
    registerHandler("get_device_auth", &firmwareHandler);
    registerHandler("get_firmware_metadata", &firmwareHandler);
    
    // LOG_INFO("WebSocket", "WebSocket command handlers initialized successfully");
} 