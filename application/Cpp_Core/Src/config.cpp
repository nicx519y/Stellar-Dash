#include "config.hpp"
#include "qspi-w25q64.h"
#include "cJSON.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "board_cfg.h"
#include <map>
#include <string>
#include "configs/websocket_command_handler.hpp" // For ProfileCommandHandler
#include "system_logger.h"

#define CONFIG_ADDR_ORIGIN  CONFIG_ADDR

// ============================================================================
// ConfigUtils Mappings
// ============================================================================

namespace ConfigUtils {

static const std::map<InputMode, const char*> INPUT_MODE_STRINGS = {
    {InputMode::INPUT_MODE_XINPUT, "XINPUT"},
    {InputMode::INPUT_MODE_PS4, "PS4"},
    {InputMode::INPUT_MODE_PS5, "PS5"},
    {InputMode::INPUT_MODE_XBOX, "XBOX"},
    {InputMode::INPUT_MODE_SWITCH, "SWITCH"}
};

static const std::map<std::string, InputMode> STRING_TO_INPUT_MODE = [](){
    std::map<std::string, InputMode> reverse_map;
    for(const auto& pair : INPUT_MODE_STRINGS) {
        reverse_map[pair.second] = pair.first;
    }
    return reverse_map;
}();

static const std::map<std::string, GamepadHotkey> STRING_TO_GAMEPAD_HOTKEY = {
    {"WebConfigMode", GamepadHotkey::HOTKEY_INPUT_MODE_WEBCONFIG},
    {"NSwitchMode", GamepadHotkey::HOTKEY_INPUT_MODE_SWITCH},
    {"XInputMode", GamepadHotkey::HOTKEY_INPUT_MODE_XINPUT},
    {"PS4Mode", GamepadHotkey::HOTKEY_INPUT_MODE_PS4},
    {"PS5Mode", GamepadHotkey::HOTKEY_INPUT_MODE_PS5},
    {"XBoxMode", GamepadHotkey::HOTKEY_INPUT_MODE_XBOX},
    {"LedsEffectStyleNext", GamepadHotkey::HOTKEY_LEDS_EFFECTSTYLE_NEXT},
    {"LedsEffectStylePrev", GamepadHotkey::HOTKEY_LEDS_EFFECTSTYLE_PREV},
    {"LedsBrightnessUp", GamepadHotkey::HOTKEY_LEDS_BRIGHTNESS_UP},
    {"LedsBrightnessDown", GamepadHotkey::HOTKEY_LEDS_BRIGHTNESS_DOWN},
    {"LedsEnableSwitch", GamepadHotkey::HOTKEY_LEDS_ENABLE_SWITCH},
    {"AmbientLightEffectStyleNext", GamepadHotkey::HOTKEY_AMBIENT_LIGHT_EFFECTSTYLE_NEXT},
    {"AmbientLightEffectStylePrev", GamepadHotkey::HOTKEY_AMBIENT_LIGHT_EFFECTSTYLE_PREV},
    {"AmbientLightBrightnessUp", GamepadHotkey::HOTKEY_AMBIENT_LIGHT_BRIGHTNESS_UP},
    {"AmbientLightBrightnessDown", GamepadHotkey::HOTKEY_AMBIENT_LIGHT_BRIGHTNESS_DOWN},
    {"AmbientLightEnableSwitch", GamepadHotkey::HOTKEY_AMBIENT_LIGHT_ENABLE_SWITCH},
    {"CalibrationMode", GamepadHotkey::HOTKEY_INPUT_MODE_CALIBRATION},
    {"SystemReboot", GamepadHotkey::HOTKEY_SYSTEM_REBOOT}
};

static const std::map<GamepadHotkey, const char*> GAMEPAD_HOTKEY_TO_STRING = [](){
    std::map<GamepadHotkey, const char*> reverse_map;
    for(const auto& pair : STRING_TO_GAMEPAD_HOTKEY) {
        reverse_map[pair.second] = pair.first.c_str();
    }
    return reverse_map;
}();

const char* getInputModeString(InputMode mode) {
    auto it = INPUT_MODE_STRINGS.find(mode);
    if (it != INPUT_MODE_STRINGS.end()) {
        return it->second;
    }
    return "XINPUT"; // Default
}

InputMode getInputModeFromString(const char* str) {
    if (!str) return InputMode::INPUT_MODE_XINPUT;
    auto it = STRING_TO_INPUT_MODE.find(str);
    if (it != STRING_TO_INPUT_MODE.end()) {
        return it->second;
    }
    return InputMode::INPUT_MODE_XINPUT;
}

const char* getGamepadHotkeyString(GamepadHotkey action) {
    auto it = GAMEPAD_HOTKEY_TO_STRING.find(action);
    if (it != GAMEPAD_HOTKEY_TO_STRING.end()) {
        return it->second;
    }
    return "None";
}

GamepadHotkey getGamepadHotkeyFromString(const char* str) {
    if (!str) return GamepadHotkey::HOTKEY_NONE;
    auto it = STRING_TO_GAMEPAD_HOTKEY.find(str);
    if (it != STRING_TO_GAMEPAD_HOTKEY.end()) {
        return it->second;
    }
    return GamepadHotkey::HOTKEY_NONE;
}

cJSON* buildHotkeysConfigJSON(Config& config) {
    cJSON* hotkeysConfigJSON = cJSON_CreateArray();

    for(uint8_t i = 0; i < NUM_GAMEPAD_HOTKEYS; i++) {
        cJSON* hotkeyJSON = cJSON_CreateObject();
        
        // 添加快捷键动作(转换为字符串)
        cJSON_AddStringToObject(hotkeyJSON, "action", getGamepadHotkeyString(config.hotkeys[i].action));

        // 添加快捷键序号
        cJSON_AddNumberToObject(hotkeyJSON, "key", config.hotkeys[i].virtualPin);

        // 添加是否长按
        cJSON_AddBoolToObject(hotkeyJSON, "isHold", config.hotkeys[i].isHold);

        // 添加锁定状态
        cJSON_AddBoolToObject(hotkeyJSON, "isLocked", config.hotkeys[i].isLocked);
        
        // 添加到组
        cJSON_AddItemToArray(hotkeysConfigJSON, hotkeyJSON);
    }

    return hotkeysConfigJSON;
}

cJSON* toJSON(Config& config) {
    cJSON* exportJSON = cJSON_CreateObject();

    // 1. 全局配置
    cJSON* globalConfigJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(globalConfigJSON, "inputMode", getInputModeString(config.inputMode));
    cJSON_AddStringToObject(globalConfigJSON, "defaultProfileId", config.defaultProfileId);
    
    cJSON_AddItemToObject(exportJSON, "globalConfig", globalConfigJSON);

    // 2. 快捷键配置
    cJSON* hotkeysConfigJSON = buildHotkeysConfigJSON(config);
    cJSON_AddItemToObject(exportJSON, "hotkeysConfig", hotkeysConfigJSON);

    // 3. 所有配置文件
    cJSON* profilesJSON = cJSON_CreateArray();
    for (int i = 0; i < NUM_PROFILES; i++) {
        if (config.profiles[i].enabled) {
            cJSON* profileJSON = ProfileCommandHandler::buildProfileJSON(&config.profiles[i]);
            if (profileJSON) {
                cJSON_AddItemToArray(profilesJSON, profileJSON);
            }
        }
    }
    cJSON_AddItemToObject(exportJSON, "profiles", profilesJSON);

    return exportJSON;
}

bool fromJSON(Config& config, cJSON* json) {
    if (!json) return false;

    // 1. 全局配置
    cJSON* globalConfig = cJSON_GetObjectItem(json, "globalConfig");
    if (globalConfig) {
        cJSON* inputModeItem = cJSON_GetObjectItem(globalConfig, "inputMode");
        if (inputModeItem && cJSON_IsString(inputModeItem)) {
            config.inputMode = getInputModeFromString(inputModeItem->valuestring);
        }

        cJSON* defaultProfileId = cJSON_GetObjectItem(globalConfig, "defaultProfileId");
        if (defaultProfileId && cJSON_IsString(defaultProfileId)) {
            if (strlen(defaultProfileId->valuestring) < sizeof(config.defaultProfileId)) { 
                 strncpy(config.defaultProfileId, defaultProfileId->valuestring, sizeof(config.defaultProfileId) - 1);
                 config.defaultProfileId[sizeof(config.defaultProfileId) - 1] = '\0';
            } else {
                APP_DBG("ConfigUtils::fromJSON - defaultProfileId too long");
            }
        }
    }

    // 2. 快捷键配置
    cJSON* hotkeysConfig = cJSON_GetObjectItem(json, "hotkeysConfig");
    if (hotkeysConfig && cJSON_IsArray(hotkeysConfig)) {
        int numHotkeys = cJSON_GetArraySize(hotkeysConfig);
        for (int i = 0; i < numHotkeys && i < NUM_GAMEPAD_HOTKEYS; i++) {
            cJSON* hotkeyItem = cJSON_GetArrayItem(hotkeysConfig, i);
            if (!hotkeyItem || !cJSON_IsObject(hotkeyItem)) continue;

            cJSON* keyItem = cJSON_GetObjectItem(hotkeyItem, "key");
            if (keyItem && cJSON_IsNumber(keyItem)) {
                int keyIndex = keyItem->valueint;
                if (keyIndex >= -1 && keyIndex < (NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS)) {
                     config.hotkeys[i].virtualPin = keyIndex;
                }
            }

            cJSON* actionItem = cJSON_GetObjectItem(hotkeyItem, "action");
            if (actionItem && cJSON_IsString(actionItem)) {
                config.hotkeys[i].action = getGamepadHotkeyFromString(actionItem->valuestring);
            }
            
            cJSON* isHoldItem = cJSON_GetObjectItem(hotkeyItem, "isHold");
            if (isHoldItem && cJSON_IsBool(isHoldItem)) {
                config.hotkeys[i].isHold = cJSON_IsTrue(isHoldItem);
            }

            // 锁定状态不能修改
            // cJSON* isLockedItem = cJSON_GetObjectItem(hotkeyItem, "isLocked");
            // if (isLockedItem) {
            //     config.hotkeys[i].isLocked = cJSON_IsTrue(isLockedItem);
            // }
        }
    }

    // 3. Profiles
    cJSON* profiles = cJSON_GetObjectItem(json, "profiles");
    if (profiles && cJSON_IsArray(profiles)) {
        cJSON* profileItem;
        cJSON_ArrayForEach(profileItem, profiles) {
            if (!cJSON_IsObject(profileItem)) continue;

            cJSON* idItem = cJSON_GetObjectItem(profileItem, "id");
            if (idItem && cJSON_IsString(idItem)) {
                 if (strlen(idItem->valuestring) >= sizeof(config.profiles[0].id)) {
                     APP_DBG("ConfigUtils::fromJSON - profile id too long: %s", idItem->valuestring);
                     continue;
                 }

                 // Find profile by ID
                 bool profileFound = false;
                 for (int i=0; i < NUM_PROFILES; i++) {
                     if (strncmp(config.profiles[i].id, idItem->valuestring, sizeof(config.profiles[i].id)) == 0) {
                         ProfileCommandHandler::parseProfileJSON(profileItem, &config.profiles[i]);
                         config.profiles[i].enabled = true;
                         profileFound = true;
                         break;
                     }
                 }
                
                // 不接受自定义 profile-id，也就是说 不在已经定义的profile-id范围内的配置，不会被导入
                //  if (!profileFound) {
                //      // Try to add new profile
                //      for (int i=0; i < NUM_PROFILES; i++) {
                //          if (!config.profiles[i].enabled) {
                //              APP_DBG("ConfigUtils::fromJSON - creating new profile: %s", idItem->valuestring);
                //              ConfigUtils::makeDefaultProfile(config.profiles[i], idItem->valuestring, true);
                //              ProfileCommandHandler::parseProfileJSON(profileItem, &config.profiles[i]);
                //              break;
                //          }
                //      }
                //  }
            }
        }
    }

    return true;
}

} // namespace ConfigUtils

void ConfigUtils::makeDefaultProfile(GamepadProfile& profile, const char* id, bool isEnabled)
{
    // 设置profile id, name, enabled
    sprintf(profile.id, id);
    sprintf(profile.name, "Profile-1");
    profile.enabled = isEnabled;
    
    APP_DBG("ConfigUtils::makeDefaultProfile - base init done");

    // 设置keysConfig
    profile.keysConfig.socdMode = SOCDMode::SOCD_MODE_NEUTRAL;
    profile.keysConfig.fourWayMode = false;
    profile.keysConfig.invertXAxis = false;
    profile.keysConfig.invertYAxis = false;
    memset(profile.keysConfig.keysEnableTag, true, NUM_ADC_BUTTONS); // 默认启用所有按钮

    APP_DBG("ConfigUtils::makeDefaultProfile - keysConfig base init done");

    
    // 默认映射 - 将 std::map 操作改为数组操作
    memset(profile.keysConfig.keyMapping, 0, sizeof(profile.keysConfig.keyMapping)); // 先清零所有映射

    // 设置默认映射
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_DPAD_UP] = (1 << 1) | (1 << 8);
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_DPAD_DOWN] = 1 << 6;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_DPAD_LEFT] = 1 << 5;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_DPAD_RIGHT] = 1 << 7;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_B1] = 1 << 9;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_B2] = 1 << 11;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_B3] = 1 << 10;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_B4] = 1 << 12;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_L1] = 1 << 14;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_R1] = 1 << 16;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_L2] = 1 << 13;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_R2] = 1 << 15;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_S1] = 1 << 18;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_S2] = 1 << 17;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_L3] = 1 << 0;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_R3] = 1 << 2;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_A1] = 1 << 19;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_A2] = 0;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_FN] = FN_BUTTON_VIRTUAL_PIN;

    APP_DBG("ConfigUtils::makeDefaultProfile - keyMapping init done");

    memset(profile.keysConfig.keyCombinations, 0, sizeof(profile.keysConfig.keyCombinations)); // 默认清空所有按键组合键

    APP_DBG("ConfigUtils::makeDefaultProfile - keyCombinations init done");

    // 设置triggerConfigs 
    profile.triggerConfigs.isAllBtnsConfiguring = true;
    // 设置防抖算法 无
    profile.triggerConfigs.debounceAlgorithm = ADCButtonDebounceAlgorithm::NONE;

    for(uint8_t l = 0; l < NUM_ADC_BUTTONS; l++) {
        profile.triggerConfigs.triggerConfigs[l] = {
            .virtualPin = l,
            .pressAccuracy = 0.1f,
            .releaseAccuracy = 0.1f,
            .topDeadzone = 0.3f,
            .bottomDeadzone = 0.3f
        };
    }

    APP_DBG("ConfigUtils::makeDefaultProfile - triggerConfigs init done");

    // 设置ledProfile
    profile.ledsConfigs.ledEnabled = false;
    profile.ledsConfigs.ledEffect = LEDEffect::STATIC;
    profile.ledsConfigs.ledColor1 = 0x00ff00;
    profile.ledsConfigs.ledColor2 = 0x0000ff;
    profile.ledsConfigs.ledColor3 = 0x000000;
    profile.ledsConfigs.ledBrightness = 50;
    profile.ledsConfigs.ledAnimationSpeed = 3;
    
    // 设置环绕灯配置
    profile.ledsConfigs.aroundLedEnabled = false;
    profile.ledsConfigs.aroundLedSyncToMainLed = true;
    profile.ledsConfigs.aroundLedTriggerByButton = false;
    profile.ledsConfigs.aroundLedEffect = AroundLEDEffect::AROUND_STATIC;
    profile.ledsConfigs.aroundLedColor1 = 0xff0000;  // 红色
    profile.ledsConfigs.aroundLedColor2 = 0x00ff00;  // 绿色
    profile.ledsConfigs.aroundLedColor3 = 0x0000ff;  // 蓝色
    profile.ledsConfigs.aroundLedBrightness = 50;
    profile.ledsConfigs.aroundLedAnimationSpeed = 3;

    APP_DBG("ConfigUtils::makeDefaultProfile - ledsConfigs init done");
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

        APP_DBG("ConfigUtils::load - base config init done");

        // 设置profiles
        for(uint8_t k = 0; k < NUM_PROFILES; k++) {
            // 设置profile id, name, enabled
            char profileId[16];
            sprintf(profileId, "profile-%d", k);
            APP_DBG("ConfigUtils::load - make default profile %d id: %s", k, profileId);
            ConfigUtils::makeDefaultProfile(config.profiles[k], profileId, k == 0);
            APP_DBG("ConfigUtils::load - make profile %d init done", k);
        }

        APP_DBG("ConfigUtils::load - profiles init done");

        // 设置hotkeys 默认快捷键
        for(uint8_t m = 0; m < NUM_GAMEPAD_HOTKEYS; m++) {
            if(m < sizeof(DEFAULT_HOTKEY_LIST) / sizeof(DefaultHotkeyConfig)) {
                config.hotkeys[m].isLocked = DEFAULT_HOTKEY_LIST[m].isLocked;
                config.hotkeys[m].action = DEFAULT_HOTKEY_LIST[m].action;
                config.hotkeys[m].isHold = DEFAULT_HOTKEY_LIST[m].isHold;
                config.hotkeys[m].virtualPin = DEFAULT_HOTKEY_LIST[m].virtualPin;
            } else {
                config.hotkeys[m].isLocked = false;
                config.hotkeys[m].action = GamepadHotkey::HOTKEY_NONE;
                config.hotkeys[m].virtualPin = -1;
                config.hotkeys[m].isHold = false;
            }
        }

        APP_DBG("ConfigUtils::load - success.");

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
