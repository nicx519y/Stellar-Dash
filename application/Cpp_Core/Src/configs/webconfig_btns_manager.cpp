#include "webconfig_btns_manager.hpp"
#include <cJSON.h>
#include "adc_btns/adc_btns_worker.hpp"
#include "gpio_btns/gpio_btns_worker.hpp"
#include "board_cfg.h"
#include "storagemanager.hpp"

WebConfigBtnsManager& WebConfigBtnsManager::getInstance() {
    static WebConfigBtnsManager instance;
    return instance;
}

WebConfigBtnsManager::WebConfigBtnsManager() {
    // 初始化按键状态数组，总按键数量 = ADC按键 + GPIO按键
    btnStates.resize(NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS, false);
    prevBtnStates.resize(NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS, false);
    
    // 初始化触发掩码
    triggerMask = 0;
    
    // 初始化工作器活跃状态
    isWorkerActive = false;
    
    // 初始化ADC按键WebConfig配置
    adcButtonConfigs.resize(NUM_ADC_BUTTONS);
    resetADCButtonsConfig(); // 设置默认配置
    
    // 初始化按键工作器
    setupButtonWorkers();
    isWorkerActive = true;  // 默认启动状态
}

WebConfigBtnsManager::~WebConfigBtnsManager() {
    // 清理按键工作器
    cleanupButtonWorkers();
}

void WebConfigBtnsManager::setupButtonWorkers() {
    // WebConfig模式下一律使用外部配置
    // 转换WebConfig配置为外部配置格式
    ExternalADCButtonConfig externalConfigs[NUM_ADC_BUTTONS];
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        const WebConfigADCButtonConfig& webConfig = adcButtonConfigs[i];
        externalConfigs[i].pressAccuracy = webConfig.pressAccuracy;
        externalConfigs[i].releaseAccuracy = webConfig.releaseAccuracy;
        externalConfigs[i].topDeadzone = webConfig.topDeadzone;
        externalConfigs[i].bottomDeadzone = webConfig.bottomDeadzone;
    }
    
    ADCBtnsError adcResult = ADC_BTNS_WORKER.setup(externalConfigs);
    APP_DBG("WebConfigBtnsManager::setupButtonWorkers - Using WebConfig external configurations");
    
    if (adcResult != ADCBtnsError::SUCCESS) {
        APP_ERR("WebConfigBtnsManager::setupButtonWorkers - ADC setup failed with error: %d", (int)adcResult);
    }
    
    // 设置GPIO按键工作器
    GPIO_BTNS_WORKER.setup();
}

void WebConfigBtnsManager::cleanupButtonWorkers() {
    // 清理ADC按键工作器
    ADC_BTNS_WORKER.deinit();
    
    // GPIO按键工作器通过析构函数自动清理
}

void WebConfigBtnsManager::startButtonWorkers() {
    if (!isWorkerActive) {
        APP_DBG("WebConfigBtnsManager::startButtonWorkers - starting button workers");
        setupButtonWorkers();
        isWorkerActive = true;
    }
}

void WebConfigBtnsManager::stopButtonWorkers() {
    if (isWorkerActive) {
        cleanupButtonWorkers();
        isWorkerActive = false;
        
        // 清空所有状态
        std::fill(btnStates.begin(), btnStates.end(), false);
        std::fill(prevBtnStates.begin(), prevBtnStates.end(), false);
        triggerMask = 0;
    }
}

bool WebConfigBtnsManager::isActive() const {
    return isWorkerActive;
}

void WebConfigBtnsManager::update() {
    // 只有在工作器活跃时才更新按键状态
    if (!isWorkerActive) {
        return;
    }
    
    // 保存上一次状态
    prevBtnStates = btnStates;
    
    // 读取ADC按键状态
    uint32_t adcBtnMask = ADC_BTNS_WORKER.read();
    
    // 读取GPIO按键状态  
    uint32_t gpioBtnMask = GPIO_BTNS_WORKER.read();

    // 更新ADC按键状态（索引 0 到 NUM_ADC_BUTTONS-1）
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        // ADC按键的虚拟引脚从0开始
        btnStates[i] = (adcBtnMask & (1U << i)) != 0;
    }
    
    // 更新GPIO按键状态（索引 NUM_ADC_BUTTONS 到 NUM_ADC_BUTTONS+NUM_GPIO_BUTTONS-1）
    for (uint8_t i = 0; i < NUM_GPIO_BUTTONS; i++) {
        // GPIO按键的虚拟引脚从NUM_ADC_BUTTONS开始
        uint8_t virtualPin = NUM_ADC_BUTTONS + i;
        uint8_t arrayIndex = NUM_ADC_BUTTONS + i;
        btnStates[arrayIndex] = (gpioBtnMask & (1U << virtualPin)) != 0;
    }
    
    // 检查按键触发（从0到1的跳变）
    checkButtonTriggers();
    
    // 动态校准ADC按键
    ADC_BTNS_WORKER.dynamicCalibration();
}

void WebConfigBtnsManager::checkButtonTriggers() {
    // 检查所有按键的0->1跳变
    for (uint8_t i = 0; i < btnStates.size() && i < 32; i++) {
        // 检测从false到true的跳变（按键按下瞬间）
        if (!prevBtnStates[i] && btnStates[i]) {
            // 设置对应位的触发标志
            triggerMask |= (1U << i);
        }
    }
}

uint32_t WebConfigBtnsManager::getAndClearTriggerMask() {
    uint32_t result = triggerMask;
    triggerMask = 0; // 清空触发掩码
    return result;
}

bool WebConfigBtnsManager::hasTriggers() const {
    return triggerMask != 0;
}

uint8_t WebConfigBtnsManager::getTotalButtonCount() const {
    return NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS;
}

std::vector<bool> WebConfigBtnsManager::getButtonStates() const {
    return btnStates;
}

// ========== ADC按键WebConfig模式专用配置接口实现 ==========

bool WebConfigBtnsManager::setADCButtonConfig(uint8_t buttonIndex, const WebConfigADCButtonConfig& config) {
    if (buttonIndex >= NUM_ADC_BUTTONS) {
        APP_ERR("WebConfigBtnsManager::setADCButtonConfig - Invalid button index: %d", buttonIndex);
        return false;
    }
    
    // 直接设置配置，不进行范围验证
    adcButtonConfigs[buttonIndex] = config;
    
    APP_DBG("WebConfigBtnsManager::setADCButtonConfig - Button %d config updated: "
            "pressAccuracy=%.3f, releaseAccuracy=%.3f, topDeadzone=%.3f, bottomDeadzone=%.3f, highSensitivity=%s",
            buttonIndex, config.pressAccuracy, config.releaseAccuracy, 
            config.topDeadzone, config.bottomDeadzone,
            config.enableHighSensitivity ? "true" : "false");
    
    // 如果工作器处于活跃状态，重新初始化以应用新配置
    if (isWorkerActive) {
        // 重新初始化ADC工作器以应用新配置
        ADC_BTNS_WORKER.deinit();
        setupButtonWorkers();
    }
    
    return true;
}

WebConfigADCButtonConfig WebConfigBtnsManager::getADCButtonConfig(uint8_t buttonIndex) const {
    if (buttonIndex >= NUM_ADC_BUTTONS) {
        APP_ERR("WebConfigBtnsManager::getADCButtonConfig - Invalid button index: %d", buttonIndex);
        return WebConfigADCButtonConfig(); // 返回默认配置
    }
    
    return adcButtonConfigs[buttonIndex];
}

void WebConfigBtnsManager::setAllADCButtonsConfig(const WebConfigADCButtonConfig& config) {
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        adcButtonConfigs[i] = config;
    }
    
    APP_DBG("WebConfigBtnsManager::setAllADCButtonsConfig - All ADC buttons config updated: "
            "pressAccuracy=%.3f, releaseAccuracy=%.3f, topDeadzone=%.3f, bottomDeadzone=%.3f, highSensitivity=%s",
            config.pressAccuracy, config.releaseAccuracy, 
            config.topDeadzone, config.bottomDeadzone,
            config.enableHighSensitivity ? "true" : "false");
    
    // 如果工作器处于活跃状态，重新初始化以应用新配置
    if (isWorkerActive) {
        // 重新初始化ADC工作器以应用新配置
        ADC_BTNS_WORKER.deinit();
        setupButtonWorkers();
    }
}

void WebConfigBtnsManager::resetADCButtonsConfig() {
    WebConfigADCButtonConfig defaultConfig; // 使用宏定义的默认值
    
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        adcButtonConfigs[i] = defaultConfig;
    }
    
    APP_DBG("WebConfigBtnsManager::resetADCButtonsConfig - All ADC buttons reset to default config: "
            "pressAccuracy=%.3f, releaseAccuracy=%.3f, topDeadzone=%.3f, bottomDeadzone=%.3f",
            defaultConfig.pressAccuracy, defaultConfig.releaseAccuracy,
            defaultConfig.topDeadzone, defaultConfig.bottomDeadzone);
    
    // 如果工作器处于活跃状态，重新初始化以应用新配置
    if (isWorkerActive) {
        // 重新初始化ADC工作器以应用新配置
        ADC_BTNS_WORKER.deinit();
        setupButtonWorkers();
    }
}

std::string WebConfigBtnsManager::getADCConfigJSON() const {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return "{}";
    }
    
    cJSON* buttonsArray = cJSON_CreateArray();
    if (!buttonsArray) {
        cJSON_Delete(root);
        return "{}";
    }
    
    // 为每个ADC按键创建配置对象
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        const WebConfigADCButtonConfig& config = adcButtonConfigs[i];
        
        cJSON* buttonObj = cJSON_CreateObject();
        if (buttonObj) {
            cJSON_AddNumberToObject(buttonObj, "index", i);
            cJSON_AddNumberToObject(buttonObj, "pressAccuracy", config.pressAccuracy);
            cJSON_AddNumberToObject(buttonObj, "releaseAccuracy", config.releaseAccuracy);
            cJSON_AddNumberToObject(buttonObj, "topDeadzone", config.topDeadzone);
            cJSON_AddNumberToObject(buttonObj, "bottomDeadzone", config.bottomDeadzone);
            cJSON_AddBoolToObject(buttonObj, "enableHighSensitivity", config.enableHighSensitivity);
            
            cJSON_AddItemToArray(buttonsArray, buttonObj);
        }
    }
    
    cJSON_AddItemToObject(root, "adcButtons", buttonsArray);
    cJSON_AddNumberToObject(root, "totalADCButtons", NUM_ADC_BUTTONS);
    
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

bool WebConfigBtnsManager::setADCConfigFromJSON(const std::string& jsonStr) {
    cJSON* root = cJSON_Parse(jsonStr.c_str());
    if (!root) {
        APP_ERR("WebConfigBtnsManager::setADCConfigFromJSON - Invalid JSON format");
        return false;
    }
    
    cJSON* buttonsArray = cJSON_GetObjectItem(root, "adcButtons");
    if (!buttonsArray || !cJSON_IsArray(buttonsArray)) {
        APP_ERR("WebConfigBtnsManager::setADCConfigFromJSON - Missing or invalid 'adcButtons' array");
        cJSON_Delete(root);
        return false;
    }
    
    int arraySize = cJSON_GetArraySize(buttonsArray);
    bool success = true;
    
    for (int i = 0; i < arraySize && i < NUM_ADC_BUTTONS; i++) {
        cJSON* buttonObj = cJSON_GetArrayItem(buttonsArray, i);
        if (!buttonObj) continue;
        
        // 获取按键索引
        cJSON* indexItem = cJSON_GetObjectItem(buttonObj, "index");
        if (!indexItem || !cJSON_IsNumber(indexItem)) continue;
        
        uint8_t buttonIndex = (uint8_t)indexItem->valueint;
        if (buttonIndex >= NUM_ADC_BUTTONS) continue;
        
        // 解析配置参数，使用当前配置作为默认值
        WebConfigADCButtonConfig config = adcButtonConfigs[buttonIndex];
        
        cJSON* item;
        if ((item = cJSON_GetObjectItem(buttonObj, "pressAccuracy")) && cJSON_IsNumber(item)) {
            config.pressAccuracy = (float)item->valuedouble;
        }
        if ((item = cJSON_GetObjectItem(buttonObj, "releaseAccuracy")) && cJSON_IsNumber(item)) {
            config.releaseAccuracy = (float)item->valuedouble;
        }
        if ((item = cJSON_GetObjectItem(buttonObj, "topDeadzone")) && cJSON_IsNumber(item)) {
            config.topDeadzone = (float)item->valuedouble;
        }
        if ((item = cJSON_GetObjectItem(buttonObj, "bottomDeadzone")) && cJSON_IsNumber(item)) {
            config.bottomDeadzone = (float)item->valuedouble;
        }
        if ((item = cJSON_GetObjectItem(buttonObj, "enableHighSensitivity")) && cJSON_IsBool(item)) {
            config.enableHighSensitivity = cJSON_IsTrue(item);
        }
        
        // 应用配置（不再进行范围验证）
        if (!setADCButtonConfig(buttonIndex, config)) {
            success = false;
        }
    }
    
    cJSON_Delete(root);
    
    if (success) {
        APP_DBG("WebConfigBtnsManager::setADCConfigFromJSON - ADC config updated from JSON successfully");
    } else {
        APP_ERR("WebConfigBtnsManager::setADCConfigFromJSON - Some ADC config updates failed");
    }
    
    return success;
}

// ========== WebConfig模式专用方法实现 ==========
// 注意：新的实现方式不再需要修改Profile配置，而是直接传入外部配置到ADC工作器 