#include "webconfig_leds_manager.hpp"
#include <cJSON.h>
#include "leds/leds_manager.hpp"
#include "storagemanager.hpp"
#include "utils.h"
#include "board_cfg.h"

WebConfigLedsManager& WebConfigLedsManager::getInstance() {
    static WebConfigLedsManager instance;
    return instance;
}

WebConfigLedsManager::WebConfigLedsManager() 
    : previewMode(false), lastButtonMask(0) {
    
    
    // 初始化预览配置为默认值
    previewConfig = {
        .ledEnabled = true,
        .ledEffect = LEDEffect::STATIC,
        .ledColor1 = 0x00FF00,
        .ledColor2 = 0x0000FF, 
        .ledColor3 = 0xFF0000,
        .ledBrightness = 75,
        .ledAnimationSpeed = 3
    };
}

WebConfigLedsManager::~WebConfigLedsManager() {
    // 确保在析构时清除预览模式
    clearPreviewConfig();
}

void WebConfigLedsManager::initializeEnabledKeysMask() {
    const bool* enabledKeys = STORAGE_MANAGER.getDefaultGamepadProfile()->keysConfig.keysEnableTag;
    enabledKeysMask = 0;
    for(uint8_t i = 0; i < 32; i++) {
        if(i < NUM_ADC_BUTTONS) {
            enabledKeysMask |= (enabledKeys[i] ? (1 << i) : 0);
        } else {
            enabledKeysMask |= (1 << i);
        }
    }
    APP_DBG("WebConfigLedsManager: initialized enabled keys mask = 0x%08lX", enabledKeysMask);
}

void WebConfigLedsManager::applyPreviewConfig(const LEDProfile& config) {
    APP_DBG("WebConfigLedsManager::applyPreviewConfig - Applying LED preview config");
    APP_DBG("LED Enabled: %s, Effect: %d, Brightness: %d, Speed: %d", 
            config.ledEnabled ? "true" : "false", 
            config.ledEffect, 
            config.ledBrightness, 
            config.ledAnimationSpeed);
    APP_DBG("Colors: #%06lX, #%06lX, #%06lX", config.ledColor1, config.ledColor2, config.ledColor3);
    
    initializeEnabledKeysMask();

    // 保存预览配置
    previewConfig = config;
    previewMode = true;
    
    // 通过LEDsManager应用临时配置
    LEDsManager& ledsManager = LEDsManager::getInstance();
    ledsManager.setTemporaryConfig(previewConfig, enabledKeysMask);
    
    APP_DBG("WebConfigLedsManager::applyPreviewConfig - Preview config applied successfully");
}

void WebConfigLedsManager::clearPreviewConfig() {
    if (previewMode) {
        APP_DBG("WebConfigLedsManager::clearPreviewConfig - Clearing preview mode");
        
        previewMode = false;
        lastButtonMask = 0;
        
        // 更新启用按键掩码（以防在预览过程中按键启用状态发生了变化）
        initializeEnabledKeysMask();
        
        // 恢复LEDsManager的默认配置
        LEDsManager& ledsManager = LEDsManager::getInstance();
        ledsManager.restoreDefaultConfig();
        // 关闭led
        ledsManager.deinit();
        
        APP_DBG("WebConfigLedsManager::clearPreviewConfig - Preview mode cleared");
    }
}

bool WebConfigLedsManager::isInPreviewMode() const {
    return previewMode;
}

void WebConfigLedsManager::update(uint32_t buttonMask) {
    if (!previewMode) {
        return;
    }
    
    // 只保留启用按键的状态，禁用的按键强制为未按下状态
    uint32_t filteredButtonMask = buttonMask & enabledKeysMask;
    
    // 检查按键状态是否发生变化
    bool buttonsChanged = (filteredButtonMask != lastButtonMask);
    
    // 添加基础状态调试
    if (buttonsChanged) {
        APP_DBG("WebConfigLedsManager::update - Button state changed: 0x%08lX -> 0x%08lX (filtered: 0x%08lX)", 
                lastButtonMask, buttonMask, filteredButtonMask);
        APP_DBG("WebConfigLedsManager::update - Enabled keys mask: 0x%08lX", enabledKeysMask);
        APP_DBG("WebConfigLedsManager::update - Current effect: %d", previewConfig.ledEffect);
        APP_DBG("WebConfigLedsManager::update - LED enabled: %s", previewConfig.ledEnabled ? "true" : "false");
    }
    
    // 通过LEDsManager更新LED效果，使用过滤后的按键状态
    LEDsManager& ledsManager = LEDsManager::getInstance();
    ledsManager.loop(filteredButtonMask);
    
    // 为涟漪效果提供详细的调试信息
    if (previewConfig.ledEffect == LEDEffect::RIPPLE) {
        if (buttonsChanged) {
            // 检测新按下的按钮
            uint32_t newPressed = filteredButtonMask & ~lastButtonMask;
            uint32_t newReleased = lastButtonMask & ~filteredButtonMask;

        }
        

    }
    
    // 更新最后的按键状态（使用过滤后的状态）
    lastButtonMask = filteredButtonMask;
}

std::string WebConfigLedsManager::toJSON() const {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return "{}";
    }
    
    // 添加预览模式状态
    cJSON_AddBoolToObject(root, "previewMode", previewMode);
    
    // 添加当前配置
    cJSON* configObj = cJSON_CreateObject();
    if (configObj) {
        cJSON_AddBoolToObject(configObj, "ledEnabled", previewConfig.ledEnabled);
        cJSON_AddNumberToObject(configObj, "ledEffect", static_cast<int>(previewConfig.ledEffect));
        cJSON_AddNumberToObject(configObj, "ledBrightness", previewConfig.ledBrightness);
        cJSON_AddNumberToObject(configObj, "ledAnimationSpeed", previewConfig.ledAnimationSpeed);
        
        // 添加颜色数组
        cJSON* colorsArray = cJSON_CreateArray();
        if (colorsArray) {
            char colorStr[8];
            sprintf(colorStr, "#%06lX", previewConfig.ledColor1);
            cJSON_AddItemToArray(colorsArray, cJSON_CreateString(colorStr));
            sprintf(colorStr, "#%06lX", previewConfig.ledColor2);
            cJSON_AddItemToArray(colorsArray, cJSON_CreateString(colorStr));
            sprintf(colorStr, "#%06lX", previewConfig.ledColor3);
            cJSON_AddItemToArray(colorsArray, cJSON_CreateString(colorStr));
            
            cJSON_AddItemToObject(configObj, "ledColors", colorsArray);
        }
        
        cJSON_AddItemToObject(root, "currentConfig", configObj);
    }
    
    // 添加按键状态
    cJSON_AddNumberToObject(root, "lastButtonMask", lastButtonMask);
    
    char* str = cJSON_PrintUnformatted(root);
    std::string result;
    if (str) {
        result = str;
        free(str);
    } else {
        result = "{}";
    }
    
    cJSON_Delete(root);
    return result;
} 