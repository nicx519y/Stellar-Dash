#include "micro_timer.hpp"

// TIM2 的时钟频率是 240MHz
#define TIM2_CLOCK_FREQ 240000000
// TIM2 的预分频值 (PSC + 1)
#define TIM2_PRESCALER 24
// TIM2 的计数周期 (ARR + 1)
#define TIM2_PERIOD 100

// 简化计算（因为每个tick正好是0.1us）
#define TICKS_TO_US(ticks) ((ticks) / 10)
#define US_TO_TICKS(us) ((us) * 10)

MicrosTimer::MicrosTimer() : overflowCount(0) {
    // TIM2 已在 MX_TIM2_Init() 中初始化并启动
}

uint32_t MicrosTimer::micros() {
    // 禁用中断以确保读取的原子性
    __disable_irq();
    
    uint32_t count = __HAL_TIM_GET_COUNTER(&htim2);
    uint32_t overflow = overflowCount;
    
    // 检查是否发生了溢出但中断还未处理
    if(__HAL_TIM_GET_FLAG(&htim2, TIM_FLAG_UPDATE) && count < TIM2_PERIOD/2) {
        overflow++;
    }
    
    __enable_irq();
    
    // 计算总微秒数
    uint32_t total_ticks = overflow * TIM2_PERIOD + count;
    return TICKS_TO_US(total_ticks);
}

void MicrosTimer::reset() {
    __disable_irq();
    overflowCount = 0;
    __HAL_TIM_SET_COUNTER(&htim2, 0);
    __enable_irq();
}

void MicrosTimer::delayMicros(uint32_t us) {
    uint32_t start = micros();
    while((micros() - start) < us) {
        __NOP();  // 防止编译器优化掉循环
    }
}

void MicrosTimer::handleOverflow() {
    overflowCount++;
}

// TIM2 溢出中断回调
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if(htim->Instance == TIM2) {
        MICROS_TIMER.handleOverflow();
    }
}

bool MicrosTimer::checkInterval(uint32_t interval_us, uint32_t& lastTime) {
    uint32_t currentTime = micros();
    
    // 处理溢出情况
    if(currentTime < lastTime) {
        if((0xFFFFFFFF - lastTime) + currentTime + 1 >= interval_us) {
            lastTime = currentTime;
            return true;
        }
    }
    // 正常情况
    else if(currentTime - lastTime >= interval_us) {
        lastTime = currentTime;
        return true;
    }
    
    return false;
}
