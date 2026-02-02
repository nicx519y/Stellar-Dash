#include "hotkeys_manager.hpp"
#include "system_logger.h"

HotkeysManager::HotkeysManager() : hotkeys(STORAGE_MANAGER.getGamepadHotkeyEntry()) {
    // 初始化所有热键状态
    for (int i = 0; i < NUM_GAMEPAD_HOTKEYS; i++) {
        resetHotkeyState(i);
    }
    
    // 构建action到index的映射Map
    buildActionToIndexMap();
}

HotkeysManager::~HotkeysManager() {
}

void HotkeysManager::buildActionToIndexMap() {
    actionToIndexMap.clear();
    
    // 遍历所有热键，构建action到index的映射
    for (int i = 0; i < NUM_GAMEPAD_HOTKEYS; i++) {
        if (hotkeys[i].action != GamepadHotkey::HOTKEY_NONE) {
            actionToIndexMap[hotkeys[i].action] = i;
        }
    }
    
    LOG_INFO("HOTKEYS", "Built action to index map with %d entries", actionToIndexMap.size());
}

int HotkeysManager::findHotkeyIndexByAction(GamepadHotkey action) const {
    auto it = actionToIndexMap.find(action);
    if (it != actionToIndexMap.end()) {
        return it->second;
    }
    return -1; // 未找到对应的hotkeyIndex
}

void HotkeysManager::refreshActionToIndexMap() {
    // 重新获取最新的热键配置
    hotkeys = STORAGE_MANAGER.getGamepadHotkeyEntry();
    
    // 重新构建Map
    buildActionToIndexMap();
    
    LOG_INFO("HOTKEYS", "Refreshed action to index map");
}

void HotkeysManager::runVirtualPinMask(uint32_t virtualPinMask) {
    for (int i = 0; i < NUM_GAMEPAD_HOTKEYS; i++) {
        if ( hotkeys[i].virtualPin >= 0 && (uint32_t)(1U << hotkeys[i].virtualPin | FN_BUTTON_VIRTUAL_PIN) == virtualPinMask) {
            runAction(hotkeys[i].action);
            break;
        }
    }
}

bool HotkeysManager::isValidHotkey(int hotkeyIndex, uint32_t currentTime, bool currentPressed, bool lastPressed) {

    bool isValid = false;

    // 检测按键按下
    if (currentPressed && !lastPressed) {
        hotkeyStates[hotkeyIndex].isPressed = true;
        hotkeyStates[hotkeyIndex].hasTriggered = false;
        hotkeyStates[hotkeyIndex].pressStartTime = currentTime;
    }
    // 检测按键释放
    else if (!currentPressed && lastPressed) {
        if (hotkeyStates[hotkeyIndex].isPressed && !hotkeyStates[hotkeyIndex].hasTriggered) {
            // 按键释放且未触发过，检查是否为click
            if (!hotkeys[hotkeyIndex].isHold) {
                isValid = true;
            }
        }
        resetHotkeyState(hotkeyIndex);
    }
    // 检测长按
    else if (currentPressed && hotkeyStates[hotkeyIndex].isPressed && !hotkeyStates[hotkeyIndex].hasTriggered) {
        if (hotkeys[hotkeyIndex].isHold && (currentTime - hotkeyStates[hotkeyIndex].pressStartTime) >= HOLD_THRESHOLD_MS) {
            // 配置为hold模式且达到长按时间，触发
            hotkeyStates[hotkeyIndex].hasTriggered = true; 
            isValid = true;
        }
    }

    return isValid;
}

/**
 * 更新热键状态
 * 1. 使用Map快速查找特殊action的索引 首先处理 webconfig 和 calibration 这两个action
 * 2. 如果特殊action被触发，则直接运行对应action
 * 3. 如果特殊action未被触发，则遍历所有热键，检查是否为click模式，如果是，则运行对应action
 * 4. 如果所有热键都不是click模式，则不运行任何action 
 * @param currentVirtualPinMask 当前虚拟pin掩码
 * @param lastVirtualPinMask 上一次虚拟pin掩码
 */
void HotkeysManager::updateHotkeyState(uint32_t currentVirtualPinMask, uint32_t lastVirtualPinMask) {
    uint32_t currentTime = HAL_GetTick();
    
    // 使用Map快速查找特殊action的索引
    int webconfigIndex = findHotkeyIndexByAction(GamepadHotkey::HOTKEY_INPUT_MODE_WEBCONFIG);

    if (webconfigIndex != -1) {
        if (isValidHotkey(
            webconfigIndex, currentTime, 
            isHotkeyPressed(currentVirtualPinMask, webconfigIndex, false), 
            isHotkeyPressed(lastVirtualPinMask, webconfigIndex, false))) {
            runAction(hotkeys[webconfigIndex].action);
            return;
        }
    }

    int calibrationIndex = findHotkeyIndexByAction(GamepadHotkey::HOTKEY_INPUT_MODE_CALIBRATION);

    if (calibrationIndex != -1) {
        if (isValidHotkey(
            calibrationIndex, currentTime, 
            isHotkeyPressed(currentVirtualPinMask, calibrationIndex, false), 
            isHotkeyPressed(lastVirtualPinMask, calibrationIndex, false))) {
            runAction(hotkeys[calibrationIndex].action);
            return;
        }
    }

    for (int i = 0; i < NUM_GAMEPAD_HOTKEYS; i++) {
        if (hotkeys[i].virtualPin < 0 || hotkeys[i].action == GamepadHotkey::HOTKEY_INPUT_MODE_WEBCONFIG || hotkeys[i].action == GamepadHotkey::HOTKEY_INPUT_MODE_CALIBRATION) continue;
        
        bool currentPressed = isHotkeyPressed(currentVirtualPinMask, i, true);
        bool lastPressed = isHotkeyPressed(lastVirtualPinMask, i, true);
        
        if (isValidHotkey(i, currentTime, currentPressed, lastPressed)) {
            runAction(hotkeys[i].action);
            break;
        }
    }
}

/**
 * 检查是否按下了对应的热键
 * @param virtualPinMask 虚拟pin掩码
 * @param hotkeyIndex 热键索引
 * @param isOnly 是否判断唯一性，如果为 true，则同时按下其他热键时，返回false，如果为 false，则同时按下其他热键时，返回true
 * @return 是否按下了对应的热键
 */
bool HotkeysManager::isHotkeyPressed(uint32_t virtualPinMask, int hotkeyIndex, bool isOnly) {
    if (hotkeyIndex < 0 || hotkeyIndex >= NUM_GAMEPAD_HOTKEYS) return false;
    if (hotkeys[hotkeyIndex].virtualPin < 0) return false;
    
    // 检查是否同时按下了FN键和对应的热键
    uint32_t expectedMask = (1U << hotkeys[hotkeyIndex].virtualPin) | FN_BUTTON_VIRTUAL_PIN;
    if (isOnly) {
        return (virtualPinMask == expectedMask); // 直接比较是否相等, 而不是按位与。目的：同时刻只有一个hotkey被触发
    } else {
        return (virtualPinMask & expectedMask) == expectedMask; // 按位与，判断是否按下了对应的热键
    }
}

void HotkeysManager::resetHotkeyState(int index) {
    if (index < 0 || index >= NUM_GAMEPAD_HOTKEYS) return;
    
    hotkeyStates[index].isPressed = false;
    hotkeyStates[index].hasTriggered = false;
    hotkeyStates[index].pressStartTime = 0;
}

void HotkeysManager::runAction(GamepadHotkey hotkeyAction) {
    LOG_INFO("HOTKEYS", "runAction: %d", hotkeyAction);
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
         case GamepadHotkey::HOTKEY_AMBIENT_LIGHT_ENABLE_SWITCH:
            LEDS_MANAGER.ambientLightEnableSwitch();
            break;
        case GamepadHotkey::HOTKEY_AMBIENT_LIGHT_EFFECTSTYLE_NEXT:
            LEDS_MANAGER.ambientLightEffectStyleNext();
            break;
        case GamepadHotkey::HOTKEY_AMBIENT_LIGHT_EFFECTSTYLE_PREV:
            LEDS_MANAGER.ambientLightEffectStylePrev();
            break;
        case GamepadHotkey::HOTKEY_AMBIENT_LIGHT_BRIGHTNESS_UP:
            LEDS_MANAGER.ambientLightBrightnessUp();
            break;
        case GamepadHotkey::HOTKEY_AMBIENT_LIGHT_BRIGHTNESS_DOWN:
            LEDS_MANAGER.ambientLightBrightnessDown();
            break;
        case GamepadHotkey::HOTKEY_INPUT_MODE_WEBCONFIG:
            STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_WEB_CONFIG);
            STORAGE_MANAGER.saveConfig();
            rebootSystem();
            break;
        case GamepadHotkey::HOTKEY_INPUT_MODE_CALIBRATION:
            STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_CALIBRATION);
            STORAGE_MANAGER.saveConfig();
            ADC_CALIBRATION_MANAGER.resetAllCalibration();
            rebootSystem();
            break;
        case GamepadHotkey::HOTKEY_INPUT_MODE_XINPUT:
            STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
            STORAGE_MANAGER.setInputMode(InputMode::INPUT_MODE_XINPUT);
            STORAGE_MANAGER.saveConfig();
            rebootSystem();
            break;
        case GamepadHotkey::HOTKEY_INPUT_MODE_PS4:
            STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
            STORAGE_MANAGER.setInputMode(InputMode::INPUT_MODE_PS4);
            STORAGE_MANAGER.saveConfig();
            rebootSystem();
            break;
        case GamepadHotkey::HOTKEY_INPUT_MODE_PS5:
            STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
            STORAGE_MANAGER.setInputMode(InputMode::INPUT_MODE_PS5);
            STORAGE_MANAGER.saveConfig();
            rebootSystem();
            break;
        case GamepadHotkey::HOTKEY_INPUT_MODE_XBONE:
            STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
            STORAGE_MANAGER.setInputMode(InputMode::INPUT_MODE_XBONE);
            STORAGE_MANAGER.saveConfig();
            rebootSystem();
            break;
        case GamepadHotkey::HOTKEY_INPUT_MODE_SWITCH:
            STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
            STORAGE_MANAGER.setInputMode(InputMode::INPUT_MODE_SWITCH);
            STORAGE_MANAGER.saveConfig();
            rebootSystem();
            break;
        case GamepadHotkey::HOTKEY_SYSTEM_REBOOT:
            rebootSystem();
            break;
       
        default:
            break;
    }
}

void HotkeysManager::rebootSystem() {
    WS2812B_Stop();
    NVIC_SystemReset();
}









