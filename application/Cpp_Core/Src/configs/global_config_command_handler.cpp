#include "configs/websocket_command_handler.hpp"
#include "storagemanager.hpp"
#include "adc_btns/adc_calibration.hpp"
#include "webconfig_leds_manager.hpp"
#include "webconfig_btns_manager.hpp"
#include "system_logger.h"
#include <map>

// ============================================================================
// GlobalConfigCommandHandler 实现
// ============================================================================

// 静态映射表定义
const std::map<InputMode, const char*> GlobalConfigCommandHandler::INPUT_MODE_STRINGS = {
    {InputMode::INPUT_MODE_XINPUT, "XINPUT"},
    {InputMode::INPUT_MODE_PS4, "PS4"},
    {InputMode::INPUT_MODE_PS5, "PS5"},
    {InputMode::INPUT_MODE_XBOX, "XBOX"},
    {InputMode::INPUT_MODE_SWITCH, "SWITCH"}
};

const std::map<std::string, InputMode> GlobalConfigCommandHandler::STRING_TO_INPUT_MODE = [](){
    std::map<std::string, InputMode> reverse_map;
    for(const auto& pair : INPUT_MODE_STRINGS) {
        reverse_map[pair.second] = pair.first;
    }
    return reverse_map;
}();

const std::map<std::string, GamepadHotkey> GlobalConfigCommandHandler::STRING_TO_GAMEPAD_HOTKEY = {
    {"WebConfigMode", GamepadHotkey::HOTKEY_INPUT_MODE_WEBCONFIG},
    {"NSwitchMode", GamepadHotkey::HOTKEY_INPUT_MODE_SWITCH},
    {"XInputMode", GamepadHotkey::HOTKEY_INPUT_MODE_XINPUT},
    {"PS4Mode", GamepadHotkey::HOTKEY_INPUT_MODE_PS4},
    {"PS5Mode", GamepadHotkey::HOTKEY_INPUT_MODE_PS5},
    {"XBoxMode", GamepadHotkey::HOTKEY_INPUT_MODE_XBOX},
    {"LedsEffectStyleNext", GamepadHotkey::HOTKEY_LEDS_EFFECTSTYLE_NEXT},
    {"LedsEffectStylePrev", GamepadHotkey::HOTKEY_LEDS_EFFECTSTYLE_PREV},
    {"LedsBrightnessUp", GamepadHotkey::HOTKEY_LEDS_BRIGHTNESS_UP},
    {"LedsBrightnessDown", GamepadHotkey::HOTKEY_LEDS_BRIGHTNESS_DOWN},
    {"LedsEnableSwitch", GamepadHotkey::HOTKEY_LEDS_ENABLE_SWITCH},
    {"AmbientLightEffectStyleNext", GamepadHotkey::HOTKEY_AMBIENT_LIGHT_EFFECTSTYLE_NEXT},
    {"AmbientLightEffectStylePrev", GamepadHotkey::HOTKEY_AMBIENT_LIGHT_EFFECTSTYLE_PREV},
    {"AmbientLightBrightnessUp", GamepadHotkey::HOTKEY_AMBIENT_LIGHT_BRIGHTNESS_UP},
    {"AmbientLightBrightnessDown", GamepadHotkey::HOTKEY_AMBIENT_LIGHT_BRIGHTNESS_DOWN},
    {"AmbientLightEnableSwitch", GamepadHotkey::HOTKEY_AMBIENT_LIGHT_ENABLE_SWITCH},
    {"CalibrationMode", GamepadHotkey::HOTKEY_INPUT_MODE_CALIBRATION},
    {"SystemReboot", GamepadHotkey::HOTKEY_SYSTEM_REBOOT}
};

const std::map<GamepadHotkey, const char*> GlobalConfigCommandHandler::GAMEPAD_HOTKEY_TO_STRING = [](){
    std::map<GamepadHotkey, const char*> reverse_map;
    for(const auto& pair : STRING_TO_GAMEPAD_HOTKEY) {
        reverse_map[pair.second] = pair.first.c_str();
    }
    return reverse_map;
}();

GlobalConfigCommandHandler& GlobalConfigCommandHandler::getInstance() {
    static GlobalConfigCommandHandler instance;
    return instance;
}

cJSON* GlobalConfigCommandHandler::buildHotkeysConfigJSON(Config& config) {
    cJSON* hotkeysConfigJSON = cJSON_CreateArray();

    for(uint8_t i = 0; i < NUM_GAMEPAD_HOTKEYS; i++) {
        cJSON* hotkeyJSON = cJSON_CreateObject();
        
        // 添加快捷键动作(转换为字符串)
        auto it = GAMEPAD_HOTKEY_TO_STRING.find(config.hotkeys[i].action);
        if (it != GAMEPAD_HOTKEY_TO_STRING.end()) {
            cJSON_AddStringToObject(hotkeyJSON, "action", it->second);
        } else {
            cJSON_AddStringToObject(hotkeyJSON, "action", "None");
        }

        // 添加快捷键序号
        cJSON_AddNumberToObject(hotkeyJSON, "key", config.hotkeys[i].virtualPin);

        // 添加是否长按
        cJSON_AddBoolToObject(hotkeyJSON, "isHold", config.hotkeys[i].isHold);

        // 添加锁定状态
        cJSON_AddBoolToObject(hotkeyJSON, "isLocked", config.hotkeys[i].isLocked);
        
        // 添加到组
        cJSON_AddItemToArray(hotkeysConfigJSON, hotkeyJSON);
    }

    return hotkeysConfigJSON;
}

WebSocketDownstreamMessage GlobalConfigCommandHandler::handleGetGlobalConfig(const WebSocketUpstreamMessage& request) {
    // LOG_INFO("WebSocket", "Handling get_global_config command, cid: %d", request.getCid());

    Config& config = Storage::getInstance().config;
    
    // 创建返回数据结构
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* globalConfigJSON = cJSON_CreateObject();
    
    // 使用映射表获取输入模式字符串
    const char* modeStr = "XINPUT"; // 默认值
    auto it = INPUT_MODE_STRINGS.find(config.inputMode);
    if (it != INPUT_MODE_STRINGS.end()) {
        modeStr = it->second;
    }
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
            auto it = STRING_TO_INPUT_MODE.find(modeStr);
            if (it != STRING_TO_INPUT_MODE.end()) {
                config.inputMode = it->second;
                // LOG_INFO("WebSocket", "Updated inputMode to: %s", modeStr.c_str());
            } else {
                config.inputMode = InputMode::INPUT_MODE_XINPUT; // 默认值
                LOG_WARN("WebSocket", "Invalid inputMode '%s', using default XINPUT", modeStr.c_str());
            }
        }
        
        // 更新自动校准模式
        cJSON* autoCalibrationItem = cJSON_GetObjectItem(globalConfig, "autoCalibrationEnabled");
        if (autoCalibrationItem) {
            config.autoCalibrationEnabled = cJSON_IsTrue(autoCalibrationItem);
            // LOG_INFO("WebSocket", "Updated autoCalibrationEnabled to: %s", config.autoCalibrationEnabled ? "true" : "false");
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
    cJSON* hotkeysConfigJSON = buildHotkeysConfigJSON(config);
    
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
        if (!actionItem || !cJSON_IsString(actionItem)) continue;

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

        // 根据字符串设置动作
        const char* actionStr = actionItem->valuestring;
        auto it = STRING_TO_GAMEPAD_HOTKEY.find(actionStr);
        if (it != STRING_TO_GAMEPAD_HOTKEY.end()) {
            config.hotkeys[i].action = it->second;
        } else {
            config.hotkeys[i].action = GamepadHotkey::HOTKEY_NONE;
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
    cJSON* exportJSON = cJSON_CreateObject();

    // 1. 全局配置
    cJSON* globalConfigJSON = cJSON_CreateObject();
    const char* modeStr = "XINPUT";
    auto it = INPUT_MODE_STRINGS.find(config.inputMode);
    if (it != INPUT_MODE_STRINGS.end()) {
        modeStr = it->second;
    }
    cJSON_AddStringToObject(globalConfigJSON, "inputMode", modeStr);
    // cJSON_AddBoolToObject(globalConfigJSON, "autoCalibrationEnabled", config.autoCalibrationEnabled);
    // cJSON_AddBoolToObject(globalConfigJSON, "manualCalibrationActive", ADC_CALIBRATION_MANAGER.isCalibrationActive());
    cJSON_AddStringToObject(globalConfigJSON, "defaultProfileId", config.defaultProfileId);
    // cJSON_AddNumberToObject(globalConfigJSON, "numProfilesMax", config.numProfilesMax);

    cJSON_AddItemToObject(exportJSON, "globalConfig", globalConfigJSON);

    // 2. 快捷键配置
    cJSON* hotkeysConfigJSON = buildHotkeysConfigJSON(config);
    cJSON_AddItemToObject(exportJSON, "hotkeysConfig", hotkeysConfigJSON);

    // 3. 所有配置文件
    cJSON* profilesJSON = cJSON_CreateArray();
    for (int i = 0; i < NUM_PROFILES; i++) {
        // 导出所有配置，不仅仅是启用的，或者根据需求。
        // 用户说“所有配置”，通常意味着完整备份。
        // 但buildProfileListJSON只列出启用的。
        // 这里我们导出所有启用的。如果用户想备份未启用的，可能需要逻辑调整。
        // 但目前配置结构是静态数组，enabled只是标记。
        // 假设导出所有启用的配置是合理的。
        if (config.profiles[i].enabled) {
            cJSON* profileJSON = ProfileCommandHandler::buildProfileJSON(&config.profiles[i]);
            if (profileJSON) {
                cJSON_AddItemToArray(profilesJSON, profileJSON);
            }
        }
    }
    cJSON_AddItemToObject(exportJSON, "profiles", profilesJSON);

    return create_success_response(request.getCid(), request.getCommand(), exportJSON);
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
    } else if (command == "reboot") {
        return handleReboot(request);
    } else if (command == "push_leds_config") {
        return handlePushLedsConfig(request);
    } else if (command == "clear_leds_preview") {
        return handleClearLedsPreview(request);
    }
    
    return create_error_response(request.getCid(), command, -1, "Unknown command");
} 