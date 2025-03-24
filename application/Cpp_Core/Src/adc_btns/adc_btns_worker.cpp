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
        buttonPtrs[i]->releaseAccuracyIndex = (uint8_t)(triggerConfig->releaseAccuracy / this->mapping->step);
        buttonPtrs[i]->topDeadzoneIndex = (uint8_t)(this->mapping->length - 1 - topDeadzoon / this->mapping->step);
        buttonPtrs[i]->bottomDeadzoneIndex = (uint8_t)(triggerConfig->bottomDeadzone / this->mapping->step);

        #if ENABLED_DYNAMIC_CALIBRATION == 1
        buttonPtrs[i]->bottomValueWindow = RingBufferSlidingWindow<uint16_t>(NUM_MAPPING_INDEX_WINDOW_SIZE);
        buttonPtrs[i]->topValueWindow = RingBufferSlidingWindow<uint16_t>(NUM_MAPPING_INDEX_WINDOW_SIZE);
        buttonPtrs[i]->limitValue = UINT16_MAX;
        buttonPtrs[i]->needCalibration = false;

        #endif
        // 校准参数
        buttonPtrs[i]->initCompleted = false;
        // 初始化状态
        buttonPtrs[i]->lastStateIndex = 0;

        memset(buttonPtrs[i]->valueMapping, 0, this->mapping->length * sizeof(uint16_t));

    }

    ADC_MANAGER.startADCSamping();

    APP_DBG("ADCBtnsWorker::setup success. startADCSamping");

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
    
    for(uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        ADCBtn* const btn = buttonPtrs[i];
        if(btn && btn->needCalibration) {
            
            const uint16_t topValue = btn->topValueWindow.getAverageValue();
            const uint16_t bottomValue = std::max<uint16_t>(btn->bottomValueWindow.getAverageValue(), static_cast<uint16_t>(topValue + minValueDiff));

            APP_DBG("Dynamic calibration start. button %d, bottomValue: %d, topValue: %d", btn->virtualPin, bottomValue, topValue);
            updateButtonMapping(btn->valueMapping, bottomValue, topValue);
            btn->needCalibration = false;
        }
    }
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
                if(indexDiff >= btn->releaseAccuracyIndex && currentIndex > btn->bottomDeadzoneIndex) {
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
