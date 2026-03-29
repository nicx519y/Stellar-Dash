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

cJSON* buildScreenControlConfigJSON(Config& config) {
    cJSON* screenControlJSON = cJSON_CreateObject();
    cJSON_AddNumberToObject(screenControlJSON, "brightness", config.screenControl.brightness);
    const char* standbyDisplayStr2 = "none";
    switch (config.screenControl.standbyDisplay) {
        case 1: standbyDisplayStr2 = "backgroundImage"; break;
        case 2: standbyDisplayStr2 = "buttonLayout"; break;
        default: standbyDisplayStr2 = "none"; break;
    }
    cJSON_AddStringToObject(screenControlJSON, "standbyDisplay", standbyDisplayStr2);
    cJSON_AddNumberToObject(screenControlJSON, "backgroundColor", config.screenControl.backgroundColor);
    cJSON_AddNumberToObject(screenControlJSON, "textColor", config.screenControl.textColor);
    cJSON_AddStringToObject(screenControlJSON, "backgroundImageId", config.screenControl.backgroundImageId);
    cJSON_AddNumberToObject(screenControlJSON, "currentPageId", config.screenControl.currentPageId);
    cJSON* featuresJSON = cJSON_CreateObject();
    struct { uint8_t id; const char* key; uint32_t bit; } map[] = {
        {0, "inputModeSwitch", SCREEN_FEATURE_INPUT_MODE_SWITCH},
        {1, "profilesSwitch", SCREEN_FEATURE_PROFILES_SWITCH},
        {2, "socdModeSwitch", SCREEN_FEATURE_SOCD_MODE_SWITCH},
        {3, "tournamentModeSwitch", SCREEN_FEATURE_TOURNAMENT_MODE_SWITCH},
        {4, "ledBrightnessAdjust", SCREEN_FEATURE_LED_BRIGHTNESS_ADJUST},
        {5, "ledEffectSwitch", SCREEN_FEATURE_LED_EFFECT_SWITCH},
        {6, "ambientBrightnessAdjust", SCREEN_FEATURE_AMBIENT_BRIGHTNESS_ADJUST},
        {7, "ambientEffectSwitch", SCREEN_FEATURE_AMBIENT_EFFECT_SWITCH},
        {8, "screenBrightnessAdjust", SCREEN_FEATURE_SCREEN_BRIGHTNESS_ADJUST},
        {9, "webConfigEntry", SCREEN_FEATURE_WEB_CONFIG_ENTRY},
        {10, "calibrationModeSwitch", SCREEN_FEATURE_CALIBRATION_MODE_SWITCH},
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        cJSON_AddBoolToObject(featuresJSON, map[i].key, (config.screenControl.featuresMask & map[i].bit) != 0);
    }
    cJSON_AddItemToObject(screenControlJSON, "features", featuresJSON);

    cJSON* featuresOrderJSON = cJSON_CreateArray();
    for (uint32_t i = 0; i < SCREEN_FEATURE_COUNT; i++) {
        uint8_t id = config.screenControl.featuresOrder[i];
        const char* key = nullptr;
        for (size_t j = 0; j < sizeof(map) / sizeof(map[0]); j++) {
            if (map[j].id == id) {
                key = map[j].key;
                break;
            }
        }
        if (key) {
            cJSON_AddItemToArray(featuresOrderJSON, cJSON_CreateString(key));
        }
    }
    cJSON_AddItemToObject(screenControlJSON, "featuresOrder", featuresOrderJSON);
    return screenControlJSON;
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

    cJSON* screenControlJSON = buildScreenControlConfigJSON(config);
    cJSON_AddItemToObject(exportJSON, "screenControl", screenControlJSON);

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

    cJSON* screenControl = cJSON_GetObjectItem(json, "screenControl");
    if (screenControl && cJSON_IsObject(screenControl)) {
        cJSON* item;
        if ((item = cJSON_GetObjectItem(screenControl, "brightness")) && cJSON_IsNumber(item)) {
            int v = item->valueint;
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            config.screenControl.brightness = (uint8_t)v;
        }
        if ((item = cJSON_GetObjectItem(screenControl, "standbyDisplay")) && cJSON_IsString(item)) {
            if (strcmp(item->valuestring, "backgroundImage") == 0) config.screenControl.standbyDisplay = 1;
            else if (strcmp(item->valuestring, "buttonLayout") == 0) config.screenControl.standbyDisplay = 2;
            else config.screenControl.standbyDisplay = 0;
        }
        if ((item = cJSON_GetObjectItem(screenControl, "backgroundColor")) && cJSON_IsNumber(item)) {
            config.screenControl.backgroundColor = (uint32_t)item->valuedouble;
        }
        if ((item = cJSON_GetObjectItem(screenControl, "textColor")) && cJSON_IsNumber(item)) {
            config.screenControl.textColor = (uint32_t)item->valuedouble;
        }
        if ((item = cJSON_GetObjectItem(screenControl, "backgroundImageId")) && cJSON_IsString(item)) {
            strncpy(config.screenControl.backgroundImageId, item->valuestring, sizeof(config.screenControl.backgroundImageId) - 1);
            config.screenControl.backgroundImageId[sizeof(config.screenControl.backgroundImageId) - 1] = '\0';
        }
        if ((item = cJSON_GetObjectItem(screenControl, "currentPageId")) && cJSON_IsNumber(item)) {
            int v = item->valueint;
            if (v < 0) v = 0;
            if (v > 65535) v = 65535;
            config.screenControl.currentPageId = (uint16_t)v;
        }

        cJSON* features = cJSON_GetObjectItem(screenControl, "features");
        if (features && cJSON_IsObject(features)) {
            struct { const char* key; uint32_t bit; } map[] = {
                {"inputModeSwitch", SCREEN_FEATURE_INPUT_MODE_SWITCH},
                {"profilesSwitch", SCREEN_FEATURE_PROFILES_SWITCH},
                {"socdModeSwitch", SCREEN_FEATURE_SOCD_MODE_SWITCH},
                {"tournamentModeSwitch", SCREEN_FEATURE_TOURNAMENT_MODE_SWITCH},
                {"ledBrightnessAdjust", SCREEN_FEATURE_LED_BRIGHTNESS_ADJUST},
                {"ledEffectSwitch", SCREEN_FEATURE_LED_EFFECT_SWITCH},
                {"ambientBrightnessAdjust", SCREEN_FEATURE_AMBIENT_BRIGHTNESS_ADJUST},
                {"ambientEffectSwitch", SCREEN_FEATURE_AMBIENT_EFFECT_SWITCH},
                {"screenBrightnessAdjust", SCREEN_FEATURE_SCREEN_BRIGHTNESS_ADJUST},
                {"webConfigEntry", SCREEN_FEATURE_WEB_CONFIG_ENTRY},
                {"calibrationModeSwitch", SCREEN_FEATURE_CALIBRATION_MODE_SWITCH},
            };
            for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
                cJSON* b = cJSON_GetObjectItem(features, map[i].key);
                if (b && cJSON_IsBool(b)) {
                    if (cJSON_IsTrue(b)) config.screenControl.featuresMask |= map[i].bit;
                    else config.screenControl.featuresMask &= ~map[i].bit;
                }
            }
        }

        cJSON* featuresOrder = cJSON_GetObjectItem(screenControl, "featuresOrder");
        struct { const char* key; uint8_t id; } orderMap[] = {
            {"inputModeSwitch", 0},
            {"profilesSwitch", 1},
            {"socdModeSwitch", 2},
            {"tournamentModeSwitch", 3},
            {"ledBrightnessAdjust", 4},
            {"ledEffectSwitch", 5},
            {"ambientBrightnessAdjust", 6},
            {"ambientEffectSwitch", 7},
            {"screenBrightnessAdjust", 8},
            {"webConfigEntry", 9},
            {"calibrationModeSwitch", 10},
        };
        if (featuresOrder && cJSON_IsArray(featuresOrder)) {
            bool used[SCREEN_FEATURE_COUNT] = {false};
            uint32_t pos = 0;
            cJSON* it;
            cJSON_ArrayForEach(it, featuresOrder) {
                if (!cJSON_IsString(it)) continue;
                for (size_t j = 0; j < sizeof(orderMap) / sizeof(orderMap[0]); j++) {
                    if (strcmp(it->valuestring, orderMap[j].key) == 0) {
                        uint8_t id = orderMap[j].id;
                        if (id < SCREEN_FEATURE_COUNT && !used[id] && pos < SCREEN_FEATURE_COUNT) {
                            config.screenControl.featuresOrder[pos++] = id;
                            used[id] = true;
                        }
                        break;
                    }
                }
            }
            for (size_t j = 0; j < sizeof(orderMap) / sizeof(orderMap[0]); j++) {
                uint8_t id = orderMap[j].id;
                if (id < SCREEN_FEATURE_COUNT && !used[id] && pos < SCREEN_FEATURE_COUNT) {
                    config.screenControl.featuresOrder[pos++] = id;
                    used[id] = true;
                }
            }
            while (pos < SCREEN_FEATURE_COUNT) {
                config.screenControl.featuresOrder[pos] = (uint8_t)pos;
                pos++;
            }
        } else {
            for (uint32_t i = 0; i < SCREEN_FEATURE_COUNT; i++) {
                config.screenControl.featuresOrder[i] = (uint8_t)i;
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

    memset(profile.keysConfig.macros, 0, sizeof(profile.keysConfig.macros));

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
        memset(config.reserved0, 0, sizeof(config.reserved0));
        config.screenControl.brightness = 100;
        config.screenControl.standbyDisplay = 0;
        memset(config.screenControl.reserved0, 0, sizeof(config.screenControl.reserved0));
        config.screenControl.backgroundColor = 0x000000;
        config.screenControl.textColor = 0xFFFFFF;
        config.screenControl.backgroundImageId[0] = '\0';
        config.screenControl.currentPageId = 0;
        config.screenControl.reserved1 = 0;
        config.screenControl.featuresMask =
            SCREEN_FEATURE_INPUT_MODE_SWITCH |
            SCREEN_FEATURE_PROFILES_SWITCH |
            SCREEN_FEATURE_SOCD_MODE_SWITCH |
            SCREEN_FEATURE_TOURNAMENT_MODE_SWITCH |
            SCREEN_FEATURE_LED_BRIGHTNESS_ADJUST |
            SCREEN_FEATURE_LED_EFFECT_SWITCH |
            SCREEN_FEATURE_AMBIENT_BRIGHTNESS_ADJUST |
            SCREEN_FEATURE_AMBIENT_EFFECT_SWITCH |
            SCREEN_FEATURE_SCREEN_BRIGHTNESS_ADJUST |
            SCREEN_FEATURE_WEB_CONFIG_ENTRY |
            SCREEN_FEATURE_CALIBRATION_MODE_SWITCH;
        for (uint32_t i = 0; i < SCREEN_FEATURE_COUNT; i++) {
            config.screenControl.featuresOrder[i] = (uint8_t)i;
        }
        config.screenControl.reserved2 = 0;

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

static uint32_t align_up_u32(uint32_t v, uint32_t a) {
    return (v + (a - 1)) & ~(a - 1);
}

static int8_t qspi_write_buffer_no_erase(uint8_t* pBuffer, uint32_t writeAddr, uint32_t numBytes) {
    writeAddr &= 0x00FFFFFF;
    int8_t status = QSPI_W25Qxx_OK;
    while (numBytes > 0) {
        uint32_t current_addr = writeAddr;
        uint32_t current_size = W25Qxx_PageSize - (current_addr % W25Qxx_PageSize);
        if (current_size > numBytes) current_size = numBytes;
        status = QSPI_W25Qxx_WritePage(pBuffer, current_addr, (uint16_t)current_size);
        if (status != QSPI_W25Qxx_OK) return status;
        writeAddr += current_size;
        pBuffer += current_size;
        numBytes -= current_size;
    }
    return status;
}

static int8_t qspi_erase_and_write_config(uint8_t* pBuffer, uint32_t addr, uint32_t size) {
    const bool was_mmap = QSPI_W25Qxx_IsMemoryMappedMode();
    if (was_mmap) {
        QSPI_W25Qxx_ExitMemoryMappedMode();
    }

    const uint32_t erase_size = align_up_u32(size, W25Qxx_SECTOR_SIZE);
    int8_t er = QSPI_W25Qxx_BufferErase(addr, erase_size);
    if (er != QSPI_W25Qxx_OK) {
        if (was_mmap) QSPI_W25Qxx_EnterMemoryMappedMode();
        return er;
    }

    int8_t wr = qspi_write_buffer_no_erase(pBuffer, addr, size);
    if (was_mmap) {
        QSPI_W25Qxx_EnterMemoryMappedMode();
    }
    return wr;
}

bool ConfigUtils::save(Config& config)
{
    APP_DBG("ConfigUtils::save begin");

    const uint32_t cfgSize = (uint32_t)sizeof(Config);
    int8_t result = qspi_erase_and_write_config((uint8_t*)&config, CONFIG_ADDR_ORIGIN, cfgSize);
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
