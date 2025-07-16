#include "configs/websocket_command_handler.hpp"
#include "storagemanager.hpp"
#include "system_logger.h"

// ============================================================================
// ProfileCommandHandler 实现
// ============================================================================

ProfileCommandHandler& ProfileCommandHandler::getInstance() {
    static ProfileCommandHandler instance;
    return instance;
}

cJSON* ProfileCommandHandler::buildKeyMappingJSON(uint32_t virtualMask) {
    cJSON* keyMappingJSON = cJSON_CreateArray();
    
    for(uint8_t i = 0; i < NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS; i++) {
        if(virtualMask & (1 << i)) {
            cJSON_AddItemToArray(keyMappingJSON, cJSON_CreateNumber(i));
        }
    }

    return keyMappingJSON;
}

uint32_t ProfileCommandHandler::getKeyMappingVirtualMask(cJSON* keyMappingJSON) {
    if(!keyMappingJSON || !cJSON_IsArray(keyMappingJSON)) {
        return 0;
    }

    uint32_t virtualMask = 0;
    cJSON* item = NULL;
    cJSON_ArrayForEach(item, keyMappingJSON) {
        virtualMask |= (1 << (int)cJSON_GetNumberValue(item));
    }
    return virtualMask;
}

cJSON* ProfileCommandHandler::buildProfileListJSON() {
    Config& config = Storage::getInstance().config;
    
    // 创建返回数据结构
    cJSON* profileListJSON = cJSON_CreateObject();
    cJSON* itemsJSON = cJSON_CreateArray();

    // 添加默认配置ID和最大配置数
    cJSON_AddStringToObject(profileListJSON, "defaultId", config.defaultProfileId);
    cJSON_AddNumberToObject(profileListJSON, "maxNumProfiles", config.numProfilesMax);

    // 添加所有配置文件信息
    for(uint8_t i = 0; i < NUM_PROFILES; i++) {
        if(config.profiles[i].enabled) {  // 只添加已启用的配置文件
            cJSON* profileJSON = cJSON_CreateObject();
            
            // 清理字符串中的控制字符，确保UTF-8编码有效
            char cleanId[17] = {0};
            char cleanName[25] = {0};
            
            // 复制并清理ID
            strncpy(cleanId, config.profiles[i].id, sizeof(cleanId) - 1);
            for(int j = 0; cleanId[j]; j++) {
                if(cleanId[j] < 32 && cleanId[j] != ' ' && cleanId[j] != '\n' && cleanId[j] != '\r' && cleanId[j] != '\t') {
                    cleanId[j] = '_';  // 替换控制字符为下划线
                }
            }
            
            // 复制并清理name
            strncpy(cleanName, config.profiles[i].name, sizeof(cleanName) - 1);
            for(int j = 0; cleanName[j]; j++) {
                if(cleanName[j] < 32 && cleanName[j] != ' ' && cleanName[j] != '\n' && cleanName[j] != '\r' && cleanName[j] != '\t') {
                    cleanName[j] = '_';  // 替换控制字符为下划线
                }
            }
            
            // 基本信息
            cJSON_AddStringToObject(profileJSON, "id", cleanId);
            cJSON_AddStringToObject(profileJSON, "name", cleanName);
            cJSON_AddBoolToObject(profileJSON, "enabled", config.profiles[i].enabled);

            // 添加到数组
            cJSON_AddItemToArray(itemsJSON, profileJSON);
        }
    }

    // 构建返回结构
    cJSON_AddItemToObject(profileListJSON, "items", itemsJSON);

    return profileListJSON;
}

cJSON* ProfileCommandHandler::buildProfileJSON(GamepadProfile* profile) {
    if (!profile) {
        return nullptr;
    }

    cJSON* profileDetailsJSON = cJSON_CreateObject();

    // 清理字符串中的控制字符，确保UTF-8编码有效
    char cleanId[17] = {0};
    char cleanName[25] = {0};
    
    // 复制并清理ID
    strncpy(cleanId, profile->id, sizeof(cleanId) - 1);
    for(int j = 0; cleanId[j]; j++) {
        if(cleanId[j] < 32 && cleanId[j] != ' ' && cleanId[j] != '\n' && cleanId[j] != '\r' && cleanId[j] != '\t') {
            cleanId[j] = '_';  // 替换控制字符为下划线
        }
    }
    
    // 复制并清理name
    strncpy(cleanName, profile->name, sizeof(cleanName) - 1);
    for(int j = 0; cleanName[j]; j++) {
        if(cleanName[j] < 32 && cleanName[j] != ' ' && cleanName[j] != '\n' && cleanName[j] != '\r' && cleanName[j] != '\t') {
            cleanName[j] = '_';  // 替换控制字符为下划线
        }
    }

    // 基本信息
    cJSON_AddStringToObject(profileDetailsJSON, "id", cleanId);
    cJSON_AddStringToObject(profileDetailsJSON, "name", cleanName);

    // 按键配置
    cJSON* keysConfigJSON = cJSON_CreateObject();
    cJSON_AddBoolToObject(keysConfigJSON, "invertXAxis", profile->keysConfig.invertXAxis);
    cJSON_AddBoolToObject(keysConfigJSON, "invertYAxis", profile->keysConfig.invertYAxis);
    cJSON_AddBoolToObject(keysConfigJSON, "fourWayMode", profile->keysConfig.fourWayMode);

    cJSON_AddNumberToObject(keysConfigJSON, "socdMode", profile->keysConfig.socdMode);

    // 按键开启状态
    cJSON* enabledKeysJSON = cJSON_CreateArray();
    for(uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        cJSON_AddItemToArray(enabledKeysJSON, cJSON_CreateBool(profile->keysConfig.keysEnableTag[i]));
    }
    cJSON_AddItemToObject(keysConfigJSON, "keysEnableTag", enabledKeysJSON);

    // 按键映射
    cJSON* keyMappingJSON = cJSON_CreateObject();

    cJSON_AddItemToObject(keyMappingJSON, "DPAD_UP", buildKeyMappingJSON(profile->keysConfig.keyDpadUp));
    cJSON_AddItemToObject(keyMappingJSON, "DPAD_DOWN", buildKeyMappingJSON(profile->keysConfig.keyDpadDown));
    cJSON_AddItemToObject(keyMappingJSON, "DPAD_LEFT", buildKeyMappingJSON(profile->keysConfig.keyDpadLeft));
    cJSON_AddItemToObject(keyMappingJSON, "DPAD_RIGHT", buildKeyMappingJSON(profile->keysConfig.keyDpadRight));
    cJSON_AddItemToObject(keyMappingJSON, "B1", buildKeyMappingJSON(profile->keysConfig.keyButtonB1));
    cJSON_AddItemToObject(keyMappingJSON, "B2", buildKeyMappingJSON(profile->keysConfig.keyButtonB2));
    cJSON_AddItemToObject(keyMappingJSON, "B3", buildKeyMappingJSON(profile->keysConfig.keyButtonB3));
    cJSON_AddItemToObject(keyMappingJSON, "B4", buildKeyMappingJSON(profile->keysConfig.keyButtonB4));
    cJSON_AddItemToObject(keyMappingJSON, "L1", buildKeyMappingJSON(profile->keysConfig.keyButtonL1));
    cJSON_AddItemToObject(keyMappingJSON, "L2", buildKeyMappingJSON(profile->keysConfig.keyButtonL2));
    cJSON_AddItemToObject(keyMappingJSON, "R1", buildKeyMappingJSON(profile->keysConfig.keyButtonR1));
    cJSON_AddItemToObject(keyMappingJSON, "R2", buildKeyMappingJSON(profile->keysConfig.keyButtonR2));
    cJSON_AddItemToObject(keyMappingJSON, "S1", buildKeyMappingJSON(profile->keysConfig.keyButtonS1));
    cJSON_AddItemToObject(keyMappingJSON, "S2", buildKeyMappingJSON(profile->keysConfig.keyButtonS2));
    cJSON_AddItemToObject(keyMappingJSON, "L3", buildKeyMappingJSON(profile->keysConfig.keyButtonL3));
    cJSON_AddItemToObject(keyMappingJSON, "R3", buildKeyMappingJSON(profile->keysConfig.keyButtonR3));
    cJSON_AddItemToObject(keyMappingJSON, "A1", buildKeyMappingJSON(profile->keysConfig.keyButtonA1));
    cJSON_AddItemToObject(keyMappingJSON, "A2", buildKeyMappingJSON(profile->keysConfig.keyButtonA2));
    cJSON_AddItemToObject(keyMappingJSON, "Fn", buildKeyMappingJSON(profile->keysConfig.keyButtonFn));
    cJSON_AddItemToObject(keysConfigJSON, "keyMapping", keyMappingJSON);

    // LED配置
    cJSON* ledsConfigJSON = cJSON_CreateObject();
    cJSON_AddBoolToObject(ledsConfigJSON, "ledEnabled", profile->ledsConfigs.ledEnabled);
    cJSON_AddNumberToObject(ledsConfigJSON, "ledsEffectStyle", static_cast<int>(profile->ledsConfigs.ledEffect));
    
    // LED颜色数组
    cJSON* ledColorsJSON = cJSON_CreateArray();
    char colorStr[8];
    sprintf(colorStr, "#%06X", profile->ledsConfigs.ledColor1);
    cJSON_AddItemToArray(ledColorsJSON, cJSON_CreateString(colorStr));
    sprintf(colorStr, "#%06X", profile->ledsConfigs.ledColor2);
    cJSON_AddItemToArray(ledColorsJSON, cJSON_CreateString(colorStr));
    sprintf(colorStr, "#%06X", profile->ledsConfigs.ledColor3);
    cJSON_AddItemToArray(ledColorsJSON, cJSON_CreateString(colorStr));
    
    cJSON_AddItemToObject(ledsConfigJSON, "ledColors", ledColorsJSON);
    cJSON_AddNumberToObject(ledsConfigJSON, "ledBrightness", profile->ledsConfigs.ledBrightness);
    cJSON_AddNumberToObject(ledsConfigJSON, "ledAnimationSpeed", profile->ledsConfigs.ledAnimationSpeed);

    // 氛围灯配置
    cJSON_AddBoolToObject(ledsConfigJSON, "hasAroundLed", HAS_LED_AROUND); // 是否包含氛围灯，由主板决定
    cJSON_AddBoolToObject(ledsConfigJSON, "aroundLedEnabled", profile->ledsConfigs.aroundLedEnabled);
    cJSON_AddBoolToObject(ledsConfigJSON, "aroundLedSyncToMainLed", profile->ledsConfigs.aroundLedSyncToMainLed);
    cJSON_AddBoolToObject(ledsConfigJSON, "aroundLedTriggerByButton", profile->ledsConfigs.aroundLedTriggerByButton);
    cJSON_AddNumberToObject(ledsConfigJSON, "aroundLedEffectStyle", static_cast<int>(profile->ledsConfigs.aroundLedEffect));
    
    cJSON* aroundLedColorsJSON = cJSON_CreateArray();
    sprintf(colorStr, "#%06X", profile->ledsConfigs.aroundLedColor1);
    cJSON_AddItemToArray(aroundLedColorsJSON, cJSON_CreateString(colorStr));
    sprintf(colorStr, "#%06X", profile->ledsConfigs.aroundLedColor2);
    cJSON_AddItemToArray(aroundLedColorsJSON, cJSON_CreateString(colorStr));
    sprintf(colorStr, "#%06X", profile->ledsConfigs.aroundLedColor3);
    cJSON_AddItemToArray(aroundLedColorsJSON, cJSON_CreateString(colorStr));
    cJSON_AddItemToObject(ledsConfigJSON, "aroundLedColors", aroundLedColorsJSON);

    cJSON_AddNumberToObject(ledsConfigJSON, "aroundLedBrightness", profile->ledsConfigs.aroundLedBrightness);
    cJSON_AddNumberToObject(ledsConfigJSON, "aroundLedAnimationSpeed", profile->ledsConfigs.aroundLedAnimationSpeed);

    // 触发器配置
    cJSON* triggerConfigsJSON = cJSON_CreateObject();   
    cJSON* triggerConfigsArrayJSON = cJSON_CreateArray();
    
    for(uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        cJSON* triggerJSON = cJSON_CreateObject();
        RapidTriggerProfile* trigger = &profile->triggerConfigs.triggerConfigs[i];
        char buffer[32];
        // 使用snprintf限制小数点后4位
        snprintf(buffer, sizeof(buffer), "%.4f", trigger->topDeadzone);
        cJSON_AddRawToObject(triggerJSON, "topDeadzone", buffer);
        snprintf(buffer, sizeof(buffer), "%.4f", trigger->bottomDeadzone);
        cJSON_AddRawToObject(triggerJSON, "bottomDeadzone", buffer);
        snprintf(buffer, sizeof(buffer), "%.4f", trigger->pressAccuracy);
        cJSON_AddRawToObject(triggerJSON, "pressAccuracy", buffer);
        snprintf(buffer, sizeof(buffer), "%.4f", trigger->releaseAccuracy);
        cJSON_AddRawToObject(triggerJSON, "releaseAccuracy", buffer);
        cJSON_AddItemToArray(triggerConfigsArrayJSON, triggerJSON);

    }

    cJSON_AddBoolToObject(triggerConfigsJSON, "isAllBtnsConfiguring", profile->triggerConfigs.isAllBtnsConfiguring);

    uint8_t debounceAlgorithm = static_cast<uint8_t>(profile->triggerConfigs.debounceAlgorithm);
    cJSON_AddNumberToObject(triggerConfigsJSON, "debounceAlgorithm", debounceAlgorithm);
    cJSON_AddItemToObject(triggerConfigsJSON, "triggerConfigs", triggerConfigsArrayJSON);

    // // 组装最终结构
    cJSON_AddItemToObject(profileDetailsJSON, "keysConfig", keysConfigJSON);
    cJSON_AddItemToObject(profileDetailsJSON, "ledsConfigs", ledsConfigJSON);
    cJSON_AddItemToObject(profileDetailsJSON, "triggerConfigs", triggerConfigsJSON);

    return profileDetailsJSON;
}

WebSocketDownstreamMessage ProfileCommandHandler::handleGetProfileList(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling get_profile_list command, cid: %d", request.getCid());
    
    // 创建返回数据结构
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* profileListJSON = buildProfileListJSON();
    
    if (!profileListJSON) {
        LOG_ERROR("WebSocket", "get_profile_list: Failed to build profile list JSON");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to build profile list JSON");
    }

    // 构建返回结构
    cJSON_AddItemToObject(dataJSON, "profileList", profileListJSON);

    LOG_INFO("WebSocket", "get_profile_list command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage ProfileCommandHandler::handleGetDefaultProfile(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling get_default_profile command, cid: %d", request.getCid());

    Config& config = Storage::getInstance().config;
    
    // 查找默认配置文件
    GamepadProfile* defaultProfile = nullptr;
    for(uint8_t i = 0; i < NUM_PROFILES; i++) {
        if(strcmp(config.defaultProfileId, config.profiles[i].id) == 0) {
            defaultProfile = &config.profiles[i];
            break;
        }
    }

    if(!defaultProfile) {
        LOG_ERROR("WebSocket", "get_default_profile: Default profile not found");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Default profile not found");
    }

    // 创建返回数据结构
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* profileDetailsJSON = buildProfileJSON(defaultProfile);

    if (!profileDetailsJSON) {
        LOG_ERROR("WebSocket", "get_default_profile: Failed to build profile JSON");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to build profile JSON");
    }
    
    cJSON_AddItemToObject(dataJSON, "profileDetails", profileDetailsJSON);

    LOG_INFO("WebSocket", "get_default_profile command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage ProfileCommandHandler::handleUpdateProfile(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling update_profile command, cid: %d", request.getCid());
    
    Config& config = Storage::getInstance().config;
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("WebSocket", "update_profile: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    cJSON* details = cJSON_GetObjectItem(params, "profileDetails");
    
    if(!details) {
        LOG_ERROR("WebSocket", "update_profile: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    // 获取profile ID并查找对应的配置文件
    cJSON* idItem = cJSON_GetObjectItem(details, "id");
    if(!idItem) {
        LOG_ERROR("WebSocket", "update_profile: Profile ID not provided");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Profile ID not provided");
    }

    GamepadProfile* targetProfile = nullptr;
    for(uint8_t i = 0; i < NUM_PROFILES; i++) {
        if(strcmp(idItem->valuestring, config.profiles[i].id) == 0) {
            targetProfile = &config.profiles[i];
            break;
        }
    }

    if(!targetProfile) {
        LOG_ERROR("WebSocket", "update_profile: Profile not found");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Profile not found");
    }

    // 更新基本信息
    cJSON* nameItem = cJSON_GetObjectItem(details, "name");
    if(nameItem) {
        strcpy(targetProfile->name, nameItem->valuestring);
    }

    // 更新按键配置
    cJSON* keysConfig = cJSON_GetObjectItem(details, "keysConfig");
    if(keysConfig) {
        cJSON* item;
        
        if((item = cJSON_GetObjectItem(keysConfig, "invertXAxis"))) {
            targetProfile->keysConfig.invertXAxis = item->type == cJSON_True;
        }
        if((item = cJSON_GetObjectItem(keysConfig, "invertYAxis"))) {
            targetProfile->keysConfig.invertYAxis = item->type == cJSON_True;
        }
        if((item = cJSON_GetObjectItem(keysConfig, "fourWayMode"))) {
            targetProfile->keysConfig.fourWayMode = item->type == cJSON_True;
        }
        if((item = cJSON_GetObjectItem(keysConfig, "socdMode")) 
            && item->type == cJSON_Number 
            && item->valueint >= 0 
            && item->valueint < SOCDMode::NUM_SOCD_MODES) {
            targetProfile->keysConfig.socdMode = static_cast<SOCDMode>(item->valueint);
        }

        // 更新按键启用状态
        cJSON* keysEnableTag = cJSON_GetObjectItem(keysConfig, "keysEnableTag");
        if(keysEnableTag) {
            for(uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
                targetProfile->keysConfig.keysEnableTag[i] = cJSON_GetArrayItem(keysEnableTag, i)->type == cJSON_True;
            }
        }
      
        // 更新按键映射
        cJSON* keyMapping = cJSON_GetObjectItem(keysConfig, "keyMapping");                                          
        if(keyMapping) {
            if((item = cJSON_GetObjectItem(keyMapping, "DPAD_UP"))) 
                targetProfile->keysConfig.keyDpadUp = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "DPAD_DOWN"))) 
                targetProfile->keysConfig.keyDpadDown = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "DPAD_LEFT"))) 
                targetProfile->keysConfig.keyDpadLeft = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "DPAD_RIGHT"))) 
                targetProfile->keysConfig.keyDpadRight = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "B1"))) 
                targetProfile->keysConfig.keyButtonB1 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "B2"))) 
                targetProfile->keysConfig.keyButtonB2 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "B3"))) 
                targetProfile->keysConfig.keyButtonB3 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "B4"))) 
                targetProfile->keysConfig.keyButtonB4 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "L1"))) 
                targetProfile->keysConfig.keyButtonL1 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "L2"))) 
                targetProfile->keysConfig.keyButtonL2 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "R1"))) 
                targetProfile->keysConfig.keyButtonR1 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "R2"))) 
                targetProfile->keysConfig.keyButtonR2 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "S1"))) 
                targetProfile->keysConfig.keyButtonS1 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "S2"))) 
                targetProfile->keysConfig.keyButtonS2 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "L3"))) 
                targetProfile->keysConfig.keyButtonL3 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "R3"))) 
                targetProfile->keysConfig.keyButtonR3 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "A1"))) 
                targetProfile->keysConfig.keyButtonA1 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "A2"))) 
                targetProfile->keysConfig.keyButtonA2 = getKeyMappingVirtualMask(item);
            if((item = cJSON_GetObjectItem(keyMapping, "Fn"))) 
                targetProfile->keysConfig.keyButtonFn = getKeyMappingVirtualMask(item);
        }
    }

    // 更新LED配置
    cJSON* ledsConfig = cJSON_GetObjectItem(details, "ledsConfigs");
    if(ledsConfig) {
        cJSON* item;
        
        if((item = cJSON_GetObjectItem(ledsConfig, "ledEnabled"))) {
            targetProfile->ledsConfigs.ledEnabled = item->type == cJSON_True;
        }
        
        // 解析LED效果
        if((item = cJSON_GetObjectItem(ledsConfig, "ledsEffectStyle")) 
            && item->type == cJSON_Number 
            && item->valueint >= 0 
            && item->valueint < LEDEffect::NUM_EFFECTS) {
            targetProfile->ledsConfigs.ledEffect = static_cast<LEDEffect>(item->valueint);
        }

        // 解析LED颜色
        cJSON* ledColors = cJSON_GetObjectItem(ledsConfig, "ledColors");
        if(ledColors && cJSON_GetArraySize(ledColors) >= 3) {
            sscanf(cJSON_GetArrayItem(ledColors, 0)->valuestring, "#%x", &targetProfile->ledsConfigs.ledColor1);
            sscanf(cJSON_GetArrayItem(ledColors, 1)->valuestring, "#%x", &targetProfile->ledsConfigs.ledColor2);
            sscanf(cJSON_GetArrayItem(ledColors, 2)->valuestring, "#%x", &targetProfile->ledsConfigs.ledColor3);
        }
        
        if((item = cJSON_GetObjectItem(ledsConfig, "ledBrightness"))) {
            targetProfile->ledsConfigs.ledBrightness = item->valueint;
        }

        if((item = cJSON_GetObjectItem(ledsConfig, "ledAnimationSpeed"))) { 
            targetProfile->ledsConfigs.ledAnimationSpeed = item->valueint;
        }

        // 氛围灯配置
        if((item = cJSON_GetObjectItem(ledsConfig, "aroundLedEnabled"))) {
            targetProfile->ledsConfigs.aroundLedEnabled = item->type == cJSON_True;
        }

        if((item = cJSON_GetObjectItem(ledsConfig, "aroundLedSyncToMainLed"))) {
            targetProfile->ledsConfigs.aroundLedSyncToMainLed = item->type == cJSON_True;
        }

        if((item = cJSON_GetObjectItem(ledsConfig, "aroundLedTriggerByButton"))) {
            targetProfile->ledsConfigs.aroundLedTriggerByButton = item->type == cJSON_True;
        }

        if((item = cJSON_GetObjectItem(ledsConfig, "aroundLedEffectStyle"))) {
            targetProfile->ledsConfigs.aroundLedEffect = static_cast<AroundLEDEffect>(item->valueint);
        }

        cJSON* aroundLedColors = cJSON_GetObjectItem(ledsConfig, "aroundLedColors");
        if(aroundLedColors && cJSON_GetArraySize(aroundLedColors) >= 3) {
            sscanf(cJSON_GetArrayItem(aroundLedColors, 0)->valuestring, "#%x", &targetProfile->ledsConfigs.aroundLedColor1);
            sscanf(cJSON_GetArrayItem(aroundLedColors, 1)->valuestring, "#%x", &targetProfile->ledsConfigs.aroundLedColor2);
            sscanf(cJSON_GetArrayItem(aroundLedColors, 2)->valuestring, "#%x", &targetProfile->ledsConfigs.aroundLedColor3);
        }

        if((item = cJSON_GetObjectItem(ledsConfig, "aroundLedBrightness"))) {
            targetProfile->ledsConfigs.aroundLedBrightness = item->valueint;
        }

        if((item = cJSON_GetObjectItem(ledsConfig, "aroundLedAnimationSpeed"))) {
            targetProfile->ledsConfigs.aroundLedAnimationSpeed = item->valueint;
        }
    }

    // 更新按键行程配置
    cJSON* triggerConfigs = cJSON_GetObjectItem(details, "triggerConfigs");
    if(triggerConfigs) {
        cJSON* isAllBtnsConfiguring = cJSON_GetObjectItem(triggerConfigs, "isAllBtnsConfiguring");
        if(isAllBtnsConfiguring) {
            targetProfile->triggerConfigs.isAllBtnsConfiguring = isAllBtnsConfiguring->type == cJSON_True;
        }

        cJSON* debounceAlgorithm = cJSON_GetObjectItem(triggerConfigs, "debounceAlgorithm");
        if(debounceAlgorithm) {
            if(debounceAlgorithm->type == cJSON_Number 
                && debounceAlgorithm->valueint >= 0 
                && debounceAlgorithm->valueint < ADCButtonDebounceAlgorithm::NUM_ADC_BUTTON_DEBOUNCE_ALGORITHMS) {
                targetProfile->triggerConfigs.debounceAlgorithm = static_cast<ADCButtonDebounceAlgorithm>(debounceAlgorithm->valueint);
            } else {
                targetProfile->triggerConfigs.debounceAlgorithm = ADCButtonDebounceAlgorithm::NONE;
            }
        }

        cJSON* configs = cJSON_GetObjectItem(triggerConfigs, "triggerConfigs");
        if(configs) {
            for(uint8_t i = 0; i < NUM_ADC_BUTTONS && i < cJSON_GetArraySize(configs); i++) {
                cJSON* trigger = cJSON_GetArrayItem(configs, i);
                RapidTriggerProfile* triggerProfile = &targetProfile->triggerConfigs.triggerConfigs[i];
                if(trigger) {
                    cJSON* item;
                    if((item = cJSON_GetObjectItem(trigger, "topDeadzone")))
                        triggerProfile->topDeadzone = item->valuedouble;
                    if((item = cJSON_GetObjectItem(trigger, "bottomDeadzone")))
                        triggerProfile->bottomDeadzone = item->valuedouble;
                    if((item = cJSON_GetObjectItem(trigger, "pressAccuracy")))
                        triggerProfile->pressAccuracy = item->valuedouble;
                    if((item = cJSON_GetObjectItem(trigger, "releaseAccuracy")))
                        triggerProfile->releaseAccuracy = item->valuedouble;
                }
            }
        }
    }

    // 保存配置
    if(!STORAGE_MANAGER.saveConfig()) {
        LOG_ERROR("WebSocket", "update_profile: Failed to save configuration");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to save configuration");
    }

    // 构建返回数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* profileDetailsJSON = buildProfileJSON(targetProfile);
    if (!profileDetailsJSON) {
        LOG_ERROR("WebSocket", "update_profile: Failed to build profile JSON");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to build profile JSON");
    }
    
    cJSON_AddItemToObject(dataJSON, "profileDetails", profileDetailsJSON);
    
    LOG_INFO("WebSocket", "update_profile command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage ProfileCommandHandler::handleCreateProfile(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling create_profile command, cid: %d", request.getCid());
    
    Config& config = Storage::getInstance().config;
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("WebSocket", "create_profile: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    // 检查是否达到最大配置文件数
    uint8_t enabledCount = 0;
    for(uint8_t i = 0; i < NUM_PROFILES; i++) {
        if(config.profiles[i].enabled) {
            enabledCount++;
        }
    }

    if(enabledCount >= config.numProfilesMax) {
        LOG_ERROR("WebSocket", "create_profile: Maximum number of profiles reached");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Maximum number of profiles reached");
    }

    // 查找第一个未启用的配置文件
    GamepadProfile* targetProfile = nullptr;
    for(uint8_t i = 0; i < NUM_PROFILES; i++) {
        if(!config.profiles[i].enabled) {
            targetProfile = &config.profiles[i];
            break;
        }
    }

    if(!targetProfile) {
        LOG_ERROR("WebSocket", "create_profile: No available profile slot");
        return create_error_response(request.getCid(), request.getCommand(), 1, "No available profile slot");
    }

    // 获取新配置文件名称
    cJSON* nameItem = cJSON_GetObjectItem(params, "profileName");
    if(nameItem && nameItem->valuestring) {
        ConfigUtils::makeDefaultProfile(*targetProfile, targetProfile->id, true); // 启用配置文件 并且 初始化配置文件
        strncpy(targetProfile->name, nameItem->valuestring, sizeof(targetProfile->name) - 1); // 设置配置文件名称
        targetProfile->name[sizeof(targetProfile->name) - 1] = '\0';  // 确保字符串结束
        strcpy(config.defaultProfileId, targetProfile->id); // 设置默认配置文件ID 为新创建的配置文件ID
    } else {
        LOG_ERROR("WebSocket", "create_profile: Profile name not provided");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Profile name not provided");
    }

    // 保存配置
    if(!STORAGE_MANAGER.saveConfig()) {
        LOG_ERROR("WebSocket", "create_profile: Failed to save configuration");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to save configuration");
    }

    // 构建返回数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* profileListJSON = buildProfileListJSON();
    
    if (!profileListJSON) {
        LOG_ERROR("WebSocket", "create_profile: Failed to build profile list JSON");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to build profile list JSON");
    }

    cJSON_AddItemToObject(dataJSON, "profileList", profileListJSON);
    
    LOG_INFO("WebSocket", "create_profile command completed successfully");

    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage ProfileCommandHandler::handleDeleteProfile(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling delete_profile command, cid: %d", request.getCid());
    
    Config& config = Storage::getInstance().config;
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("WebSocket", "delete_profile: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    // 获取要删除的配置文件ID
    cJSON* profileIdItem = cJSON_GetObjectItem(params, "profileId");
    if(!profileIdItem || !profileIdItem->valuestring) {
        LOG_ERROR("WebSocket", "delete_profile: Profile ID not provided");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Profile ID not provided");
    }

    // 查找目标配置文件
    GamepadProfile* targetProfile = nullptr;
    uint8_t numEnabledProfiles = 0;
    uint8_t targetIndex = 0;

    for(uint8_t i = 0; i < NUM_PROFILES; i++) {
        if(config.profiles[i].enabled) {
            numEnabledProfiles++;
            if(strcmp(profileIdItem->valuestring, config.profiles[i].id) == 0) {
                targetProfile = &config.profiles[i];
                targetIndex = i;
            }
        }
    }

    // 如果目标配置文件不存在，则返回错误
    if(!targetProfile) {
        LOG_ERROR("WebSocket", "delete_profile: Profile not found");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Profile not found");
    }

    // 不允许关闭最后一个启用的配置文件
    if(numEnabledProfiles <= 1) {
        LOG_ERROR("WebSocket", "delete_profile: Cannot delete the last active profile");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Cannot delete the last active profile");
    }

    // 禁用配置文件（相当于删除）
    config.profiles[targetIndex].enabled = false;

    // 为了保持内存连续性，将目标配置文件之后的配置文件向前移动一位
    // 保存目标配置文件的副本
    GamepadProfile* tempProfile = (GamepadProfile*)malloc(sizeof(GamepadProfile));
    if(!tempProfile) {
        LOG_ERROR("WebSocket", "delete_profile: Failed to allocate memory");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to allocate memory");
    }
    // 保存目标配置文件
    memcpy(tempProfile, &config.profiles[targetIndex], sizeof(GamepadProfile));
    // 将目标配置文件之后的配置文件向前移动一位
    memmove(&config.profiles[targetIndex], 
            &config.profiles[targetIndex + 1], 
            (NUM_PROFILES - targetIndex - 1) * sizeof(GamepadProfile));
    // 将目标配置文件放到最后一个
    memcpy(&config.profiles[NUM_PROFILES - 1], tempProfile, sizeof(GamepadProfile));
    free(tempProfile);

    // 设置下一个启用的配置文件为默认配置文件
    for(uint8_t i = targetIndex; i >= 0; i --) {
        if(config.profiles[i].enabled) {
            strcpy(config.defaultProfileId, config.profiles[i].id);
            break;
        }
    }

    // 保存配置
    if(!STORAGE_MANAGER.saveConfig()) {
        LOG_ERROR("WebSocket", "delete_profile: Failed to save configuration");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to save configuration");
    }

    // 构建返回数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* profileListJSON = buildProfileListJSON();
    
    if (!profileListJSON) {
        LOG_ERROR("WebSocket", "delete_profile: Failed to build profile list JSON");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to build profile list JSON");
    }

    cJSON_AddItemToObject(dataJSON, "profileList", profileListJSON);
    
    LOG_INFO("WebSocket", "delete_profile command completed successfully");

    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage ProfileCommandHandler::handleSwitchDefaultProfile(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling switch_default_profile command, cid: %d", request.getCid());
    
    Config& config = Storage::getInstance().config;
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("WebSocket", "switch_default_profile: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    // 获取要设置为默认的配置文件ID
    cJSON* profileIdItem = cJSON_GetObjectItem(params, "profileId");
    if(!profileIdItem || !profileIdItem->valuestring) {
        LOG_ERROR("WebSocket", "switch_default_profile: Profile ID not provided");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Profile ID not provided");
    }

    // 查找目标配置文件
    GamepadProfile* targetProfile = nullptr;
    for(uint8_t i = 0; i < NUM_PROFILES; i++) {
        if(strcmp(profileIdItem->valuestring, config.profiles[i].id) == 0) {
            targetProfile = &config.profiles[i];
            break;
        }
    }

    if(!targetProfile) {
        LOG_ERROR("WebSocket", "switch_default_profile: Profile not found");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Profile not found");
    }

    // 检查目标配置文件是否已启用
    if(!targetProfile->enabled) {
        LOG_ERROR("WebSocket", "switch_default_profile: Cannot set disabled profile as default");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Cannot set disabled profile as default");
    }

    strcpy(config.defaultProfileId, targetProfile->id);

    // 保存配置
    if(!STORAGE_MANAGER.saveConfig()) {
        LOG_ERROR("WebSocket", "switch_default_profile: Failed to save configuration");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to save configuration");
    }

    // 构建返回数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* profileListJSON = buildProfileListJSON();
    
    if (!profileListJSON) {
        LOG_ERROR("WebSocket", "switch_default_profile: Failed to build profile list JSON");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to build profile list JSON");
    }

    cJSON_AddItemToObject(dataJSON, "profileList", profileListJSON);
    
    LOG_INFO("WebSocket", "switch_default_profile command completed successfully");

    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

WebSocketDownstreamMessage ProfileCommandHandler::handle(const WebSocketUpstreamMessage& request) {
    const std::string& command = request.getCommand();
    
    if (command == "get_profile_list") {
        return handleGetProfileList(request);
    } else if (command == "get_default_profile") {
        return handleGetDefaultProfile(request);
    } else if (command == "update_profile") {
        return handleUpdateProfile(request);
    } else if (command == "create_profile") {
        return handleCreateProfile(request);
    } else if (command == "delete_profile") {
        return handleDeleteProfile(request);
    } else if (command == "switch_default_profile") {
        return handleSwitchDefaultProfile(request);
    }
    
    return create_error_response(request.getCid(), command, -1, "Unknown command");
} 