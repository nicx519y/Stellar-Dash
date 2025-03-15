#ifndef _LEDS_MANAGER_H_
#define _LEDS_MANAGER_H_

#include "pwm-ws2812b.h"
#include "storagemanager.hpp"
#include "utils.h"
#include "enums.hpp"
#include "types.hpp"
#include "config.hpp"
#include "leds/gradient_color.hpp"
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
    private:
        LEDsManager();
        uint32_t t;
        GradientColor gtc;
        LEDProfile* opts;
        RGBColor frontColor;
        RGBColor backgroundColor1;
        RGBColor backgroundColor2;
        uint8_t brightness;
        uint32_t lastBreathTime = 0;
        uint8_t breathBrightness = 0;
};

#define LEDS_MANAGER LEDsManager::getInstance()

#endif // _LEDS_MANAGER_H_