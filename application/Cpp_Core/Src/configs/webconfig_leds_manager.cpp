#include "webconfig_leds_manager.hpp"
#include <cJSON.h>
#include "leds/leds_manager.hpp"
#include "storagemanager.hpp"
#include "utils.h"

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

void WebConfigLedsManager::applyPreviewConfig(const LEDProfile& config) {
    APP_DBG("WebConfigLedsManager::applyPreviewConfig - Applying LED preview config");
    APP_DBG("LED Enabled: %s, Effect: %d, Brightness: %d, Speed: %d", 
            config.ledEnabled ? "true" : "false", 
            config.ledEffect, 
            config.ledBrightness, 
            config.ledAnimationSpeed);
    APP_DBG("Colors: #%06lX, #%06lX, #%06lX", config.ledColor1, config.ledColor2, config.ledColor3);
    
    // 保存预览配置
    previewConfig = config;
    previewMode = true;
    
    // 通过LEDsManager应用临时配置
    LEDsManager& ledsManager = LEDsManager::getInstance();
    ledsManager.setTemporaryConfig(previewConfig);
    
    APP_DBG("WebConfigLedsManager::applyPreviewConfig - Preview config applied successfully");
}

void WebConfigLedsManager::clearPreviewConfig() {
    if (previewMode) {
        APP_DBG("WebConfigLedsManager::clearPreviewConfig - Clearing preview mode");
        
        previewMode = false;
        lastButtonMask = 0;
        
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
    
    // 检查按键状态是否发生变化
    bool buttonsChanged = (buttonMask != lastButtonMask);
    
    // 添加基础状态调试
    if (buttonsChanged) {
        APP_DBG("WebConfigLedsManager::update - Button state changed: 0x%08lX -> 0x%08lX", lastButtonMask, buttonMask);
        APP_DBG("WebConfigLedsManager::update - Current effect: %d", previewConfig.ledEffect);
        APP_DBG("WebConfigLedsManager::update - LED enabled: %s", previewConfig.ledEnabled ? "true" : "false");
    }
    
    // 通过LEDsManager更新LED效果
    LEDsManager& ledsManager = LEDsManager::getInstance();
    ledsManager.loop(buttonMask);
    
    // 为涟漪效果提供详细的调试信息
    if (previewConfig.ledEffect == LEDEffect::RIPPLE) {
        if (buttonsChanged) {
            // 检测新按下的按钮
            uint32_t newPressed = buttonMask & ~lastButtonMask;
            uint32_t newReleased = lastButtonMask & ~buttonMask;

        }
        

    }
    
    // 更新最后的按键状态
    lastButtonMask = buttonMask;
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