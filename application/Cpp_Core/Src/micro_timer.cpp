#include "micro_timer.hpp"
#include "board_cfg.h"

// STM32H750的CPU频率 - 根据实际系统时钟配置调整
#define CYCLES_PER_MICROSECOND (SYSTEM_CLOCK_FREQ / 1000000UL)

MicrosTimer::MicrosTimer() : overflowCount(0) {
    // 启用DWT和CYCCNT
    initDWT();
}

void MicrosTimer::initDWT() {
    // 启用DWT
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    
    // 重置CYCCNT
    DWT->CYCCNT = 0;
    
    // 启用CYCCNT计数器
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t MicrosTimer::micros() {
    // 读取当前的循环计数
    uint32_t cycles = DWT->CYCCNT;
    
    // 转换为微秒
    return cycles / CYCLES_PER_MICROSECOND;
}

void MicrosTimer::reset() {
    // 重置DWT计数器
    DWT->CYCCNT = 0;
    overflowCount = 0;
}

void MicrosTimer::delayMicros(uint32_t us) {
    uint32_t start_cycles = DWT->CYCCNT;
    uint32_t delay_cycles = us * CYCLES_PER_MICROSECOND;
    
    // 等待指定的循环数
    while((DWT->CYCCNT - start_cycles) < delay_cycles) {
        __NOP();  // 防止编译器优化掉循环
    }
}

bool MicrosTimer::checkInterval(uint32_t interval_us, uint32_t& lastTime) {
    uint32_t currentTime = micros();
    uint32_t elapsed;
    
    if (currentTime >= lastTime) {
        // 正常情况
        elapsed = currentTime - lastTime;
    } else {
        // 溢出情况
        // 最大微秒数：0xFFFFFFFF / CYCLES_PER_MICROSECOND
        uint32_t max_micros = 0xFFFFFFFF / CYCLES_PER_MICROSECOND;
        elapsed = (max_micros - lastTime) + currentTime + 1;
    }
    
    if (elapsed >= interval_us) {
        lastTime = currentTime;
        return true;
    }
    
    return false;
}

// 由于使用DWT，不再需要定时器回调
// 但为了保持兼容性，保留空实现
// void MicrosTimer::handleOverflow() {
//     // DWT方案不需要处理定时器溢出
//     // 溢出处理在checkInterval和micros中自动处理
// }

// // 定时器回调函数 - 现在为空实现
// void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
//     // DWT方案不需要定时器回调
//     // 保留此函数以防其他代码依赖
// }
