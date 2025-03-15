#include "hotkeys_manager.hpp"


HotkeysManager::~HotkeysManager() {
}

void HotkeysManager::setup() {
}

void HotkeysManager::loop() {
}

void HotkeysManager::LEDsEffectStyleNext() {
    LEDS_MANAGER.effectStyleNext();
}

void HotkeysManager::LEDsEffectStylePrev() {
    LEDS_MANAGER.effectStylePrev();
}

void HotkeysManager::LEDsBrightnessUp() {
    LEDS_MANAGER.brightnessUp();
}

void HotkeysManager::LEDsBrightnessDown() {
    LEDS_MANAGER.brightnessDown();
}

void HotkeysManager::LEDsEnableSwitch() {
    LEDS_MANAGER.enableSwitch();
}

void HotkeysManager::setInputWebConfig() {
    STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_WEB_CONFIG);
    NVIC_SystemReset();
}


void HotkeysManager::InputModeXInput() {
    STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
    GAMEPAD.setInputMode(InputMode::INPUT_MODE_XINPUT);
    NVIC_SystemReset();
}

void HotkeysManager::InputModePS4() {
    STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
    GAMEPAD.setInputMode(InputMode::INPUT_MODE_PS4);
    NVIC_SystemReset();
}

void HotkeysManager::InputModeSwitch() {
    STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
    GAMEPAD.setInputMode(InputMode::INPUT_MODE_SWITCH);
    NVIC_SystemReset();
}






