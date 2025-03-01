#include "adc_btns/adc_btns_worker.hpp"
#include "board_cfg.h"


ADCBtnsWorker::ADCBtnsWorker()
    : firstValueWindow(NUM_MAPPING_INDEX_WINDOW_SIZE)
    , lastValueWindow(NUM_MAPPING_INDEX_WINDOW_SIZE)
    , travelValueWindow(NUM_MAPPING_INDEX_WINDOW_SIZE)
    , lastWorkTime(0)
    #if ENABLED_DYNAMIC_CALIBRATION == 1
    , lastCalibrationTime(0)
    #endif
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
    
    // 初始化按钮配置
    for(uint8_t i = 0; i < adcBtnInfos.size(); i++) {
        const ADCButtonValueInfo& adcBtnInfo = adcBtnInfos[i];
        const uint8_t virtualPin = adcBtnInfo.virtualPin;
        
        RapidTriggerProfile* triggerConfig = &profile->triggerConfigs.triggerConfigs[i];
        // 初始化按钮配置 默认triggerConfig是按照ADCButtonValueInfo的virtualPin排序的
        buttonPtrs[i]->virtualPin = adcBtnInfo.virtualPin;
        buttonPtrs[i]->pressAccuracyIndex = (uint8_t)(triggerConfig->pressAccuracy / this->mapping->step);
        buttonPtrs[i]->releaseAccuracyIndex = (uint8_t)(triggerConfig->releaseAccuracy / this->mapping->step);
        buttonPtrs[i]->topDeadzoneIndex = (uint8_t)(this->mapping->length - 1 - triggerConfig->topDeadzone / this->mapping->step);
        buttonPtrs[i]->bottomDeadzoneIndex = (uint8_t)(triggerConfig->bottomDeadzone / this->mapping->step);

        // 校准参数
        buttonPtrs[i]->initCompleted = false;
        // 初始化状态
        buttonPtrs[i]->lastTriggerIndex = 0;
        buttonPtrs[i]->lastStateIndex = 0;

        #if ENABLED_DYNAMIC_CALIBRATION == 1
        buttonPtrs[i]->needCalibration = false;
        #endif

        // 初始化按钮映射
        memcpy(buttonPtrs[i]->valueMapping, this->mapping->originalValues, this->mapping->length * sizeof(uint16_t));

    }

    ADC_MANAGER.startADCSamping();

    APP_DBG("ADCBtnsWorker::setup success. startADCSamping\n");

    return ADCBtnsError::SUCCESS;
}

ADCBtnsError ADCBtnsWorker::deinit() {
    ADC_MANAGER.stopADCSamping();
    return ADCBtnsError::SUCCESS;
}


/**
 * 处理ADC转换完成消息
 * @param data ADC句柄指针
 */
uint32_t ADCBtnsWorker::read() {
    // 使用引用避免拷贝
    const std::array<ADCButtonValueInfo, NUM_ADC_BUTTONS>& adcValues = ADC_MANAGER.readADCValues();
    
    // 缓存频繁使用的值
    const uint16_t noise = this->mapping->samplingNoise;
    const uint8_t mappingLength = this->mapping->length;

    for(uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        ADCBtn* const btn = buttonPtrs[i];
        if(!btn || !btn->initCompleted) {
            continue;
        }

        // 使用 valuePtr 获取 ADC 值
        const uint16_t adcValue = *adcValues[i].valuePtr;
        if(adcValue == 0 || adcValue > UINT16_MAX) {
            continue;
        }
        
        const uint8_t currentIndex = this->searchIndexInMapping(i, adcValue);
        
        // 获取按钮事件
        const ButtonEvent event = getButtonEvent(btn, currentIndex, adcValue);
        
        // 处理状态转换
        if(event != ButtonEvent::NONE) {
            handleButtonState(btn, currentIndex, adcValue, event);
            
            APP_DBG("Button %d state: %d, event: %d, index: %d\n", 
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


/**
 * 动态校准
 */
void ADCBtnsWorker::dynamicCalibration() {
    #if ENABLED_DYNAMIC_CALIBRATION == 1
    const uint16_t firstValue = firstValueWindow.getAverageValue();
    const uint16_t lastValue = lastValueWindow.getAverageValue();
    
    for(uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        ADCBtn* const btn = buttonPtrs[i];
        if(btn && btn->needCalibration) {
            updateButtonMapping(btn->valueMapping, firstValue, lastValue);
            btn->needCalibration = false;
        }
    }
    #endif
}

/**
 * 更新按钮映射
 * @param mapping uint16_t数组指针，用于存储映射值
 * @param firstValue 新的起始值
 * @param lastValue 新的结束值
 */
void ADCBtnsWorker::updateButtonMapping(uint16_t* mapping, uint16_t firstValue, uint16_t lastValue) {
    if (!mapping || !this->mapping || firstValue == lastValue) {
        return;
    }

    // 计算原始范围和新范围
    int32_t oldRange = (int32_t)this->mapping->originalValues[this->mapping->length - 1] - 
                      (int32_t)this->mapping->originalValues[0];
    int32_t newRange = (int32_t)lastValue - (int32_t)firstValue;

    // 防止除零
    if (oldRange == 0) {
        return;
    }

    // 对每个值进行线性映射
    for (size_t i = 0; i < this->mapping->length; i++) {
        // 计算原始值在原范围内的相对位置 (0.0 到 1.0)
        float_t relativePosition = ((float_t)((int32_t)this->mapping->originalValues[i] - 
                                            (int32_t)this->mapping->originalValues[0])) / oldRange;
        
        // 使用相对位置计算新范围内的值
        int32_t newValue = (int32_t)(firstValue + (relativePosition * newRange));
        
        // 确保值在uint16_t范围内
        newValue = newValue < 0 ? 0 : (newValue > UINT16_MAX ? UINT16_MAX : newValue);
        
        // 更新映射值
        mapping[i] = (uint16_t)newValue;
    }
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

    // 处理边界情况
    if(value <= btn->valueMapping[0]) {
        return 0;
    }
    if(value >= btn->valueMapping[mapping->length - 1]) {
        return mapping->length - 1;
    }

    // 二分查找最接近的索引
    uint8_t left = 0;
    uint8_t right = mapping->length - 1;

    while(left <= right) {
        uint8_t mid = (left + right) / 2;
        uint16_t midValue = btn->valueMapping[mid];

        if(value == midValue) {
            return mid;
        }
        
        if(value < midValue) {
            if(mid == 0 || value > btn->valueMapping[mid - 1]) {
                // 找到最接近的值
                return (value - btn->valueMapping[mid - 1] < midValue - value) ? 
                       (mid - 1) : mid;
            }
            right = mid - 1;
        } else {
            if(mid == mapping->length - 1 || value < btn->valueMapping[mid + 1]) {
                // 找到最接近的值
                return (btn->valueMapping[mid + 1] - value < value - midValue) ? 
                       (mid + 1) : mid;
            }
            left = mid + 1;
        }
    }

    return left;
}



// 状态转换处理函数
ADCBtnsWorker::ButtonEvent ADCBtnsWorker::getButtonEvent(ADCBtn* btn, const uint8_t currentIndex, const uint16_t currentValue) {
    uint8_t indexDiff;
    
    switch(btn->state) {
        case ButtonState::RELEASED:
            if(currentIndex < btn->lastStateIndex) {
                indexDiff = btn->lastStateIndex - currentIndex;
                if(indexDiff >= btn->pressAccuracyIndex) {
                    return ButtonEvent::PRESS_START;
                }
            } else {
                btn->lastStateIndex = currentIndex;  // 更新状态索引
            }
            break;

        case ButtonState::PRESSING:
            if(currentIndex < btn->lastTriggerIndex) {
                indexDiff = btn->lastTriggerIndex - currentIndex;
                if(indexDiff >= btn->pressAccuracyIndex) {
                    return ButtonEvent::PRESS_COMPLETE;
                }
            } else if(currentIndex > btn->lastStateIndex) {
                return ButtonEvent::RELEASE_START;
            } else {
                btn->lastStateIndex = currentIndex;  // 更新状态索引
            }
            break;

        case ButtonState::PRESSED:
            if(currentIndex > btn->lastStateIndex) {
                return ButtonEvent::RELEASE_START;
            } else {
                btn->lastStateIndex = currentIndex;  // 更新状态索引
            }
            break;

        case ButtonState::RELEASING:
            if(currentIndex > btn->lastTriggerIndex) {
                indexDiff = currentIndex - btn->lastTriggerIndex;
                if(indexDiff >= btn->releaseAccuracyIndex) {
                    return ButtonEvent::RELEASE_COMPLETE;
                }
            } else if(currentIndex < btn->lastStateIndex) {
                return ButtonEvent::PRESS_START;
            } else {
                btn->lastStateIndex = currentIndex;  // 更新状态索引
            }
            break;
    }
    
    return ButtonEvent::NONE;
}

/**
 * 处理按钮状态转换
 * 附带校准逻辑
 * 校准逻辑：
 * 1. 当按下开始时，将过渡值滑动窗口的最小值存储到lastValueWindow滑动窗口
 * 2. 整个按下过程，将过渡值滑动窗口的值存储到travelValueWindow滑动窗口
 * 3. 当释放开始时，将过渡值滑动窗口的最大值存储到firstValueWindow滑动窗口
 * 4. 整个释放过程，将过渡值滑动窗口的值存储到travelValueWindow滑动窗口
 * @param btn 按钮指针
 * @param currentIndex 当前索引
 * @param currentValue 当前值
 * @param event 事件
 */
void ADCBtnsWorker::handleButtonState(ADCBtn* btn, const uint8_t currentIndex, const uint16_t currentValue, const ButtonEvent event) {
    switch(event) {
        case ButtonEvent::PRESS_START:
            btn->state = ButtonState::PRESSING;
            btn->lastStateIndex = currentIndex;

            #if ENABLED_DYNAMIC_CALIBRATION == 1
            /**
             * 校准逻辑，当按下开始时，将过渡值滑动窗口的最小值存储到lastValueWindow滑动窗口
             * 然后清空过渡值滑动窗口后并存储当前值到过渡值滑动窗口
             */
            lastValueWindow.push(travelValueWindow.getMinValue());
            travelValueWindow.clear();
            travelValueWindow.push(currentValue);
            btn->needCalibration = true;
            #endif

            break;

        case ButtonEvent::PRESS_COMPLETE:
            btn->state = ButtonState::PRESSED;
            btn->lastStateIndex = currentIndex;
            
            // 如果当前索引小于上死区索引，则认为按钮被按下
            if(currentIndex < btn->topDeadzoneIndex) {
                btn->lastTriggerIndex = currentIndex;
                buttonTriggerStatusChanged = true;
                this->virtualPinMask |= (1U << btn->virtualPin);
            }

            #if ENABLED_DYNAMIC_CALIBRATION == 1
            /**
             * 校准逻辑，当按下完成时（整个按下过程），将当前值存储到过渡值滑动窗口
             */
            travelValueWindow.push(currentValue);
            #endif

            break;

        case ButtonEvent::RELEASE_START:
            btn->state = ButtonState::RELEASING;
            btn->lastStateIndex = currentIndex;

            #if ENABLED_DYNAMIC_CALIBRATION == 1
            /**
             * 校准逻辑，当释放开始时，将过渡值滑动窗口的最大值存储到firstValueWindow滑动窗口
             * 然后清空过渡值滑动窗口后并存储当前值到过渡值滑动窗口
             */
            firstValueWindow.push(travelValueWindow.getMaxValue());
            travelValueWindow.clear();
            travelValueWindow.push(currentValue);
            btn->needCalibration = true;
            #endif

            break;

        case ButtonEvent::RELEASE_COMPLETE:
            btn->state = ButtonState::RELEASED;
            btn->lastStateIndex = currentIndex;

            // 如果当前索引大于下死区索引，则认为按钮被释放
            if(currentIndex > btn->bottomDeadzoneIndex) {
                btn->lastTriggerIndex = currentIndex;
                buttonTriggerStatusChanged = true;
                this->virtualPinMask &= ~(1U << btn->virtualPin);
            }

            #if ENABLED_DYNAMIC_CALIBRATION == 1
            /**
             * 校准逻辑，当释放完成时（整个释放过程），将当前值存储到过渡值滑动窗口
             */
            travelValueWindow.push(currentValue);
            #endif

            break;

        default:
            break;
    }
}
