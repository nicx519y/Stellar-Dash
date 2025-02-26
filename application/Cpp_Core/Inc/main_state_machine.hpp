#ifndef _MAIN_STATE_MACHINE_
#define _MAIN_STATE_MACHINE_

#include "storagemanager.hpp"
#include "drivermanager.hpp"
#include "configmanager.hpp"
#include "gamepad.hpp"
#include "enums.hpp"
#include "config.hpp"
#include "stm32h7xx.h"
#include "states/base_state.hpp"
#include "states/webconfig_state.hpp"
#include "tim.h"
#include "constant.hpp"
#include "adc_btns/adc_btns_worker.hpp"
#include "gpio_btns/gpio_btns_worker.hpp"
#include "utils.h"
#include "leds/leds_manager.hpp"
#include "board_cfg.h"

class MainStateMachine {
    public:
        MainStateMachine(MainStateMachine const&) = delete;
        void operator=(MainStateMachine const&) = delete;
        static MainStateMachine& getInstance() {
            static MainStateMachine instance;
            return instance;
        }
        void setup();

    private:
        MainStateMachine();
        Gamepad& gamepad;
        Storage& storage;
        BaseState& state;

        uint32_t virtualPinMask = 0x0;
        uint32_t workTime = 0;
        uint32_t calibrationTime = 0;
        uint32_t ledAnimationTime = 0;

};

#define MAIN_STATE_MACHINE MainStateMachine::getInstance()

#endif // ! _MAIN_STATE_MACHINE_
