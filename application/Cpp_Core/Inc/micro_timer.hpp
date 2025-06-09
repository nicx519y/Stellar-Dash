#ifndef __MICROS_TIMER_HPP__
#define __MICROS_TIMER_HPP__

#include <stdint.h>
#include "tim.h"

class MicrosTimer {
public:
    // 禁止拷贝构造和赋值
    MicrosTimer(const MicrosTimer&) = delete;
    MicrosTimer& operator=(const MicrosTimer&) = delete;

    // 获取单例实例
    static MicrosTimer& getInstance() {
        static MicrosTimer instance;
        return instance;
    }

    // 获取当前微秒时间
    uint32_t micros();

    // 检查是否达到指定的时间间隔
    // 如果达到间隔则返回true并更新时间戳，否则返回false
    bool checkInterval(uint32_t interval_us, uint32_t& lastTime);


    // 重置计时器
    void reset();

    // 延时指定微秒
    void delayMicros(uint32_t us);

    

private:
    MicrosTimer();  // 私有构造函数
    volatile uint32_t overflowCount;  // 溢出计数
    
    // TIM2 溢出回调
    void handleOverflow();
    
    friend void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);
};

#define MICROS_TIMER MicrosTimer::getInstance()

#endif // __MICROS_TIMER_HPP__
