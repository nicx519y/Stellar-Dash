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

    // 计算最小值差值
    minValueDiff = (uint16_t)((float_t)(this->mapping->originalValues[0] - this->mapping->originalValues[this->mapping->length - 1]) * MIN_VALUE_DIFF_RATIO);
    
    // 初始化按钮配置
    for(uint8_t i = 0; i < adcBtnInfos.size(); i++) {
        const ADCButtonValueInfo& adcBtnInfo = adcBtnInfos[i];
        const uint8_t virtualPin = adcBtnInfo.virtualPin;

        RapidTriggerProfile* triggerConfig = &profile->triggerConfigs.triggerConfigs[i];
        float_t topDeadzoon = triggerConfig->topDeadzone;

        // 如果顶部死区小于最小值，则设置为最小值
        if(topDeadzoon < MIN_ADC_TOP_DEADZONE) {
            topDeadzoon = MIN_ADC_TOP_DEADZONE;
        }

        // 初始化按钮配置 默认triggerConfig是按照ADCButtonValueInfo的virtualPin排序的
        buttonPtrs[i]->virtualPin = adcBtnInfo.virtualPin;
        buttonPtrs[i]->pressAccuracyIndex = (uint8_t)(triggerConfig->pressAccuracy / this->mapping->step);
        
        // 对于releaseAccuracyIndex，确保至少为1，避免在后半段无法触发释放
        float_t releaseAccuracyFloat = triggerConfig->releaseAccuracy / this->mapping->step;
        buttonPtrs[i]->releaseAccuracyIndex = (uint8_t)std::max(1.0f, releaseAccuracyFloat);
        
        buttonPtrs[i]->topDeadzoneIndex = (uint8_t)(this->mapping->length - 1 - topDeadzoon / this->mapping->step);
        
        // 对于bottomDeadzoneIndex，也确保合理的值
        float_t bottomDeadzoneFloat = triggerConfig->bottomDeadzone / this->mapping->step;
        buttonPtrs[i]->bottomDeadzoneIndex = (uint8_t)bottomDeadzoneFloat;

        // 计算高精度配置 (基于0.01mm精度，即mapping->step/10)
        float_t highPrecisionStep = this->mapping->step / 10.0f;
        buttonPtrs[i]->highPrecisionReleaseAccuracyIndex = (uint8_t)std::max(1.0f, triggerConfig->releaseAccuracy / highPrecisionStep);
        buttonPtrs[i]->highPrecisionBottomDeadzoneIndex = (uint8_t)(triggerConfig->bottomDeadzone / highPrecisionStep);

        // 根据校准模式初始化按键映射
        uint16_t topValue, bottomValue;
        ADCBtnsError calibrationResult = ADC_MANAGER.getCalibrationValues(id.c_str(), i, isAutoCalibrationEnabled, topValue, bottomValue);
        
        if(calibrationResult == ADCBtnsError::SUCCESS && topValue != 0 && bottomValue != 0) {
            // 使用存储的校准值初始化映射
            APP_DBG("Using %s calibration values for button %d: top=%d, bottom=%d", 
                    isAutoCalibrationEnabled ? "auto" : "manual", i, topValue, bottomValue);
            initButtonMappingWithCalibration(buttonPtrs[i], topValue, bottomValue);
        } else {
            // 如果没有校准值，使用默认的偏移初始化方式
            APP_DBG("No calibration values found for button %d, using default offset initialization", i);
            // 这里需要等待第一次ADC读取来初始化
            buttonPtrs[i]->initCompleted = false;
        }

        #if ENABLED_DYNAMIC_CALIBRATION == 1
        // 只有在自动校准模式下才启用动态校准
        if(isAutoCalibrationEnabled) {
            buttonPtrs[i]->bottomValueWindow = RingBufferSlidingWindow<uint16_t>(NUM_MAPPING_INDEX_WINDOW_SIZE);
            buttonPtrs[i]->topValueWindow = RingBufferSlidingWindow<uint16_t>(NUM_MAPPING_INDEX_WINDOW_SIZE);
            buttonPtrs[i]->limitValue = UINT16_MAX;
            buttonPtrs[i]->needCalibration = false;
            buttonPtrs[i]->needSaveCalibration = false;
            buttonPtrs[i]->lastCalibrationTime = 0;
            buttonPtrs[i]->lastSaveTime = 0;
        }
        #endif
        
        // 初始化状态
        buttonPtrs[i]->lastStateIndex = 0;

        // 如果没有使用校准值初始化，则清空映射数组
        if(calibrationResult != ADCBtnsError::SUCCESS || topValue == 0 || bottomValue == 0) {
            memset(buttonPtrs[i]->valueMapping, 0, this->mapping->length * sizeof(uint16_t));
        }
    }

    ADC_MANAGER.startADCSamping();

    APP_DBG("ADCBtnsWorker::setup success. Calibration mode: %s", 
            isAutoCalibrationEnabled ? "Auto" : "Manual");

    return ADCBtnsError::SUCCESS;
}

ADCBtnsError ADCBtnsWorker::deinit() {
    ADC_MANAGER.stopADCSamping();
    #if ENABLED_DYNAMIC_CALIBRATION == 1
    for(uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        ADCBtn* const btn = buttonPtrs[i];
        
        btn->bottomValueWindow.clear();
        btn->topValueWindow.clear();
    }
    #endif
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
            if(i == 0) {
                APP_DBG("ADC_BTNS_WORKER::initButtonMapping: first %d, last: %d", btn->valueMapping[0], btn->valueMapping[this->mapping->length - 1]);
            }

            btn->initCompleted = true;
            continue;
        }
        
        const uint8_t currentIndex = this->searchIndexInMapping(i, adcValue);
        
        // 获取按钮事件
        const ButtonEvent event = getButtonEvent(btn, currentIndex, adcValue);
        
        // 处理状态转换
        if(event != ButtonEvent::NONE) {
            handleButtonState(btn, event);
            
            APP_DBG("Button %d state: %d, event: %d, index: %d", 
                i, static_cast<int>(btn->state), 
                static_cast<int>(event), currentIndex);
        }
    }

    if(buttonTriggerStatusChanged) {
        MC.publish(MessageId::ADC_BTNS_STATE_CHANGED, &this->virtualPinMask);
        buttonTriggerStatusChanged = false;
    }

    return this->virtualPinMask;
}

#if ENABLED_DYNAMIC_CALIBRATION == 1
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
            
            const uint16_t topValue = btn->topValueWindow.getAverageValue();
            const uint16_t bottomValue = std::max<uint16_t>(btn->bottomValueWindow.getAverageValue(), static_cast<uint16_t>(topValue + minValueDiff));

            APP_DBG("Dynamic calibration start. button %d, bottomValue: %d, topValue: %d", btn->virtualPin, bottomValue, topValue);
            
            // 更新按钮映射
            updateButtonMapping(btn->valueMapping, bottomValue, topValue);
            
            // 标记需要保存，但不立即保存
            btn->needSaveCalibration = true;
            btn->lastCalibrationTime = currentTime;
            btn->needCalibration = false;
            hasCalibrationUpdate = true;
            
            APP_DBG("Auto calibration updated for button %d: top=%d, bottom=%d (save pending)", i, topValue, bottomValue);
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
            
            const uint16_t topValue = btn->topValueWindow.getAverageValue();
            const uint16_t bottomValue = std::max<uint16_t>(btn->bottomValueWindow.getAverageValue(), static_cast<uint16_t>(topValue + minValueDiff));
            
            // 保存自动校准值到存储
            ADCBtnsError saveResult = ADC_MANAGER.setCalibrationValues(mappingId.c_str(), i, true, topValue, bottomValue);
            if (saveResult == ADCBtnsError::SUCCESS) {
                APP_DBG("Auto calibration values saved to storage for button %d: top=%d, bottom=%d", i, topValue, bottomValue);
                btn->needSaveCalibration = false;
                btn->lastSaveTime = currentTime;
            } else {
                APP_DBG("Failed to save auto calibration values for button %d", i);
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

#endif

/**
 * 更新按钮映射
 * @param mapping uint16_t数组指针，用于存储映射值
 * @param firstValue 新的起始值
 * @param lastValue 新的结束值
 */
void ADCBtnsWorker::updateButtonMapping(uint16_t* mapping, uint16_t bottomValue, uint16_t topValue) {
    if (!mapping || !this->mapping || bottomValue == topValue) {
        return;
    }

    // 计算原始范围和新范围
    int32_t oldRange = (int32_t)this->mapping->originalValues[this->mapping->length - 1] - 
                      (int32_t)this->mapping->originalValues[0];
    int32_t newRange = (int32_t)topValue - (int32_t)bottomValue;

    // 防止除零
    if (oldRange == 0) {
        return;
    }

    // 对每个值进行线性映射
    for (size_t i = 0; i < this->mapping->length; i++) {
        // 计算原始值在原范围内的相对位置 (0.0 到 1.0)
        double relativePosition = ((double)((int32_t)this->mapping->originalValues[i] - 
                                         (int32_t)this->mapping->originalValues[0])) / oldRange;
        
        // 使用相对位置计算新范围内的值
        int32_t newValue = bottomValue + (int32_t)(relativePosition * newRange + 0.5);
        
        // 确保值在uint16_t范围内
        mapping[i] = (uint16_t)std::max<int32_t>(0, std::min<int32_t>(UINT16_MAX, newValue));
    }
    
    // 更新对应按钮的高精度映射表
    for (uint8_t btnIndex = 0; btnIndex < NUM_ADC_BUTTONS; btnIndex++) {
        ADCBtn* btn = buttonPtrs[btnIndex];
        if (btn && btn->valueMapping == mapping) {
            initHighPrecisionMapping(btn);
            break;
        }
    }
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

    APP_DBG("ADC_BTNS_WORKER::initButtonMapping - offset: %d, releaseValue: %d", offset, releaseValue);

    // 对每个值进行平移
    for (size_t i = 0; i < mapping->length; i++) {
        // 使用int32_t避免uint16_t相加减可能的溢出
        int32_t newValue = (int32_t)mapping->originalValues[i] + offset;
        
        // 确保值在uint16_t范围内
        newValue = newValue < 0 ? 0 : (newValue > UINT16_MAX ? UINT16_MAX : newValue);
        
        // 更新映射值
        btn->valueMapping[i] = (uint16_t)newValue;

    }

    #if ENABLED_DYNAMIC_CALIBRATION == 1
    // 初始化滑动窗口
    btn->bottomValueWindow.clear();
    btn->topValueWindow.clear();
    btn->limitValue = UINT16_MAX;

    btn->bottomValueWindow.push(btn->valueMapping[0]);
    btn->topValueWindow.push(btn->valueMapping[mapping->length - 1]);
    #endif
    
    // 初始化高精度映射表
    initHighPrecisionMapping(btn);
}

/**
 * 使用校准值初始化按钮映射
 * @param btn 按钮指针
 * @param topValue 完全按下时的校准值
 * @param bottomValue 完全释放时的校准值
 */
void ADCBtnsWorker::initButtonMappingWithCalibration(ADCBtn* btn, uint16_t topValue, uint16_t bottomValue) {
    if (!btn || !mapping || topValue == bottomValue) {
        return;
    }

    // 确保topValue < bottomValue (完全按下的值应该小于完全释放的值)
    if (topValue > bottomValue) {
        uint16_t temp = topValue;
        topValue = bottomValue;
        bottomValue = temp;
        APP_DBG("ADC_BTNS_WORKER::initButtonMappingWithCalibration - Swapped values: top=%d, bottom=%d", topValue, bottomValue);
    }

    // 使用校准值更新按钮映射
    updateButtonMapping(btn->valueMapping, bottomValue, topValue);

    #if ENABLED_DYNAMIC_CALIBRATION == 1
    // 如果启用了动态校准，初始化滑动窗口
    if (STORAGE_MANAGER.config.autoCalibrationEnabled) {
        btn->bottomValueWindow.clear();
        btn->topValueWindow.clear();
        btn->limitValue = UINT16_MAX;

        btn->bottomValueWindow.push(bottomValue);
        btn->topValueWindow.push(topValue);
        btn->needSaveCalibration = false;
        btn->lastCalibrationTime = 0;
        btn->lastSaveTime = 0;
    }
    #endif
    
    // 初始化高精度映射表
    initHighPrecisionMapping(btn);
    
    // 标记初始化完成
    btn->initCompleted = true;
    
    APP_DBG("ADC_BTNS_WORKER::initButtonMappingWithCalibration - Button %d initialized with calibration: top=%d, bottom=%d", 
            btn->virtualPin, topValue, bottomValue);
}

/**
 * 根据当前搜索按钮值在映射数组中的位置，返回索引
 * @param buttonIndex 按钮索引
 * @param value ADC输入值
 * @return 返回在映射数组中的索引位置（最大值为 length-2）
 */
uint8_t ADCBtnsWorker::searchIndexInMapping(const uint8_t buttonIndex, const uint16_t value) {
    ADCBtn* btn = buttonPtrs[buttonIndex];
    if(!btn || !mapping) {
        return 0;
    }

    const uint16_t doubleNoise = this->mapping->samplingNoise * 2;

    // 处理边界情况
    if(value >= btn->valueMapping[1]) {  // 如果大于等于索引1的值
        return 0;
    }
    if(value < btn->valueMapping[mapping->length - 2] + doubleNoise) { // 如果小于倒数第二个值 加入噪声 为了防止误判
        return mapping->length - 1;
    }

    // 二分查找区间
    uint8_t left = 1;
    uint8_t right = mapping->length - 2;

    while(left <= right) {
        uint8_t mid = (left + right) / 2;
        
        // 检查当前区间
        if(value >= btn->valueMapping[mid] && value < btn->valueMapping[mid - 1]) {
            return mid;
        }
        
        if(value >= btn->valueMapping[mid]) {
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }

    return 0;
}



// 状态转换处理函数
ADCBtnsWorker::ButtonEvent ADCBtnsWorker::getButtonEvent(ADCBtn* btn, const uint8_t currentIndex, const uint16_t currentValue) {
    uint8_t indexDiff;

    switch(btn->state) {
        case ButtonState::RELEASED:

            #if ENABLED_DYNAMIC_CALIBRATION == 1
            if(currentValue < btn->limitValue) { // 抬起时，记录最小值
                btn->limitValue = currentValue;
            }
            #endif

            if(currentIndex < btn->lastStateIndex) {
                indexDiff = btn->lastStateIndex - currentIndex;
                if(indexDiff >= btn->pressAccuracyIndex && currentIndex < btn->topDeadzoneIndex) {
                    btn->lastStateIndex = currentIndex;

                    #if ENABLED_DYNAMIC_CALIBRATION == 1
                    // 如果btn->limitValue < btn->topValueWindow.getAverageValue，则以2倍率push，否则以1倍率push。小优先
                    if(btn->limitValue < btn->topValueWindow.getAverageValue()) {
                        btn->topValueWindow.push(btn->limitValue, 2);
                    } else {
                        btn->topValueWindow.push(btn->limitValue);
                    }
                    // btn->topValueWindow.printAllValues();
                    APP_DBG("limitValue: %d, topValueWindow.getAverageValue: %d", btn->limitValue, btn->topValueWindow.getAverageValue());
                    btn->limitValue = 0; // 重置限制值，用于下次校准，此时是按下，重置为0
                    btn->needCalibration = true;
                    #endif

                    return ButtonEvent::PRESS_COMPLETE;
                }
            } else {
                btn->lastStateIndex = currentIndex;
            }
            break;

        case ButtonState::PRESSED:

            #if ENABLED_DYNAMIC_CALIBRATION == 1
            if(currentValue > btn->limitValue) { // 按下时，记录最大值
                btn->limitValue = currentValue;
            }
            #endif

            if(currentIndex > btn->lastStateIndex) {
                indexDiff = currentIndex - btn->lastStateIndex;
                bool shouldRelease = false;
                
                // 根据当前位置选择合适的精度进行释放判断
                if (isInHighPrecisionRange(btn, currentIndex)) {
                    // 当前在高精度范围内（前半段），使用高精度检测
                    uint8_t highPrecisionIndex = searchIndexInHighPrecisionMapping(btn, currentValue);
                    uint8_t highPrecisionLastIndex = (btn->lastStateIndex <= btn->halfwayIndex) ? 
                                                    (btn->lastStateIndex * 10) : (btn->halfwayIndex * 10 - 1);
                    
                    if (highPrecisionIndex > highPrecisionLastIndex) {
                        uint8_t highPrecisionDiff = highPrecisionIndex - highPrecisionLastIndex;
                        
                        // 使用高精度释放精度和死区判断
                        if (highPrecisionDiff >= btn->highPrecisionReleaseAccuracyIndex && 
                            highPrecisionIndex > btn->highPrecisionBottomDeadzoneIndex) {
                            shouldRelease = true;
                            APP_DBG("High precision release detected: button %d, highPrecisionIndex: %d, diff: %d", 
                                    btn->virtualPin, highPrecisionIndex, highPrecisionDiff);
                        }
                    }
                } else {
                    // 当前在后半段，需要考虑跨边界情况
                    float_t totalMovement = 0.0f;
                    
                    if (btn->lastStateIndex <= btn->halfwayIndex) {
                        // 跨边界情况：lastStateIndex在前半段，currentIndex在后半段
                        // 计算从lastStateIndex到边界的高精度移动距离
                        float_t frontHalfMovement = (btn->halfwayIndex - btn->lastStateIndex) * 10 * (this->mapping->step / 10.0f);
                        // 计算从边界到currentIndex的常规移动距离
                        float_t backHalfMovement = (currentIndex - btn->halfwayIndex) * this->mapping->step;
                        totalMovement = frontHalfMovement + backHalfMovement;
                        
                        APP_DBG("Cross-boundary release check: button %d, frontHalf=%.2f, backHalf=%.2f, total=%.2f", 
                                btn->virtualPin, frontHalfMovement, backHalfMovement, totalMovement);
                    } else {
                        // 都在后半段，使用常规计算
                        totalMovement = indexDiff * this->mapping->step;
                    }
                    
                    // 使用配置的releaseAccuracy进行判断（物理距离）
                    if (totalMovement >= btn->releaseAccuracyIndex * this->mapping->step && currentIndex > btn->bottomDeadzoneIndex) {
                        shouldRelease = true;
                        APP_DBG("Standard precision release detected: button %d, totalMovement=%.2f, required=%.2f", 
                                btn->virtualPin, totalMovement, btn->releaseAccuracyIndex * this->mapping->step);
                    }
                }
                
                if (shouldRelease) {
                    btn->lastStateIndex = currentIndex;

                    #if ENABLED_DYNAMIC_CALIBRATION == 1
                    // 如果btn->limitValue > btn->bottomValueWindow.getAverageValue，则以2倍率push，否则以1倍率push。大优先
                    if(btn->limitValue > btn->bottomValueWindow.getAverageValue()) {
                        btn->bottomValueWindow.push(btn->limitValue, 2);
                    } else {
                        btn->bottomValueWindow.push(btn->limitValue);
                    }
                    // btn->bottomValueWindow.printAllValues();
                    APP_DBG("limitValue: %d, bottomValueWindow.getAverageValue: %d", btn->limitValue, btn->bottomValueWindow.getAverageValue());
                    btn->limitValue = UINT16_MAX; // 重置限制值，用于下次校准，此时是释放，重置为最大值
                    btn->needCalibration = true;
                    #endif

                    return ButtonEvent::RELEASE_COMPLETE;
                }
            } else {
                btn->lastStateIndex = currentIndex;
            }
            break;

        default:
            btn->lastStateIndex = currentIndex;
            break;
    }
    
    return ButtonEvent::NONE;
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
            APP_DBG("PRESS_COMPLETE: %d", btn->virtualPin);
            buttonTriggerStatusChanged = true;
            this->virtualPinMask |= (1U << btn->virtualPin);

            break;

        case ButtonEvent::RELEASE_COMPLETE:
            btn->state = ButtonState::RELEASED;
            APP_DBG("RELEASE_COMPLETE: %d", btn->virtualPin);
            buttonTriggerStatusChanged = true;
            this->virtualPinMask &= ~(1U << btn->virtualPin);

            break;

        default:
            break;
    }
}

/**
 * 初始化高精度映射表
 * 为按键抬起行程的前半段（完全按下到弹起一半）生成0.01mm精度的插值映射表
 * @param btn 按钮指针
 */
void ADCBtnsWorker::initHighPrecisionMapping(ADCBtn* btn) {
    if (!btn || !mapping || mapping->length == 0) {
        return;
    }

    // 计算中点索引（完全按下到弹起一半的位置）
    btn->halfwayIndex = mapping->length / 2;
    
    // 确保halfwayIndex不为0，避免除零错误
    if (btn->halfwayIndex == 0) {
        btn->halfwayIndex = 1;
    }
    
    // 高精度映射表长度为前半段的10倍精度
    uint16_t calculatedLength = btn->halfwayIndex * 10;
    
    // 边界检查：确保不超过数组大小
    btn->highPrecisionLength = (calculatedLength > MAX_ADC_VALUES_LENGTH * 10) ? 
                               MAX_ADC_VALUES_LENGTH * 10 : calculatedLength;
    
    // 确保高精度长度至少为1
    if (btn->highPrecisionLength == 0) {
        btn->highPrecisionLength = 1;
    }
    
    // 为前半段行程生成10倍精度的插值映射表
    for (uint16_t i = 0; i < btn->highPrecisionLength; i++) {
        // 计算在原始映射表中的相对位置
        float_t relativePos = (btn->highPrecisionLength > 1) ? 
                             (float_t)i / (btn->highPrecisionLength - 1) : 0.0f;
        float_t originalIndex = relativePos * (btn->halfwayIndex - 1);
        
        // 获取插值的两个端点
        uint8_t lowerIndex = (uint8_t)originalIndex;
        uint8_t upperIndex = lowerIndex + 1;
        
        // 边界检查
        if (upperIndex >= btn->halfwayIndex) {
            upperIndex = btn->halfwayIndex - 1;
            lowerIndex = upperIndex;
        }
        
        // 确保索引在有效范围内
        if (lowerIndex >= mapping->length) lowerIndex = mapping->length - 1;
        if (upperIndex >= mapping->length) upperIndex = mapping->length - 1;
        
        // 线性插值
        float_t fraction = originalIndex - lowerIndex;
        uint16_t lowerValue = btn->valueMapping[lowerIndex];
        uint16_t upperValue = btn->valueMapping[upperIndex];
        
        btn->highPrecisionMapping[i] = (uint16_t)(lowerValue + fraction * (upperValue - lowerValue));
    }
    
    APP_DBG("ADC_BTNS_WORKER::initHighPrecisionMapping - button %d, halfwayIndex: %d, highPrecisionLength: %d", 
            btn->virtualPin, btn->halfwayIndex, btn->highPrecisionLength);
}

/**
 * 在高精度映射表中搜索索引
 * @param btn 按钮指针
 * @param value ADC输入值
 * @return 返回在高精度映射表中的索引位置
 */
uint8_t ADCBtnsWorker::searchIndexInHighPrecisionMapping(ADCBtn* btn, const uint16_t value) {
    if (!btn || btn->highPrecisionLength == 0) {
        return 0;
    }

    const uint16_t doubleNoise = this->mapping->samplingNoise * 2;

    // 处理边界情况
    if (btn->highPrecisionLength < 2) {
        return 0;
    }
    
    if (value >= btn->highPrecisionMapping[1]) {
        return 0;
    }
    if (value < btn->highPrecisionMapping[btn->highPrecisionLength - 2] + doubleNoise) {
        return btn->highPrecisionLength - 1;
    }

    // 二分查找区间
    uint16_t left = 1;
    uint16_t right = btn->highPrecisionLength - 2;

    while (left <= right) {
        uint16_t mid = (left + right) / 2;
        
        // 检查当前区间
        if (value >= btn->highPrecisionMapping[mid] && value < btn->highPrecisionMapping[mid - 1]) {
            return (uint8_t)std::min<uint16_t>(mid, 255); // 确保返回值在uint8_t范围内
        }
        
        if (value >= btn->highPrecisionMapping[mid]) {
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }

    return 0;
}

/**
 * 判断当前索引是否在高精度检测范围内（前半段行程）
 * @param btn 按钮指针
 * @param currentIndex 当前在常规映射表中的索引
 * @return true表示在高精度范围内，false表示不在
 */
bool ADCBtnsWorker::isInHighPrecisionRange(ADCBtn* btn, const uint8_t currentIndex) {
    if (!btn) {
        return false;
    }
    
    // 如果当前索引在前半段行程内（从完全按下到弹起一半），则使用高精度检测
    return currentIndex <= btn->halfwayIndex;
}