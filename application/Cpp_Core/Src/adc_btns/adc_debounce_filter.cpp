#include "adc_btns/adc_debounce_filter.hpp"

/*
 * ======================================================================
 * ADC按钮防抖算法 - UltraFast版本
 * ======================================================================
 * 
 * UltraFast算法特点：
 * - 延迟：约150μs (3次采样 × 50μs间隔)
 * - 完全避免时间戳获取
 * - 使用计数器代替时间判断
 * - 最小化CPU开销
 * - 适合竞技游戏场景
 * 
 * 工作原理：
 * 1. 每个按钮维护独立的状态机
 * 2. 连续N次检测到相同状态才确认状态变化
 * 3. 状态变化时重置计数器
 * 
 * ======================================================================
 */

ADCDebounceFilter::ADCDebounceFilter(const Config& config) : config_(config) {
    reset();
}

uint32_t ADCDebounceFilter::filterMask(uint32_t currentMask, uint32_t currentTime) {
    uint32_t result = 0;
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        bool currentBit = (currentMask & (1U << i)) != 0;
        bool filteredBit = filterUltraFastSingle(i, currentBit);
        if (filteredBit) {
            result |= (1U << i);
        }
    }
    return result;
}

void ADCDebounceFilter::reset() {
    // 重置超快版本状态
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        ultrafastStates_[i] = UltraFastButtonState();
    }
}

void ADCDebounceFilter::resetButton(uint8_t buttonIndex) {
    if (buttonIndex >= NUM_ADC_BUTTONS) {
        return;
    }
    
    // 超快版本
    ultrafastStates_[buttonIndex] = UltraFastButtonState();
}

void ADCDebounceFilter::setConfig(const Config& config) {
    config_ = config;
    reset(); // 重置状态以应用新配置
}

uint8_t ADCDebounceFilter::getButtonDebounceState(uint8_t buttonIndex) const {
    if (buttonIndex >= NUM_ADC_BUTTONS) {
        return 0;
    }
    
    return ultrafastStates_[buttonIndex].sameValueCounter;
}

void ADCDebounceFilter::getDetailedDebounceState(uint8_t buttonIndex, bool& lastInput, bool& stableValue, uint8_t& counter) const {
    if (buttonIndex >= NUM_ADC_BUTTONS) {
        lastInput = false;
        stableValue = false;
        counter = 0;
        return;
    }
    
    const UltraFastButtonState& state = ultrafastStates_[buttonIndex];
    lastInput = state.lastInputValue;
    stableValue = state.lastStableValue;
    counter = state.sameValueCounter;
}

bool ADCDebounceFilter::filterUltraFastSingle(uint8_t buttonIndex, bool currentState) {
    if (buttonIndex >= NUM_ADC_BUTTONS) {
        return currentState; // 边界检查
    }
    
    UltraFastButtonState& state = ultrafastStates_[buttonIndex];
    
    if (currentState == state.lastInputValue) {
        // 输入值未变化，增加相同值计数器
        if (state.sameValueCounter < config_.ultrafastThreshold) {
            state.sameValueCounter++;
        }
        
        // 如果达到阈值且与当前稳定状态不同，则更新稳定状态
        if (state.sameValueCounter >= config_.ultrafastThreshold && currentState != state.lastStableValue) {
            state.lastStableValue = currentState;
        }
    } else {
        // 输入值发生变化，重置计数器并记录新的输入值
        state.lastInputValue = currentState;
        state.sameValueCounter = 1;
    }
    
    // 返回当前稳定状态
    return (state.lastStableValue == currentState);
} 