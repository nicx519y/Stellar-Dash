#ifndef __HOTKEYS_MANAGER_HPP__
#define __HOTKEYS_MANAGER_HPP__

#include "enums.hpp"
#include "leds/leds_manager.hpp"
#include "storagemanager.hpp"
#include "gamepad.hpp"
class HotkeysManager {
    public:
        HotkeysManager(HotkeysManager const&) = delete;
        void operator=(HotkeysManager const&) = delete;

        static HotkeysManager& getInstance() {
            static HotkeysManager instance;
            return instance;
        }

        void setup();
        void loop();

        ~HotkeysManager();

        void LEDsEffectStyleNext() {}
        void LEDsEffectStylePrev();
        void LEDsBrightnessUp();
        void LEDsBrightnessDown();
        void LEDsEnableSwitch();
        void setInputWebConfig();
        void InputModeXInput();
        void InputModePS4();
        void InputModeSwitch();
        void SystemReboot();

    private:
        HotkeysManager() = default;
};

#define HOTKEYS_MANAGER HotkeysManager::getInstance()

#endif
