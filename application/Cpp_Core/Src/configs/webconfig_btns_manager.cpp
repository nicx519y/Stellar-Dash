#include "webconfig_btns_manager.hpp"
#include <cJSON.h>
#include "adc_btns/adc_btns_worker.hpp"
#include "gpio_btns/gpio_btns_worker.hpp"
#include "board_cfg.h"
#include "storagemanager.hpp"
#include "stm32h7xx_hal.h"  // 为HAL_GetTick()
#include "configs/common_command_handler.hpp"  // 为WebSocket推送

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
    
    // 初始化技术测试模式
    isTestModeEnabled_ = false;
    
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

    // 在技术测试模式下，不更新按键掩码，只处理ADC按键触发事件
    if (isTestModeEnabled_) {
        // 清空事件收集器
        testEventCollector.clear();
        
        // 仍然调用read()以触发ADC按键事件检测，但不使用返回的掩码
        ADC_BTNS_WORKER.read();
        
        // 检查ADC按键状态变化并处理测试事件
        for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
            bool isPressEvent;
            uint16_t adcValue;
            if (ADC_BTNS_WORKER.getButtonStateChange(i, isPressEvent, adcValue)) {
                processADCBtnTestEvent(i, isPressEvent, adcValue);
            }
        }
        
        // 清除状态变化标志
        ADC_BTNS_WORKER.clearButtonStateChange();
        
        // 如果有收集到测试事件，一次性推送
        if (!testEventCollector.empty()) {
            CommonCommandHandler::getInstance().sendADCBtnTestEventNotification(testEventCollector);
        }
        
        // GPIO按键仍然正常处理
        uint32_t gpioMask = GPIO_BTNS_WORKER.read();
        currentMask = gpioMask;
    } else {
        // 正常模式：处理所有按键
        currentMask = ADC_BTNS_WORKER.read() | GPIO_BTNS_WORKER.read();
    }

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

// ========== 技术测试模式相关方法实现 ==========

void WebConfigBtnsManager::setADCBtnTestCallback(ADCBtnTestCallback callback) {
    adcBtnTestCallback = callback;
    APP_DBG("WebConfigBtnsManager::setADCBtnTestCallback - ADC button test callback set");
}

void WebConfigBtnsManager::enableTestMode(bool enabled) {
    isTestModeEnabled_ = enabled;
    
    if (enabled) {
        APP_DBG("WebConfigBtnsManager::enableTestMode - Test mode enabled");
    } else {
        APP_DBG("WebConfigBtnsManager::enableTestMode - Test mode disabled");
    }
}

bool WebConfigBtnsManager::isTestModeEnabled() const {
    return isTestModeEnabled_;
}

void WebConfigBtnsManager::processADCBtnTestEvent(uint8_t buttonIndex, bool isPressEvent, uint16_t adcValue) {
    if (!isTestModeEnabled_ || !adcBtnTestCallback) {
        return;
    }
    
    // 获取按钮详细信息
    uint16_t currentAdcValue, limitValue;
    float triggerDistance, limitValueDistance;
    
    if (!ADC_BTNS_WORKER.getButtonDetails(buttonIndex, currentAdcValue, triggerDistance, limitValue, limitValueDistance)) {
        APP_ERR("WebConfigBtnsManager::processADCBtnTestEvent - Failed to get button details for index %d", buttonIndex);
        return;
    }
    
    // 获取虚拟引脚
    uint8_t virtualPin = ADC_BTNS_WORKER.getButtonVirtualPin(buttonIndex);
    if (virtualPin == 0xFF) {
        APP_ERR("WebConfigBtnsManager::processADCBtnTestEvent - Invalid virtual pin for button index %d", buttonIndex);
        return;
    }
    
    // 构造测试事件
    ADCBtnTestEvent testEvent;
    testEvent.buttonIndex = buttonIndex;
    testEvent.virtualPin = virtualPin;
    testEvent.adcValue = currentAdcValue;
    testEvent.triggerDistance = triggerDistance;
    testEvent.limitValueDistance = limitValueDistance;
    testEvent.limitValue = limitValue;
    testEvent.isPressEvent = isPressEvent;
    testEvent.timestamp = HAL_GetTick();
    
    // 添加到事件收集器
    testEventCollector.push_back(testEvent);
    
    // 调用回调（如果设置了的话）
    if (adcBtnTestCallback) {
        adcBtnTestCallback(testEvent);
    }
    
    APP_DBG("WebConfigBtnsManager::processADCBtnTestEvent - Button %d (VP:%d) %s event: ADC=%d, Travel=%.2fmm, Limit=%.2fmm", 
            buttonIndex, virtualPin, isPressEvent ? "PRESS" : "RELEASE", 
            currentAdcValue, triggerDistance, limitValueDistance);
}

/*
 * 技术测试模式使用示例：
 * 
 * // 1. 设置测试回调
 * WEBCONFIG_BTNS_MANAGER.setADCBtnTestCallback([](const ADCBtnTestEvent& event) {
 *     printf("Button %d (VP:%d) %s: ADC=%d, Travel=%.2fmm, Limit=%.2fmm\n",
 *            event.buttonIndex, event.virtualPin, 
 *            event.isPressEvent ? "PRESS" : "RELEASE",
 *            event.adcValue, event.triggerDistance, event.limitValueDistance);
 * });
 * 
 * // 2. 启用技术测试模式
 * WEBCONFIG_BTNS_MANAGER.enableTestMode(true);
 * 
 * // 3. 启动按键工作器
 * WEBCONFIG_BTNS_MANAGER.startButtonWorkers();
 * 
 * // 4. 在主循环中调用update()
 * WEBCONFIG_BTNS_MANAGER.update();
 * 
 * // 5. 禁用技术测试模式（恢复正常模式）
 * WEBCONFIG_BTNS_MANAGER.enableTestMode(false);
 */ 