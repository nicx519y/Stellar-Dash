#ifndef _LEDS_MANAGER_H_
#define _LEDS_MANAGER_H_

#include "pwm-ws2812b.h"
#include "storagemanager.hpp"
#include "utils.h"
#include "enums.hpp"
#include "types.hpp"
#include "config.hpp"
#include "leds/gradient_color.hpp"
#include "leds/led_animation.hpp"
#include "board_cfg.h"

class LEDsManager {
    public:
        LEDsManager(LEDsManager const&) = delete;
        void operator=(LEDsManager const&) = delete;
        static LEDsManager& getInstance() {
            static LEDsManager instance;
            return instance;
        }

        void setup();
        void loop(uint32_t virtualPinMask);
        void deinit();
        void effectStyleNext();
        void effectStylePrev();
        void brightnessUp();
        void brightnessDown();
        void enableSwitch();
        void setBrightness(uint8_t brightness);
        
        // 配置管理函数
        void setTemporaryConfig(const LEDProfile& tempConfig);
        void restoreDefaultConfig();
        bool isUsingTemporaryConfig() const;
        
        // 测试函数
        void testAnimation(LEDEffect effect, float progress = 0.5f, uint32_t buttonMask = 0);
        void previewAnimation(LEDEffect effect, uint32_t duration = 5000);
    private:
        LEDsManager();
        uint32_t t;
        GradientColor gtc;
        LEDProfile* opts;
        LEDProfile temporaryConfig;  // 临时配置存储
        bool usingTemporaryConfig;   // 是否正在使用临时配置
        RGBColor frontColor;
        RGBColor backgroundColor1;
        RGBColor backgroundColor2;
        RGBColor defaultBackColor;
        uint8_t brightness;
        uint32_t lastBreathTime = 0;
        uint8_t breathBrightness = 0;
        
        // 新增动画系统相关成员
        uint32_t animationStartTime;
        uint32_t lastButtonState;
        Ripple ripples[5];
        uint8_t rippleCount;
        
        // 环绕灯动画系统相关成员
#if HAS_LED_AROUND
        uint32_t aroundLedAnimationStartTime;
        Ripple aroundLedRipples[5];
        uint8_t aroundLedRippleCount;
        
        // 震荡动画状态管理
        uint32_t lastQuakeTriggerTime;    // 最后一次震荡触发时间
        uint32_t lastButtonPressTime;     // 最后一次按键时间，用于触发震荡重置
#endif
        
        // 动画处理函数
        void processButtonPress(uint32_t virtualPinMask);
        void updateRipples();
        float getAnimationProgress();
        
#if HAS_LED_AROUND
        // 环绕灯动画处理函数
        void processAroundLedAnimation();
        float getAroundLedAnimationProgress();
        void updateAroundLedColors();
#endif
        
        // 内部配置管理
        void updateColorsFromConfig();
};

#define LEDS_MANAGER LEDsManager::getInstance()

#endif // _LEDS_MANAGER_H_