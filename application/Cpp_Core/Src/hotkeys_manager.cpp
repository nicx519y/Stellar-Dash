#include "hotkeys_manager.hpp"


HotkeysManager::HotkeysManager() : hotkeys(STORAGE_MANAGER.getGamepadHotkeyEntry()) {
}

HotkeysManager::~HotkeysManager() {
}

void HotkeysManager::runVirtualPinMask(uint32_t virtualPinMask) {
    for (int i = 0; i < NUM_GAMEPAD_HOTKEYS; i++) {
        if ( hotkeys[i].virtualPin >= 0 && (uint32_t)(1U << hotkeys[i].virtualPin | FN_BUTTON_VIRTUAL_PIN) == virtualPinMask) {
            runAction(hotkeys[i].action);
            break;
        }
    }
}

void HotkeysManager::runAction(GamepadHotkey hotkeyAction) {
    switch (hotkeyAction) {
        case GamepadHotkey::HOTKEY_LEDS_EFFECTSTYLE_NEXT:
            LEDS_MANAGER.effectStyleNext();
            break;
        case GamepadHotkey::HOTKEY_LEDS_EFFECTSTYLE_PREV:
            LEDS_MANAGER.effectStylePrev();
            break;
        case GamepadHotkey::HOTKEY_LEDS_BRIGHTNESS_UP:
            LEDS_MANAGER.brightnessUp();
            break;
        case GamepadHotkey::HOTKEY_LEDS_BRIGHTNESS_DOWN:
            LEDS_MANAGER.brightnessDown();
            break;
        case GamepadHotkey::HOTKEY_LEDS_ENABLE_SWITCH:
            LEDS_MANAGER.enableSwitch();
            break;
        case GamepadHotkey::HOTKEY_INPUT_MODE_WEBCONFIG:
            STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_WEB_CONFIG);
            STORAGE_MANAGER.saveConfig();
            NVIC_SystemReset();
            break;
        // case GamepadHotkey::HOTKEY_INPUT_MODE_CALIBRATION_START:
        //     STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_CALIBRATION);
        //     STORAGE_MANAGER.saveConfig();
        //     break;
        // case GamepadHotkey::HOTKEY_INPUT_MODE_CALIBRATION_STOP:
        //     STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
        //     STORAGE_MANAGER.saveConfig();
        //     break;
        // case GamepadHotkey::HOTKEY_CLEAR_CALIBRATION_DATA:
        //     ADC_CALIBRATION_MANAGER.resetAllCalibration();
        //     break;
        case GamepadHotkey::HOTKEY_INPUT_MODE_XINPUT:
            STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
            STORAGE_MANAGER.setInputMode(InputMode::INPUT_MODE_XINPUT);
            STORAGE_MANAGER.saveConfig();
            NVIC_SystemReset();
            break;
        case GamepadHotkey::HOTKEY_INPUT_MODE_PS4:
            STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
            STORAGE_MANAGER.setInputMode(InputMode::INPUT_MODE_PS4);
            STORAGE_MANAGER.saveConfig();
            NVIC_SystemReset();
            break;
        case GamepadHotkey::HOTKEY_INPUT_MODE_PS5:
            STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
            STORAGE_MANAGER.setInputMode(InputMode::INPUT_MODE_PS5);
            STORAGE_MANAGER.saveConfig();
            NVIC_SystemReset();
            break;
        case GamepadHotkey::HOTKEY_INPUT_MODE_SWITCH:
            STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
            STORAGE_MANAGER.setInputMode(InputMode::INPUT_MODE_SWITCH);
            STORAGE_MANAGER.saveConfig();
            NVIC_SystemReset();
            break;
        case GamepadHotkey::HOTKEY_SYSTEM_REBOOT:
            NVIC_SystemReset();
            break;
        default:
            break;
    }
}






