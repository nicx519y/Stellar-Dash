#include "hotkeys_manager.hpp"


HotkeysManager::HotkeysManager() : hotkeys(STORAGE_MANAGER.getGamepadHotkeyEntry()) {
    // 初始化所有热键状态
    for (int i = 0; i < NUM_GAMEPAD_HOTKEYS; i++) {
        resetHotkeyState(i);
    }
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

void HotkeysManager::updateHotkeyState(uint32_t currentVirtualPinMask, uint32_t lastVirtualPinMask) {
    uint64_t currentTime = MICROS_TIMER.micros() / 1000; // 转换为毫秒
    
    for (int i = 0; i < NUM_GAMEPAD_HOTKEYS; i++) {
        if (hotkeys[i].virtualPin < 0) continue; // 跳过未配置的热键
        
        bool currentPressed = isHotkeyPressed(currentVirtualPinMask, i);
        bool lastPressed = isHotkeyPressed(lastVirtualPinMask, i);
        
        // 检测按键按下
        if (currentPressed && !lastPressed) {
            hotkeyStates[i].isPressed = true;
            hotkeyStates[i].hasTriggered = false;
            hotkeyStates[i].pressStartTime = currentTime;
        }
        // 检测按键释放
        else if (!currentPressed && lastPressed) {
            if (hotkeyStates[i].isPressed && !hotkeyStates[i].hasTriggered) {
                // 按键释放且未触发过，检查是否为click
                if (!hotkeys[i].isHold) {
                    // 配置为click模式，直接触发
                    runAction(hotkeys[i].action);
                }
            }
            resetHotkeyState(i);
        }
        // 检测长按
        else if (currentPressed && hotkeyStates[i].isPressed && !hotkeyStates[i].hasTriggered) {
            if (hotkeys[i].isHold && (currentTime - hotkeyStates[i].pressStartTime) >= HOLD_THRESHOLD_MS) {
                // 配置为hold模式且达到长按时间，触发
                runAction(hotkeys[i].action);
                hotkeyStates[i].hasTriggered = true;
            }
        }
    }
}

bool HotkeysManager::isHotkeyPressed(uint32_t virtualPinMask, int hotkeyIndex) {
    if (hotkeyIndex < 0 || hotkeyIndex >= NUM_GAMEPAD_HOTKEYS) return false;
    if (hotkeys[hotkeyIndex].virtualPin < 0) return false;
    
    // 检查是否同时按下了FN键和对应的热键
    uint32_t expectedMask = (1U << hotkeys[hotkeyIndex].virtualPin) | FN_BUTTON_VIRTUAL_PIN;
    return (virtualPinMask & expectedMask) == expectedMask;
}

void HotkeysManager::resetHotkeyState(int index) {
    if (index < 0 || index >= NUM_GAMEPAD_HOTKEYS) return;
    
    hotkeyStates[index].isPressed = false;
    hotkeyStates[index].hasTriggered = false;
    hotkeyStates[index].pressStartTime = 0;
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
        case GamepadHotkey::HOTKEY_INPUT_MODE_CALIBRATION_TOGGLE:
            BootMode bootMode = STORAGE_MANAGER.getBootMode() == BootMode::BOOT_MODE_CALIBRATION ? BootMode::BOOT_MODE_INPUT : BootMode::BOOT_MODE_CALIBRATION;
            STORAGE_MANAGER.setBootMode(bootMode);
            STORAGE_MANAGER.saveConfig();
            if (bootMode == BootMode::BOOT_MODE_CALIBRATION) { // 如果切换到校准模式，则重置所有校准数据
                ADC_CALIBRATION_MANAGER.resetAllCalibration();
            }
            NVIC_SystemReset();
            break;
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






