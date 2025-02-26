#include "leds/gradient_color.hpp"
#include "utils.h"
#include <stdio.h>
#include <math.h>

// 定义 PI 常量
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

GradientColor::GradientColor() : 
    cr(0), cg(0), cb(0),
    er(0), eg(0), eb(0),
    b1(0), b2(0),
    startTime(0),
    animationCycle(1000) // 默认1秒
{}

void GradientColor::setup(
    const struct RGBColor color1, 
    const struct RGBColor color2, 
    const uint8_t brightness1,
    const uint8_t brightness2, 
    const uint32_t cycle
) {
    // 保存颜色参数
    cr = static_cast<double_t>(color1.r);
    cg = static_cast<double_t>(color1.g);
    cb = static_cast<double_t>(color1.b);
    er = static_cast<double_t>(color2.r);
    eg = static_cast<double_t>(color2.g);
    eb = static_cast<double_t>(color2.b);
    
    // 保存亮度参数
    b1 = static_cast<double_t>(brightness1);
    b2 = static_cast<double_t>(brightness2);
    
    // 设置周期和开始时间
    animationCycle = cycle;
    startTime = HAL_GetTick();
}

struct GradientState GradientColor::getCurrentState() {
    // 计算当前时间点
    uint32_t currentTime = HAL_GetTick();
    
    // 我们只关心一个周期内的时间，所以直接用减法
    // 即使发生溢出，模运算也能得到正确的周期内时间
    uint32_t elapsedTime = currentTime - startTime;  
    
    // 计算渐变比例 (0.0 - 1.0)
    // 由于使用了模运算，所以不需要担心溢出问题
    double_t phase = static_cast<double_t>(elapsedTime % animationCycle) / animationCycle;
    
    // 使用正弦波计算渐变比例，只计算一次
    double_t ratio = (sin(2.0 * M_PI * phase) + 1.0) / 2.0;
    
    // 返回颜色和亮度
    return {
        .color = {
            .r = static_cast<uint8_t>(cr + (er - cr) * ratio),
            .g = static_cast<uint8_t>(cg + (eg - cg) * ratio),
            .b = static_cast<uint8_t>(cb + (eb - cb) * ratio)
        },
        .brightness = static_cast<uint8_t>(b1 + (b2 - b1) * ratio)
    };
}

