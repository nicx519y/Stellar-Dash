#include "storagemanager.hpp"
#include "configs/websocket_command_handler.hpp"
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

    // 更新快捷键配置
    cJSON* hotkeysConfig = cJSON_GetObjectItem(params, "hotkeysConfig");
    if (hotkeysConfig && cJSON_IsArray(hotkeysConfig)) {
        int index = 0;
        cJSON* hotkeyItem;
        cJSON_ArrayForEach(hotkeyItem, hotkeysConfig) {
            if (index >= NUM_GAMEPAD_HOTKEYS) break;
            
            cJSON* actionItem = cJSON_GetObjectItem(hotkeyItem, "action");
            cJSON* pinItem = cJSON_GetObjectItem(hotkeyItem, "virtualPin");
            cJSON* lockedItem = cJSON_GetObjectItem(hotkeyItem, "isLocked");
            cJSON* holdItem = cJSON_GetObjectItem(hotkeyItem, "isHold");

            if (actionItem && cJSON_IsString(actionItem)) {
                config.hotkeys[index].action = ConfigUtils::getGamepadHotkeyFromString(actionItem->valuestring);
            }
            if (pinItem && cJSON_IsNumber(pinItem)) {
                config.hotkeys[index].virtualPin = pinItem->valueint;
            }
            if (lockedItem) {
                config.hotkeys[index].isLocked = cJSON_IsTrue(lockedItem);
            }
            if (holdItem) {
                config.hotkeys[index].isHold = cJSON_IsTrue(holdItem);
            }
            index++;
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

WebSocketDownstreamMessage GlobalConfigCommandHandler::handleGetScreenControlConfig(const WebSocketUpstreamMessage& request) {
    Config& config = Storage::getInstance().config;

    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* screenControlJSON = cJSON_CreateObject();

    cJSON_AddNumberToObject(screenControlJSON, "brightness", config.screenControl.brightness);
    cJSON_AddNumberToObject(screenControlJSON, "backgroundColor", config.screenControl.backgroundColor);
    cJSON_AddNumberToObject(screenControlJSON, "textColor", config.screenControl.textColor);
    cJSON_AddStringToObject(screenControlJSON, "backgroundImageId", config.screenControl.backgroundImageId);
    cJSON_AddNumberToObject(screenControlJSON, "currentPageId", config.screenControl.currentPageId);

    cJSON* featuresJSON = cJSON_CreateObject();
    cJSON_AddBoolToObject(featuresJSON, "inputModeSwitch", (config.screenControl.featuresMask & SCREEN_FEATURE_INPUT_MODE_SWITCH) != 0);
    cJSON_AddBoolToObject(featuresJSON, "profilesSwitch", (config.screenControl.featuresMask & SCREEN_FEATURE_PROFILES_SWITCH) != 0);
    cJSON_AddBoolToObject(featuresJSON, "socdModeSwitch", (config.screenControl.featuresMask & SCREEN_FEATURE_SOCD_MODE_SWITCH) != 0);
    cJSON_AddBoolToObject(featuresJSON, "tournamentModeSwitch", (config.screenControl.featuresMask & SCREEN_FEATURE_TOURNAMENT_MODE_SWITCH) != 0);
    cJSON_AddBoolToObject(featuresJSON, "ledBrightnessAdjust", (config.screenControl.featuresMask & SCREEN_FEATURE_LED_BRIGHTNESS_ADJUST) != 0);
    cJSON_AddBoolToObject(featuresJSON, "ledEffectSwitch", (config.screenControl.featuresMask & SCREEN_FEATURE_LED_EFFECT_SWITCH) != 0);
    cJSON_AddBoolToObject(featuresJSON, "ambientBrightnessAdjust", (config.screenControl.featuresMask & SCREEN_FEATURE_AMBIENT_BRIGHTNESS_ADJUST) != 0);
    cJSON_AddBoolToObject(featuresJSON, "ambientEffectSwitch", (config.screenControl.featuresMask & SCREEN_FEATURE_AMBIENT_EFFECT_SWITCH) != 0);
    cJSON_AddBoolToObject(featuresJSON, "screenBrightnessAdjust", (config.screenControl.featuresMask & SCREEN_FEATURE_SCREEN_BRIGHTNESS_ADJUST) != 0);
    cJSON_AddBoolToObject(featuresJSON, "webConfigEntry", (config.screenControl.featuresMask & SCREEN_FEATURE_WEB_CONFIG_ENTRY) != 0);
    cJSON_AddBoolToObject(featuresJSON, "calibrationModeSwitch", (config.screenControl.featuresMask & SCREEN_FEATURE_CALIBRATION_MODE_SWITCH) != 0);
    cJSON_AddItemToObject(screenControlJSON, "features", featuresJSON);

    cJSON_AddItemToObject(dataJSON, "screenControl", screenControlJSON);
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage GlobalConfigCommandHandler::handleUpdateScreenControlConfig(const WebSocketUpstreamMessage& request) {
    Config& config = Storage::getInstance().config;

    cJSON* params = request.getParams();
    if (!params) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    cJSON* screenControl = cJSON_GetObjectItem(params, "screenControl");
    if (!screenControl || !cJSON_IsObject(screenControl)) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid screenControl");
    }

    cJSON* item;
    if ((item = cJSON_GetObjectItem(screenControl, "brightness")) && cJSON_IsNumber(item)) {
        int v = item->valueint;
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        config.screenControl.brightness = (uint8_t)v;
    }
    if ((item = cJSON_GetObjectItem(screenControl, "backgroundColor")) && cJSON_IsNumber(item)) {
        config.screenControl.backgroundColor = (uint32_t)item->valuedouble;
    }
    if ((item = cJSON_GetObjectItem(screenControl, "textColor")) && cJSON_IsNumber(item)) {
        config.screenControl.textColor = (uint32_t)item->valuedouble;
    }
    if ((item = cJSON_GetObjectItem(screenControl, "backgroundImageId")) && cJSON_IsString(item)) {
        strncpy(config.screenControl.backgroundImageId, item->valuestring, sizeof(config.screenControl.backgroundImageId) - 1);
        config.screenControl.backgroundImageId[sizeof(config.screenControl.backgroundImageId) - 1] = '\0';
    }
    if ((item = cJSON_GetObjectItem(screenControl, "currentPageId")) && cJSON_IsNumber(item)) {
        int v = item->valueint;
        if (v < 0) v = 0;
        if (v > 65535) v = 65535;
        config.screenControl.currentPageId = (uint16_t)v;
    }

    cJSON* features = cJSON_GetObjectItem(screenControl, "features");
    if (features && cJSON_IsObject(features)) {
        struct { const char* key; uint32_t bit; } map[] = {
            {"inputModeSwitch", SCREEN_FEATURE_INPUT_MODE_SWITCH},
            {"profilesSwitch", SCREEN_FEATURE_PROFILES_SWITCH},
            {"socdModeSwitch", SCREEN_FEATURE_SOCD_MODE_SWITCH},
            {"tournamentModeSwitch", SCREEN_FEATURE_TOURNAMENT_MODE_SWITCH},
            {"ledBrightnessAdjust", SCREEN_FEATURE_LED_BRIGHTNESS_ADJUST},
            {"ledEffectSwitch", SCREEN_FEATURE_LED_EFFECT_SWITCH},
            {"ambientBrightnessAdjust", SCREEN_FEATURE_AMBIENT_BRIGHTNESS_ADJUST},
            {"ambientEffectSwitch", SCREEN_FEATURE_AMBIENT_EFFECT_SWITCH},
            {"screenBrightnessAdjust", SCREEN_FEATURE_SCREEN_BRIGHTNESS_ADJUST},
            {"webConfigEntry", SCREEN_FEATURE_WEB_CONFIG_ENTRY},
            {"calibrationModeSwitch", SCREEN_FEATURE_CALIBRATION_MODE_SWITCH},
        };
        for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
            cJSON* b = cJSON_GetObjectItem(features, map[i].key);
            if (b && cJSON_IsBool(b)) {
                if (cJSON_IsTrue(b)) config.screenControl.featuresMask |= map[i].bit;
                else config.screenControl.featuresMask &= ~map[i].bit;
            }
        }
    }

    if (!STORAGE_MANAGER.saveConfig()) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to save configuration");
    }

    return handleGetScreenControlConfig(request);
}

WebSocketDownstreamMessage GlobalConfigCommandHandler::handleExportAllConfig(const WebSocketUpstreamMessage& request) {
    // LOG_INFO("WebSocket", "Handling export_all_config command, cid: %d", request.getCid());
    
    Config& config = Storage::getInstance().config;

    WebSocketConnection* conn = request.getConnection();
    if (!conn) {
         LOG_ERROR("WebSocket", "export_all_config: No connection");
         return create_error_response(request.getCid(), request.getCommand(), 1, "No connection");
    }

    auto sendPart = [&](const char* section, cJSON* data) {
        cJSON* msgData = cJSON_CreateObject();
        cJSON_AddStringToObject(msgData, "section", section);
        if (data) cJSON_AddItemToObject(msgData, "data", data);
        
        cJSON* response = cJSON_CreateObject();
        cJSON_AddNumberToObject(response, "cid", request.getCid());
        cJSON_AddStringToObject(response, "command", request.getCommand().c_str());
        cJSON_AddNumberToObject(response, "errNo", 0);
        cJSON_AddItemToObject(response, "data", msgData);
        
        char* str = cJSON_PrintUnformatted(response);
        if (str) {
            conn->send_text(str);
            free(str);
        }
        cJSON_Delete(response);
    };

    // 1. 发送 Global Config
    {
        cJSON* globalConfigJSON = cJSON_CreateObject();
        const char* modeStr = ConfigUtils::getInputModeString(config.inputMode);
        cJSON_AddStringToObject(globalConfigJSON, "inputMode", modeStr);
        cJSON_AddBoolToObject(globalConfigJSON, "autoCalibrationEnabled", config.autoCalibrationEnabled);
        cJSON_AddStringToObject(globalConfigJSON, "defaultProfileId", config.defaultProfileId);
        sendPart("global", globalConfigJSON);
    }

    // 2. 发送 Hotkeys Config
    {
        sendPart("hotkeys", ConfigUtils::buildHotkeysConfigJSON(config));
    }

    // 3. 发送 Profiles
    for (int i = 0; i < NUM_PROFILES; i++) {
        if (config.profiles[i].enabled) {
            cJSON* profileJSON = ProfileCommandHandler::buildProfileJSON(&config.profiles[i]);
            if (profileJSON) {
                sendPart("profile", profileJSON);
                // 简单的延时以防止发送缓冲区溢出
                // HAL_Delay(10);
            }
        }
    }

    // 4. 发送结束信号 (通过返回最后的响应)
    cJSON* finishData = cJSON_CreateObject();
    cJSON_AddStringToObject(finishData, "section", "end");

    return create_success_response(request.getCid(), request.getCommand(), finishData);
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

WebSocketDownstreamMessage GlobalConfigCommandHandler::handleImportConfigPart(const WebSocketUpstreamMessage& request) {
    Config& config = Storage::getInstance().config;
    cJSON* params = request.getParams();
    
    if (!params) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    cJSON* sectionItem = cJSON_GetObjectItem(params, "section");
    cJSON* dataItem = cJSON_GetObjectItem(params, "data");

    if (!sectionItem || !cJSON_IsString(sectionItem) || !dataItem) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing section or data");
    }

    std::string section = sectionItem->valuestring;

    if (section == "global") {
        cJSON* globalConfigJSON = dataItem;
        cJSON* item;
        if ((item = cJSON_GetObjectItem(globalConfigJSON, "inputMode")) && cJSON_IsString(item)) {
            config.inputMode = ConfigUtils::getInputModeFromString(item->valuestring);
        }
        if ((item = cJSON_GetObjectItem(globalConfigJSON, "defaultProfileId")) && cJSON_IsString(item)) {
             strncpy(config.defaultProfileId, item->valuestring, sizeof(config.defaultProfileId) - 1);
        }
        if ((item = cJSON_GetObjectItem(globalConfigJSON, "autoCalibrationEnabled"))) {
             config.autoCalibrationEnabled = cJSON_IsTrue(item);
        }
    } else if (section == "hotkeys") {
        if (cJSON_IsArray(dataItem)) {
            int index = 0;
            cJSON* hotkeyItem;
            cJSON_ArrayForEach(hotkeyItem, dataItem) {
                if (index >= NUM_GAMEPAD_HOTKEYS) break;
                
                cJSON* actionItem = cJSON_GetObjectItem(hotkeyItem, "action");
                cJSON* pinItem = cJSON_GetObjectItem(hotkeyItem, "virtualPin");
                cJSON* lockedItem = cJSON_GetObjectItem(hotkeyItem, "isLocked");
                cJSON* holdItem = cJSON_GetObjectItem(hotkeyItem, "isHold");

                if (actionItem && cJSON_IsString(actionItem)) {
                    config.hotkeys[index].action = ConfigUtils::getGamepadHotkeyFromString(actionItem->valuestring);
                }
                if (pinItem && cJSON_IsNumber(pinItem)) {
                    config.hotkeys[index].virtualPin = pinItem->valueint;
                }
                if (lockedItem) {
                    config.hotkeys[index].isLocked = cJSON_IsTrue(lockedItem);
                }
                if (holdItem) {
                    config.hotkeys[index].isHold = cJSON_IsTrue(holdItem);
                }
                index++;
            }
        }
    } else if (section == "profile") {
        cJSON* profileItem = dataItem;
        cJSON* idItem = cJSON_GetObjectItem(profileItem, "id");
        if (idItem && cJSON_IsString(idItem)) {
             // Find profile by ID
             for (int i=0; i < NUM_PROFILES; i++) {
                 if (strncmp(config.profiles[i].id, idItem->valuestring, sizeof(config.profiles[i].id)) == 0) {
                     ProfileCommandHandler::parseProfileJSON(profileItem, &config.profiles[i]);
                     config.profiles[i].enabled = true;
                     break;
                 }
             }
        }
    } else if (section == "screenControl") {
        cJSON* screenControl = dataItem;
        cJSON* item;
        if ((item = cJSON_GetObjectItem(screenControl, "brightness")) && cJSON_IsNumber(item)) {
            int v = item->valueint;
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            config.screenControl.brightness = (uint8_t)v;
        }
        if ((item = cJSON_GetObjectItem(screenControl, "backgroundColor")) && cJSON_IsNumber(item)) {
            config.screenControl.backgroundColor = (uint32_t)item->valuedouble;
        }
        if ((item = cJSON_GetObjectItem(screenControl, "textColor")) && cJSON_IsNumber(item)) {
            config.screenControl.textColor = (uint32_t)item->valuedouble;
        }
        if ((item = cJSON_GetObjectItem(screenControl, "backgroundImageId")) && cJSON_IsString(item)) {
            strncpy(config.screenControl.backgroundImageId, item->valuestring, sizeof(config.screenControl.backgroundImageId) - 1);
            config.screenControl.backgroundImageId[sizeof(config.screenControl.backgroundImageId) - 1] = '\0';
        }
        if ((item = cJSON_GetObjectItem(screenControl, "currentPageId")) && cJSON_IsNumber(item)) {
            int v = item->valueint;
            if (v < 0) v = 0;
            if (v > 65535) v = 65535;
            config.screenControl.currentPageId = (uint16_t)v;
        }
        cJSON* features = cJSON_GetObjectItem(screenControl, "features");
        if (features && cJSON_IsObject(features)) {
            struct { const char* key; uint32_t bit; } map[] = {
                {"inputModeSwitch", SCREEN_FEATURE_INPUT_MODE_SWITCH},
                {"profilesSwitch", SCREEN_FEATURE_PROFILES_SWITCH},
                {"socdModeSwitch", SCREEN_FEATURE_SOCD_MODE_SWITCH},
                {"tournamentModeSwitch", SCREEN_FEATURE_TOURNAMENT_MODE_SWITCH},
                {"ledBrightnessAdjust", SCREEN_FEATURE_LED_BRIGHTNESS_ADJUST},
                {"ledEffectSwitch", SCREEN_FEATURE_LED_EFFECT_SWITCH},
                {"ambientBrightnessAdjust", SCREEN_FEATURE_AMBIENT_BRIGHTNESS_ADJUST},
                {"ambientEffectSwitch", SCREEN_FEATURE_AMBIENT_EFFECT_SWITCH},
                {"screenBrightnessAdjust", SCREEN_FEATURE_SCREEN_BRIGHTNESS_ADJUST},
                {"webConfigEntry", SCREEN_FEATURE_WEB_CONFIG_ENTRY},
                {"calibrationModeSwitch", SCREEN_FEATURE_CALIBRATION_MODE_SWITCH},
            };
            for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
                cJSON* b = cJSON_GetObjectItem(features, map[i].key);
                if (b && cJSON_IsBool(b)) {
                    if (cJSON_IsTrue(b)) config.screenControl.featuresMask |= map[i].bit;
                    else config.screenControl.featuresMask &= ~map[i].bit;
                }
            }
        }
    } else {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Unknown section");
    }

    return create_success_response(request.getCid(), request.getCommand(), nullptr);
}

WebSocketDownstreamMessage GlobalConfigCommandHandler::handleImportConfigFinish(const WebSocketUpstreamMessage& request) {
    if (!STORAGE_MANAGER.saveConfig()) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to save configuration");
    }
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
    } else if (command == "get_screen_control_config") {
        return handleGetScreenControlConfig(request);
    } else if (command == "update_screen_control_config") {
        return handleUpdateScreenControlConfig(request);
    } else if (command == "export_all_config") {
        return handleExportAllConfig(request);
    } else if (command == "import_all_config") {
        return handleImportAllConfig(request);
    } else if (command == "import_config_part") {
        return handleImportConfigPart(request);
    } else if (command == "import_config_finish") {
        return handleImportConfigFinish(request);
    } else if (command == "reboot") {
        return handleReboot(request);
    } else if (command == "push_leds_config") {
        return handlePushLedsConfig(request);
    } else if (command == "clear_leds_preview") {
        return handleClearLedsPreview(request);
    }
    
    return create_error_response(request.getCid(), command, -1, "Unknown command");
}
