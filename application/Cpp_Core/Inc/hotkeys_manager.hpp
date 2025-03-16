#ifndef __HOTKEYS_MANAGER_HPP__
#define __HOTKEYS_MANAGER_HPP__

#include "enums.hpp"
#include "leds/leds_manager.hpp"
#include "storagemanager.hpp"
#include "gamepad.hpp"
#include "board_cfg.h"
class HotkeysManager {
    public:
        HotkeysManager(HotkeysManager const&) = delete;
        void operator=(HotkeysManager const&) = delete;

        static HotkeysManager& getInstance() {
            static HotkeysManager instance;
            return instance;
        }

        

        void runVirtualPinMask(uint32_t virtualPinMask);

    private:
        HotkeysManager();
        ~HotkeysManager();
        GamepadHotkeyEntry* hotkeys;

        void runAction(GamepadHotkey hotkeyAction);
};

#define HOTKEYS_MANAGER HotkeysManager::getInstance()

#endif
