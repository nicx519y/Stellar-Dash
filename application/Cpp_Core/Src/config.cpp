#include "config.hpp"
#include "qspi-w25q64.h"
#include "cJSON.h"
#include "utils.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "board_cfg.h"
#include <map>
#include <string>
#include "configs/websocket_command_handler.hpp" // For ProfileCommandHandler
#include "system_logger.h"

#define CONFIG_ADDR_ORIGIN  CONFIG_ADDR
#define CONFIG_VERSION_SCREEN_STYLE_MIGRATE_FROM 0x00001Bu
#define CONFIG_VERSION_POWER_MIGRATE_FROM 0x00001Cu
#define CONFIG_VERSION_LATEST_PCB_MIGRATE_FROM 0x00001Du
#define DEFAULT_POWER_WAKE_HOLD_MS 3000u
#define DEFAULT_POWER_AUTO_STANDBY_MS 300000u
#define LATEST_PCB_BATTERY_PACK_COUNT 1u
#define LATEST_PCB_KEY_LED_COUNT ((uint8_t)(NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS))
#define LATEST_PCB_AMBIENT_LED_COUNT ((uint8_t)NUM_LED_AROUND)

/*
 * Power-loss-safe configuration journal.
 *
 * APP_CONFIG and the currently unused LOG_STORAGE regions form two independent
 * 64 KiB banks. A bank only becomes visible after the payload write completes
 * and the commit word is programmed in a separate final page-program
 * operation. Until then the previously committed bank remains authoritative.
 *
 * The payload deliberately remains the native Config image so existing config
 * version migrations still apply after the journal layer selects a bank.
 */
#define CONFIG_BANK_A_ADDR           CONFIG_ADDR_ORIGIN
#define CONFIG_BANK_B_ADDR           LOG_STORAGE_ADDR
#define CONFIG_BANK_SIZE             (64u * 1024u)
#define CONFIG_JOURNAL_MAGIC         0x47464348u /* "HCFG", little endian */
#define CONFIG_JOURNAL_VERSION       1u
#define CONFIG_JOURNAL_COMMIT        0x54494D43u /* "CMIT", little endian */
#define CONFIG_JOURNAL_ERASED_WORD   0xFFFFFFFFu

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint16_t formatVersion;
    uint16_t headerSize;
    uint64_t generation;
    uint32_t payloadLength;
    uint8_t reservedDigest[32];
    uint8_t reserved[8];
    uint32_t commit;
} ConfigJournalHeader;
#pragma pack(pop)

static_assert(sizeof(ConfigJournalHeader) == 64u,
              "Config journal header must remain one fixed 64-byte record");
static_assert(offsetof(ConfigJournalHeader, commit) == 60u,
              "Commit word must be programmed separately at offset 60");
static_assert(sizeof(Config) + sizeof(ConfigJournalHeader) <= CONFIG_BANK_SIZE,
              "Config payload must fit in one journal bank");

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

static const std::map<ConnectionMode, const char*> CONNECTION_MODE_STRINGS = {
    {ConnectionMode::CONNECTION_MODE_USB, "USB"},
    {ConnectionMode::CONNECTION_MODE_RF24G, "RF24G"},
};

static const std::map<std::string, ConnectionMode> STRING_TO_CONNECTION_MODE = [](){
    std::map<std::string, ConnectionMode> reverse_map;
    for(const auto& pair : CONNECTION_MODE_STRINGS) {
        reverse_map[pair.second] = pair.first;
    }
    return reverse_map;
}();

static const std::map<WirelessReportRate, const char*> WIRELESS_RATE_STRINGS = {
    {WirelessReportRate::RFM_RATE_1K, "1K"},
    {WirelessReportRate::RFM_RATE_2K, "2K"},
    {WirelessReportRate::RFM_RATE_4K, "4K"},
    {WirelessReportRate::RFM_RATE_8K, "8K"},
};

static const std::map<std::string, WirelessReportRate> STRING_TO_WIRELESS_RATE = [](){
    std::map<std::string, WirelessReportRate> reverse_map;
    for(const auto& pair : WIRELESS_RATE_STRINGS) {
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

const char* getConnectionModeString(ConnectionMode mode) {
    auto it = CONNECTION_MODE_STRINGS.find(mode);
    if (it != CONNECTION_MODE_STRINGS.end()) {
        return it->second;
    }
    return "USB";
}

ConnectionMode getConnectionModeFromString(const char* str) {
    if (!str) return ConnectionMode::CONNECTION_MODE_USB;
    auto it = STRING_TO_CONNECTION_MODE.find(str);
    if (it != STRING_TO_CONNECTION_MODE.end()) {
        return it->second;
    }
    return ConnectionMode::CONNECTION_MODE_USB;
}

const char* getWirelessReportRateString(WirelessReportRate rate) {
    auto it = WIRELESS_RATE_STRINGS.find(rate);
    if (it != WIRELESS_RATE_STRINGS.end()) {
        return it->second;
    }
    return "1K";
}

WirelessReportRate getWirelessReportRateFromString(const char* str) {
    if (!str) return WirelessReportRate::RFM_RATE_1K;
    auto it = STRING_TO_WIRELESS_RATE.find(str);
    if (it != STRING_TO_WIRELESS_RATE.end()) {
        return it->second;
    }
    return WirelessReportRate::RFM_RATE_1K;
}

uint16_t getWirelessReportRateHz(WirelessReportRate rate) {
    return static_cast<uint16_t>(rate);
}

const char* getScreenStyleString(uint8_t style) {
    return (style == SCREEN_STYLE_LIGHT) ? "light" : "dark";
}

uint8_t getScreenStyleFromString(const char* str) {
    if (!str) return SCREEN_STYLE_DARK;
    if (strcmp(str, "light") == 0) return SCREEN_STYLE_LIGHT;
    return SCREEN_STYLE_DARK;
}

static uint32_t color_luma(uint32_t rgb) {
    uint32_t r = (rgb >> 16) & 0xFFu;
    uint32_t g = (rgb >> 8) & 0xFFu;
    uint32_t b = rgb & 0xFFu;
    return r * 299u + g * 587u + b * 114u;
}

static uint8_t infer_screen_style_from_colors(uint32_t bg, uint32_t fg) {
    return (color_luma(bg) > color_luma(fg)) ? SCREEN_STYLE_LIGHT : SCREEN_STYLE_DARK;
}

static uint32_t read_legacy_screen_bg(const ScreenControlConfig& sc) {
    return ((uint32_t)sc.screenStyle) |
           ((uint32_t)sc.reservedStyle[0] << 8) |
           ((uint32_t)sc.reservedStyle[1] << 16) |
           ((uint32_t)sc.reservedStyle[2] << 24);
}

static uint32_t read_legacy_screen_fg(const ScreenControlConfig& sc) {
    return ((uint32_t)sc.reservedStyle[3]) |
           ((uint32_t)sc.reservedStyle[4] << 8) |
           ((uint32_t)sc.reservedStyle[5] << 16) |
           ((uint32_t)sc.reservedStyle[6] << 24);
}

static void sanitize_screen_style(ScreenControlConfig& sc) {
    if (sc.screenStyle != SCREEN_STYLE_LIGHT) {
        sc.screenStyle = SCREEN_STYLE_DARK;
    }
    memset(sc.reservedStyle, 0, sizeof(sc.reservedStyle));
}

static void sanitize_screen_recovery_entry(ScreenControlConfig& sc) {
    static const uint8_t requiredOrder[SCREEN_FEATURE_COUNT] = {
        3, 0, 1, 2, 11, 4, 5, 6, 7, 8, 9, 10
    };
    uint8_t normalized[SCREEN_FEATURE_COUNT] = {0};
    bool seen[SCREEN_FEATURE_COUNT] = {false};
    uint8_t count = 0u;

    sc.featuresMask |= SCREEN_FEATURE_WEB_CONFIG_ENTRY;
    for (uint8_t i = 0u; i < SCREEN_FEATURE_COUNT; ++i) {
        const uint8_t id = sc.featuresOrder[i];
        if (id < SCREEN_FEATURE_COUNT && !seen[id]) {
            normalized[count++] = id;
            seen[id] = true;
        }
    }
    for (uint8_t i = 0u; i < SCREEN_FEATURE_COUNT; ++i) {
        const uint8_t id = requiredOrder[i];
        if (!seen[id]) {
            normalized[count++] = id;
            seen[id] = true;
        }
    }
    memcpy(sc.featuresOrder, normalized, sizeof(sc.featuresOrder));
}

static void sanitize_screen_service_flags(ScreenControlConfig& sc) {
    constexpr uint8_t allowed =
        SCREEN_SERVICE_CH585_MANUAL_ISP_ACTIVE |
        SCREEN_SERVICE_CH585_IAP_CONFIRMED;
    sc.serviceFlags &= allowed;
}

static uint32_t clamp_power_wake_hold_ms(uint32_t value) {
    if (value < 1000u) return 1000u;
    if (value > 5000u) return 5000u;
    return (value / 1000u) * 1000u;
}

static uint32_t sanitize_power_auto_standby_ms(uint32_t value) {
    switch (value) {
        case 10000u:
        case 30000u:
        case 60000u:
        case 120000u:
        case 300000u:
            return value;
        default:
            return DEFAULT_POWER_AUTO_STANDBY_MS;
    }
}

static void init_power_defaults(PowerConfig& power) {
    power.wakeHoldMs = DEFAULT_POWER_WAKE_HOLD_MS;
    power.autoStandbyMs = DEFAULT_POWER_AUTO_STANDBY_MS;
}

static void sanitize_power_config(PowerConfig& power) {
    power.wakeHoldMs = clamp_power_wake_hold_ms(power.wakeHoldMs);
    power.autoStandbyMs = sanitize_power_auto_standby_ms(power.autoStandbyMs);
}

static void init_hardware_layout(HardwareLayoutConfig& hardware) {
    hardware.batteryPackCount = LATEST_PCB_BATTERY_PACK_COUNT;
    hardware.keyLedCount = LATEST_PCB_KEY_LED_COUNT;
    hardware.ambientLedCount = LATEST_PCB_AMBIENT_LED_COUNT;
}

static void sanitize_hardware_layout(HardwareLayoutConfig& hardware) {
    /* Board population is immutable; imported/stale values are informational only. */
    init_hardware_layout(hardware);
}

static void add_power_json(cJSON* globalConfigJSON, const PowerConfig& power) {
    cJSON* powerJSON = cJSON_CreateObject();
    cJSON_AddNumberToObject(powerJSON, "wakeHoldMs", power.wakeHoldMs);
    cJSON_AddNumberToObject(powerJSON, "autoStandbyMs", power.autoStandbyMs);
    cJSON_AddItemToObject(globalConfigJSON, "power", powerJSON);
}

static void parse_power_json(PowerConfig& power, cJSON* globalConfig) {
    if (!globalConfig) return;
    cJSON* powerJSON = cJSON_GetObjectItem(globalConfig, "power");
    if (!powerJSON || !cJSON_IsObject(powerJSON)) return;

    cJSON* wakeHoldItem = cJSON_GetObjectItem(powerJSON, "wakeHoldMs");
    if (wakeHoldItem && cJSON_IsNumber(wakeHoldItem)) {
        int v = wakeHoldItem->valueint;
        power.wakeHoldMs = (v > 0) ? (uint32_t)v : DEFAULT_POWER_WAKE_HOLD_MS;
    }

    cJSON* autoStandbyItem = cJSON_GetObjectItem(powerJSON, "autoStandbyMs");
    if (autoStandbyItem && cJSON_IsNumber(autoStandbyItem)) {
        int v = autoStandbyItem->valueint;
        power.autoStandbyMs = (v > 0) ? (uint32_t)v : 0u;
    }

    sanitize_power_config(power);
}

static void parse_screen_style_json(ScreenControlConfig& sc, cJSON* screenControl) {
    if (!screenControl) return;
    cJSON* item = cJSON_GetObjectItem(screenControl, "screenStyle");
    if (item && cJSON_IsString(item)) {
        sc.screenStyle = getScreenStyleFromString(item->valuestring);
        sanitize_screen_style(sc);
        return;
    }

    cJSON* bg = cJSON_GetObjectItem(screenControl, "backgroundColor");
    cJSON* fg = cJSON_GetObjectItem(screenControl, "textColor");
    if (bg && fg && cJSON_IsNumber(bg) && cJSON_IsNumber(fg)) {
        sc.screenStyle = infer_screen_style_from_colors((uint32_t)bg->valuedouble, (uint32_t)fg->valuedouble);
        sanitize_screen_style(sc);
    }
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

static uint32_t keep_first_virtual_pin_mask(uint32_t mask) {
    if (mask == 0u) return 0u;
    return mask & (0u - mask);
}

static void sanitize_competition_profile(GamepadProfile& profile) {
    if (!profile.enabled || !profile.isCompetitionProfile) return;
    profile.keysConfig.socdMode = SOCDMode::SOCD_MODE_NEUTRAL;
    for (uint32_t i = 0; i < NUM_GAME_CONTROLLER_BUTTONS; i++) {
        profile.keysConfig.keyMapping[i] = keep_first_virtual_pin_mask(profile.keysConfig.keyMapping[i]);
    }
    memset(profile.keysConfig.keyCombinations, 0, sizeof(profile.keysConfig.keyCombinations));
    memset(profile.keysConfig.macros, 0, sizeof(profile.keysConfig.macros));
}

static void sanitize_competition_profiles(Config& config) {
    for (uint32_t i = 0; i < NUM_PROFILES; i++) {
        sanitize_competition_profile(config.profiles[i]);
    }
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
    cJSON_AddStringToObject(screenControlJSON, "screenStyle", getScreenStyleString(config.screenControl.screenStyle));
    cJSON_AddStringToObject(screenControlJSON, "backgroundImageId", config.screenControl.backgroundImageId);
    cJSON_AddNumberToObject(screenControlJSON, "currentPageId", config.screenControl.currentPageId);
    cJSON* featuresJSON = cJSON_CreateObject();
    struct { uint8_t id; const char* key; uint32_t bit; } map[] = {
        {0, "inputModeSwitch", SCREEN_FEATURE_INPUT_MODE_SWITCH},
        {1, "profilesSwitch", SCREEN_FEATURE_PROFILES_SWITCH},
        {2, "socdModeSwitch", SCREEN_FEATURE_SOCD_MODE_SWITCH},
        {3, "connectionModeSwitch", SCREEN_FEATURE_TOURNAMENT_MODE_SWITCH},
        {11, "buttonsPerformanceQuickSet", SCREEN_FEATURE_BUTTONS_PERFORMANCE_QUICK_SET},
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
    cJSON_AddStringToObject(globalConfigJSON, "connectionMode", getConnectionModeString(config.connectionMode));
    cJSON_AddStringToObject(globalConfigJSON, "wirelessReportRate", getWirelessReportRateString(config.wirelessReportRate));
    cJSON_AddStringToObject(globalConfigJSON, "defaultProfileId", config.defaultProfileId);
    sanitize_power_config(config.power);
    add_power_json(globalConfigJSON, config.power);
    
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

        cJSON* connectionModeItem = cJSON_GetObjectItem(globalConfig, "connectionMode");
        if (connectionModeItem && cJSON_IsString(connectionModeItem)) {
            config.connectionMode = getConnectionModeFromString(connectionModeItem->valuestring);
        }

        cJSON* reportRateItem = cJSON_GetObjectItem(globalConfig, "wirelessReportRate");
        if (reportRateItem && cJSON_IsString(reportRateItem)) {
            config.wirelessReportRate = getWirelessReportRateFromString(reportRateItem->valuestring);
        }

        if (config.connectionMode == CONNECTION_MODE_RF24G) {
            config.inputMode = INPUT_MODE_XINPUT;
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

        parse_power_json(config.power, globalConfig);
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
        parse_screen_style_json(config.screenControl, screenControl);
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
                {"connectionModeSwitch", SCREEN_FEATURE_TOURNAMENT_MODE_SWITCH},
                {"buttonsPerformanceQuickSet", SCREEN_FEATURE_BUTTONS_PERFORMANCE_QUICK_SET},
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
            {"connectionModeSwitch", 3},
            {"inputModeSwitch", 0},
            {"profilesSwitch", 1},
            {"socdModeSwitch", 2},
            {"buttonsPerformanceQuickSet", 11},
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
                config.screenControl.featuresOrder[i] = orderMap[i].id;
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
    profile.isCompetitionProfile = false;
    
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
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_S1] = 1 << 19;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_S2] = 1 << 18;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_L3] = 1 << 0;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_R3] = 1 << 2;
    profile.keysConfig.keyMapping[GameControllerButton::GAME_CONTROLLER_BUTTON_A1] = 1 << 20;
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
        sanitize_screen_style(config.screenControl);
        sanitize_screen_recovery_entry(config.screenControl);
        sanitize_screen_service_flags(config.screenControl);
        sanitize_power_config(config.power);
        sanitize_hardware_layout(config.hardware);
        uint32_t ver = config.version;
        APP_DBG("Config Version: %d.%d.%d", (ver>>16) & 0xff, (ver>>8) & 0xff, ver & 0xff);
        return true;
    } else if (fjResult == true && config.version == CONFIG_VERSION_SCREEN_STYLE_MIGRATE_FROM) {
        uint32_t oldBg = read_legacy_screen_bg(config.screenControl);
        uint32_t oldFg = read_legacy_screen_fg(config.screenControl);
        config.screenControl.screenStyle = infer_screen_style_from_colors(oldBg, oldFg);
        sanitize_screen_style(config.screenControl);
        sanitize_screen_recovery_entry(config.screenControl);
        sanitize_screen_service_flags(config.screenControl);
        init_power_defaults(config.power);
        init_hardware_layout(config.hardware);
        config.version = CONFIG_VERSION;
        APP_DBG("ConfigUtils::load - migrated screen style from bg=0x%06lx fg=0x%06lx style=%u",
                (unsigned long)(oldBg & 0xFFFFFFu),
                (unsigned long)(oldFg & 0xFFFFFFu),
                (unsigned int)config.screenControl.screenStyle);
        return save(config);
    } else if (fjResult == true && config.version == CONFIG_VERSION_POWER_MIGRATE_FROM) {
        sanitize_screen_style(config.screenControl);
        sanitize_screen_recovery_entry(config.screenControl);
        sanitize_screen_service_flags(config.screenControl);
        init_power_defaults(config.power);
        init_hardware_layout(config.hardware);
        config.version = CONFIG_VERSION;
        APP_DBG("ConfigUtils::load - migrated power config defaults");
        return save(config);
    } else if (fjResult == true && config.version == CONFIG_VERSION_LATEST_PCB_MIGRATE_FROM) {
        sanitize_screen_style(config.screenControl);
        sanitize_screen_recovery_entry(config.screenControl);
        sanitize_screen_service_flags(config.screenControl);
        sanitize_power_config(config.power);
        init_hardware_layout(config.hardware);
        config.version = CONFIG_VERSION;
        APP_DBG("ConfigUtils::load - migrated latest PCB layout: battery=%u keyLeds=%u ambientLeds=%u",
                (unsigned int)config.hardware.batteryPackCount,
                (unsigned int)config.hardware.keyLedCount,
                (unsigned int)config.hardware.ambientLedCount);
        return save(config);
    } else {

        APP_DBG("init config, version: %d.%d.%d", (CONFIG_VERSION>>16) & 0xff, (CONFIG_VERSION>>8) & 0xff, CONFIG_VERSION & 0xff);
        // 设置基础配置
        config.version = CONFIG_VERSION;
        config.bootMode = BOOT_MODE_INPUT;
        config.inputMode = InputMode::INPUT_MODE_XINPUT;
        config.connectionMode = ConnectionMode::CONNECTION_MODE_USB;
        config.wirelessReportRate = WirelessReportRate::RFM_RATE_1K;
        config.reservedConnection0 = 0;
        strcpy(config.defaultProfileId, "profile-0");
        config.numProfilesMax = NUM_PROFILES;
        config.autoCalibrationEnabled = false; // 默认关闭自动校准
        init_hardware_layout(config.hardware);
        config.screenControl.brightness = 100;
        config.screenControl.standbyDisplay = 0;
        memset(config.screenControl.reserved0, 0, sizeof(config.screenControl.reserved0));
        config.screenControl.screenStyle = SCREEN_STYLE_DARK;
        memset(config.screenControl.reservedStyle, 0, sizeof(config.screenControl.reservedStyle));
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
            SCREEN_FEATURE_CALIBRATION_MODE_SWITCH |
            SCREEN_FEATURE_BUTTONS_PERFORMANCE_QUICK_SET;
        const uint8_t defaultFeatureOrder[SCREEN_FEATURE_COUNT] = {3, 0, 1, 2, 11, 4, 5, 6, 7, 8, 9, 10};
        memcpy(config.screenControl.featuresOrder, defaultFeatureOrder, sizeof(config.screenControl.featuresOrder));
        sanitize_screen_recovery_entry(config.screenControl);
        config.screenControl.serviceFlags = 0;
        init_power_defaults(config.power);

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

class ConfigQspiIndirectGuard {
public:
    ConfigQspiIndirectGuard()
        : wasMemoryMapped_(QSPI_W25Qxx_IsMemoryMappedMode()),
          ready_(true) {
        if (wasMemoryMapped_ &&
            QSPI_W25Qxx_ExitMemoryMappedMode() != QSPI_W25Qxx_OK) {
            ready_ = false;
        }
    }

    ~ConfigQspiIndirectGuard() {
        if (wasMemoryMapped_) {
            (void)QSPI_W25Qxx_EnterMemoryMappedMode();
        }
    }

    bool ready() const {
        return ready_;
    }

private:
    bool wasMemoryMapped_;
    bool ready_;
};

enum class ConfigBankStatus : uint8_t {
    IO_ERROR = 0,
    INVALID,
    VALID
};

typedef struct {
    ConfigBankStatus status;
    uint32_t address;
    ConfigJournalHeader header;
} ConfigBankState;

static bool is_supported_legacy_config_version(uint32_t version) {
    return version == CONFIG_VERSION ||
           version == CONFIG_VERSION_SCREEN_STYLE_MIGRATE_FROM ||
           version == CONFIG_VERSION_POWER_MIGRATE_FROM ||
           version == CONFIG_VERSION_LATEST_PCB_MIGRATE_FROM;
}

static ConfigBankState inspect_config_bank(uint32_t bankAddress) {
    ConfigBankState state = {};
    state.status = ConfigBankStatus::IO_ERROR;
    state.address = bankAddress;

    int8_t result = QSPI_W25Qxx_ReadBuffer(
        reinterpret_cast<uint8_t*>(&state.header),
        bankAddress,
        sizeof(state.header));
    if (result != QSPI_W25Qxx_OK) {
        return state;
    }

    if (state.header.magic != CONFIG_JOURNAL_MAGIC ||
        state.header.formatVersion != CONFIG_JOURNAL_VERSION ||
        state.header.headerSize != sizeof(ConfigJournalHeader) ||
        state.header.generation == 0u ||
        state.header.payloadLength != sizeof(Config) ||
        state.header.payloadLength >
            (CONFIG_BANK_SIZE - sizeof(ConfigJournalHeader)) ||
        state.header.commit != CONFIG_JOURNAL_COMMIT) {
        state.status = ConfigBankStatus::INVALID;
        return state;
    }

    state.status = ConfigBankStatus::VALID;
    return state;
}

static const ConfigBankState* select_newest_config_bank(
    const ConfigBankState& bankA,
    const ConfigBankState& bankB) {
    const bool aValid = bankA.status == ConfigBankStatus::VALID;
    const bool bValid = bankB.status == ConfigBankStatus::VALID;
    if (!aValid) {
        return bValid ? &bankB : nullptr;
    }
    if (!bValid) {
        return &bankA;
    }

    /*
     * Equal generations are not emitted by save(). If a service tool cloned a
     * committed bank, selecting A gives deterministic recovery.
     */
    return (bankB.header.generation > bankA.header.generation)
        ? &bankB
        : &bankA;
}

static bool write_config_bank(uint32_t bankAddress,
                              uint64_t generation,
                              const Config& config) {
    ConfigJournalHeader header;
    memset(&header, 0xFF, sizeof(header));
    header.magic = CONFIG_JOURNAL_MAGIC;
    header.formatVersion = CONFIG_JOURNAL_VERSION;
    header.headerSize = sizeof(ConfigJournalHeader);
    header.generation = generation;
    header.payloadLength = sizeof(Config);
    header.commit = CONFIG_JOURNAL_ERASED_WORD;

    if (QSPI_W25Qxx_BufferErase(bankAddress, CONFIG_BANK_SIZE) !=
        QSPI_W25Qxx_OK) {
        return false;
    }

    /*
     * The uncommitted header and payload may be interrupted at any byte. Such
     * a bank is ignored because commit remains erased.
     */
    if (qspi_write_buffer_no_erase(
            reinterpret_cast<uint8_t*>(&header),
            bankAddress,
            offsetof(ConfigJournalHeader, commit)) != QSPI_W25Qxx_OK ||
        qspi_write_buffer_no_erase(
            const_cast<uint8_t*>(
                reinterpret_cast<const uint8_t*>(&config)),
            bankAddress + sizeof(ConfigJournalHeader),
            sizeof(config)) != QSPI_W25Qxx_OK) {
        return false;
    }

    /*
     * This is the sole commit point. It intentionally uses its own page
     * program after the complete payload write has returned successfully.
     */
    uint32_t commit = CONFIG_JOURNAL_COMMIT;
    if (qspi_write_buffer_no_erase(
            reinterpret_cast<uint8_t*>(&commit),
            bankAddress + offsetof(ConfigJournalHeader, commit),
            sizeof(commit)) != QSPI_W25Qxx_OK) {
        return false;
    }

    ConfigBankState committed = inspect_config_bank(bankAddress);
    return committed.status == ConfigBankStatus::VALID &&
           committed.header.generation == generation;
}

bool ConfigUtils::save(Config& config)
{
    APP_DBG("ConfigUtils::save begin");
    sanitize_competition_profiles(config);
    sanitize_hardware_layout(config.hardware);
    sanitize_screen_recovery_entry(config.screenControl);
    sanitize_screen_service_flags(config.screenControl);

    ConfigQspiIndirectGuard guard;
    if (!guard.ready()) {
        APP_ERR("ConfigUtils::save - failed to enter QSPI indirect mode.");
        return false;
    }

    const ConfigBankState bankA = inspect_config_bank(CONFIG_BANK_A_ADDR);
    const ConfigBankState bankB = inspect_config_bank(CONFIG_BANK_B_ADDR);
    if (bankA.status == ConfigBankStatus::IO_ERROR ||
        bankB.status == ConfigBankStatus::IO_ERROR) {
        APP_ERR("ConfigUtils::save - failed to inspect journal banks.");
        return false;
    }

    const ConfigBankState* active =
        select_newest_config_bank(bankA, bankB);
    const uint32_t targetAddress =
        (active != nullptr && active->address == CONFIG_BANK_B_ADDR)
            ? CONFIG_BANK_A_ADDR
            : CONFIG_BANK_B_ADDR;
    const uint64_t nextGeneration =
        (active == nullptr) ? 1u : active->header.generation + 1u;
    if (nextGeneration == 0u) {
        APP_ERR("ConfigUtils::save - journal generation exhausted.");
        return false;
    }

    if (!write_config_bank(targetAddress, nextGeneration, config)) {
        APP_ERR("ConfigUtils::save - journal write/verify failure.");
        return false;
    }

    APP_DBG("ConfigUtils::save - committed generation %lu to bank 0x%08lx.",
            (unsigned long)nextGeneration,
            (unsigned long)targetAddress);
    return true;
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
    {
        ConfigQspiIndirectGuard guard;
        if (!guard.ready()) {
            APP_ERR("ConfigUtils::reset - failed to enter QSPI indirect mode.");
            return false;
        }

        /*
         * Always attempt both erases. A false return means callers must not
         * claim reset success because stale committed data may still exist.
         */
        const int8_t eraseA =
            QSPI_W25Qxx_BufferErase(CONFIG_BANK_A_ADDR, CONFIG_BANK_SIZE);
        const int8_t eraseB =
            QSPI_W25Qxx_BufferErase(CONFIG_BANK_B_ADDR, CONFIG_BANK_SIZE);
        if (eraseA != QSPI_W25Qxx_OK || eraseB != QSPI_W25Qxx_OK) {
            APP_ERR("ConfigUtils::reset - dual-bank erase failure.");
            return false;
        }
    }

    memset(&config, 0, sizeof(config));
    if (!ConfigUtils::load(config)) {
        APP_ERR("ConfigUtils::reset - failed to persist defaults.");
        return false;
    }
    return true;
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
    APP_DBG("ConfigUtils::fromStorage begin.");

    ConfigQspiIndirectGuard guard;
    if (!guard.ready()) {
        APP_ERR("ConfigUtils::fromStorage - failed to enter QSPI indirect mode.");
        return false;
    }

    const ConfigBankState bankA = inspect_config_bank(CONFIG_BANK_A_ADDR);
    const ConfigBankState bankB = inspect_config_bank(CONFIG_BANK_B_ADDR);
    const ConfigBankState* active =
        select_newest_config_bank(bankA, bankB);
    if (active != nullptr) {
        if (QSPI_W25Qxx_ReadBuffer(
                reinterpret_cast<uint8_t*>(&config),
                active->address + sizeof(ConfigJournalHeader),
                sizeof(config)) != QSPI_W25Qxx_OK) {
            APP_ERR("ConfigUtils::fromStorage - journal payload read failure.");
            return false;
        }
        APP_DBG("ConfigUtils::fromStorage - loaded journal generation %lu.",
                (unsigned long)active->header.generation);
        return true;
    }

    /*
     * One-time V1 compatibility: before the first journal save, APP_CONFIG
     * contains a bare Config at offset zero. Never reinterpret a damaged
     * journal header as legacy data.
     */
    uint32_t firstWord = 0u;
    if (QSPI_W25Qxx_ReadBuffer(
            reinterpret_cast<uint8_t*>(&firstWord),
            CONFIG_BANK_A_ADDR,
            sizeof(firstWord)) != QSPI_W25Qxx_OK) {
        APP_ERR("ConfigUtils::fromStorage - legacy header read failure.");
        return false;
    }
    if (firstWord == CONFIG_JOURNAL_MAGIC ||
        firstWord == CONFIG_JOURNAL_ERASED_WORD ||
        !is_supported_legacy_config_version(firstWord)) {
        APP_DBG("ConfigUtils::fromStorage - no valid committed config.");
        return false;
    }

    if (QSPI_W25Qxx_ReadBuffer(
            reinterpret_cast<uint8_t*>(&config),
            CONFIG_BANK_A_ADDR,
            sizeof(config)) != QSPI_W25Qxx_OK) {
        APP_ERR("ConfigUtils::fromStorage - legacy payload read failure.");
        return false;
    }

    APP_DBG("ConfigUtils::fromStorage - loaded legacy raw Config.");
    return true;
}
