#ifndef _GRADIENT_COLOR_
#define _GRADIENT_COLOR_

#include "stm32h750xx.h"
#include "stm32h7xx_hal.h"
#include "utils.h"
// 定义包含颜色和亮度的结构体
struct GradientState {
    RGBColor color;  // 现在 RGBColor 已定义，可以使用
    uint8_t brightness;
};

class GradientColor {
    public:
        GradientColor();
        void setup(
            const struct RGBColor color1,    // 起始颜色
            const struct RGBColor color2,    // 结束颜色
            const uint8_t brightness1,       // 起始亮度
            const uint8_t brightness2,       // 结束亮度
            const uint32_t cycle            // 渐变周期(ms)
        );
        // 合并后的新函数
        struct GradientState getCurrentState();
    private:
        // 起始颜色
        double_t cr, cg, cb;
        // 结束颜色
        double_t er, eg, eb;
        // 亮度
        double_t b1, b2;
        // 开始时间
        uint32_t startTime;
        // 渐变周期
        uint32_t animationCycle;
};

#endif // !_GRADIENT_COLOR_
