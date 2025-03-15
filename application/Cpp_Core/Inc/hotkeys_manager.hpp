#ifndef __HOTKEYS_MANAGER_HPP__
#define __HOTKEYS_MANAGER_HPP__

#include "enums.hpp"
#include "leds/leds_manager.hpp"
#include "storagemanager.hpp"
#include "gamepad.hpp"
class HotkeysManager {
    public:
        HotkeysManager(HotkeysManager const&);
        void operator=(HotkeysManager const&) = delete;

        static HotkeysManager& getInstance() {
            static HotkeysManager instance;
            return instance;
        }

        ~HotkeysManager();

        void runVirtualPinMask(uint32_t virtualPinMask);

    private:
        HotkeysManager() = default;
        GamepadHotkeyEntry *hotkeys[NUM_GAMEPAD_HOTKEYS];

        void runAction(GamepadHotkey hotkeyAction);
};

#define HOTKEYS_MANAGER HotkeysManager::getInstance()

#endif
