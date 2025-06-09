#ifndef __HOTKEYS_MANAGER_HPP__
#define __HOTKEYS_MANAGER_HPP__

#include "enums.hpp"
#include "leds/leds_manager.hpp"
#include "storagemanager.hpp"
#include "gamepad.hpp"
#include "board_cfg.h"
#include "adc_btns/adc_calibration.hpp"
#include "micro_timer.hpp"

class HotkeysManager {
    public:
        HotkeysManager(HotkeysManager const&) = delete;
        void operator=(HotkeysManager const&) = delete;

        static HotkeysManager& getInstance() {
            static HotkeysManager instance;
            return instance;
        }

        void runVirtualPinMask(uint32_t virtualPinMask);
        void updateHotkeyState(uint32_t currentVirtualPinMask, uint32_t lastVirtualPinMask);

    private:
        HotkeysManager();
        ~HotkeysManager();
        GamepadHotkeyEntry* hotkeys;

        // Hold状态跟踪
        struct HotkeyState {
            bool isPressed;
            bool hasTriggered;
            uint64_t pressStartTime;
        };
        
        HotkeyState hotkeyStates[NUM_GAMEPAD_HOTKEYS];

        void runAction(GamepadHotkey hotkeyAction);
        void resetHotkeyState(int index);
        bool isHotkeyPressed(uint32_t virtualPinMask, int hotkeyIndex);
        void rebootSystem();
};

#define HOTKEYS_MANAGER HotkeysManager::getInstance()

#endif
