#include "adc_btns/adc_btns_worker.hpp"
#include "board_cfg.h"
#include "stm32h7xx_hal.h"  // 为HAL_GetTick()

/*
 * ADC Hall 按钮工作逻辑：
 * 
 * 1. 初始化：
 *    - 在构造函数中，动态分配每个按钮的内存，并注册消息。
 *    - 在析构函数中，释放分配的内存并注销消息。
 * 
 * 2. 设置：
 *    - 从 ADC 管理器获取默认映射和游戏手柄配置。
 *    - 计算最小值差值，并初始化每个按钮的配置，包括虚拟引脚、精度索引和死区索引。
 *    - 启动 ADC 采样。
 * 
 * 3. 读取：
 *    - 从 ADC 管理器读取 ADC 值。
 *    - 对每个按钮，检查其状态并根据当前 ADC 值更新其状态。
 *    - 如果按钮状态发生变化，发布状态改变消息。
 * 
 * 4. 动态校准（如果启用）：
 *    - 对需要校准的按钮，计算其顶部和底部值的平均值。
 *    - 更新按钮映射。
 * 
 * 5. 状态转换：
 *    - 根据当前索引和 ADC 值，确定按钮事件（按下或释放）。
 *    - 处理状态转换，并根据事件更新按钮状态和触发状态。
 * 
 * 6. 映射更新：
 *    - 根据新的起始值和结束值，更新按钮的映射。
 *    - 计算原始范围和新范围，并进行线性映射。
 * 
 * 7. 按钮按下和回弹逻辑：
 *    - 按下逻辑：通过比较当前索引与上次状态索引，判断是否满足按下条件（索引差值大于等于按下精度索引，且当前索引小于顶部死区索引）。
 *    - 回弹逻辑：通过比较当前索引与上次状态索引，判断是否满足回弹条件（索引差值大于等于释放精度索引，且当前索引大于底部死区索引）。
 *    - 映射分区：根据映射数组，将行程分为不同的区间，通过索引判断按钮的状态变化。
 */

ADCBtnsWorker::ADCBtnsWorker()
{
    // 初始化指针数组为 nullptr
    memset(buttonPtrs, 0, sizeof(buttonPtrs));

    // 初始化按钮配置
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        buttonPtrs[i] = new ADCBtn();  // 动态分配新对象
    }

    // 初始化防抖过滤器，使用ULTRAFAST算法以获得最低延迟
    // 根据默认游戏手柄配置初始化防抖过滤器
    ADCButtonDebounceAlgorithm debounceAlgorithm = STORAGE_MANAGER.getDefaultGamepadProfile()->triggerConfigs.debounceAlgorithm;

    ADCDebounceFilter::Config debounceConfig;

    switch (debounceAlgorithm) {
        case ADCButtonDebounceAlgorithm::NONE:
            debounceConfig.ultrafastThreshold = ULTRAFast_THRESHOLD_NONE;
            break;
        case ADCButtonDebounceAlgorithm::NORMAL:
            debounceConfig.ultrafastThreshold = ULTRAFast_THRESHOLD_NORMAL;
            break;
        case ADCButtonDebounceAlgorithm::MAX:
            debounceConfig.ultrafastThreshold = ULTRAFast_THRESHOLD_MAX;
            break;
        default:
            // 处理未知的防抖算法，使用默认值
            debounceConfig.ultrafastThreshold = ULTRAFast_THRESHOLD_NORMAL;
            break;
    }
    debounceFilter_.setConfig(debounceConfig);

    // 初始化状态变化跟踪数组
    for (auto& change : buttonStateChanges_) {
        change.hasChange = false;
        change.isPressEvent = false;
        change.adcValue = 0;
        change.timestamp = 0;
    }

    // 初始化virtualPin到buttonIndex的映射表
    // 根据board_cfg.h中的映射定义初始化
    const uint8_t ADC1_MAPPING[] = {2, 7, 4, 0, 5, 6};
    const uint8_t ADC2_MAPPING[] = {1, 3, 14, 12, 8, 9};
    const uint8_t ADC3_MAPPING[] = {16, 17, 15, 13, 10, 11};
    
    // 初始化映射表为无效值
    for (auto& index : virtualPinToButtonIndex_) {
        index = 0xFF; // 无效值
    }
    
    // 填充映射表
    for (uint8_t i = 0; i < NUM_ADC1_BUTTONS; i++) {
        virtualPinToButtonIndex_[ADC1_MAPPING[i]] = i;
    }
    for (uint8_t i = 0; i < NUM_ADC2_BUTTONS; i++) {
        virtualPinToButtonIndex_[ADC2_MAPPING[i]] = i + NUM_ADC1_BUTTONS;
    }
    for (uint8_t i = 0; i < NUM_ADC3_BUTTONS; i++) {
        virtualPinToButtonIndex_[ADC3_MAPPING[i]] = i + NUM_ADC1_BUTTONS + NUM_ADC2_BUTTONS;
    }

    MC.registerMessage(MessageId::ADC_BTNS_STATE_CHANGED);
}

ADCBtnsWorker::~ADCBtnsWorker() {
    // 释放动态分配的内存
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        if (buttonPtrs[i] != nullptr) {
            delete buttonPtrs[i];
            buttonPtrs[i] = nullptr;
        }
    }

    MC.unregisterMessage(MessageId::ADC_BTNS_STATE_CHANGED);
}


ADCBtnsError ADCBtnsWorker::setup(const ExternalADCButtonConfig* externalConfigs) {
    std::string id = ADC_MANAGER.getDefaultMapping();
    if(id.empty()) {
        return ADCBtnsError::MAPPING_NOT_FOUND;
    }

    // 从storage读取游戏手柄配置
    GamepadProfile* profile = STORAGE_MANAGER.getGamepadProfile(STORAGE_MANAGER.config.defaultProfileId);
    const std::array<ADCButtonValueInfo, NUM_ADC_BUTTONS>& adcBtnInfos = ADC_MANAGER.readADCValues();
    const ADCValuesMapping* mapping = ADC_MANAGER.getMapping(id.c_str());

    if(profile == nullptr) {
        return ADCBtnsError::GAMEPAD_PROFILE_NOT_FOUND;
    }

    if(mapping == nullptr) {
        return ADCBtnsError::MAPPING_NOT_FOUND;
    }

    // 如果没有外部配置 则按照配置删除键 用于工作场景，如果有外部配置 是非工作场景，则全部按键生效
    if(externalConfigs == nullptr) {
        // 初始化启用按键掩码
        const bool* enabledKeys = profile->keysConfig.keysEnableTag;
        enabledKeysMask = 0;
        for(uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
            enabledKeysMask |= (enabledKeys[i] ? (1 << i) : 0);
        }
    } else {
        enabledKeysMask = 0;
        for(uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
            enabledKeysMask |= (1 << i);
        }
    }

    this->mapping = mapping;

    // 获取校准模式配置
    bool isAutoCalibrationEnabled = STORAGE_MANAGER.config.autoCalibrationEnabled;

    // 计算最小值差值 - 使用原始映射计算
    minValueDiff = (uint16_t)((float_t)(this->mapping->originalValues[0] - this->mapping->originalValues[this->mapping->length - 1]) * MIN_VALUE_DIFF_RATIO);
    
    // 初始化按钮配置
    for(uint8_t i = 0; i < adcBtnInfos.size(); i++) {
        const ADCButtonValueInfo& adcBtnInfo = adcBtnInfos[i];

        // 先从storage读取默认配置
        RapidTriggerProfile* triggerConfig = &profile->triggerConfigs.triggerConfigs[i];
        
        // 准备最终配置，初始值来自storage
        float_t pressAccuracy = triggerConfig->pressAccuracy;
        float_t releaseAccuracy = triggerConfig->releaseAccuracy;
        float_t topDeadzone = triggerConfig->topDeadzone;
        float_t bottomDeadzone = triggerConfig->bottomDeadzone;
        
        // 如果有外部配置，用外部配置覆盖对应项
        if (externalConfigs != nullptr) {
            const ExternalADCButtonConfig& extConfig = externalConfigs[i];
            pressAccuracy = extConfig.pressAccuracy;
            releaseAccuracy = extConfig.releaseAccuracy;
            topDeadzone = extConfig.topDeadzone;
            bottomDeadzone = extConfig.bottomDeadzone;
        }

        // 应用最小值约束
        if(topDeadzone < MIN_ADC_TOP_DEADZONE) {
            topDeadzone = MIN_ADC_TOP_DEADZONE;
        }

        if(bottomDeadzone < MIN_ADC_BOTTOM_DEADZONE) {
            bottomDeadzone = MIN_ADC_BOTTOM_DEADZONE;
        }

        // 使用最终配置初始化按钮
        buttonPtrs[i]->virtualPin = adcBtnInfo.virtualPin;
        buttonPtrs[i]->pressAccuracyMm = pressAccuracy;
        // 弹起精度 在行程前端使用高精度，后端使用低精度，低精度最小为0.1f
        buttonPtrs[i]->releaseAccuracyMm = std::max<float_t>(releaseAccuracy, MIN_ADC_RELEASE_ACCURACY);
        buttonPtrs[i]->highPrecisionReleaseAccuracyMm = releaseAccuracy; // 默认与释放精度相同
        buttonPtrs[i]->topDeadzoneMm = topDeadzone;
        buttonPtrs[i]->bottomDeadzoneMm = bottomDeadzone;
        
        // 计算中点距离（用于高精度判断）
        float totalTravelMm = (this->mapping->length - 1) * this->mapping->step;
        buttonPtrs[i]->halfwayDistanceMm = totalTravelMm / 2.0f;

        // 根据校准模式初始化按键映射
        uint16_t topValue, bottomValue;
        ADCBtnsError calibrationResult = ADC_MANAGER.getCalibrationValues(id.c_str(), i, isAutoCalibrationEnabled, topValue, bottomValue);
        
        if(calibrationResult == ADCBtnsError::SUCCESS && topValue != 0 && bottomValue != 0) {
            // 使用校准值生成完整的校准后映射
            generateCalibratedMapping(buttonPtrs[i], topValue, bottomValue);
            buttonPtrs[i]->initCompleted = true;
        } else {
            // 这里需要等待第一次ADC读取来初始化
            buttonPtrs[i]->initCompleted = false;
            // 清空映射数组
            memset(buttonPtrs[i]->valueMapping, 0, this->mapping->length * sizeof(uint16_t));
            memset(buttonPtrs[i]->calibratedMapping, 0, this->mapping->length * sizeof(uint16_t));
        }

        // 只有在自动校准模式下才启用动态校准
        if(isAutoCalibrationEnabled) {
            buttonPtrs[i]->bottomValueWindow = RingBufferSlidingWindow<uint16_t>(NUM_MAPPING_INDEX_WINDOW_SIZE);
            buttonPtrs[i]->topValueWindow = RingBufferSlidingWindow<uint16_t>(NUM_MAPPING_INDEX_WINDOW_SIZE);
            buttonPtrs[i]->limitValue = UINT16_MAX; // 初始状态为释放，记录最小值
            buttonPtrs[i]->needCalibration = false;
            buttonPtrs[i]->needSaveCalibration = false;
            buttonPtrs[i]->lastCalibrationTime = 0;
            buttonPtrs[i]->lastSaveTime = 0;
        }
        
        // 初始化状态
        buttonPtrs[i]->lastTravelDistance = 0.0f;
        buttonPtrs[i]->lastAdcValue = 0;
        buttonPtrs[i]->state = ButtonState::RELEASED;  // 明确设置初始状态为释放
    }

    ADC_MANAGER.startADCSamping();

    return ADCBtnsError::SUCCESS;
}

ADCBtnsError ADCBtnsWorker::deinit() {
    // ADC_MANAGER.stopADCSamping();
    
    // 只有在自动校准模式下才清理滑动窗口
    if (STORAGE_MANAGER.config.autoCalibrationEnabled) {
        for(uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
            ADCBtn* const btn = buttonPtrs[i];
            
            btn->bottomValueWindow.clear();
            btn->topValueWindow.clear();
        }
    }
    
    // 重置防抖状态
    debounceFilter_.reset();
    
    return ADCBtnsError::SUCCESS;
}


/**
 * 处理ADC转换完成消息
 * @param data ADC句柄指针
 */
uint32_t ADCBtnsWorker::read() {
    // 使用引用避免拷贝
    const std::array<ADCButtonValueInfo, NUM_ADC_BUTTONS>& adcValues = ADC_MANAGER.readADCValues();

    for(uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        ADCBtn* const btn = buttonPtrs[i];
        if(!btn) {
            continue;
        }

        // 使用 valuePtr 获取 ADC 值
        const uint16_t adcValue = *adcValues[i].valuePtr;

        if(adcValue == 0 || adcValue > UINT16_MAX) {
            continue;
        }

        if(!btn->initCompleted) {

            initButtonMapping(btn, adcValue);

            // 确保初始状态为释放
            btn->state = ButtonState::RELEASED;
            btn->initCompleted = true;
            continue;
    }
        
        // 获取按钮事件（新算法直接基于ADC值）
        const ButtonEvent event = getButtonEvent(btn, adcValue, i);
        

        // 处理状态转换
        if(event != ButtonEvent::NONE) {
            handleButtonState(btn, event);
        }
    }

    // APP_DBG("ADCBtnsWorker::read - virtualPinMask: 0x%08lX", &this->virtualPinMask);

    // if(buttonTriggerStatusChanged) {
    //     MC.publish(MessageId::ADC_BTNS_STATE_CHANGED, &this->virtualPinMask);
    //     buttonTriggerStatusChanged = false;
    // }
    // if(buttonTriggerStatusChanged) {
    //     printBinary("this->virtualPinMask", this->virtualPinMask);
    //     printBinary("enabledKeysMask", enabledKeysMask);
    //     printBinary("this->virtualPinMask & enabledKeysMask", this->virtualPinMask & enabledKeysMask);
    //     printf("====================\n");
    // }

    // 返回按键状态掩码，只返回启用的按键，其他按键不返回
    return (this->virtualPinMask & enabledKeysMask);
}

/**
 * 动态校准
 */
void ADCBtnsWorker::dynamicCalibration() {
    // 只有在自动校准模式下才进行动态校准
    if (!STORAGE_MANAGER.config.autoCalibrationEnabled) {
        return;
    }
    
    uint32_t currentTime = HAL_GetTick();
    bool hasCalibrationUpdate = false;
    
    for(uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        ADCBtn* const btn = buttonPtrs[i];
        if(btn && btn->needCalibration) {
            
            // 检查是否满足最小校准间隔
            if (currentTime - btn->lastCalibrationTime < MIN_CALIBRATION_INTERVAL_MS) {
                continue;
            }
            
            // topValueWindow实际存储的是按下过程中的最小值（按下值）
            // bottomValueWindow实际存储的是释放过程中的最大值（释放值）
            const uint16_t pressedValue = btn->topValueWindow.getAverageValue();    // 完全按下时的值（较大）
            const uint16_t releasedValue = btn->bottomValueWindow.getAverageValue(); // 完全释放时的值（较小）
            
            // 确保释放值小于按下值，并保持最小差值
            const uint16_t topValue = releasedValue;  // 释放值（较小）
            const uint16_t bottomValue = std::max<uint16_t>(pressedValue, static_cast<uint16_t>(releasedValue + minValueDiff)); // 按下值（较大）

            
            // 使用新的校准值生成完整的校准后映射
            generateCalibratedMapping(btn, topValue, bottomValue);
            
            // 标记需要保存，但不立即保存
            btn->needSaveCalibration = true;
            btn->lastCalibrationTime = currentTime;
            btn->needCalibration = false;
            hasCalibrationUpdate = true;
            
        }
    }
    
    // 检查是否需要保存校准值到存储
    if (hasCalibrationUpdate) {
        saveCalibrationValues();
    }
}

/**
 * 保存校准值到存储
 * 使用延迟保存机制，减少Flash写入频率
 */
void ADCBtnsWorker::saveCalibrationValues() {
    std::string mappingId = ADC_MANAGER.getDefaultMapping();
    if (mappingId.empty()) {
        return;
    }
    
    uint32_t currentTime = HAL_GetTick();
    
    for(uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        ADCBtn* const btn = buttonPtrs[i];
        if(btn && btn->needSaveCalibration && shouldSaveCalibration(btn, currentTime)) {
            
            // topValueWindow实际存储的是按下过程中的最小值（按下值）
            // bottomValueWindow实际存储的是释放过程中的最大值（释放值）
            const uint16_t pressedValue = btn->topValueWindow.getAverageValue();    // 完全按下时的值（较大）
            const uint16_t releasedValue = btn->bottomValueWindow.getAverageValue(); // 完全释放时的值（较小）
            
            // 确保释放值小于按下值，并保持最小差值
            const uint16_t topValue = releasedValue;  // 释放值（较小）
            const uint16_t bottomValue = std::max<uint16_t>(pressedValue, static_cast<uint16_t>(releasedValue + minValueDiff)); // 按下值（较大）
            
            // 保存自动校准值到存储
            ADCBtnsError saveResult = ADC_MANAGER.setCalibrationValues(mappingId.c_str(), i, true, topValue, bottomValue);
            if (saveResult == ADCBtnsError::SUCCESS) {
                btn->needSaveCalibration = false;
                btn->lastSaveTime = currentTime;
            }
        }
    }
}

/**
 * 判断是否应该保存校准值
 * @param btn 按钮指针
 * @param currentTime 当前时间
 * @return true表示应该保存，false表示不应该保存
 */
bool ADCBtnsWorker::shouldSaveCalibration(ADCBtn* btn, uint32_t currentTime) {
    if (!btn || !btn->needSaveCalibration) {
        return false;
    }
    
    // 检查是否达到保存延迟时间
    return (currentTime - btn->lastCalibrationTime) >= CALIBRATION_SAVE_DELAY_MS;
}

/**
 * 初始化按钮映射
 * @param btn 按钮指针
 * @param releaseValue 释放值
 */
void ADCBtnsWorker::initButtonMapping(ADCBtn* btn, const uint16_t releaseValue) {
    if (!btn || !mapping || releaseValue == 0) {
        return;
    }

    // 计算差值：当前释放值与原始映射末尾值的差
    const int32_t offset = (int32_t)releaseValue - (int32_t)mapping->originalValues[mapping->length - 1];

    // 对每个值进行平移，生成校准后的映射
    for (size_t i = 0; i < mapping->length; i++) {
        // 使用int32_t避免uint16_t相加减可能的溢出
        int32_t newValue = (int32_t)mapping->originalValues[i] + offset;
        
        // 确保值在uint16_t范围内
        newValue = newValue < 0 ? 0 : (newValue > UINT16_MAX ? UINT16_MAX : newValue);
        
        // 更新校准后的映射
        btn->calibratedMapping[i] = (uint16_t)newValue;
    }
    
    // 将校准后的映射复制到当前使用的映射
    memcpy(btn->valueMapping, btn->calibratedMapping, mapping->length * sizeof(uint16_t));

    // 只有在自动校准模式下才初始化滑动窗口
    if (STORAGE_MANAGER.config.autoCalibrationEnabled) {
        btn->bottomValueWindow.clear();
        btn->topValueWindow.clear();
        btn->limitValue = UINT16_MAX; // 初始状态为释放，需要记录最小值，所以初始化为最大值

        btn->bottomValueWindow.push(btn->valueMapping[0]);
        btn->topValueWindow.push(btn->valueMapping[mapping->length - 1]);
    } else {
        // 非自动校准模式下也需要初始化limitValue
        btn->limitValue = UINT16_MAX; // 初始状态为释放，需要记录最小值，所以初始化为最大值
    }

    // 确保初始状态为释放
    btn->state = ButtonState::RELEASED;
    btn->initCompleted = true;

    // 预计算精度映射表
    calculatePrecisionMapping(btn);

    // 初始化缓存的阈值
    btn->cachedPressThreshold = getInterpolatedPressThreshold(btn, btn->limitValue);
    btn->cachedReleaseThreshold = getInterpolatedReleaseThreshold(btn, btn->limitValue);
}

/**
 * 根据校准值生成完整的校准后映射
 * @param btn 按钮指针
 * @param topValue 完全释放时的校准值（较小的ADC值）
 * @param bottomValue 完全按下时的校准值（较大的ADC值）
 */
void ADCBtnsWorker::generateCalibratedMapping(ADCBtn* btn, uint16_t topValue, uint16_t bottomValue) {
    if (!btn || !mapping || topValue == bottomValue) {
        return;
    }

    // 确保topValue < bottomValue (完全释放的值应该小于完全按下的值)
    if (topValue > bottomValue) {
        uint16_t temp = topValue;
        topValue = bottomValue;
        bottomValue = temp;
    }

    // 计算原始范围和新范围
    int32_t oldRange = (int32_t)this->mapping->originalValues[0] - 
                      (int32_t)this->mapping->originalValues[this->mapping->length - 1];

    // 防止除零
    if (oldRange == 0) {
        return;
    }

    // 对每个值进行线性映射，生成完整的校准后映射
    for (size_t i = 0; i < this->mapping->length; i++) {
        // 计算原始值在原范围内的相对位置 (0.0 到 1.0)
        double relativePosition = ((double)((int32_t)this->mapping->originalValues[i] - 
                                         (int32_t)this->mapping->originalValues[0])) / oldRange;

        int32_t newValue = bottomValue + (int32_t)(relativePosition * (bottomValue - topValue) + 0.5);
        
        // 确保值在uint16_t范围内
        btn->calibratedMapping[i] = (uint16_t)std::max<int32_t>(0, std::min<int32_t>(UINT16_MAX, newValue));

    }
    
    // 更新映射
    memcpy(btn->valueMapping, btn->calibratedMapping, sizeof(btn->calibratedMapping));
    
    // 确保初始状态为释放
    btn->state = ButtonState::RELEASED;
    btn->limitValue = UINT16_MAX; // 初始状态为释放，需要记录最小值，所以初始化为最大值
    
    // 预计算精度映射表
    calculatePrecisionMapping(btn);
    
    // 初始化缓存的阈值
    btn->cachedPressThreshold = getInterpolatedPressThreshold(btn, btn->limitValue);
    btn->cachedReleaseThreshold = getInterpolatedReleaseThreshold(btn, btn->limitValue);
}

// 状态转换处理函数
ADCBtnsWorker::ButtonEvent ADCBtnsWorker::getButtonEvent(ADCBtn* btn, const uint16_t currentValue, const uint8_t buttonIndex) {
    if (!btn || !btn->initCompleted || !btn->precisionMap.isValid) {
        return ButtonEvent::NONE;
    }

    // 更新limitValue和缓存的阈值
    updateLimitValue(btn, currentValue);

    uint16_t noise = this->mapping->samplingNoise;

    switch(btn->state) {
        case ButtonState::RELEASED:
            
            // 判断当前值是否达到按下阈值
            if (currentValue >= btn->cachedPressThreshold + noise) {
                // 应用防抖过滤
                bool debounceConfirmed = debounceFilter_.filterUltraFastSingle(buttonIndex, true);
                
                if (debounceConfirmed) {
                    resetLimitValue(btn, currentValue);
                    btn->lastTravelDistance = getDistanceByValue(btn, currentValue);
                    btn->lastAdcValue = currentValue;
                    return ButtonEvent::PRESS_COMPLETE;
                }
            }
            break;

        case ButtonState::PRESSED:
            
            // 判断当前值是否达到释放阈值
            if (currentValue <= btn->cachedReleaseThreshold - noise) {
                // 应用防抖过滤
                bool debounceConfirmed = debounceFilter_.filterUltraFastSingle(buttonIndex, false);

                if (debounceConfirmed) {
                    resetLimitValue(btn, currentValue);
                    btn->lastTravelDistance = getDistanceByValue(btn, currentValue);
                    btn->lastAdcValue = currentValue;
                    return ButtonEvent::RELEASE_COMPLETE;
                }
            }
            break;

        default:
            break;
    }
    
    // 无状态切换，更新位置信息
    btn->lastTravelDistance = getDistanceByValue(btn, currentValue);
    btn->lastAdcValue = currentValue;
    return ButtonEvent::NONE;
}

/**
 * 在没有触发时，更新limitValue和缓存的阈值
 */
void ADCBtnsWorker::updateLimitValue(ADCBtn* btn, const uint16_t currentValue) {
    uint16_t noise = this->mapping->samplingNoise;
    bool limitValueUpdated = false;
    
    if(btn->state == ButtonState::RELEASED) {
        if(currentValue + noise < btn->limitValue) {
            btn->limitValue = currentValue + noise;
            limitValueUpdated = true;
        }
    } else if(btn->state == ButtonState::PRESSED) {
        if(currentValue - noise > btn->limitValue) {
            btn->limitValue = currentValue - noise;
            limitValueUpdated = true;
        }
    }
    
    // 如果limitValue更新了，重新计算缓存的阈值
    if (limitValueUpdated) {
        if (btn->state == ButtonState::RELEASED) {
            // 释放状态：计算按下阈值
            btn->cachedPressThreshold = getInterpolatedPressThreshold(btn, btn->limitValue);
        } else if (btn->state == ButtonState::PRESSED) {
            // 按下状态：计算释放阈值
            btn->cachedReleaseThreshold = getInterpolatedReleaseThreshold(btn, btn->limitValue);
        }
    }
}

/**
 * 在触发时，重置limitValue和缓存的阈值
 */
void ADCBtnsWorker::resetLimitValue(ADCBtn* btn, const uint16_t currentValue) {
    uint16_t noise = this->mapping->samplingNoise;
    
    if(btn->state == ButtonState::RELEASED) {
        btn->limitValue = currentValue - noise;
        // 计算新的释放阈值
        btn->cachedReleaseThreshold = getInterpolatedReleaseThreshold(btn, btn->limitValue);
    } else if(btn->state == ButtonState::PRESSED) {
        btn->limitValue = currentValue + noise;
        // 计算新的按下阈值
        btn->cachedPressThreshold = getInterpolatedPressThreshold(btn, btn->limitValue);
    }
}

/**
 * 根据ADC值计算对应的行程距离
 * @param btn 按钮指针
 * @param adcValue ADC值
 * @return 对应的行程距离（mm）
 */
float ADCBtnsWorker::getDistanceByValue(ADCBtn* btn, const uint16_t adcValue) const {
    if (!btn || !mapping || mapping->length == 0) {
        return 0.0f;
    }

    // 处理边界情况
    // valueMapping[0] 是最大值（完全按下位置，距离为0）
    if (adcValue >= btn->valueMapping[0]) {
        return 0.0f; // 完全按下位置
    }
    // valueMapping[length-1] 是最小值（完全释放位置，距离最大）
    if (adcValue <= btn->valueMapping[mapping->length - 1]) {
        float maxDistance = (mapping->length - 1) * this->mapping->step;
        return maxDistance; // 完全释放位置
    }

    // 在映射表中查找最接近的两个点进行线性插值
    // 映射表是从大到小排列的
    for (uint8_t i = 0; i < mapping->length - 1; i++) {
        if (adcValue <= btn->valueMapping[i] && adcValue >= btn->valueMapping[i + 1]) {
            // 线性插值计算距离
            uint16_t upperValue = btn->valueMapping[i];     // 较大的ADC值
            uint16_t lowerValue = btn->valueMapping[i + 1]; // 较小的ADC值
            float upperDistance = i * this->mapping->step;       // 较小的距离
            float lowerDistance = (i + 1) * this->mapping->step; // 较大的距离
            
            if (upperValue == lowerValue) {
                return upperDistance;
            }
            
            float ratio = (float)(upperValue - adcValue) / (upperValue - lowerValue);
            float result = upperDistance + ratio * (lowerDistance - upperDistance);
            
            return result;
        }
    }

    return 0.0f;
}

/**
 * 根据行程距离计算对应的ADC值
 * @param btn 按钮指针
 * @param baseAdcValue 基准ADC值（起始点）
 * @param distanceMm 要移动的行程距离（mm），正值表示向释放方向移动，负值表示向按下方向移动
 * @return 移动指定距离后的目标ADC值
 */
uint16_t ADCBtnsWorker::getValueByDistance(ADCBtn* btn, const uint16_t baseAdcValue, const float distanceMm) {
    if (!btn || !mapping || mapping->length == 0) {
        return baseAdcValue;
    }

    // 首先获取基准ADC值对应的距离
    float baseDistance = getDistanceByValue(btn, baseAdcValue);
    // 计算目标距离
    float targetDistance = baseDistance + distanceMm;

    // 获取最大距离
    float maxDistance = (mapping->length - 1) * this->mapping->step;

    // 处理超出边界的情况 - 使用外推而不是直接返回边界值
    if (targetDistance < 0) {
        // 向按下方向超出边界，使用前两个点进行外推
        if (mapping->length >= 2) {
            uint16_t value0 = btn->valueMapping[0];     // 距离0对应的ADC值
            uint16_t value1 = btn->valueMapping[1];     // 距离step对应的ADC值
            float step = this->mapping->step;
            
            // 线性外推：value = value0 + (targetDistance - 0) * (value1 - value0) / step
            float extrapolatedValue = value0 + (targetDistance / step) * (value1 - value0);
            
            // 确保结果在合理范围内
            if (extrapolatedValue > UINT16_MAX) extrapolatedValue = UINT16_MAX;
            if (extrapolatedValue < 0) extrapolatedValue = 0;
            
            return (uint16_t)extrapolatedValue;
        } else {
            return btn->valueMapping[0];
        }
    }
    
    if (targetDistance > maxDistance) {
        // 向释放方向超出边界，使用最后两个点进行外推
        if (mapping->length >= 2) {
            uint16_t valueLast = btn->valueMapping[mapping->length - 1];     // 最大距离对应的ADC值
            uint16_t valueSecondLast = btn->valueMapping[mapping->length - 2]; // 倒数第二个距离对应的ADC值
            float step = this->mapping->step;
            
            // 线性外推
            float extrapolatedValue = valueLast + ((targetDistance - maxDistance) / step) * (valueLast - valueSecondLast);
            
            // 确保结果在合理范围内
            if (extrapolatedValue > UINT16_MAX) extrapolatedValue = UINT16_MAX;
            if (extrapolatedValue < 0) extrapolatedValue = 0;
            
            return (uint16_t)extrapolatedValue;
        } else {
            return btn->valueMapping[mapping->length - 1];
        }
    }
    
    // 处理正常范围内的情况
    if (targetDistance <= 0) {
        return btn->valueMapping[0]; // 完全按下位置，最大ADC值
    }
    if (targetDistance >= maxDistance) {
        return btn->valueMapping[mapping->length - 1]; // 完全释放位置，最小ADC值
    }

    // 计算在映射表中的位置
    float indexFloat = targetDistance / this->mapping->step;
    uint8_t lowerIndex = (uint8_t)indexFloat;
    uint8_t upperIndex = lowerIndex + 1;

    // 边界检查
    if (upperIndex >= mapping->length) {
        return btn->valueMapping[mapping->length - 1];
    }

    // 线性插值 - 映射表是从大到小排列的
    float fraction = indexFloat - lowerIndex;
    uint16_t upperValue = btn->valueMapping[lowerIndex];   // 距离小，ADC值大
    uint16_t lowerValue = btn->valueMapping[upperIndex];   // 距离大，ADC值小
    
    uint16_t result = (uint16_t)(upperValue - fraction * (upperValue - lowerValue));
    

    return result;
}

/**
 * 获取当前位置下的按下精度
 * @param btn 按钮指针
 * @param currentDistance 当前行程距离（mm）
 * @return 当前应使用的按下精度（mm）
 */
float ADCBtnsWorker::getCurrentPressAccuracy(ADCBtn* btn, const float currentDistance) {
    if (!btn) {
        return 0.1f; // 默认精度
    }

    // 目前简单返回配置的按下精度，后续可根据需要添加动态精度逻辑
    return btn->pressAccuracyMm;
}

/**
 * 获取当前位置下的弹起精度
 * @param btn 按钮指针  
 * @param currentDistance 当前行程距离（mm）
 * @return 当前应使用的弹起精度（mm）
 */
float ADCBtnsWorker::getCurrentReleaseAccuracy(ADCBtn* btn, const float currentDistance) {
    if (!btn) {
        return 0.1f; // 默认精度
    }


    // 如果在前半段行程内，使用高精度弹起精度
    if (currentDistance <= btn->halfwayDistanceMm) {
        return btn->highPrecisionReleaseAccuracyMm;
    }
    
    // 后半段使用常规弹起精度
    return btn->releaseAccuracyMm;
}

/**
 * 处理按钮状态转换
 * @param btn 按钮指针
 * @param event 事件
 */
void ADCBtnsWorker::handleButtonState(ADCBtn* btn, const ButtonEvent event) {
    if (!btn) {
        return;
    }

    // 声明变量，避免跨case标签跳转错误
    uint8_t buttonIndex;

    switch(event) {
        case ButtonEvent::PRESS_COMPLETE:
            btn->state = ButtonState::PRESSED;
            this->virtualPinMask |= (1U << btn->virtualPin);
            buttonTriggerStatusChanged = true;
            
            // 记录状态变化（技术测试模式用）
            buttonIndex = getButtonIndexFromVirtualPin(btn->virtualPin);
            if (buttonIndex != 0xFF) {
                buttonStateChanges_[buttonIndex].hasChange = true;
                buttonStateChanges_[buttonIndex].isPressEvent = true;
                buttonStateChanges_[buttonIndex].adcValue = btn->lastAdcValue;
                buttonStateChanges_[buttonIndex].timestamp = HAL_GetTick();
            }
            break;

        case ButtonEvent::RELEASE_COMPLETE:
            btn->state = ButtonState::RELEASED;
            this->virtualPinMask &= ~(1U << btn->virtualPin);
            buttonTriggerStatusChanged = true;
            
            // 记录状态变化（技术测试模式用）
            buttonIndex = getButtonIndexFromVirtualPin(btn->virtualPin);
            if (buttonIndex != 0xFF) {
                buttonStateChanges_[buttonIndex].hasChange = true;
                buttonStateChanges_[buttonIndex].isPressEvent = false;
                buttonStateChanges_[buttonIndex].adcValue = btn->lastAdcValue;
                buttonStateChanges_[buttonIndex].timestamp = HAL_GetTick();
            }
            break;

        default:
            return; // 无事件，直接返回
    }
    
}

/**
 * @brief 设置防抖过滤器配置
 * @param config 防抖配置
 */
void ADCBtnsWorker::setDebounceConfig(const ADCDebounceFilter::Config& config) {
    debounceFilter_.setConfig(config);
}

/**
 * @brief 获取防抖过滤器配置
 * @return 当前防抖配置
 */
const ADCDebounceFilter::Config& ADCBtnsWorker::getDebounceConfig() const {
    return debounceFilter_.getConfig();
}

/**
 * @brief 重置防抖状态
 */
void ADCBtnsWorker::resetDebounceState() {
    debounceFilter_.reset();
}

/**
 * @brief 获取指定按钮的防抖状态 (调试用)
 * @param buttonIndex 按钮索引
 * @return 防抖状态值
 */
uint8_t ADCBtnsWorker::getButtonDebounceState(uint8_t buttonIndex) const {
    return debounceFilter_.getButtonDebounceState(buttonIndex);
}

/**
 * 预计算精度映射表
 * @param btn 按钮指针
 */
void ADCBtnsWorker::calculatePrecisionMapping(ADCBtn* btn) {
    if (!btn || !mapping) {
        return;
    }
    
    // 清空映射表
    memset(&btn->precisionMap, 0, sizeof(btn->precisionMap));
    
    float maxTravelDistance = (mapping->length - 1) * mapping->step;
    
    // 为每个映射点计算按下和弹起阈值
    for (uint8_t i = 0; i < mapping->length; i++) {
        uint16_t baseValue = btn->valueMapping[i];
        
        // 计算当前距离
        float currentDistance = i * mapping->step;
        
        // 获取当前位置的按下和释放精度
        float pressAccuracy = getCurrentPressAccuracy(btn, currentDistance);
        float releaseAccuracy = getCurrentReleaseAccuracy(btn, currentDistance);
        
        // 计算按下阈值：从当前位置向按下方向移动pressAccuracy距离
        btn->precisionMap.pressThresholds[i] = getValueByDistance(btn, baseValue, -pressAccuracy);
        
        // 计算释放阈值：从当前位置向释放方向移动releaseAccuracy距离
        btn->precisionMap.releaseThresholds[i] = getValueByDistance(btn, baseValue, releaseAccuracy);
        
        // 死区处理：如果当前位置在死区内，设置合理的阈值
        if (currentDistance <= btn->bottomDeadzoneMm) {
            // 底部死区：设置释放阈值为死区边缘的值
            float edgeDistance = btn->bottomDeadzoneMm;
            btn->precisionMap.releaseThresholds[i] = getValueByDistance(btn, baseValue, -(currentDistance - edgeDistance));
        }
        
        if (currentDistance >= maxTravelDistance - btn->topDeadzoneMm) {
            // 顶部死区：设置按下阈值为死区边缘的值
            float edgeDistance = maxTravelDistance - btn->topDeadzoneMm;
            btn->precisionMap.pressThresholds[i] = getValueByDistance(btn, baseValue, edgeDistance - currentDistance);
        }
    }
    
    btn->precisionMap.isValid = true;
}

/**
 * 找到当前值对应的映射索引
 * @param btn 按钮指针
 * @param currentValue 当前ADC值
 * @return 映射索引
 */
uint8_t ADCBtnsWorker::findMappingIndex(ADCBtn* btn, const uint16_t currentValue) {
    if (!btn || !mapping || mapping->length == 0) {
        return 0;
    }
    
    // 处理边界情况
    if (currentValue >= btn->valueMapping[0]) {
        return 0; // 完全按下位置
    }
    if (currentValue <= btn->valueMapping[mapping->length - 1]) {
        return mapping->length - 1; // 完全释放位置
    }
    
    // 在映射表中查找最接近的索引
    for (uint8_t i = 0; i < mapping->length - 1; i++) {
        if (currentValue <= btn->valueMapping[i] && currentValue >= btn->valueMapping[i + 1]) {
            return i; // 返回较大的索引（距离较小的位置）
        }
    }
    
    return 0; // 默认返回第一个索引
}

/**
 * 获取插值计算的按下阈值
 * @param btn 按钮指针
 * @param currentValue 当前ADC值
 * @return 插值计算的按下阈值
 */
uint16_t ADCBtnsWorker::getInterpolatedPressThreshold(ADCBtn* btn, const uint16_t currentValue) {
    if (!btn || !mapping || mapping->length == 0) {
        return 0;
    }
    
    // 处理边界情况
    if (currentValue >= btn->valueMapping[0]) {
        return btn->precisionMap.pressThresholds[0];
    }
    if (currentValue <= btn->valueMapping[mapping->length - 1]) {
        return btn->precisionMap.pressThresholds[mapping->length - 1];
    }
    
    // 找到当前值所在的区间
    for (uint8_t i = 0; i < mapping->length - 1; i++) {
        if (currentValue <= btn->valueMapping[i] && currentValue >= btn->valueMapping[i + 1]) {
            // 计算插值权重
            uint16_t upperValue = btn->valueMapping[i];
            uint16_t lowerValue = btn->valueMapping[i + 1];
            uint16_t upperThreshold = btn->precisionMap.pressThresholds[i];
            uint16_t lowerThreshold = btn->precisionMap.pressThresholds[i + 1];
            
            // 线性插值
            float weight = (float)(currentValue - lowerValue) / (float)(upperValue - lowerValue);
            uint16_t interpolatedThreshold = (uint16_t)(lowerThreshold + weight * (upperThreshold - lowerThreshold));
            
            return interpolatedThreshold;
        }
    }
    
    return btn->precisionMap.pressThresholds[0]; // 默认返回第一个阈值
}

/**
 * 获取插值计算的释放阈值
 * @param btn 按钮指针
 * @param currentValue 当前ADC值
 * @return 插值计算的释放阈值
 */
uint16_t ADCBtnsWorker::getInterpolatedReleaseThreshold(ADCBtn* btn, const uint16_t currentValue) {
    if (!btn || !mapping || mapping->length == 0) {
        return UINT16_MAX;
    }
    
    // 处理边界情况
    if (currentValue >= btn->valueMapping[0]) {
        return btn->precisionMap.releaseThresholds[0];
    }
    if (currentValue <= btn->valueMapping[mapping->length - 1]) {
        return btn->precisionMap.releaseThresholds[mapping->length - 1];
    }
    
    // 找到当前值所在的区间
    for (uint8_t i = 0; i < mapping->length - 1; i++) {
        if (currentValue <= btn->valueMapping[i] && currentValue >= btn->valueMapping[i + 1]) {
            // 计算插值权重
            uint16_t upperValue = btn->valueMapping[i];
            uint16_t lowerValue = btn->valueMapping[i + 1];
            uint16_t upperThreshold = btn->precisionMap.releaseThresholds[i];
            uint16_t lowerThreshold = btn->precisionMap.releaseThresholds[i + 1];
            
            // 线性插值
            float weight = (float)(currentValue - lowerValue) / (float)(upperValue - lowerValue);
            uint16_t interpolatedThreshold = (uint16_t)(lowerThreshold + weight * (upperThreshold - lowerThreshold));
            
            return interpolatedThreshold;
        }
    }
    
    return btn->precisionMap.releaseThresholds[0]; // 默认返回第一个阈值
}

/**
 * @brief 获取指定按钮的详细信息 (技术测试模式用)
 * @param buttonIndex 按钮索引
 * @param adcValue 返回当前ADC值
 * @param travelDistance 返回当前物理行程（mm）
 * @param limitValue 返回当前limitValue
 * @param limitValueDistance 返回limitValue对应的物理行程（mm）
 * @return true 如果获取成功
 */
bool ADCBtnsWorker::getButtonDetails(uint8_t buttonIndex, uint16_t& adcValue, float& travelDistance, 
                                     uint16_t& limitValue, float& limitValueDistance) const {
    if (buttonIndex >= NUM_ADC_BUTTONS || !buttonPtrs[buttonIndex] || !buttonPtrs[buttonIndex]->initCompleted) {
        return false;
    }
    
    const ADCBtn* btn = buttonPtrs[buttonIndex];
    
    // 获取当前ADC值
    const std::array<ADCButtonValueInfo, NUM_ADC_BUTTONS>& adcValues = ADC_MANAGER.readADCValues();
    adcValue = *adcValues[buttonIndex].valuePtr;
    
    // 计算当前物理行程
    travelDistance = getDistanceByValue(const_cast<ADCBtn*>(btn), adcValue);
    
    // 获取limitValue和对应的物理行程
    limitValue = btn->limitValue;
    limitValueDistance = getDistanceByValue(const_cast<ADCBtn*>(btn), limitValue);
    
    return true;
}

/**
 * @brief 获取指定按钮的虚拟引脚
 * @param buttonIndex 按钮索引
 * @return 虚拟引脚号，如果索引无效返回0xFF
 */
uint8_t ADCBtnsWorker::getButtonVirtualPin(uint8_t buttonIndex) const {
    if (buttonIndex >= NUM_ADC_BUTTONS || !buttonPtrs[buttonIndex]) {
        return 0xFF;
    }
    
    return buttonPtrs[buttonIndex]->virtualPin;
}

/**
 * @brief 获取按键状态变化信息（技术测试模式用）
 * @param buttonIndex 按钮索引
 * @param isPressEvent 返回是否为按下事件
 * @param adcValue 返回触发时的ADC值
 * @return true 如果有状态变化
 */
bool ADCBtnsWorker::getButtonStateChange(uint8_t buttonIndex, bool& isPressEvent, uint16_t& adcValue) const {
    if (buttonIndex >= NUM_ADC_BUTTONS) {
        return false;
    }
    
    const ButtonStateChange& change = buttonStateChanges_[buttonIndex];
    if (change.hasChange) {
        isPressEvent = change.isPressEvent;
        adcValue = change.adcValue;
        return true;
    }
    
    return false;
}

/**
 * @brief 清除按键状态变化标志
 */
void ADCBtnsWorker::clearButtonStateChange() {
    for (auto& change : buttonStateChanges_) {
        change.hasChange = false;
    }
}

/**
 * @brief 从virtualPin获取buttonIndex
 * @param virtualPin 虚拟引脚
 * @return buttonIndex，如果virtualPin无效返回0xFF
 */
uint8_t ADCBtnsWorker::getButtonIndexFromVirtualPin(uint8_t virtualPin) const {
    if (virtualPin >= NUM_ADC_BUTTONS) {
        return 0xFF;
    }
    return virtualPinToButtonIndex_[virtualPin];
}