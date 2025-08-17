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
    
    // 初始化按键状态缓存
    buttonStateCache.resize(NUM_ADC_BUTTONS);

}

WebConfigBtnsManager::~WebConfigBtnsManager() {
    // 清理按键工作器
    cleanupButtonWorkers();
}

void WebConfigBtnsManager::setupButtonWorkers() {
    ADCBtnsError adcResult = ADC_BTNS_WORKER.setup();

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

void WebConfigBtnsManager::setButtonPerformanceMonitoringCallback(ButtonPerformanceMonitoringCallback callback) {
    buttonPerformanceMonitoringCallback = callback;
    APP_DBG("WebConfigBtnsManager::setButtonPerformanceMonitoringCallback - Button performance monitoring callback set");
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

    // 技术测试模式下，只处理ADC按键，并且每一次循环都send websocket 传输当前的状态和值

    if(isTestModeEnabled_) {
        
        currentMask = ADC_BTNS_WORKER.read();

        // if(currentMask != previousMask && buttonPerformanceMonitoringCallback) {
        if(buttonPerformanceMonitoringCallback) {
            buttonPerformanceMonitoringCallback();
        }
        // }

        previousMask = currentMask;

    } else {

        // 处理所有按键，包括ADC和GPIO按键
        currentMask = ADC_BTNS_WORKER.read() | GPIO_BTNS_WORKER.read();

        if(currentMask != previousMask && buttonStateChangedCallback) {
            buttonStateChangedCallback();
        }

        previousMask = currentMask;
    }

}

uint32_t WebConfigBtnsManager::getCurrentMask() const {
    return currentMask;
}

// ========== WebConfig模式专用方法实现 ==========
// 注意：新的实现方式不再需要修改Profile配置，而是直接传入外部配置到ADC工作器


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

// ============================================================================
// 按键性能监控数据构建实现
// ============================================================================

std::vector<uint8_t> WebConfigBtnsManager::buildButtonPerformanceMonitoringBinaryData() {
    // 收集所有ADC按键的状态
    std::vector<ButtonPerformanceData> buttonDataList;
    
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        ADCBtn* btn = ADC_BTNS_WORKER.getButtonState(i);
        if (btn) {
            ButtonPerformanceData buttonData;
            buttonData.buttonIndex = i;
            buttonData.virtualPin = ADC_BTNS_WORKER.getButtonVirtualPin(i);
            buttonData.isPressed = btn->state == ButtonState::PRESSED ? 1 : 0;
            buttonData.currentDistance = ADC_BTNS_WORKER.getDistanceByValue(btn, btn->currentValue);
            buttonData.pressTriggerDistance = ADC_BTNS_WORKER.getDistanceByValue(btn, btn->pressTriggerSnapshot);
            buttonData.releaseTriggerDistance = ADC_BTNS_WORKER.getDistanceByValue(btn, btn->releaseTriggerSnapshot);
            buttonData.pressStartDistance = ADC_BTNS_WORKER.getDistanceByValue(btn, btn->pressStartSnapshot);
            buttonData.releaseStartDistance = ADC_BTNS_WORKER.getDistanceByValue(btn, btn->releaseStartSnapshot);
            buttonData.reserved = 0;
            
            buttonDataList.push_back(buttonData);
        }
    }
    
    // 计算总数据大小：头部 + 按键数据数组
    size_t totalSize = sizeof(ButtonPerformanceMonitoringBinaryData) + 
                      buttonDataList.size() * sizeof(ButtonPerformanceData);
    
    // 创建完整的数据缓冲区
    std::vector<uint8_t> buffer(totalSize);
    uint8_t* dataPtr = buffer.data();
    
    // 构建头部数据
    ButtonPerformanceMonitoringBinaryData header;
    header.command = BUTTON_PERFORMANCE_MONITORING_CMD;
    header.isActive = isActive() ? 1 : 0;
    header.buttonCount = static_cast<uint8_t>(buttonDataList.size());
    header.reserved = 0;
    header.timestamp = HAL_GetTick();
    
    // 获取最大物理行程（从当前映射获取）
    const ADCValuesMapping* currentMapping = ADC_BTNS_WORKER.getCurrentMapping();
    if (currentMapping) {
        header.maxTravelDistance = (currentMapping->length - 1) * currentMapping->step;
    } else {
        header.maxTravelDistance = 0.0f; // 如果没有映射，设为0
    }
    
    // 复制头部数据
    memcpy(dataPtr, &header, sizeof(ButtonPerformanceMonitoringBinaryData));
    dataPtr += sizeof(ButtonPerformanceMonitoringBinaryData);
    
    // 复制按键数据
    for (const auto& buttonData : buttonDataList) {
        memcpy(dataPtr, &buttonData, sizeof(ButtonPerformanceData));
        dataPtr += sizeof(ButtonPerformanceData);
    }
    
    return buffer;
}