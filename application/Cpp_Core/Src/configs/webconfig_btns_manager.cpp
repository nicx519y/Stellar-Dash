#include "webconfig_btns_manager.hpp"
#include <cJSON.h>
#include "adc_btns/adc_btns_worker.hpp"
#include "gpio_btns/gpio_btns_worker.hpp"
#include "board_cfg.h"

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
    
    // 初始化按键工作器
    setupButtonWorkers();
    isWorkerActive = true;  // 默认启动状态
}

WebConfigBtnsManager::~WebConfigBtnsManager() {
    // 清理按键工作器
    cleanupButtonWorkers();
}

void WebConfigBtnsManager::setupButtonWorkers() {
    // 设置ADC按键工作器
    ADCBtnsError adcResult = ADC_BTNS_WORKER.setup();
    if (adcResult != ADCBtnsError::SUCCESS) {
        // TODO: 添加错误处理日志
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

std::string WebConfigBtnsManager::toJSON() const {
    cJSON* root = cJSON_CreateObject();
    cJSON* adcButtons = cJSON_CreateArray();
    cJSON* gpioButtons = cJSON_CreateArray();
    
    // 添加ADC按键状态
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        cJSON* btnObj = cJSON_CreateObject();
        cJSON_AddNumberToObject(btnObj, "index", i);
        cJSON_AddBoolToObject(btnObj, "pressed", btnStates[i]);
        cJSON_AddStringToObject(btnObj, "type", "adc");
        cJSON_AddItemToArray(adcButtons, btnObj);
    }
    
    // 添加GPIO按键状态
    for (uint8_t i = 0; i < NUM_GPIO_BUTTONS; i++) {
        cJSON* btnObj = cJSON_CreateObject();
        cJSON_AddNumberToObject(btnObj, "index", i);
        cJSON_AddBoolToObject(btnObj, "pressed", btnStates[NUM_ADC_BUTTONS + i]);
        cJSON_AddStringToObject(btnObj, "type", "gpio");
        cJSON_AddItemToArray(gpioButtons, btnObj);
    }
    
    cJSON_AddItemToObject(root, "adcButtons", adcButtons);
    cJSON_AddItemToObject(root, "gpioButtons", gpioButtons);
    cJSON_AddNumberToObject(root, "totalButtons", NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS);
    
    char* str = cJSON_PrintUnformatted(root);
    std::string result(str);
    cJSON_Delete(root);
    free(str);
    return result;
} 