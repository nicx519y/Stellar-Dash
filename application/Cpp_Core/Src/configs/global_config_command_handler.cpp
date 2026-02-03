#include "configs/websocket_command_handler.hpp"
#include "storagemanager.hpp"
#include "adc_btns/adc_calibration.hpp"
#include "webconfig_leds_manager.hpp"
#include "webconfig_btns_manager.hpp"
#include "system_logger.h"
#include "config.hpp"
#include <map>

// ============================================================================
// GlobalConfigCommandHandler 实现
// ============================================================================

GlobalConfigCommandHandler& GlobalConfigCommandHandler::getInstance() {
    static GlobalConfigCommandHandler instance;
    return instance;
}

WebSocketDownstreamMessage GlobalConfigCommandHandler::handleGetGlobalConfig(const WebSocketUpstreamMessage& request) {
    // LOG_INFO("WebSocket", "Handling get_global_config command, cid: %d", request.getCid());

    Config& config = Storage::getInstance().config;
    
    // 创建返回数据结构
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* globalConfigJSON = cJSON_CreateObject();
    
    // 使用ConfigUtils获取输入模式字符串
    const char* modeStr = ConfigUtils::getInputModeString(config.inputMode);
    cJSON_AddStringToObject(globalConfigJSON, "inputMode", modeStr);
    
    // 添加自动校准模式状态
    cJSON_AddBoolToObject(globalConfigJSON, "autoCalibrationEnabled", config.autoCalibrationEnabled);
    
    // 添加手动校准状态
    cJSON_AddBoolToObject(globalConfigJSON, "manualCalibrationActive", ADC_CALIBRATION_MANAGER.isCalibrationActive());
    
    // 构建返回结构
    cJSON_AddItemToObject(dataJSON, "globalConfig", globalConfigJSON);
    
    // LOG_INFO("WebSocket", "get_global_config command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage GlobalConfigCommandHandler::handleUpdateGlobalConfig(const WebSocketUpstreamMessage& request) {
    // LOG_INFO("WebSocket", "Handling update_global_config command, cid: %d", request.getCid());
    
    Config& config = Storage::getInstance().config;
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("WebSocket", "update_global_config: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    // 更新全局配置
    cJSON* globalConfig = cJSON_GetObjectItem(params, "globalConfig");
    if (globalConfig) {
        // 更新输入模式
        cJSON* inputModeItem = cJSON_GetObjectItem(globalConfig, "inputMode");
        if (inputModeItem && cJSON_IsString(inputModeItem)) {
            std::string modeStr = inputModeItem->valuestring;
            config.inputMode = ConfigUtils::getInputModeFromString(modeStr.c_str());
        }
        
        // 更新自动校准模式
        cJSON* autoCalibrationItem = cJSON_GetObjectItem(globalConfig, "autoCalibrationEnabled");
        if (autoCalibrationItem) {
            config.autoCalibrationEnabled = cJSON_IsTrue(autoCalibrationItem);
        }
    }

    // 保存配置
    if (!STORAGE_MANAGER.saveConfig()) {
        LOG_ERROR("WebSocket", "update_global_config: Failed to save configuration");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to save configuration");
    }

    // 返回更新后的配置
    return handleGetGlobalConfig(request);
}

WebSocketDownstreamMessage GlobalConfigCommandHandler::handleGetHotkeysConfig(const WebSocketUpstreamMessage& request) {
    // LOG_INFO("WebSocket", "Handling get_hotkeys_config command, cid: %d", request.getCid());
    
    Config& config = Storage::getInstance().config;
    
    // 创建返回数据结构
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* hotkeysConfigJSON = ConfigUtils::buildHotkeysConfigJSON(config);
    
    if (!hotkeysConfigJSON) {
        LOG_ERROR("WebSocket", "get_hotkeys_config: Failed to build hotkeys config JSON");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to build hotkeys config JSON");
    }

    // 构建返回结构
    cJSON_AddItemToObject(dataJSON, "hotkeysConfig", hotkeysConfigJSON);
    
    // LOG_INFO("WebSocket", "get_hotkeys_config command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage GlobalConfigCommandHandler::handleUpdateHotkeysConfig(const WebSocketUpstreamMessage& request) {
    // LOG_INFO("WebSocket", "Handling update_hotkeys_config command, cid: %d", request.getCid());
    
    Config& config = Storage::getInstance().config;
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("WebSocket", "update_hotkeys_config: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }
    
    // 获取快捷键配置组
    cJSON* hotkeysConfigArray = cJSON_GetObjectItem(params, "hotkeysConfig");
    if (!hotkeysConfigArray || !cJSON_IsArray(hotkeysConfigArray)) {
        LOG_ERROR("WebSocket", "update_hotkeys_config: Invalid hotkeys configuration");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid hotkeys configuration");
    }

    // 遍历并更新每个快捷键配置
    int numHotkeys = cJSON_GetArraySize(hotkeysConfigArray);
    for (int i = 0; i < numHotkeys && i < NUM_GAMEPAD_HOTKEYS; i++) {
        cJSON* hotkeyItem = cJSON_GetArrayItem(hotkeysConfigArray, i);
        if (!hotkeyItem) continue;

        // 获取快捷键序号
        cJSON* keyItem = cJSON_GetObjectItem(hotkeyItem, "key");
        if (!keyItem || !cJSON_IsNumber(keyItem)) continue;
        int keyIndex = keyItem->valueint;
        if (keyIndex < -1 || keyIndex >= (NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS)) continue;
        config.hotkeys[i].virtualPin = keyIndex;

        // 获取动作
        cJSON* actionItem = cJSON_GetObjectItem(hotkeyItem, "action");
        if (actionItem && cJSON_IsString(actionItem)) {
            config.hotkeys[i].action = ConfigUtils::getGamepadHotkeyFromString(actionItem->valuestring);
        }

        // 获取锁定状态
        cJSON* isLockedItem = cJSON_GetObjectItem(hotkeyItem, "isLocked");
        if (isLockedItem) {
            config.hotkeys[i].isLocked = cJSON_IsTrue(isLockedItem);
        }

        // 获取是否长按
        cJSON* isHoldItem = cJSON_GetObjectItem(hotkeyItem, "isHold");
        if (isHoldItem) {
            config.hotkeys[i].isHold = cJSON_IsTrue(isHoldItem);
        }
    }

    // 保存配置
    if (!STORAGE_MANAGER.saveConfig()) {
        LOG_ERROR("WebSocket", "update_hotkeys_config: Failed to save configuration");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to save configuration");
    }

    // 返回更新后的配置
    return handleGetHotkeysConfig(request);
}

WebSocketDownstreamMessage GlobalConfigCommandHandler::handleExportAllConfig(const WebSocketUpstreamMessage& request) {
    Config& config = Storage::getInstance().config;
    
    cJSON* exportJSON = ConfigUtils::toJSON(config);
    if (!exportJSON) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to export configuration");
    }

    return create_success_response(request.getCid(), request.getCommand(), exportJSON);
}

WebSocketDownstreamMessage GlobalConfigCommandHandler::handleImportAllConfig(const WebSocketUpstreamMessage& request) {
    Config& config = Storage::getInstance().config;
    cJSON* params = request.getParams();
    
    if (!params) {
        LOG_ERROR("WebSocket", "import_all_config: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    if (!ConfigUtils::fromJSON(config, params)) {
         LOG_ERROR("WebSocket", "import_all_config: Failed to parse configuration");
         return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to parse configuration");
    }

    if (!STORAGE_MANAGER.saveConfig()) {
        LOG_ERROR("WebSocket", "import_all_config: Failed to save configuration");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to save configuration");
    }

    // cJSON* exportJSON = ConfigUtils::toJSON(config);
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "message", "Configuration imported successfully");
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage GlobalConfigCommandHandler::handleReboot(const WebSocketUpstreamMessage& request) {
    // LOG_INFO("WebSocket", "Handling reboot command, cid: %d", request.getCid());
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "message", "System is rebooting");
    
    STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
    STORAGE_MANAGER.saveConfig();

    // 设置延迟重启时间
    WebSocketCommandHandler::rebootTick = HAL_GetTick() + 2000;
    WebSocketCommandHandler::needReboot = true;
    
    // LOG_INFO("WebSocket", "reboot command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage GlobalConfigCommandHandler::handlePushLedsConfig(const WebSocketUpstreamMessage& request) {
    // LOG_INFO("WebSocket", "Handling push_leds_config command, cid: %d", request.getCid());
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("WebSocket", "push_leds_config: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    // 获取当前默认配置文件作为基础
    const GamepadProfile* currentProfile = STORAGE_MANAGER.getDefaultGamepadProfile();
    if (!currentProfile) {
        LOG_ERROR("WebSocket", "push_leds_config: Failed to get current profile");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to get current profile");
    }

    // 创建临时的LED配置，基于当前配置
    LEDProfile tempLedsConfig = currentProfile->ledsConfigs;
    
    // 解析前端传来的配置参数
    cJSON* item;
    
    // LED 启用状态
    if ((item = cJSON_GetObjectItem(params, "ledEnabled"))) {
        tempLedsConfig.ledEnabled = cJSON_IsTrue(item);
    }
    
    // LED 特效类型
    if ((item = cJSON_GetObjectItem(params, "ledsEffectStyle")) 
        && cJSON_IsNumber(item) 
        && item->valueint >= 0 
        && item->valueint < LEDEffect::NUM_EFFECTS) {
        tempLedsConfig.ledEffect = static_cast<LEDEffect>(item->valueint);
    }
    
    // LED 亮度
    if ((item = cJSON_GetObjectItem(params, "ledBrightness")) 
        && cJSON_IsNumber(item)
        && item->valueint >= 0 
        && item->valueint <= 100) {
        tempLedsConfig.ledBrightness = item->valueint;
    }
    
    // LED 动画速度
    if ((item = cJSON_GetObjectItem(params, "ledAnimationSpeed")) 
        && cJSON_IsNumber(item)
        && item->valueint >= 1 
        && item->valueint <= 5) {
        tempLedsConfig.ledAnimationSpeed = item->valueint;
    }
    
    // LED 颜色数组
    cJSON* ledColors = cJSON_GetObjectItem(params, "ledColors");
    if (ledColors && cJSON_IsArray(ledColors) && cJSON_GetArraySize(ledColors) >= 3) {
        cJSON* color1 = cJSON_GetArrayItem(ledColors, 0);
        cJSON* color2 = cJSON_GetArrayItem(ledColors, 1);
        cJSON* color3 = cJSON_GetArrayItem(ledColors, 2);
        
        if (color1 && cJSON_IsString(color1)) {
            sscanf(color1->valuestring, "#%lx", &tempLedsConfig.ledColor1);
        }
        if (color2 && cJSON_IsString(color2)) {
            sscanf(color2->valuestring, "#%lx", &tempLedsConfig.ledColor2);
        }
        if (color3 && cJSON_IsString(color3)) {
            sscanf(color3->valuestring, "#%lx", &tempLedsConfig.ledColor3);
        }
    }

    // 氛围灯配置
    if ((item = cJSON_GetObjectItem(params, "aroundLedEnabled"))) {
        tempLedsConfig.aroundLedEnabled = cJSON_IsTrue(item);
    }

    if ((item = cJSON_GetObjectItem(params, "aroundLedSyncToMainLed"))) {
        tempLedsConfig.aroundLedSyncToMainLed = cJSON_IsTrue(item);
    }       

    if ((item = cJSON_GetObjectItem(params, "aroundLedTriggerByButton"))) {
        tempLedsConfig.aroundLedTriggerByButton = cJSON_IsTrue(item);
    }

    if ((item = cJSON_GetObjectItem(params, "aroundLedEffectStyle"))) {
        tempLedsConfig.aroundLedEffect = static_cast<AroundLEDEffect>(item->valueint);
    }

    cJSON* aroundLedColors = cJSON_GetObjectItem(params, "aroundLedColors");
    if (aroundLedColors && cJSON_IsArray(aroundLedColors) && cJSON_GetArraySize(aroundLedColors) >= 3) {
        cJSON* aroundColor1 = cJSON_GetArrayItem(aroundLedColors, 0);
        cJSON* aroundColor2 = cJSON_GetArrayItem(aroundLedColors, 1);
        cJSON* aroundColor3 = cJSON_GetArrayItem(aroundLedColors, 2);
        
        if (aroundColor1 && cJSON_IsString(aroundColor1)) {
            sscanf(aroundColor1->valuestring, "#%lx", &tempLedsConfig.aroundLedColor1);
        }   
        if (aroundColor2 && cJSON_IsString(aroundColor2)) {
            sscanf(aroundColor2->valuestring, "#%lx", &tempLedsConfig.aroundLedColor2);
        }
        if (aroundColor3 && cJSON_IsString(aroundColor3)) {
            sscanf(aroundColor3->valuestring, "#%lx", &tempLedsConfig.aroundLedColor3);
        }
    }

    if ((item = cJSON_GetObjectItem(params, "aroundLedBrightness"))) {
        tempLedsConfig.aroundLedBrightness = item->valueint;
    }
    
    if ((item = cJSON_GetObjectItem(params, "aroundLedAnimationSpeed"))) {
        tempLedsConfig.aroundLedAnimationSpeed = item->valueint;
    }
    
    // 通过WebConfigLedsManager应用预览配置
    WEBCONFIG_LEDS_MANAGER.applyPreviewConfig(tempLedsConfig);
    // 通过WebConfigBtnsManager启动按键工作器
    WEBCONFIG_BTNS_MANAGER.startButtonWorkers();
    
    // 返回成功响应
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "message", "LED configuration applied successfully for preview");
    
    // LOG_INFO("WebSocket", "push_leds_config command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage GlobalConfigCommandHandler::handleClearLedsPreview(const WebSocketUpstreamMessage& request) {
    // LOG_INFO("WebSocket", "Handling clear_leds_preview command, cid: %d", request.getCid());
    
    WEBCONFIG_LEDS_MANAGER.clearPreviewConfig();
    WEBCONFIG_BTNS_MANAGER.stopButtonWorkers();
    
    // 返回成功响应
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "message", "LED preview mode cleared successfully");
    
    // LOG_INFO("WebSocket", "clear_leds_preview command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage GlobalConfigCommandHandler::handle(const WebSocketUpstreamMessage& request) {
    const std::string& command = request.getCommand();
    
    if (command == "get_global_config") {
        return handleGetGlobalConfig(request);
    } else if (command == "update_global_config") {
        return handleUpdateGlobalConfig(request);
    } else if (command == "get_hotkeys_config") {
        return handleGetHotkeysConfig(request);
    } else if (command == "update_hotkeys_config") {
        return handleUpdateHotkeysConfig(request);
    } else if (command == "export_all_config") {
        return handleExportAllConfig(request);
    } else if (command == "import_all_config") {
        return handleImportAllConfig(request);
    } else if (command == "reboot") {
        return handleReboot(request);
    } else if (command == "push_leds_config") {
        return handlePushLedsConfig(request);
    } else if (command == "clear_leds_preview") {
        return handleClearLedsPreview(request);
    }
    
    return create_error_response(request.getCid(), command, -1, "Unknown command");
} 