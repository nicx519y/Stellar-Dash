#ifndef _LEDS_MANAGER_H_
#define _LEDS_MANAGER_H_

#include "pwm-ws2812b.h"
#include "storagemanager.hpp"
#include "utils.h"
#include "enums.hpp"
#include "types.hpp"
#include "constant.hpp"
#include "config.hpp"
#include "leds/gradient_color.hpp"

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
    private:
        LEDsManager();
        uint32_t t;
        GradientColor gtc;
        const LEDProfile* opts;
        RGBColor frontColor;
        RGBColor backgroundColor1;
        RGBColor backgroundColor2;
        uint8_t brightness;
        uint32_t lastBreathTime = 0;
        uint8_t breathBrightness = 0;
        bool breathIncreasing = true;
};

#define LEDS_MANAGER LEDsManager::getInstance()

#endif // _LEDS_MANAGER_H_