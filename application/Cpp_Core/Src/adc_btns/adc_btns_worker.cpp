#include "adc_btns/adc_btns_worker.hpp"
#include "board_cfg.h"

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


ADCBtnsError ADCBtnsWorker::setup() {
    std::string id = ADC_MANAGER.getDefaultMapping();
    if(id.empty()) {
        return ADCBtnsError::MAPPING_NOT_FOUND;
    }

    GamepadProfile* profile = STORAGE_MANAGER.getGamepadProfile(STORAGE_MANAGER.config.defaultProfileId);
    const std::array<ADCButtonValueInfo, NUM_ADC_BUTTONS>& adcBtnInfos = ADC_MANAGER.readADCValues();
    const ADCValuesMapping* mapping = ADC_MANAGER.getMapping(id.c_str());

    if(profile == nullptr) {
        return ADCBtnsError::GAMEPAD_PROFILE_NOT_FOUND;
    }

    if(mapping == nullptr) {
        return ADCBtnsError::MAPPING_NOT_FOUND;
    }

    this->mapping = mapping;

    // 获取校准模式配置
    bool isAutoCalibrationEnabled = STORAGE_MANAGER.config.autoCalibrationEnabled;

    // 计算最小值差值 - 使用原始映射计算
    minValueDiff = (uint16_t)((float_t)(this->mapping->originalValues[0] - this->mapping->originalValues[this->mapping->length - 1]) * MIN_VALUE_DIFF_RATIO);
    
    // 初始化按钮配置
    for(uint8_t i = 0; i < adcBtnInfos.size(); i++) {
        const ADCButtonValueInfo& adcBtnInfo = adcBtnInfos[i];

        RapidTriggerProfile* triggerConfig = &profile->triggerConfigs.triggerConfigs[i];

        float_t topDeadzoon = triggerConfig->topDeadzone;

        // 如果顶部死区小于最小值，则设置为最小值
        if(topDeadzoon < MIN_ADC_TOP_DEADZONE) {
            topDeadzoon = MIN_ADC_TOP_DEADZONE;
        }

        float_t bottomDeadzone = triggerConfig->bottomDeadzone;
        // 如果底部死区小于最小值，则设置为最小值
        if(bottomDeadzone < MIN_ADC_BOTTOM_DEADZONE) {
            bottomDeadzone = MIN_ADC_BOTTOM_DEADZONE;
        }

        // 初始化按钮配置，改为基于距离的配置
        buttonPtrs[i]->virtualPin = adcBtnInfo.virtualPin;
        buttonPtrs[i]->pressAccuracyMm = triggerConfig->pressAccuracy;
        // 弹起精度 在行程前端使用高精度，后端使用低精度，低精度最小为0.1f
        buttonPtrs[i]->releaseAccuracyMm = std::max<float_t>(triggerConfig->releaseAccuracy, MIN_ADC_RELEASE_ACCURACY);
        buttonPtrs[i]->highPrecisionReleaseAccuracyMm = triggerConfig->releaseAccuracy; // 默认与释放精度相同
        buttonPtrs[i]->topDeadzoneMm = topDeadzoon;
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

/**
 * 使用外部配置初始化ADC按键工作器（用于WebConfig等模式）
 * @param externalConfigs 外部配置数组，长度必须为NUM_ADC_BUTTONS
 * @return ADCBtnsError 初始化结果
 */
ADCBtnsError ADCBtnsWorker::setup(const ExternalADCButtonConfig* externalConfigs) {
    if (!externalConfigs) {
        return ADCBtnsError::INVALID_PARAMS;
    }
    
    std::string id = ADC_MANAGER.getDefaultMapping();
    if(id.empty()) {
        return ADCBtnsError::MAPPING_NOT_FOUND;
    }

    const std::array<ADCButtonValueInfo, NUM_ADC_BUTTONS>& adcBtnInfos = ADC_MANAGER.readADCValues();
    const ADCValuesMapping* mapping = ADC_MANAGER.getMapping(id.c_str());

    if(mapping == nullptr) {
        return ADCBtnsError::MAPPING_NOT_FOUND;
    }

    this->mapping = mapping;

    // 获取校准模式配置
    bool isAutoCalibrationEnabled = STORAGE_MANAGER.config.autoCalibrationEnabled;

    // 计算最小值差值 - 使用原始映射计算
    minValueDiff = (uint16_t)((float)(this->mapping->originalValues[0] - this->mapping->originalValues[this->mapping->length - 1]) * MIN_VALUE_DIFF_RATIO);
    
    // 使用外部配置初始化按钮配置
    for(uint8_t i = 0; i < adcBtnInfos.size(); i++) {
        const ADCButtonValueInfo& adcBtnInfo = adcBtnInfos[i];
        const ExternalADCButtonConfig& extConfig = externalConfigs[i];

        float topDeadzone = extConfig.topDeadzone;
        // 如果顶部死区小于最小值，则设置为最小值
        if(topDeadzone < MIN_ADC_TOP_DEADZONE) {
            topDeadzone = MIN_ADC_TOP_DEADZONE;
        }

        float bottomDeadzone = extConfig.bottomDeadzone;
        // 如果底部死区小于最小值，则设置为最小值
        if(bottomDeadzone < MIN_ADC_BOTTOM_DEADZONE) {
            bottomDeadzone = MIN_ADC_BOTTOM_DEADZONE;
        }

        // 初始化按钮配置，使用外部传入的配置
        buttonPtrs[i]->virtualPin = adcBtnInfo.virtualPin;
        buttonPtrs[i]->pressAccuracyMm = extConfig.pressAccuracy;
        // 弹起精度 在行程前端使用高精度，后端使用低精度，低精度最小为0.1f
        buttonPtrs[i]->releaseAccuracyMm = std::max<float>(extConfig.releaseAccuracy, MIN_ADC_RELEASE_ACCURACY);
        buttonPtrs[i]->highPrecisionReleaseAccuracyMm = extConfig.releaseAccuracy; // 默认与释放精度相同
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

            btn->initCompleted = true;
            continue;
        }
        
        // 获取按钮事件（新算法直接基于ADC值）
        const ButtonEvent event = getButtonEvent(btn, adcValue);
        

        // 处理状态转换
        if(event != ButtonEvent::NONE) {
            handleButtonState(btn, event);
        }
    }

    if(buttonTriggerStatusChanged) {
        MC.publish(MessageId::ADC_BTNS_STATE_CHANGED, &this->virtualPinMask);
        buttonTriggerStatusChanged = false;
    }

    return this->virtualPinMask;
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
    
    // 将校准后的映射复制到当前使用的映射
    memcpy(btn->valueMapping, btn->calibratedMapping, this->mapping->length * sizeof(uint16_t));
    
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
}

// 状态转换处理函数
ADCBtnsWorker::ButtonEvent ADCBtnsWorker::getButtonEvent(ADCBtn* btn, const uint16_t currentValue) {
    if (!btn || !btn->initCompleted) {
        return ButtonEvent::NONE;
    }

    // 获取当前行程距离
    float currentDistance = getDistanceByValue(btn, currentValue);
    float maxTravelDistance = (this->mapping->length - 1) * this->mapping->step;

    updateLimitValue(btn, currentValue);

    switch(btn->state) {
        case ButtonState::RELEASED:

            // 判断当前值相对于基准值的变化是否达到阈值
            if (currentValue >= btn->triggerValue && currentDistance <= maxTravelDistance - btn->topDeadzoneMm) {

                // 只有在自动校准模式下才更新滑动窗口
                if(STORAGE_MANAGER.config.autoCalibrationEnabled) {
                    if(btn->limitValue < btn->topValueWindow.getAverageValue()) {
                        btn->topValueWindow.push(btn->limitValue, 2);
                    } else {
                        btn->topValueWindow.push(btn->limitValue);
                    }
                    btn->needCalibration = true;
                }

                resetLimitValue(btn, currentValue);

                return ButtonEvent::PRESS_COMPLETE;
            }
            
            btn->lastTravelDistance = currentDistance;
            btn->lastAdcValue = currentValue;
            break;

        case ButtonState::PRESSED:

            // 判断当前值相对于基准值的变化是否达到阈值
            if (currentValue <= btn->triggerValue && currentDistance >= btn->bottomDeadzoneMm) {

                // 只有在自动校准模式下才更新滑动窗口
                if(STORAGE_MANAGER.config.autoCalibrationEnabled) {
                    if(btn->limitValue > btn->bottomValueWindow.getAverageValue()) {
                        btn->bottomValueWindow.push(btn->limitValue, 2);
                    } else {
                        btn->bottomValueWindow.push(btn->limitValue);
                    }
                    btn->needCalibration = true;
                }
                
                resetLimitValue(btn, currentValue);

                return ButtonEvent::RELEASE_COMPLETE;
            }
            
            btn->lastTravelDistance = currentDistance;
            btn->lastAdcValue = currentValue;
            break;

        default:
            btn->lastTravelDistance = currentDistance;
            btn->lastAdcValue = currentValue;
            break;
    }
    
    return ButtonEvent::NONE;
}

/**
 * 在没有触发时，更新limitValue和triggerValue
 */
void ADCBtnsWorker::updateLimitValue(ADCBtn* btn, const uint16_t currentValue) {
    uint16_t noise = this->mapping->samplingNoise;
    if(btn->state == ButtonState::RELEASED) {
        if(currentValue + noise < btn->limitValue) {
            btn->limitValue = currentValue + noise;
            float currentDistance = getDistanceByValue(btn, currentValue);
            float currentPressAccuracy = getCurrentPressAccuracy(btn, currentDistance);
            float startDistance = getDistanceByValue(btn, btn->limitValue);
            float endDistance = startDistance - currentPressAccuracy;
            uint16_t endAdcValue = getValueByDistance(btn, btn->limitValue, -currentPressAccuracy);
            btn->triggerValue = endAdcValue;
        }
    } else if(btn->state == ButtonState::PRESSED) {
        if(currentValue - noise > btn->limitValue) {
            btn->limitValue = currentValue - noise;
            float currentDistance = getDistanceByValue(btn, currentValue);
            float currentReleaseAccuracy = getCurrentReleaseAccuracy(btn, currentDistance);
            float startDistance = getDistanceByValue(btn, btn->limitValue);
            float endDistance = startDistance + currentReleaseAccuracy;
            uint16_t endAdcValue = getValueByDistance(btn, btn->limitValue, currentReleaseAccuracy);
            btn->triggerValue = endAdcValue;
        }
    }
}

/**
 * 在触发时，重置limitValue和triggerValue
 */
void ADCBtnsWorker::resetLimitValue(ADCBtn* btn, const uint16_t currentValue) {
    uint16_t noise = this->mapping->samplingNoise;
    if(btn->state == ButtonState::RELEASED) {
        btn->limitValue = currentValue - noise;
        float currentDistance = getDistanceByValue(btn, currentValue);
        float currentPressAccuracy = getCurrentPressAccuracy(btn, currentDistance);
        float startDistance = getDistanceByValue(btn, btn->limitValue);
        float endDistance = startDistance - currentPressAccuracy;
        uint16_t endAdcValue = getValueByDistance(btn, btn->limitValue, -currentPressAccuracy);
        btn->triggerValue = endAdcValue;
    } else if(btn->state == ButtonState::PRESSED) {
        btn->limitValue = currentValue + noise;
        float currentDistance = getDistanceByValue(btn, currentValue);
        float currentReleaseAccuracy = getCurrentReleaseAccuracy(btn, currentDistance);
        float startDistance = getDistanceByValue(btn, btn->limitValue);
        float endDistance = startDistance + currentReleaseAccuracy;
        uint16_t endAdcValue = getValueByDistance(btn, btn->limitValue, currentReleaseAccuracy);
        btn->triggerValue = endAdcValue;
    }
}

/**
 * 根据ADC值计算对应的行程距离
 * @param btn 按钮指针
 * @param adcValue ADC值
 * @return 对应的行程距离（mm）
 */
float ADCBtnsWorker::getDistanceByValue(ADCBtn* btn, const uint16_t adcValue) {
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
 * 附带校准逻辑
 * 校准逻辑：
 * 1. 当按下开始时，将过渡值滑动窗口的最小值存储到topValueWindow滑动窗口
 * 2. 整个按下过程，将过渡值滑动窗口的值存储到travelValueWindow滑动窗口
 * 3. 当释放开始时，将过渡值滑动窗口的最大值存储到bottomValueWindow滑动窗口
 * 4. 整个释放过程，将过渡值滑动窗口的值存储到travelValueWindow滑动窗口
 * @param btn 按钮指针
 * @param currentIndex 当前索引
 * @param currentValue 当前值
 * @param event 事件
 */
void ADCBtnsWorker::handleButtonState(ADCBtn* btn, const ButtonEvent event) {
    switch(event) {
        case ButtonEvent::PRESS_COMPLETE:
            btn->state = ButtonState::PRESSED;
            buttonTriggerStatusChanged = true;
            this->virtualPinMask |= (1U << btn->virtualPin);

            break;

        case ButtonEvent::RELEASE_COMPLETE:
            btn->state = ButtonState::RELEASED;
            buttonTriggerStatusChanged = true;
            this->virtualPinMask &= ~(1U << btn->virtualPin);

            break;

        default:
            break;
    }
}