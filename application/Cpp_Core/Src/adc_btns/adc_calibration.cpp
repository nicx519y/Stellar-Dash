#include "adc_btns/adc_calibration.hpp"
#include "adc_btns/adc_manager.hpp"
#include "board_cfg.h"

// 添加WS2812B驱动头文件
extern "C" {
#include "pwm-ws2812b.h"
// 声明WS2812B内部函数
void LEDDataToDMABuffer(const uint16_t start, const uint16_t length);
}

// 移除全局实例定义，改为单例模式

ADCCalibrationManager::ADCCalibrationManager() {
    APP_DBG("ADCCalibrationManager constructor - creating global instance");
    initializeButtonStates();
}

ADCCalibrationManager::~ADCCalibrationManager() {
    stopCalibration();
}

/**
 * 开始手动校准
 */
ADCBtnsError ADCCalibrationManager::startManualCalibration() {
    if (calibrationActive) {
        return ADCBtnsError::CALIBRATION_IN_PROGRESS;
    }
    
    // 重置所有状态
    initializeButtonStates();
    
    // 加载已有的校准数据
    loadExistingCalibration();
    
    calibrationActive = true;
    
    // 同时启动所有未校准按键的校准
    uint8_t uncalibratedCount = 0;
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        if (!buttonStates[i].isCalibrated) {
            // 设置按键为顶部值采样状态（按键释放状态）
            setButtonPhase(i, CalibrationPhase::TOP_SAMPLING);
            setButtonLEDColor(i, CalibrationLEDColor::CYAN);
            buttonStates[i].lastSampleTime = 0; // 允许立即采样
            uncalibratedCount++;
        } else {
            // 已校准的按键显示绿色
            setButtonLEDColor(i, CalibrationLEDColor::GREEN);
        }
    }
    
    if (uncalibratedCount == 0) {
        // 所有按键都已校准
        calibrationActive = false;
        APP_DBG("All buttons are already calibrated");
        updateAllLEDs();
        return ADCBtnsError::SUCCESS;
    }
    
    // 更新所有LED状态
    updateAllLEDs();
    
    APP_DBG("Manual calibration started for %d buttons simultaneously", uncalibratedCount);
    
    return ADCBtnsError::SUCCESS;
}

/**
 * 停止校准
 */
ADCBtnsError ADCCalibrationManager::stopCalibration() {
    if (!calibrationActive) {
        return ADCBtnsError::CALIBRATION_NOT_STARTED;
    }
    
    calibrationActive = false;
    
    // 更新所有LED状态
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        if (buttonStates[i].isCalibrated) {
            setButtonLEDColor(i, CalibrationLEDColor::GREEN);
        } else {
            setButtonLEDColor(i, CalibrationLEDColor::RED);
        }
    }
    
    updateAllLEDs();
    
    APP_DBG("Manual calibration stopped");
    
    return ADCBtnsError::SUCCESS;
}

/**
 * 重置单个按键校准
 */
ADCBtnsError ADCCalibrationManager::resetButtonCalibration(uint8_t buttonIndex) {
    if (buttonIndex >= NUM_ADC_BUTTONS) {
        return ADCBtnsError::INVALID_PARAMS;
    }
    
    ButtonCalibrationState& state = buttonStates[buttonIndex];
    state.phase = CalibrationPhase::IDLE;
    state.isCalibrated = false;
    state.sampleCount = 0;
    state.minSample = UINT16_MAX;
    state.maxSample = 0;
    state.bottomValue = 0;
    state.topValue = 0;
    state.lastSampleTime = 0;
    
    clearSampleBuffer(buttonIndex);
    setButtonLEDColor(buttonIndex, CalibrationLEDColor::RED);
    updateButtonLED(buttonIndex, state.ledColor);
    
    APP_DBG("Button %d calibration reset (memory only, Flash will be cleared on save)", buttonIndex);
    
    return ADCBtnsError::SUCCESS;
}

/**
 * 重置所有按键校准
 */
ADCBtnsError ADCCalibrationManager::resetAllCalibration() {
    // 1. 首先重置所有内存状态
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        ButtonCalibrationState& state = buttonStates[i];
        state.phase = CalibrationPhase::IDLE;
        state.isCalibrated = false;
        state.sampleCount = 0;
        state.minSample = UINT16_MAX;
        state.maxSample = 0;
        state.bottomValue = 0;
        state.topValue = 0;
        state.lastSampleTime = 0;
        
        clearSampleBuffer(i);
        setButtonLEDColor(i, CalibrationLEDColor::RED);
        updateButtonLED(i, state.ledColor);
    }
    
    // 2. 批量清除Flash存储中的校准数据（一次性操作）
    ADCBtnsError flashResult = clearAllCalibrationFromFlash();
    if (flashResult != ADCBtnsError::SUCCESS) {
        APP_ERR("Failed to clear all calibration data from Flash, error: %d", static_cast<int>(flashResult));
    } else {
        APP_DBG("All calibration data cleared from Flash in batch operation");
    }
    
    APP_DBG("All button calibrations reset (memory and Flash)");
    
    return flashResult;
}

/**
 * 处理校准逻辑（主循环调用）- 并行处理所有按键
 */
void ADCCalibrationManager::processCalibration() {
    if (!calibrationActive) {
        return;
    }
    
    // 获取所有ADC值
    const std::array<ADCButtonValueInfo, NUM_ADC_BUTTONS>& adcValues = ADC_MANAGER.readADCValues();
    
    // 并行处理所有按键的校准
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        if (buttonStates[i].isCalibrated) {
            continue; // 跳过已校准的按键
        }
        
        // 处理单个按键的校准逻辑
        processButtonCalibration(i);
        
        // 检查是否应该进行采样
        if (shouldSampleButton(i) && i < adcValues.size()) {
            uint16_t adcValue = *adcValues[i].valuePtr;
            if (adcValue > 0 && adcValue <= UINT16_MAX) {
                addSample(i, adcValue);
            }
        }
    }
    
    // 检查是否所有按键都已完成校准
    checkCalibrationCompletion();
}

/**
 * 处理单个按键校准
 */
void ADCCalibrationManager::processButtonCalibration(uint8_t buttonIndex) {
    // 移除超时检查逻辑，让用户有充足时间操作按键
    // 原来的超时检查代码已删除
}

/**
 * 判断是否应该对按键采样
 */
bool ADCCalibrationManager::shouldSampleButton(uint8_t buttonIndex) const {
    if (buttonIndex >= NUM_ADC_BUTTONS) {
        return false;
    }
    
    const ButtonCalibrationState& state = buttonStates[buttonIndex];
    
    // 只有在采样阶段才进行采样
    if (state.phase != CalibrationPhase::BOTTOM_SAMPLING && 
        state.phase != CalibrationPhase::TOP_SAMPLING) {
        return false;
    }
    
    // 检查采样间隔
    uint32_t currentTime = HAL_GetTick();
    if (currentTime - state.lastSampleTime < SAMPLE_INTERVAL_MS) {
        return false;
    }
    
    return true;
}

/**
 * 添加采样值
 */
ADCBtnsError ADCCalibrationManager::addSample(uint8_t buttonIndex, uint16_t adcValue) {
    if (buttonIndex >= NUM_ADC_BUTTONS) {
        return ADCBtnsError::INVALID_PARAMS;
    }
    
    if (!calibrationActive) {
        return ADCBtnsError::CALIBRATION_NOT_STARTED;
    }
    
    ButtonCalibrationState& state = buttonStates[buttonIndex];
    
    if (state.phase != CalibrationPhase::BOTTOM_SAMPLING && state.phase != CalibrationPhase::TOP_SAMPLING) {
        return ADCBtnsError::CALIBRATION_INVALID_DATA;
    }
    
    // 验证采样值
    ADCBtnsError validateResult = validateSample(buttonIndex, adcValue);
    if (validateResult != ADCBtnsError::SUCCESS) {
        clearSampleBuffer(buttonIndex);
        return validateResult;
    }
    
    // 添加采样值到缓冲区
    state.sampleBuffer[state.sampleCount] = adcValue;
    state.sampleCount++;
    
    // 更新最小最大值
    if (adcValue < state.minSample) {
        state.minSample = adcValue;
    }
    if (adcValue > state.maxSample) {
        state.maxSample = adcValue;
    }
    
    // 更新采样时间
    state.lastSampleTime = HAL_GetTick();
    
    // if(buttonIndex == 0){
    //     APP_DBG("Button %d sample %d: %d (range: %d-%d)", buttonIndex, state.sampleCount, adcValue, state.minSample, state.maxSample);
    // }
    
    // 检查是否收集完所有采样
    if (state.sampleCount >= REQUIRED_SAMPLES) {
        // 检查稳定性
        if (checkSampleStability(buttonIndex)) {
            // 完成当前阶段的采样
            finalizeSampling(buttonIndex);
        } else {
            // 稳定性检查失败，重新开始采样
            APP_DBG("Stability check failed for button %d, restarting sampling", buttonIndex);
            clearSampleBuffer(buttonIndex);
        }
    }
    
    return ADCBtnsError::SUCCESS;
}

/**
 * 验证采样值
 */
ADCBtnsError ADCCalibrationManager::validateSample(uint8_t buttonIndex, uint16_t adcValue) {
    ButtonCalibrationState& state = buttonStates[buttonIndex];
    
    uint16_t expectedValue = (state.phase == CalibrationPhase::BOTTOM_SAMPLING) ? 
                            state.expectedBottomValue : state.expectedTopValue;
    
    // if(buttonIndex == 0){
    //     APP_DBG("Button %d expected value: %d, adcValue: %d, tolerance: %d", buttonIndex, expectedValue, adcValue, state.toleranceRange);
    // }

    // 检查值是否在期望范围内
    if (abs((int32_t)adcValue - (int32_t)expectedValue) > state.toleranceRange) {
        return ADCBtnsError::CALIBRATION_INVALID_DATA;
    }
    
    // 如果已经有采样值，检查与现有样本的差异
    if (state.sampleCount > 0) {
        // 检查与已有样本的差异是否在稳定性阈值内
        for (uint8_t i = 0; i < state.sampleCount; i++) {
            if (abs((int32_t)adcValue - (int32_t)state.sampleBuffer[i]) > state.stabilityThreshold) {
                return ADCBtnsError::CALIBRATION_INVALID_DATA;
            }
        }
    }
    
    return ADCBtnsError::SUCCESS;
}

/**
 * 检查采样稳定性
 */
bool ADCCalibrationManager::checkSampleStability(uint8_t buttonIndex) {
    ButtonCalibrationState& state = buttonStates[buttonIndex];
    
    if (state.sampleCount < REQUIRED_SAMPLES) {
        return false;
    }
    
    // 检查最大值与最小值的差异
    uint16_t range = state.maxSample - state.minSample;
    if (range > state.stabilityThreshold) {
        return false;
    }
    
    return true;
}

/**
 * 完成采样
 */
ADCBtnsError ADCCalibrationManager::finalizeSampling(uint8_t buttonIndex) {
    ButtonCalibrationState& state = buttonStates[buttonIndex];
    
    // 计算平均值
    uint32_t sum = 0;
    for (uint8_t i = 0; i < state.sampleCount; i++) {
        sum += state.sampleBuffer[i];
    }
    uint16_t averageValue = sum / state.sampleCount;
    
    if (state.phase == CalibrationPhase::TOP_SAMPLING) {
        // 完成顶部值采样（按键释放状态）
        state.topValue = averageValue;
        APP_DBG("Button %d top value calibrated (RELEASED): %d (samples: %d, range: %d-%d, expected: %d)", 
                buttonIndex, averageValue, state.sampleCount, state.minSample, state.maxSample, state.expectedTopValue);
        
        // 进入底部值采样阶段（按键按下状态）
        setButtonPhase(buttonIndex, CalibrationPhase::BOTTOM_SAMPLING);
        setButtonLEDColor(buttonIndex, CalibrationLEDColor::DARK_BLUE);
        updateButtonLED(buttonIndex, CalibrationLEDColor::DARK_BLUE);
        
        clearSampleBuffer(buttonIndex);
        
    } else if (state.phase == CalibrationPhase::BOTTOM_SAMPLING) {
        // 完成底部值采样（按键按下状态）
        state.bottomValue = averageValue;
        APP_DBG("Button %d bottom value calibrated (PRESSED): %d (samples: %d, range: %d-%d, expected: %d)", 
                buttonIndex, averageValue, state.sampleCount, state.minSample, state.maxSample, state.expectedBottomValue);
        
        // 校准完成，标记需要保存到Flash（延迟保存）
        state.isCalibrated = true;
        markCalibrationForSave(buttonIndex);
        setButtonPhase(buttonIndex, CalibrationPhase::COMPLETED);
        setButtonLEDColor(buttonIndex, CalibrationLEDColor::GREEN);
        updateButtonLED(buttonIndex, CalibrationLEDColor::GREEN);
        
        // 打印单个按键完成的详细校准信息
        printButtonCalibrationCompleted(buttonIndex);
    }
    
    return ADCBtnsError::SUCCESS;
}

/**
 * 保存校准值
 */
ADCBtnsError ADCCalibrationManager::saveCalibrationValues(uint8_t buttonIndex) {
    ButtonCalibrationState& state = buttonStates[buttonIndex];
    
    std::string mappingId = ADC_MANAGER.getDefaultMapping();
    if (mappingId.empty()) {
        return ADCBtnsError::MAPPING_NOT_FOUND;
    }
    
    // 保存手动校准值（isAutoCalibration = false）
    return ADC_MANAGER.setCalibrationValues(mappingId.c_str(), buttonIndex, false, 
                                          state.topValue, state.bottomValue);
}

/**
 * 移动到下一个阶段
 */
void ADCCalibrationManager::moveToNextPhase(uint8_t buttonIndex) {
    // 移除phaseStartTime设置，不再需要超时处理
}

/**
 * 检查校准是否全部完成
 */
void ADCCalibrationManager::checkCalibrationCompletion() {
    // 检查是否所有按键都已校准完成或出错
    bool allCompleted = true;
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        CalibrationPhase phase = buttonStates[i].phase;
        if (phase == CalibrationPhase::BOTTOM_SAMPLING || 
            phase == CalibrationPhase::TOP_SAMPLING) {
            allCompleted = false;
            break;
        }
    }
    
    if (allCompleted && calibrationActive) {
        calibrationActive = false;
        
        // 批量保存所有待保存的校准数据
        ADCBtnsError saveResult = saveAllPendingCalibration();
        if (saveResult != ADCBtnsError::SUCCESS) {
            APP_ERR("Failed to save some calibration data to Flash, error: %d", static_cast<int>(saveResult));
        }
        
        // 打印所有按键校准完成的详细汇总信息
        printAllCalibrationResults();
        
        // 更新最终LED状态
        for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
            if (buttonStates[i].isCalibrated) {
                setButtonLEDColor(i, CalibrationLEDColor::GREEN);
            } else {
                setButtonLEDColor(i, CalibrationLEDColor::RED);
            }
        }
        updateAllLEDs();
    }
}

/**
 * 设置按键阶段
 */
void ADCCalibrationManager::setButtonPhase(uint8_t buttonIndex, CalibrationPhase phase) {
    if (buttonIndex < NUM_ADC_BUTTONS) {
        buttonStates[buttonIndex].phase = phase;
    }
}

/**
 * 设置按键LED颜色
 */
void ADCCalibrationManager::setButtonLEDColor(uint8_t buttonIndex, CalibrationLEDColor color) {
    if (buttonIndex < NUM_ADC_BUTTONS) {
        buttonStates[buttonIndex].ledColor = color;
    }
}

/**
 * 清空采样缓冲区
 */
void ADCCalibrationManager::clearSampleBuffer(uint8_t buttonIndex) {
    if (buttonIndex < NUM_ADC_BUTTONS) {
        ButtonCalibrationState& state = buttonStates[buttonIndex];
        state.sampleCount = 0;
        state.minSample = UINT16_MAX;
        state.maxSample = 0;
        state.sampleBuffer.fill(0);
    }
}

/**
 * 初始化按键状态
 */
void ADCCalibrationManager::initializeButtonStates() {
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        ButtonCalibrationState& state = buttonStates[i];
        state.phase = CalibrationPhase::IDLE;
        state.ledColor = CalibrationLEDColor::RED;
        state.isCalibrated = false;
        state.sampleCount = 0;
        state.minSample = UINT16_MAX;
        state.maxSample = 0;
        state.bottomValue = 0;
        state.topValue = 0;
        state.lastSampleTime = 0;
        state.sampleBuffer.fill(0);
        
        // 设置期望值（来自原始映射）
        std::string mappingId = ADC_MANAGER.getDefaultMapping();
        if (!mappingId.empty()) {
            const ADCValuesMapping* mapping = ADC_MANAGER.getMapping(mappingId.c_str());
            if (mapping) {
                state.expectedTopValue = mapping->originalValues[mapping->length - 1];      // 释放状态
                state.expectedBottomValue = mapping->originalValues[0];                     // 按下状态
                APP_DBG("initializeButtonStates Button %d expected top value: %d, bottom value: %d", i, state.expectedTopValue, state.expectedBottomValue);
            }
        }
    }
}

/**
 * 加载已有的校准数据
 */
bool ADCCalibrationManager::loadExistingCalibration() {
    std::string mappingId = ADC_MANAGER.getDefaultMapping();
    if (mappingId.empty()) {
        return false;
    }
    
    bool hasAnyCalibration = false;
    
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        uint16_t topValue, bottomValue;
        ADCBtnsError result = ADC_MANAGER.getCalibrationValues(mappingId.c_str(), i, false, topValue, bottomValue);
        
        if (result == ADCBtnsError::SUCCESS && topValue != 0 && bottomValue != 0) {
            ButtonCalibrationState& state = buttonStates[i];
            state.isCalibrated = true;
            state.phase = CalibrationPhase::COMPLETED;
            state.topValue = topValue;
            state.bottomValue = bottomValue;
            state.ledColor = CalibrationLEDColor::GREEN;
            hasAnyCalibration = true;
            
            APP_DBG("Loaded existing calibration for button %d: top=%d, bottom=%d", i, topValue, bottomValue);
        }
    }
    
    return hasAnyCalibration;
}

// 状态查询方法实现
CalibrationPhase ADCCalibrationManager::getButtonPhase(uint8_t buttonIndex) const {
    if (buttonIndex < NUM_ADC_BUTTONS) {
        return buttonStates[buttonIndex].phase;
    }
    return CalibrationPhase::IDLE;
}

CalibrationLEDColor ADCCalibrationManager::getButtonLEDColor(uint8_t buttonIndex) const {
    if (buttonIndex < NUM_ADC_BUTTONS) {
        return buttonStates[buttonIndex].ledColor;
    }
    return CalibrationLEDColor::OFF;
}

bool ADCCalibrationManager::isButtonCalibrated(uint8_t buttonIndex) const {
    if (buttonIndex < NUM_ADC_BUTTONS) {
        return buttonStates[buttonIndex].isCalibrated;
    }
    return false;
}

bool ADCCalibrationManager::isAllButtonsCalibrated() const {
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        if (!buttonStates[i].isCalibrated) {
            return false;
        }
    }
    return true;
}

uint8_t ADCCalibrationManager::getUncalibratedButtonCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        if (!buttonStates[i].isCalibrated) {
            count++;
        }
    }
    return count;
}

uint8_t ADCCalibrationManager::getActiveCalibrationButtonCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        CalibrationPhase phase = buttonStates[i].phase;
        if (phase == CalibrationPhase::BOTTOM_SAMPLING || 
            phase == CalibrationPhase::TOP_SAMPLING) {
            count++;
        }
    }
    return count;
}

ADCBtnsError ADCCalibrationManager::getCalibrationValues(uint8_t buttonIndex, uint16_t& topValue, uint16_t& bottomValue) const {
    if (buttonIndex >= NUM_ADC_BUTTONS) {
        return ADCBtnsError::INVALID_PARAMS;
    }
    
    const ButtonCalibrationState& state = buttonStates[buttonIndex];
    if (!state.isCalibrated) {
        return ADCBtnsError::CALIBRATION_VALUES_NOT_FOUND;
    }
    
    topValue = state.topValue;
    bottomValue = state.bottomValue;
    return ADCBtnsError::SUCCESS;
}

ADCBtnsError ADCCalibrationManager::setCalibrationConfig(uint8_t buttonIndex, uint16_t expectedBottom, uint16_t expectedTop, uint16_t tolerance, uint16_t stability) {
    if (buttonIndex >= NUM_ADC_BUTTONS) {
        return ADCBtnsError::INVALID_PARAMS;
    }
    
    ButtonCalibrationState& state = buttonStates[buttonIndex];
    state.expectedBottomValue = expectedBottom;
    state.expectedTopValue = expectedTop;
    state.toleranceRange = tolerance;
    state.stabilityThreshold = stability;
    
    return ADCBtnsError::SUCCESS;
}

/**
 * 更新单个按键LED（使用WS2812B驱动）
 */
void ADCCalibrationManager::updateButtonLED(uint8_t buttonIndex, CalibrationLEDColor color) {
    if (buttonIndex >= NUM_ADC_BUTTONS) {
        APP_ERR("Invalid button index: %d", buttonIndex);
        return;
    }
    
    // 确保WS2812B已初始化
    static bool ws2812b_initialized = false;
    if (!ws2812b_initialized) {
        WS2812B_Init();
        ws2812b_initialized = true;
        
        // 启动WS2812B
        if (WS2812B_Start() != WS2812B_RUNNING) {
            APP_ERR("Failed to start WS2812B");
            return;
        }
        
        APP_DBG("WS2812B initialized for calibration");
    }
    
    uint8_t red = 0, green = 0, blue = 0;
    uint8_t brightness = 80; // 适中的亮度，避免过亮
    
    // 根据校准颜色设置RGB值
    switch (color) {
        case CalibrationLEDColor::OFF:
            // 关闭LED
            red = green = blue = 0;
            brightness = 0;
            break;
            
        case CalibrationLEDColor::RED:
            // 红色 - 未校准
            red = 255; green = 0; blue = 0;
            break;
            
        case CalibrationLEDColor::CYAN:
            // 天蓝色 - 正在校准topValue（释放状态）
            red = 0; green = 255; blue = 255;
            break;
            
        case CalibrationLEDColor::DARK_BLUE:
            // 深海蓝 - 正在校准bottomValue（按下状态）
            red = 0; green = 0; blue = 139;
            break;
            
        case CalibrationLEDColor::GREEN:
            // 绿色 - 校准完成
            red = 0; green = 255; blue = 0;
            break;
            
        case CalibrationLEDColor::YELLOW:
            // 黄色 - 校准出错
            red = 255; green = 255; blue = 0;
            break;
            
        default:
            APP_ERR("Unknown calibration LED color: %d", static_cast<int>(color));
            return;
    }
    
    // 设置LED颜色和亮度
    WS2812B_SetLEDColor(red, green, blue, buttonIndex);
    WS2812B_SetLEDBrightness(brightness, buttonIndex);
    
}

/**
 * 更新所有LED
 */
void ADCCalibrationManager::updateAllLEDs() {
    // 更新每个按键的LED
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        updateButtonLED(i, buttonStates[i].ledColor);
    }
    
    // 确保WS2812B状态正确
    if (WS2812B_GetState() == WS2812B_RUNNING) {
        // 触发DMA缓冲区更新，使LED显示立即生效
        LEDDataToDMABuffer(0, NUM_ADC_BUTTONS);
        
        APP_DBG("All button LEDs updated");
    } else {
        APP_ERR("WS2812B not running, LED update skipped");
    }
}

/**
 * LED测试函数 - 循环显示所有颜色以验证LED工作状态
 */
void ADCCalibrationManager::testAllLEDs() {
    APP_DBG("Starting LED test for all buttons");
    
    // 确保WS2812B初始化
    static bool ws2812b_test_initialized = false;
    if (!ws2812b_test_initialized) {
        WS2812B_Init();
        if (WS2812B_Start() != WS2812B_RUNNING) {
            APP_ERR("Failed to start WS2812B for LED test");
            return;
        }
        ws2812b_test_initialized = true;
    }
    
    const CalibrationLEDColor test_colors[] = {
        CalibrationLEDColor::RED,
        CalibrationLEDColor::CYAN,
        CalibrationLEDColor::DARK_BLUE,
        CalibrationLEDColor::GREEN, 
        CalibrationLEDColor::YELLOW,
        CalibrationLEDColor::OFF
    };
    
    const char* color_names[] = {
        "RED", "CYAN", "DARK_BLUE", "GREEN", "YELLOW", "OFF"
    };
    
    // 为每种颜色测试所有LED
    for (uint8_t color_idx = 0; color_idx < 6; color_idx++) {
        
        // 设置所有按键为当前测试颜色
        for (uint8_t btn = 0; btn < NUM_ADC_BUTTONS; btn++) {
            updateButtonLED(btn, test_colors[color_idx]);
        }
        
        // 更新DMA缓冲区
        if (WS2812B_GetState() == WS2812B_RUNNING) {
            LEDDataToDMABuffer(0, NUM_ADC_BUTTONS);
        }
        
        // 等待500ms观察效果
        HAL_Delay(500);
    }
    
    // 恢复所有LED为关闭状态
    for (uint8_t btn = 0; btn < NUM_ADC_BUTTONS; btn++) {
        updateButtonLED(btn, CalibrationLEDColor::OFF);
    }
    LEDDataToDMABuffer(0, NUM_ADC_BUTTONS);
    
    APP_DBG("LED test completed");
}

/**
 * 打印单个按键校准完成信息
 */
void ADCCalibrationManager::printButtonCalibrationCompleted(uint8_t buttonIndex) {
    if (buttonIndex >= NUM_ADC_BUTTONS) {
        return;
    }
    
    const ButtonCalibrationState& state = buttonStates[buttonIndex];
    
    APP_DBG("========================================");
    APP_DBG("🎉 Button %d Calibration COMPLETED! 🎉", buttonIndex);
    APP_DBG("========================================");
    APP_DBG("📊 Calibration Results:");
    APP_DBG("   • Top Value (Pressed):    %d", state.topValue);
    APP_DBG("   • Bottom Value (Released): %d", state.bottomValue);
    APP_DBG("   • Value Range:             %d", abs((int32_t)state.bottomValue - (int32_t)state.topValue));
    APP_DBG("   • Expected Top:            %d", state.expectedTopValue);
    APP_DBG("   • Expected Bottom:         %d", state.expectedBottomValue);
    
    // 计算校准精度
    int32_t topError = abs((int32_t)state.topValue - (int32_t)state.expectedTopValue);
    int32_t bottomError = abs((int32_t)state.bottomValue - (int32_t)state.expectedBottomValue);
    
    APP_DBG("📈 Calibration Accuracy:");
    APP_DBG("   • Top Value Error:         %d (%.1f%%)", topError, 
            (float)topError / state.expectedTopValue * 100.0f);
    APP_DBG("   • Bottom Value Error:      %d (%.1f%%)", bottomError,
            (float)bottomError / state.expectedBottomValue * 100.0f);
    
    // 显示校准进度
    uint8_t completedCount = 0;
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        if (buttonStates[i].isCalibrated) {
            completedCount++;
        }
    }
    
    APP_DBG("🚀 Overall Progress: %d/%d buttons completed (%.1f%%)", 
            completedCount, NUM_ADC_BUTTONS, 
            (float)completedCount / NUM_ADC_BUTTONS * 100.0f);
    APP_DBG("========================================");
}

/**
 * 打印所有按键校准完成的详细汇总信息
 */
void ADCCalibrationManager::printAllCalibrationResults() {
    uint8_t calibratedCount = 0;
    uint8_t errorCount = 0;
    
    // 统计校准结果
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        if (buttonStates[i].isCalibrated) {
            calibratedCount++;
        } else if (buttonStates[i].phase == CalibrationPhase::ERROR) {
            errorCount++;
        }
    }
    
    APP_DBG("========================================");
    APP_DBG("🏁 ALL BUTTONS CALIBRATION COMPLETED! 🏁");
    APP_DBG("========================================");
    APP_DBG("📋 Final Summary:");
    APP_DBG("   • Total Buttons:           %d", NUM_ADC_BUTTONS);
    APP_DBG("   • Successfully Calibrated: %d (%.1f%%)", calibratedCount, (float)calibratedCount / NUM_ADC_BUTTONS * 100.0f);
    APP_DBG("   • Failed/Error:            %d (%.1f%%)", errorCount, (float)errorCount / NUM_ADC_BUTTONS * 100.0f);
    APP_DBG("   • Not Attempted:           %d", NUM_ADC_BUTTONS - calibratedCount - errorCount);
    APP_DBG("");
    
    if (calibratedCount > 0) {
        APP_DBG("📊 Detailed Calibration Data:");
        APP_DBG("┌─────────┬────────────┬────────────┬────────────┬─────────────┐");
        APP_DBG("│ Button  │ Top Value  │ Bot Value  │ Range      │ Status      │");
        APP_DBG("├─────────┼────────────┼────────────┼────────────┼─────────────┤");
        
        for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
            const ButtonCalibrationState& state = buttonStates[i];
            
            if (state.isCalibrated) {
                uint16_t range = abs((int32_t)state.bottomValue - (int32_t)state.topValue);
                APP_DBG("│ %7d │ %10d │ %10d │ %10d │ ✅ Success  │", 
                        i, state.topValue, state.bottomValue, range);
            } else if (state.phase == CalibrationPhase::ERROR) {
                APP_DBG("│ %7d │ %10s │ %10s │ %10s │ ❌ Error    │", 
                        i, "N/A", "N/A", "N/A");
            } else {
                APP_DBG("│ %7d │ %10s │ %10s │ %10s │ ⏭ Skipped   │", 
                        i, "N/A", "N/A", "N/A");
            }
        }
        
        APP_DBG("└─────────┴────────────┴────────────┴────────────┴─────────────┘");
        APP_DBG("");
        
        // 计算校准质量统计
        if (calibratedCount > 0) {
            uint32_t totalRange = 0;
            uint32_t minRange = UINT32_MAX;
            uint32_t maxRange = 0;
            
            for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
                if (buttonStates[i].isCalibrated) {
                    uint32_t range = abs((int32_t)buttonStates[i].bottomValue - (int32_t)buttonStates[i].topValue);
                    totalRange += range;
                    if (range < minRange) minRange = range;
                    if (range > maxRange) maxRange = range;
                }
            }
            
            uint32_t avgRange = totalRange / calibratedCount;
            
            APP_DBG("📈 Calibration Quality Analysis:");
            APP_DBG("   • Average Range:    %d ADC units", avgRange);
            APP_DBG("   • Minimum Range:    %d ADC units", minRange);
            APP_DBG("   • Maximum Range:    %d ADC units", maxRange);
            APP_DBG("   • Range Variation:  %d ADC units", maxRange - minRange);
            
            // 校准质量评估
            if (avgRange >= 2000) {
                APP_DBG("   • Quality Rating:   🌟🌟🌟 EXCELLENT (Large range, high sensitivity)");
            } else if (avgRange >= 1000) {
                APP_DBG("   • Quality Rating:   🌟🌟   GOOD (Adequate range for stable operation)");
            } else if (avgRange >= 500) {
                APP_DBG("   • Quality Rating:   🌟     FAIR (Small range, may affect precision)");
            } else {
                APP_DBG("   • Quality Rating:   ⚠️     POOR (Very small range, check hardware)");
            }
        }
    }
    
    if (errorCount > 0) {
        APP_DBG("");
        APP_DBG("❌ Failed Buttons Details:");
        for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
            if (buttonStates[i].phase == CalibrationPhase::ERROR) {
                APP_DBG("   • Button %d: Calibration failed (timeout or validation error)", i);
            }
        }
        APP_DBG("   💡 Tip: Check button hardware and try manual operation");
    }
    
    APP_DBG("");
    
    if (calibratedCount == NUM_ADC_BUTTONS) {
        APP_DBG("🎉 CONGRATULATIONS! All buttons successfully calibrated!");
        APP_DBG("✅ Your ADC button system is ready for use.");
    } else if (calibratedCount > 0) {
        APP_DBG("⚠️  Partial success: %d/%d buttons calibrated.", calibratedCount, NUM_ADC_BUTTONS);
        APP_DBG("🔧 Consider re-calibrating failed buttons for optimal performance.");
    } else {
        APP_DBG("❌ No buttons were successfully calibrated.");
        APP_DBG("🔧 Please check hardware connections and try again.");
    }
    
    APP_DBG("========================================");
}

// ==================== Flash存储优化相关方法 ====================

/**
 * 批量清除Flash中的校准数据
 * 一次性清除所有按键的校准数据，避免多次Flash写入
 */
ADCBtnsError ADCCalibrationManager::clearAllCalibrationFromFlash() {
    std::string mappingId = ADC_MANAGER.getDefaultMapping();
    if (mappingId.empty()) {
        return ADCBtnsError::MAPPING_NOT_FOUND;
    }
    
    APP_DBG("Clearing all calibration data from Flash...");
    
    // 批量清除所有按键的校准数据
    ADCBtnsError finalResult = ADCBtnsError::SUCCESS;
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        ADCBtnsError result = ADC_MANAGER.setCalibrationValues(mappingId.c_str(), i, false, 0, 0);
        if (result != ADCBtnsError::SUCCESS) {
            APP_ERR("Failed to clear calibration data for button %d, error: %d", i, static_cast<int>(result));
            finalResult = result; // 记录最后一个错误
        }
    }
    
    if (finalResult == ADCBtnsError::SUCCESS) {
        APP_DBG("All calibration data cleared from Flash successfully");
    } else {
        APP_ERR("Some calibration data failed to clear from Flash");
    }
    
    return finalResult;
}

/**
 * 保存所有待保存的校准数据
 * 批量保存所有标记为需要保存的校准数据
 */
ADCBtnsError ADCCalibrationManager::saveAllPendingCalibration() {
    std::string mappingId = ADC_MANAGER.getDefaultMapping();
    if (mappingId.empty()) {
        return ADCBtnsError::MAPPING_NOT_FOUND;
    }
    
    uint8_t pendingCount = 0;
    ADCBtnsError finalResult = ADCBtnsError::SUCCESS;
    
    // 统计需要保存的按键数量
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        if (buttonStates[i].needSaveToFlash) {
            pendingCount++;
        }
    }
    
    if (pendingCount == 0) {
        APP_DBG("No calibration data pending to save");
        return ADCBtnsError::SUCCESS;
    }
    
    APP_DBG("Saving %d pending calibration values to Flash...", pendingCount);
    
    // 批量保存所有待保存的校准数据
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        ButtonCalibrationState& state = buttonStates[i];
        
        if (state.needSaveToFlash && state.isCalibrated) {
            ADCBtnsError result = ADC_MANAGER.setCalibrationValues(mappingId.c_str(), i, false, 
                                                                state.topValue, state.bottomValue);
            if (result == ADCBtnsError::SUCCESS) {
                state.needSaveToFlash = false; // 清除保存标志
                APP_DBG("Button %d calibration saved to Flash: top=%d, bottom=%d", 
                        i, state.topValue, state.bottomValue);
            } else {
                APP_ERR("Failed to save calibration for button %d, error: %d", i, static_cast<int>(result));
                finalResult = result; // 记录最后一个错误
            }
        }
    }
    
    if (finalResult == ADCBtnsError::SUCCESS) {
        APP_DBG("All pending calibration data saved to Flash successfully");
    } else {
        APP_ERR("Some calibration data failed to save to Flash");
    }
    
    return finalResult;
}

/**
 * 标记校准数据需要保存
 * 延迟保存机制，避免频繁的Flash写入
 */
void ADCCalibrationManager::markCalibrationForSave(uint8_t buttonIndex) {
    if (buttonIndex < NUM_ADC_BUTTONS) {
        buttonStates[buttonIndex].needSaveToFlash = true;
        APP_DBG("Button %d marked for Flash save", buttonIndex);
    }
}

/**
 * 手动保存待保存的校准数据
 * 供外部调用的公共接口
 */
ADCBtnsError ADCCalibrationManager::savePendingCalibration() {
    return saveAllPendingCalibration();
}

/**
 * 获取待保存的校准数据数量
 * 用于查询有多少校准数据等待保存
 */
uint8_t ADCCalibrationManager::getPendingCalibrationCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        if (buttonStates[i].needSaveToFlash) {
            count++;
        }
    }
    return count;
}