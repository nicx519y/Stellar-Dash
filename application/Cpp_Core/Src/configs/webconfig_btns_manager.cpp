#include "webconfig_btns_manager.hpp"
#include <cJSON.h>
#include "adc_btns/adc_btns_worker.hpp"
#include "gpio_btns/gpio_btns_worker.hpp"
#include "board_cfg.h"
#include "storagemanager.hpp"

// 前向声明，避免循环依赖
class CommonCommandHandler;

WebConfigBtnsManager& WebConfigBtnsManager::getInstance() {
    static WebConfigBtnsManager instance;
    return instance;
}

WebConfigBtnsManager::WebConfigBtnsManager() {
    
    // 初始化触发掩码
    currentMask = 0;
    previousMask = 0;
    
    // 初始化工作器活跃状态
    isWorkerActive = false;
    
    // 初始化ADC按键WebConfig配置
    adcButtonConfigs.resize(NUM_ADC_BUTTONS);

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
        setupButtonWorkers(); // 设置专用配置，并且开始ADC采样
        isWorkerActive = true;
    }
}

void WebConfigBtnsManager::stopButtonWorkers() {
    if (isWorkerActive) {
        cleanupButtonWorkers();
        isWorkerActive = false;
        
        currentMask = 0;
        previousMask = 0;
    }
}

bool WebConfigBtnsManager::isActive() const {
    return isWorkerActive;
}

uint8_t WebConfigBtnsManager::getTotalButtonCount() const {
    return NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS;
}

void WebConfigBtnsManager::setButtonStateChangedCallback(ButtonStateChangedCallback callback) {
    buttonStateChangedCallback = callback;
    APP_DBG("WebConfigBtnsManager::setButtonStateChangedCallback - Button state changed callback set");
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

void WebConfigBtnsManager::update() {
    if(!isWorkerActive) {
        return;
    }

    currentMask = ADC_BTNS_WORKER.read() | GPIO_BTNS_WORKER.read();

    if(currentMask != previousMask && buttonStateChangedCallback) {
        buttonStateChangedCallback();
    }

    // APP_DBG("WebConfigBtnsManager::update - Current mask: 0x%08lX", currentMask);

    previousMask = currentMask;
}

uint32_t WebConfigBtnsManager::getCurrentMask() const {
    return currentMask;
}

// ========== WebConfig模式专用方法实现 ==========
// 注意：新的实现方式不再需要修改Profile配置，而是直接传入外部配置到ADC工作器 