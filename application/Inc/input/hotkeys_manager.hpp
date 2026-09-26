#ifndef __HOTKEYS_MANAGER_HPP__
#define __HOTKEYS_MANAGER_HPP__

#include "enums.hpp"
#include "leds/leds_manager.hpp"
#include "storagemanager.hpp"
#include "gamepad.hpp"
#include "board_cfg.h"
#include "adc_btns/adc_calibration.hpp"
#include "micro_timer.hpp"
#include <map>

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
        
        // 新增：根据action快速查找hotkeyIndex
        int findHotkeyIndexByAction(GamepadHotkey action) const;
        
        // 新增：当热键配置发生变化时，重新构建Map
        void refreshActionToIndexMap();

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
        
        // 新增：action到hotkeyIndex的映射Map
        std::map<GamepadHotkey, int> actionToIndexMap;
        
        // 新增：构建action到index的映射
        void buildActionToIndexMap();

        bool isValidHotkey(int hotkeyIndex, uint32_t currentTime, bool currentPressed, bool lastPressed);
        void runAction(GamepadHotkey hotkeyAction);
        void resetHotkeyState(int index);
        bool isHotkeyPressed(uint32_t virtualPinMask, int hotkeyIndex, bool isOnly = true);
        void rebootSystem();
};

#define HOTKEYS_MANAGER HotkeysManager::getInstance()

#endif
