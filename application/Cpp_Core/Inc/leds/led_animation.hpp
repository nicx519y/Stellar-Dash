#ifndef _LED_ANIMATION_HPP_
#define _LED_ANIMATION_HPP_

#include "stm32h750xx.h"
#include "stm32h7xx_hal.h"
#include "utils.h"
#include "enums.hpp"
#include "board_cfg.h"
#include <cmath>

// 按钮位置结构体
struct ButtonPosition {
    float x;
    float y;
    float r;
};

// LED动画参数结构体
struct LedAnimationParams {
    uint8_t index;                  // 按钮索引
    float progress;                 // 动画进度 (0.0-1.0)
    bool pressed;                   // 是否按下
    bool colorEnabled;              // 是否启用颜色
    RGBColor frontColor;            // 前景色
    RGBColor backColor1;            // 背景色1
    RGBColor backColor2;            // 背景色2
    RGBColor defaultBackColor;      // 默认背景色
    LEDEffect effectStyle;          // 效果样式
    uint8_t brightness;             // 亮度
    uint8_t animationSpeed;         // 动画速度 (1-5)
    
    // 全局动画参数
    struct {
        uint8_t rippleCount;            // 涟漪数量
        uint8_t rippleCenters[5];       // 涟漪中心索引 (最多5个)
        float rippleProgress[5];        // 涟漪进度
#if HAS_LED_AROUND
        bool aroundLedSyncMode;         // 环绕灯是否同步到主LED
#endif
    } global;
};

// 涟漪结构体
struct Ripple {
    uint8_t centerIndex;
    uint32_t startTime;
};

// LED动画算法函数类型
typedef RGBColor (*LedAnimationAlgorithm)(const LedAnimationParams& params);

// 按钮位置数组声明
extern const ButtonPosition HITBOX_LED_POS_LIST[NUM_LED + NUM_LED_AROUND];

// 主LED和环绕LED坐标数组声明
extern const ButtonPosition* MAIN_LED_POS_LIST;
#if HAS_LED_AROUND
extern const ButtonPosition* AROUND_LED_POS_LIST;
#endif

// 颜色插值函数
RGBColor lerpColor(const RGBColor& colorA, const RGBColor& colorB, float t);

// 各种动画算法
RGBColor staticAnimation(const LedAnimationParams& params);
RGBColor breathingAnimation(const LedAnimationParams& params);
RGBColor starAnimation(const LedAnimationParams& params);
RGBColor flowingAnimation(const LedAnimationParams& params);
RGBColor rippleAnimation(const LedAnimationParams& params);
RGBColor transformAnimation(const LedAnimationParams& params);

// 获取动画算法函数
LedAnimationAlgorithm getLedAnimation(LEDEffect effect);

#if HAS_LED_AROUND
// 环绕灯流星动画函数
RGBColor aroundLedMeteorAnimation(float progress, uint8_t ledIndex, uint32_t color1, uint32_t color2, uint8_t brightness, uint8_t animationSpeed);
#endif

#endif // _LED_ANIMATION_HPP_ 