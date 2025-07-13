#include "config.hpp"
#include "qspi-w25q64.h"
#include "cJSON.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "board_cfg.h"

#define CONFIG_ADDR_ORIGIN  CONFIG_ADDR

void ConfigUtils::makeDefaultProfile(GamepadProfile& profile, const char* id, bool isEnabled)
{
    // 设置profile id, name, enabled
    sprintf(profile.id, id);
    sprintf(profile.name, "Profile XInput");
    profile.enabled = isEnabled;

    // 设置keysConfig
    profile.keysConfig.socdMode = SOCDMode::SOCD_MODE_NEUTRAL;
    profile.keysConfig.fourWayMode = false;
    profile.keysConfig.invertXAxis = false;
    profile.keysConfig.invertYAxis = false;
    memset(profile.keysConfig.keysEnableTag, true, NUM_ADC_BUTTONS); // 默认启用所有按钮
    profile.keysConfig.keyDpadUp = (1 << 0) | (1 << 7);
    profile.keysConfig.keyDpadDown = 1 << 4;
    profile.keysConfig.keyDpadLeft = 1 << 5;
    profile.keysConfig.keyDpadRight = 1 << 6;
    profile.keysConfig.keyButtonB1 = 1 << 8;
    profile.keysConfig.keyButtonB2 = 1 << 11;
    profile.keysConfig.keyButtonB3 = 1 << 9;
    profile.keysConfig.keyButtonB4 = 1 << 12;
    profile.keysConfig.keyButtonL1 = 1 << 14;
    profile.keysConfig.keyButtonL2 = 1 << 13;
    profile.keysConfig.keyButtonR1 = 1 << 16;
    profile.keysConfig.keyButtonR2 = 1 << 15;
    profile.keysConfig.keyButtonL3 = 1 << 1;
    profile.keysConfig.keyButtonR3 = 1 << 2;
    profile.keysConfig.keyButtonA1 = 1 << 19;
    profile.keysConfig.keyButtonA2 = 1 << 10;
    profile.keysConfig.keyButtonS1 = 1 << 17;
    profile.keysConfig.keyButtonS2 = 1 << 18;
    profile.keysConfig.keyButtonFn = FN_BUTTON_VIRTUAL_PIN;

    // 设置triggerConfigs 
    profile.triggerConfigs.isAllBtnsConfiguring = true;
    // 设置防抖算法 无
    profile.triggerConfigs.debounceAlgorithm = ADCButtonDebounceAlgorithm::NONE;

    for(uint8_t l = 0; l < NUM_ADC_BUTTONS; l++) {
        profile.triggerConfigs.triggerConfigs[l] = {
            .virtualPin = l,
            .pressAccuracy = 0.1f,
            .releaseAccuracy = 0.1f,
            .topDeadzone = 0.2f,
            .bottomDeadzone = 0.2f
        };
    }

    // 设置ledProfile
    profile.ledsConfigs.ledEnabled = false;
    profile.ledsConfigs.ledEffect = LEDEffect::STATIC;
    profile.ledsConfigs.ledColor1 = 0x00ff00;
    profile.ledsConfigs.ledColor2 = 0x0000ff;
    profile.ledsConfigs.ledColor3 = 0x000000;
    profile.ledsConfigs.ledBrightness = 100;
    profile.ledsConfigs.ledAnimationSpeed = 3;
    
    // 设置环绕灯配置
    profile.ledsConfigs.aroundLedEnabled = false;
    profile.ledsConfigs.aroundLedSyncToMainLed = true;
    profile.ledsConfigs.aroundLedTriggerByButton = false;
    profile.ledsConfigs.aroundLedEffect = AroundLEDEffect::AROUND_STATIC;
    profile.ledsConfigs.aroundLedColor1 = 0xff0000;  // 红色
    profile.ledsConfigs.aroundLedColor2 = 0x00ff00;  // 绿色
    profile.ledsConfigs.aroundLedColor3 = 0x0000ff;  // 蓝色
    profile.ledsConfigs.aroundLedBrightness = 100;
    profile.ledsConfigs.aroundLedAnimationSpeed = 3;
}

bool ConfigUtils::load(Config& config)
{
    bool fjResult;
    fjResult = fromStorage(config);

    if(fjResult == true && config.version == CONFIG_VERSION) { // 版本号一致
        uint32_t ver = config.version;
        APP_DBG("Config Version: %d.%d.%d", (ver>>16) & 0xff, (ver>>8) & 0xff, ver & 0xff);
        return true;
    } else {

        APP_DBG("init config, version: %d.%d.%d", (CONFIG_VERSION>>16) & 0xff, (CONFIG_VERSION>>8) & 0xff, CONFIG_VERSION & 0xff);
        // 设置基础配置
        config.version = CONFIG_VERSION;
        config.bootMode = BOOT_MODE_WEB_CONFIG;
        config.inputMode = InputMode::INPUT_MODE_XINPUT;
        strcpy(config.defaultProfileId, "profile-0");
        config.numProfilesMax = NUM_PROFILES;
        config.autoCalibrationEnabled = false; // 默认关闭自动校准

        // 设置profiles
        for(uint8_t k = 0; k < NUM_PROFILES; k++) {
            // 设置profile id, name, enabled
            char profileId[16];
            sprintf(profileId, "profile-%d", k);
            ConfigUtils::makeDefaultProfile(config.profiles[k], profileId, k == 0);
        }

        // 设置hotkeys 默认快捷键
        for(uint8_t m = 0; m < NUM_GAMEPAD_HOTKEYS; m++) {
            if(m == 0) { // 第一个快捷键锁定, 锁定为webconfig
                config.hotkeys[m].isLocked = true;
                config.hotkeys[m].action = GamepadHotkey::HOTKEY_INPUT_MODE_WEBCONFIG;
                config.hotkeys[m].isHold = true;
                config.hotkeys[m].virtualPin = 19;
            } else if(m == 1) { // 第二个快捷键锁定, 锁定为校准模式
                config.hotkeys[m].isLocked = true;
                config.hotkeys[m].action = GamepadHotkey::HOTKEY_INPUT_MODE_CALIBRATION;
                config.hotkeys[m].isHold = true;
                config.hotkeys[m].virtualPin = 18;
            } else if(m == 2) {
                config.hotkeys[m].isLocked = false;
                config.hotkeys[m].action = GamepadHotkey::HOTKEY_LEDS_EFFECTSTYLE_NEXT;
                config.hotkeys[m].isHold = false;
                config.hotkeys[m].virtualPin = 16;
            } else if(m == 3) {
                config.hotkeys[m].isLocked = false;
                config.hotkeys[m].action = GamepadHotkey::HOTKEY_LEDS_EFFECTSTYLE_PREV;
                config.hotkeys[m].isHold = false;
                config.hotkeys[m].virtualPin = 15;
            } else if(m == 4) {
                config.hotkeys[m].isLocked = false;
                config.hotkeys[m].action = GamepadHotkey::HOTKEY_LEDS_BRIGHTNESS_UP;
                config.hotkeys[m].isHold = false;
                config.hotkeys[m].virtualPin = 14;
            } else if(m == 5) {
                config.hotkeys[m].isLocked = false;
                config.hotkeys[m].action = GamepadHotkey::HOTKEY_LEDS_BRIGHTNESS_DOWN;
                config.hotkeys[m].isHold = false;
                config.hotkeys[m].virtualPin = 13;
            } else if(m == 6) {
                config.hotkeys[m].isLocked = false;
                config.hotkeys[m].action = GamepadHotkey::HOTKEY_AMBIENT_LIGHT_EFFECTSTYLE_NEXT;
                config.hotkeys[m].isHold = false;
                config.hotkeys[m].virtualPin = 12;
            } else if(m == 7) {
                config.hotkeys[m].isLocked = false;
                config.hotkeys[m].action = GamepadHotkey::HOTKEY_AMBIENT_LIGHT_EFFECTSTYLE_PREV;
                config.hotkeys[m].isHold = false;
                config.hotkeys[m].virtualPin = 11;
            } else if(m == 8) {
                config.hotkeys[m].isLocked = false;
                config.hotkeys[m].action = GamepadHotkey::HOTKEY_AMBIENT_LIGHT_BRIGHTNESS_UP;
                config.hotkeys[m].isHold = false;
                config.hotkeys[m].virtualPin = 9;
            } else if(m == 9) {
                config.hotkeys[m].isLocked = false;
                config.hotkeys[m].action = GamepadHotkey::HOTKEY_AMBIENT_LIGHT_BRIGHTNESS_DOWN;
                config.hotkeys[m].isHold = false;
                config.hotkeys[m].virtualPin = 8;
            } else if(m == 10) {
                config.hotkeys[m].isLocked = false;
                config.hotkeys[m].action = GamepadHotkey::HOTKEY_LEDS_ENABLE_SWITCH;
                config.hotkeys[m].isHold = true;
                config.hotkeys[m].virtualPin = 0;
            } else { // 其他快捷键不锁定
                config.hotkeys[m].isLocked = false;
                config.hotkeys[m].action = GamepadHotkey::HOTKEY_NONE;
                config.hotkeys[m].virtualPin = -1;
                config.hotkeys[m].isHold = false;
            }
        }


        return save(config);
    } 
}

bool ConfigUtils::save(Config& config)
{
    APP_DBG("ConfigUtils::save begin");


    // 写入配置数据
    int8_t result = QSPI_W25Qxx_WriteBuffer_WithXIPOrNot((uint8_t*)&config, CONFIG_ADDR_ORIGIN, sizeof(Config));
    if(result == QSPI_W25Qxx_OK) {
        APP_DBG("ConfigUtils::save - success.");
        return true;
    } else {
        APP_ERR("ConfigUtils::save - Write failure.");
        return false;
    }
}

/**
 * @brief 重置配置
 * 
 * @param config 
 * @return true 
 * @return false 
 */
bool ConfigUtils::reset(Config& config)
{
    int8_t result = QSPI_W25Qxx_BufferErase(CONFIG_ADDR_ORIGIN, 64*1024);
    if(result != QSPI_W25Qxx_OK) {
        APP_ERR("ConfigUtils::reset - block erase failure.");
        return false;
    }
    return ConfigUtils::load(config);
}

/**
 * @brief 从存储中读取配置
 * 
 * @param config 
 * @return true 
 * @return false 
 */
bool ConfigUtils::fromStorage(Config& config)
{
    int8_t result;
    APP_DBG("ConfigUtils::fromStorage begin. CONFIG_ADDR_ORIGIN: %p", (void*)CONFIG_ADDR_ORIGIN);
    
    result = QSPI_W25Qxx_ReadBuffer_WithXIPOrNot((uint8_t*)&config, CONFIG_ADDR_ORIGIN, sizeof(Config));
    if(result == QSPI_W25Qxx_OK) {
        APP_DBG("ConfigUtils::fromStorage - success.");
        return true;
    } else {
        APP_ERR("ConfigUtils::fromStorage - Read failure.");
        return false;
    }
}