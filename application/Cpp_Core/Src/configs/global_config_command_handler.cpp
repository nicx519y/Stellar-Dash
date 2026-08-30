#include "storagemanager.hpp"
#include "configs/device_command_handler.hpp"
#include "adc_btns/adc_calibration.hpp"
#include "adc_btns/adc_manager.hpp"
#include "webconfig_leds_manager.hpp"
#include "webconfig_btns_manager.hpp"
#include "system_logger.h"
#include "config.hpp"
#include "board_mode.hpp"
#include "usb_board_link.hpp"
#include "usbdriver.hpp"
#include "config_transport_sink.hpp"
#include "firmware_metadata.h"
#include "leds/led_config_safety.hpp"
#include <map>
#include <stdio.h>
#include <cstring>
#include <cmath>

// ============================================================================
// GlobalConfigCommandHandler 实现
// ============================================================================

GlobalConfigCommandHandler& GlobalConfigCommandHandler::getInstance() {
    static GlobalConfigCommandHandler instance;
    return instance;
}

namespace {

struct ConfigImportTransaction {
    bool active;
    bool strict;
    bool replaceProfiles;
    bool globalSeen;
    bool hotkeysSeen;
    bool screenSeen;
    bool profileSeen[NUM_PROFILES];
    Config candidate;
};

// A complete Config plus ADC rollback snapshots is intentionally kept out of
// the primary AXI SRAM.  The application already reserves SRAM-D2 for large
// runtime buffers; this transaction is CPU-only and never handed to DMA.
// .DMA_Section* is NOLOAD, so initialize it explicitly on first use.
__attribute__((section(".DMA_Section.ConfigImport"), aligned(32)))
ConfigImportTransaction g_configImport;
bool g_configImportInitialized = false;

void ensure_config_import_initialized() {
    if (!g_configImportInitialized) {
        memset(&g_configImport, 0, sizeof(g_configImport));
        g_configImportInitialized = true;
    }
}

void reset_config_import() {
    ensure_config_import_initialized();
    memset(&g_configImport, 0, sizeof(g_configImport));
}

void begin_config_import(bool strict, bool replaceProfiles) {
    reset_config_import();
    g_configImport.active = true;
    g_configImport.strict = strict;
    g_configImport.replaceProfiles = replaceProfiles;
    memcpy(&g_configImport.candidate,
           &Storage::getInstance().config,
           sizeof(g_configImport.candidate));
}

bool json_short_string(cJSON* item, size_t capacity) {
    return item && cJSON_IsString(item) && item->valuestring &&
           item->valuestring[0] != '\0' && strlen(item->valuestring) < capacity;
}

bool valid_default_profile(const Config& config) {
    for (uint8_t i = 0; i < NUM_PROFILES; ++i) {
        if (config.profiles[i].enabled &&
            strncmp(config.profiles[i].id, config.defaultProfileId,
                    sizeof(config.defaultProfileId)) == 0) {
            return true;
        }
    }
    return false;
}

bool validate_legacy_import_profiles(cJSON* root,
                                     const Config& config,
                                     std::string& error) {
    cJSON* profiles = root ? cJSON_GetObjectItem(root, "profiles") : nullptr;
    if (!profiles) return true;
    if (!cJSON_IsArray(profiles)) {
        error = "Profiles section must be an array";
        return false;
    }

    bool seen[NUM_PROFILES] = {false};
    cJSON* profile = nullptr;
    cJSON_ArrayForEach(profile, profiles) {
        cJSON* id = profile && cJSON_IsObject(profile)
            ? cJSON_GetObjectItem(profile, "id") : nullptr;
        if (!json_short_string(id, sizeof(config.profiles[0].id))) {
            error = "Profile section has an invalid ID";
            return false;
        }
        int matchedIndex = -1;
        for (int i = 0; i < NUM_PROFILES; ++i) {
            if (strncmp(config.profiles[i].id, id->valuestring,
                        sizeof(config.profiles[i].id)) == 0) {
                matchedIndex = i;
                break;
            }
        }
        if (matchedIndex < 0) {
            error = "Profile ID is not supported by this device";
            return false;
        }
        if (seen[matchedIndex]) {
            error = "Duplicate profile section";
            return false;
        }
        seen[matchedIndex] = true;
    }
    return true;
}

} // namespace

static uint32_t screen_color_luma(uint32_t rgb) {
    uint32_t r = (rgb >> 16) & 0xFFu;
    uint32_t g = (rgb >> 8) & 0xFFu;
    uint32_t b = rgb & 0xFFu;
    return r * 299u + g * 587u + b * 114u;
}

static uint8_t infer_screen_style_from_json_colors(uint32_t bg, uint32_t fg) {
    return (screen_color_luma(bg) > screen_color_luma(fg)) ? SCREEN_STYLE_LIGHT : SCREEN_STYLE_DARK;
}

static void set_screen_style_from_json(ScreenControlConfig& sc, cJSON* screenControl) {
    if (!screenControl) return;
    cJSON* item = cJSON_GetObjectItem(screenControl, "screenStyle");
    if (item && cJSON_IsString(item)) {
        sc.screenStyle = ConfigUtils::getScreenStyleFromString(item->valuestring);
    } else {
        cJSON* bg = cJSON_GetObjectItem(screenControl, "backgroundColor");
        cJSON* fg = cJSON_GetObjectItem(screenControl, "textColor");
        if (bg && fg && cJSON_IsNumber(bg) && cJSON_IsNumber(fg)) {
            sc.screenStyle = infer_screen_style_from_json_colors((uint32_t)bg->valuedouble, (uint32_t)fg->valuedouble);
        }
    }
    if (sc.screenStyle != SCREEN_STYLE_LIGHT) {
        sc.screenStyle = SCREEN_STYLE_DARK;
    }
    memset(sc.reservedStyle, 0, sizeof(sc.reservedStyle));
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
            return 300000u;
    }
}

static void add_power_json(cJSON* globalConfigJSON, const PowerConfig& power) {
    cJSON* powerJSON = cJSON_CreateObject();
    cJSON_AddNumberToObject(powerJSON, "wakeHoldMs", power.wakeHoldMs);
    cJSON_AddNumberToObject(powerJSON, "autoStandbyMs", power.autoStandbyMs);
    cJSON_AddItemToObject(globalConfigJSON, "power", powerJSON);
}

static void parse_power_json(PowerConfig& power, cJSON* globalConfigJSON) {
    if (!globalConfigJSON) return;
    cJSON* powerJSON = cJSON_GetObjectItem(globalConfigJSON, "power");
    if (!powerJSON || !cJSON_IsObject(powerJSON)) return;

    cJSON* wakeHoldItem = cJSON_GetObjectItem(powerJSON, "wakeHoldMs");
    if (wakeHoldItem && cJSON_IsNumber(wakeHoldItem)) {
        int v = wakeHoldItem->valueint;
        if (v > 0) power.wakeHoldMs = clamp_power_wake_hold_ms((uint32_t)v);
    }

    cJSON* autoStandbyItem = cJSON_GetObjectItem(powerJSON, "autoStandbyMs");
    if (autoStandbyItem && cJSON_IsNumber(autoStandbyItem)) {
        int v = autoStandbyItem->valueint;
        power.autoStandbyMs = sanitize_power_auto_standby_ms(v > 0 ? (uint32_t)v : 0u);
    }
}

static cJSON* get_hotkey_key_item(cJSON* hotkeyItem) {
    cJSON* keyItem = cJSON_GetObjectItemCaseSensitive(hotkeyItem, "key");
    if (cJSON_IsNumber(keyItem)) {
        return keyItem;
    }

    // Compatibility for configuration payloads produced by legacy clients.
    cJSON* legacyVirtualPinItem =
        cJSON_GetObjectItemCaseSensitive(hotkeyItem, "virtualPin");
    return cJSON_IsNumber(legacyVirtualPinItem) ? legacyVirtualPinItem : nullptr;
}

static const char* physical_mode_string() {
    if (!BOARD_MODE.isStable()) {
        return "UNKNOWN";
    }

    switch (BOARD_MODE.current()) {
        case BoardMode::Usb:
            return "USB";
        case BoardMode::Rf:
            return "RF24G";
        case BoardMode::CenterOff:
            return "OFF";
        case BoardMode::Fault:
        default:
            return "FAULT";
    }
}

static ConnectionMode physical_connection_mode(const Config& config) {
    if (BOARD_MODE.isStable()) {
        if (BOARD_MODE.current() == BoardMode::Rf) {
            return ConnectionMode::CONNECTION_MODE_RF24G;
        }
        if (BOARD_MODE.current() == BoardMode::Usb) {
            return ConnectionMode::CONNECTION_MODE_USB;
        }
    }

    /* Compatibility value only while the switch is OFF/faulted/not stable. */
    return config.connectionMode;
}

static bool physical_rf_selected() {
    return BOARD_MODE.isStable() && BOARD_MODE.current() == BoardMode::Rf;
}

static void add_latest_board_json(cJSON* globalConfigJSON, const Config& config) {
    cJSON_AddBoolToObject(globalConfigJSON, "connectionModeReadOnly", true);
    cJSON_AddStringToObject(globalConfigJSON, "connectionModeSource", "PHYSICAL_SWITCH");
    cJSON_AddStringToObject(globalConfigJSON, "physicalConnectionMode", physical_mode_string());

    cJSON* hardwareJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(hardwareJSON, "batteryTopology", "SINGLE_1S2P");
    cJSON_AddNumberToObject(hardwareJSON, "batteryPackCount", config.hardware.batteryPackCount);
    cJSON_AddNumberToObject(hardwareJSON, "keyLedCount", config.hardware.keyLedCount);
    cJSON_AddNumberToObject(hardwareJSON, "ambientLedCount", config.hardware.ambientLedCount);
    /* This is the signed PCB revision, not the STM32 silicon REV_ID. */
    cJSON_AddStringToObject(hardwareJSON,
                            "hardwareVersion",
                            HARDWARE_VERSION_STRING);
    cJSON_AddItemToObject(globalConfigJSON, "hardware", hardwareJSON);

    const usb_board_role_t role = USB_BOARD_LINK.role();
    const char* roleString = "UNKNOWN";
    if (role == USB_BOARD_ROLE_RF) {
        roleString = "RF";
    } else if (role == USB_BOARD_ROLE_USB) {
        roleString = "USB";
    } else if (role == USB_BOARD_ROLE_MAINTENANCE) {
        roleString = "MAINTENANCE";
    }

    cJSON* ch585JSON = cJSON_CreateObject();
    cJSON_AddStringToObject(ch585JSON, "role", roleString);
    cJSON_AddBoolToObject(ch585JSON, "capabilitiesValid", USB_BOARD_LINK.isCompatible());
    if (USB_BOARD_LINK.isCompatible()) {
        const usb_board_caps_v1_t& caps = USB_BOARD_LINK.capabilities();
        char version[16] = {};
        snprintf(version, sizeof(version), "%u.%u.%u",
                 (unsigned int)caps.firmware_major,
                 (unsigned int)caps.firmware_minor,
                 (unsigned int)caps.firmware_patch);
        cJSON_AddStringToObject(ch585JSON, "firmwareVersion", version);
    } else {
        /* RF role intentionally closes the 0x5A parser after role ACK. */
        cJSON_AddStringToObject(ch585JSON, "firmwareVersion", "");
    }
    cJSON_AddItemToObject(globalConfigJSON, "ch585", ch585JSON);

    const uint16_t requestedHz = static_cast<uint16_t>(
        config.wirelessReportRate);
    uint16_t effectiveHz = 1000u;
    UsbReportRateLimit limit = UsbReportRateLimit::NotHighSpeed;
    usb_board_usb_speed_t speed = USB_BOARD_USB_SPEED_NONE;
    if (BOARD_MODE.isStable() && BOARD_MODE.current() == BoardMode::Rf) {
        effectiveHz = requestedHz;
        limit = UsbReportRateLimit::None;
    } else if (BOARD_MODE.isStable() &&
               BOARD_MODE.current() == BoardMode::Usb) {
        effectiveHz = USB_DRIVER.effectiveReportRateHz(config.inputMode,
                                                       requestedHz);
        limit = USB_DRIVER.reportRateLimit(config.inputMode, requestedHz);
        speed = USB_DRIVER.usbSpeed();
    }

    const char* limitString = "NONE";
    switch (limit) {
        case UsbReportRateLimit::Profile:
            limitString = "USB_PROFILE_LIMIT";
            break;
        case UsbReportRateLimit::NotHighSpeed:
            limitString = "USB_NOT_HIGH_SPEED";
            break;
        case UsbReportRateLimit::BoardLinkCompatibility:
            limitString = "BOARD_LINK_COMPAT";
            break;
        case UsbReportRateLimit::None:
        default:
            break;
    }
    const char* speedString = speed == USB_BOARD_USB_SPEED_HIGH
        ? "HIGH"
        : speed == USB_BOARD_USB_SPEED_FULL ? "FULL" : "NONE";
    const WirelessReportRate effectiveRate =
        static_cast<WirelessReportRate>(effectiveHz);
    cJSON* rateStatusJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(
        rateStatusJSON, "requested",
        ConfigUtils::getWirelessReportRateString(config.wirelessReportRate));
    cJSON_AddStringToObject(
        rateStatusJSON, "effective",
        ConfigUtils::getWirelessReportRateString(effectiveRate));
    cJSON_AddStringToObject(rateStatusJSON, "usbSpeed", speedString);
    cJSON_AddStringToObject(rateStatusJSON, "limit", limitString);
    cJSON_AddItemToObject(globalConfigJSON,
                          "reportRateStatus",
                          rateStatusJSON);
}

DeviceCommandResponse GlobalConfigCommandHandler::handleGetGlobalConfig(const DeviceCommandRequest& request) {
    // LOG_INFO("DeviceCommand", "Handling get_global_config command, cid: %d", request.getCid());

    Config& config = Storage::getInstance().config;
    
    // 创建返回数据结构
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* globalConfigJSON = cJSON_CreateObject();
    
    // 使用ConfigUtils获取输入模式字符串
    const char* modeStr = ConfigUtils::getInputModeString(config.inputMode);
    cJSON_AddStringToObject(globalConfigJSON, "inputMode", modeStr);
    cJSON_AddStringToObject(globalConfigJSON, "connectionMode",
                            ConfigUtils::getConnectionModeString(physical_connection_mode(config)));
    cJSON_AddStringToObject(globalConfigJSON, "wirelessReportRate", ConfigUtils::getWirelessReportRateString(config.wirelessReportRate));
    cJSON_AddStringToObject(globalConfigJSON, "defaultProfileId", config.defaultProfileId);
    add_latest_board_json(globalConfigJSON, config);
    
    // 添加自动校准模式状态
    cJSON_AddBoolToObject(globalConfigJSON, "autoCalibrationEnabled", config.autoCalibrationEnabled);
    add_power_json(globalConfigJSON, config.power);
    
    // 添加手动校准状态
    cJSON_AddBoolToObject(globalConfigJSON, "manualCalibrationActive", ADC_CALIBRATION_MANAGER.isCalibrationActive());
    
    // 构建返回结构
    cJSON_AddItemToObject(dataJSON, "globalConfig", globalConfigJSON);
    
    // LOG_INFO("DeviceCommand", "get_global_config command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse GlobalConfigCommandHandler::handleUpdateGlobalConfig(const DeviceCommandRequest& request) {
    // LOG_INFO("DeviceCommand", "Handling update_global_config command, cid: %d", request.getCid());
    
    Config& config = Storage::getInstance().config;
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("DeviceCommand", "update_global_config: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    // 更新全局配置
    cJSON* globalConfig = cJSON_GetObjectItem(params, "globalConfig");
    if (globalConfig) {
        // 更新输入模式
        cJSON* inputModeItem = cJSON_GetObjectItem(globalConfig, "inputMode");
        if (inputModeItem && cJSON_IsString(inputModeItem)) {
            std::string modeStr = inputModeItem->valuestring;
            config.inputMode = ConfigUtils::getInputModeFromString(modeStr.c_str());
        }

        cJSON* reportRateItem = cJSON_GetObjectItem(globalConfig, "wirelessReportRate");
        if (reportRateItem && cJSON_IsString(reportRateItem)) {
            config.wirelessReportRate = ConfigUtils::getWirelessReportRateFromString(reportRateItem->valuestring);
        }

        if (physical_rf_selected()) {
            config.inputMode = INPUT_MODE_XINPUT;
        }
        
        // 更新自动校准模式
        cJSON* autoCalibrationItem = cJSON_GetObjectItem(globalConfig, "autoCalibrationEnabled");
        if (autoCalibrationItem) {
            config.autoCalibrationEnabled = cJSON_IsTrue(autoCalibrationItem);
        }

        parse_power_json(config.power, globalConfig);
    }

    // 保存配置
    if (!STORAGE_MANAGER.saveConfig()) {
        LOG_ERROR("DeviceCommand", "update_global_config: Failed to save configuration");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to save configuration");
    }

    // 返回更新后的配置
    return handleGetGlobalConfig(request);
}

DeviceCommandResponse GlobalConfigCommandHandler::handleGetHotkeysConfig(const DeviceCommandRequest& request) {
    // LOG_INFO("DeviceCommand", "Handling get_hotkeys_config command, cid: %d", request.getCid());
    
    Config& config = Storage::getInstance().config;
    
    // 创建返回数据结构
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* hotkeysConfigJSON = ConfigUtils::buildHotkeysConfigJSON(config);
    
    if (!hotkeysConfigJSON) {
        LOG_ERROR("DeviceCommand", "get_hotkeys_config: Failed to build hotkeys config JSON");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to build hotkeys config JSON");
    }

    // 构建返回结构
    cJSON_AddItemToObject(dataJSON, "hotkeysConfig", hotkeysConfigJSON);
    
    // LOG_INFO("DeviceCommand", "get_hotkeys_config command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse GlobalConfigCommandHandler::handleUpdateHotkeysConfig(const DeviceCommandRequest& request) {
    // LOG_INFO("DeviceCommand", "Handling update_hotkeys_config command, cid: %d", request.getCid());
    
    Config& config = Storage::getInstance().config;
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("DeviceCommand", "update_hotkeys_config: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    // 更新快捷键配置
    cJSON* hotkeysConfig = cJSON_GetObjectItem(params, "hotkeysConfig");
    if (hotkeysConfig && cJSON_IsArray(hotkeysConfig)) {
        int index = 0;
        cJSON* hotkeyItem;
        cJSON_ArrayForEach(hotkeyItem, hotkeysConfig) {
            if (index >= NUM_GAMEPAD_HOTKEYS) break;
            if (config.hotkeys[index].isLocked) {
                index++;
                continue;
            }
            
            cJSON* actionItem = cJSON_GetObjectItem(hotkeyItem, "action");
            cJSON* pinItem = get_hotkey_key_item(hotkeyItem);
            cJSON* holdItem = cJSON_GetObjectItem(hotkeyItem, "isHold");

            if (actionItem && cJSON_IsString(actionItem)) {
                config.hotkeys[index].action = ConfigUtils::getGamepadHotkeyFromString(actionItem->valuestring);
            }
            if (pinItem && cJSON_IsNumber(pinItem)) {
                config.hotkeys[index].virtualPin = pinItem->valueint;
            }
            if (holdItem) {
                config.hotkeys[index].isHold = cJSON_IsTrue(holdItem);
            }
            index++;
        }
    }

    // 保存配置
    if (!STORAGE_MANAGER.saveConfig()) {
        LOG_ERROR("DeviceCommand", "update_hotkeys_config: Failed to save configuration");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to save configuration");
    }

    // 返回更新后的配置
    return handleGetHotkeysConfig(request);
}

DeviceCommandResponse GlobalConfigCommandHandler::handleGetScreenControlConfig(const DeviceCommandRequest& request) {
    Config& config = Storage::getInstance().config;

    cJSON* dataJSON = cJSON_CreateObject();
    cJSON* screenControlJSON = cJSON_CreateObject();

    cJSON_AddNumberToObject(screenControlJSON, "brightness", config.screenControl.brightness);
    const char* standbyDisplayStr = "none";
    switch (config.screenControl.standbyDisplay) {
        case 1: standbyDisplayStr = "backgroundImage"; break;
        case 2: standbyDisplayStr = "buttonLayout"; break;
        default: standbyDisplayStr = "none"; break;
    }
    cJSON_AddStringToObject(screenControlJSON, "standbyDisplay", standbyDisplayStr);
    cJSON_AddStringToObject(screenControlJSON, "screenStyle", ConfigUtils::getScreenStyleString(config.screenControl.screenStyle));
    cJSON_AddStringToObject(screenControlJSON, "backgroundImageId", config.screenControl.backgroundImageId);
    cJSON_AddNumberToObject(screenControlJSON, "currentPageId", config.screenControl.currentPageId);

    cJSON* featuresJSON = cJSON_CreateObject();
    cJSON_AddBoolToObject(featuresJSON, "inputModeSwitch", (config.screenControl.featuresMask & SCREEN_FEATURE_INPUT_MODE_SWITCH) != 0);
    cJSON_AddBoolToObject(featuresJSON, "profilesSwitch", (config.screenControl.featuresMask & SCREEN_FEATURE_PROFILES_SWITCH) != 0);
    cJSON_AddBoolToObject(featuresJSON, "socdModeSwitch", (config.screenControl.featuresMask & SCREEN_FEATURE_SOCD_MODE_SWITCH) != 0);
    cJSON_AddBoolToObject(featuresJSON, "connectionModeSwitch", (config.screenControl.featuresMask & SCREEN_FEATURE_TOURNAMENT_MODE_SWITCH) != 0);
    cJSON_AddBoolToObject(featuresJSON, "buttonsPerformanceQuickSet", (config.screenControl.featuresMask & SCREEN_FEATURE_BUTTONS_PERFORMANCE_QUICK_SET) != 0);
    cJSON_AddBoolToObject(featuresJSON, "ledBrightnessAdjust", (config.screenControl.featuresMask & SCREEN_FEATURE_LED_BRIGHTNESS_ADJUST) != 0);
    cJSON_AddBoolToObject(featuresJSON, "ledEffectSwitch", (config.screenControl.featuresMask & SCREEN_FEATURE_LED_EFFECT_SWITCH) != 0);
    cJSON_AddBoolToObject(featuresJSON, "ambientBrightnessAdjust", (config.screenControl.featuresMask & SCREEN_FEATURE_AMBIENT_BRIGHTNESS_ADJUST) != 0);
    cJSON_AddBoolToObject(featuresJSON, "ambientEffectSwitch", (config.screenControl.featuresMask & SCREEN_FEATURE_AMBIENT_EFFECT_SWITCH) != 0);
    cJSON_AddBoolToObject(featuresJSON, "screenBrightnessAdjust", (config.screenControl.featuresMask & SCREEN_FEATURE_SCREEN_BRIGHTNESS_ADJUST) != 0);
    cJSON_AddBoolToObject(featuresJSON, "webConfigEntry", (config.screenControl.featuresMask & SCREEN_FEATURE_WEB_CONFIG_ENTRY) != 0);
    cJSON_AddBoolToObject(featuresJSON, "calibrationModeSwitch", (config.screenControl.featuresMask & SCREEN_FEATURE_CALIBRATION_MODE_SWITCH) != 0);
    cJSON_AddItemToObject(screenControlJSON, "features", featuresJSON);

    cJSON* featuresOrderJSON = cJSON_CreateArray();
    struct { uint8_t id; const char* key; } orderMap[] = {
        {0, "inputModeSwitch"},
        {1, "profilesSwitch"},
        {2, "socdModeSwitch"},
        {3, "connectionModeSwitch"},
        {11, "buttonsPerformanceQuickSet"},
        {4, "ledBrightnessAdjust"},
        {5, "ledEffectSwitch"},
        {6, "ambientBrightnessAdjust"},
        {7, "ambientEffectSwitch"},
        {8, "screenBrightnessAdjust"},
        {9, "webConfigEntry"},
        {10, "calibrationModeSwitch"},
    };
    for (uint32_t i = 0; i < SCREEN_FEATURE_COUNT; i++) {
        uint8_t id = config.screenControl.featuresOrder[i];
        const char* key = nullptr;
        for (size_t j = 0; j < sizeof(orderMap) / sizeof(orderMap[0]); j++) {
            if (orderMap[j].id == id) {
                key = orderMap[j].key;
                break;
            }
        }
        if (key) {
            cJSON_AddItemToArray(featuresOrderJSON, cJSON_CreateString(key));
        }
    }
    cJSON_AddItemToObject(screenControlJSON, "featuresOrder", featuresOrderJSON);

    cJSON_AddItemToObject(dataJSON, "screenControl", screenControlJSON);
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse GlobalConfigCommandHandler::handleUpdateScreenControlConfig(const DeviceCommandRequest& request) {
    Config& config = Storage::getInstance().config;

    cJSON* params = request.getParams();
    if (!params) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    cJSON* screenControl = cJSON_GetObjectItem(params, "screenControl");
    if (!screenControl || !cJSON_IsObject(screenControl)) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid screenControl");
    }

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
    set_screen_style_from_json(config.screenControl, screenControl);
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
    if (featuresOrder && cJSON_IsArray(featuresOrder)) {
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
    }

    if (!STORAGE_MANAGER.saveConfig()) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to save configuration");
    }

    return handleGetScreenControlConfig(request);
}

DeviceCommandResponse GlobalConfigCommandHandler::handleExportAllConfig(const DeviceCommandRequest& request) {
    return create_error_response(
        request.getCid(),
        request.getCommand(),
        409,
        "Use incremental device configuration export");
}
DeviceCommandResponse GlobalConfigCommandHandler::handleImportAllConfig(const DeviceCommandRequest& request) {
    cJSON* params = request.getParams();
    
    if (!params) {
        LOG_ERROR("DeviceCommand", "import_all_config: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    begin_config_import(false, false);
    Config& candidate = g_configImport.candidate;
    std::string profileError;
    if (!validate_legacy_import_profiles(params, candidate, profileError)) {
        reset_config_import();
        return create_error_response(request.getCid(), request.getCommand(), 1,
                                     profileError);
    }
    if (!ConfigUtils::fromJSON(candidate, params)) {
         reset_config_import();
         LOG_ERROR("DeviceCommand", "import_all_config: Failed to parse configuration");
         return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to parse configuration");
    }
    if (physical_rf_selected()) {
        candidate.inputMode = INPUT_MODE_XINPUT;
    }
    if (!valid_default_profile(candidate)) {
        reset_config_import();
        return create_error_response(request.getCid(), request.getCommand(), 1,
                                     "Default profile is missing or disabled");
    }
    if (!ConfigUtils::save(candidate)) {
        reset_config_import();
        LOG_ERROR("DeviceCommand", "import_all_config: Failed to save configuration");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to save configuration");
    }
    memcpy(&Storage::getInstance().config, &candidate, sizeof(candidate));
    reset_config_import();

    // cJSON* exportJSON = ConfigUtils::toJSON(config);
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "message", "Configuration imported successfully");
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse GlobalConfigCommandHandler::handleImportConfigBegin(const DeviceCommandRequest& request) {
    cJSON* params = request.getParams();
    bool strict = false;
    bool replaceProfiles = false;
    if (params && cJSON_IsObject(params)) {
        cJSON* strictItem = cJSON_GetObjectItem(params, "strict");
        cJSON* replaceItem = cJSON_GetObjectItem(params, "replaceProfiles");
        strict = strictItem && cJSON_IsTrue(strictItem);
        replaceProfiles = replaceItem && cJSON_IsTrue(replaceItem);
    }
    begin_config_import(strict, replaceProfiles);
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddBoolToObject(dataJSON, "strict", strict);
    cJSON_AddBoolToObject(dataJSON, "replaceProfiles", replaceProfiles);
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse GlobalConfigCommandHandler::handleImportConfigPart(const DeviceCommandRequest& request) {
    ensure_config_import_initialized();
    cJSON* params = request.getParams();
    
    if (!params) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    cJSON* sectionItem = cJSON_GetObjectItem(params, "section");
    cJSON* dataItem = cJSON_GetObjectItem(params, "data");

    if (!sectionItem || !cJSON_IsString(sectionItem) || !dataItem) {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing section or data");
    }

    std::string section = sectionItem->valuestring;

    // Legacy clients did not send BEGIN. They still receive transactional RAM
    // staging, but retain the historical merge semantics.
    if (!g_configImport.active) {
        begin_config_import(false, false);
    }
    Config& config = g_configImport.candidate;

    if (section == "global") {
        if (!cJSON_IsObject(dataItem)) {
            return create_error_response(request.getCid(), request.getCommand(), 1,
                                         "Global section must be an object");
        }
        cJSON* globalConfigJSON = dataItem;
        cJSON* item;
        if ((item = cJSON_GetObjectItem(globalConfigJSON, "inputMode")) && cJSON_IsString(item)) {
            config.inputMode = ConfigUtils::getInputModeFromString(item->valuestring);
        }
        // connectionMode is selected by the physical switch and is never
        // restored from a configuration file.
        if ((item = cJSON_GetObjectItem(globalConfigJSON, "wirelessReportRate")) && cJSON_IsString(item)) {
            config.wirelessReportRate = ConfigUtils::getWirelessReportRateFromString(item->valuestring);
        }
        if (physical_rf_selected()) {
            config.inputMode = INPUT_MODE_XINPUT;
        }
        if ((item = cJSON_GetObjectItem(globalConfigJSON, "defaultProfileId")) && cJSON_IsString(item)) {
            if (strlen(item->valuestring) >= sizeof(config.defaultProfileId)) {
                return create_error_response(request.getCid(), request.getCommand(), 1,
                                             "Default profile ID is too long");
            }
            strncpy(config.defaultProfileId, item->valuestring, sizeof(config.defaultProfileId) - 1);
            config.defaultProfileId[sizeof(config.defaultProfileId) - 1] = '\0';
        }
        if ((item = cJSON_GetObjectItem(globalConfigJSON, "autoCalibrationEnabled"))) {
             config.autoCalibrationEnabled = cJSON_IsTrue(item);
        }
        parse_power_json(config.power, globalConfigJSON);
        g_configImport.globalSeen = true;
    } else if (section == "hotkeys") {
        if (!cJSON_IsArray(dataItem) ||
            (g_configImport.strict && cJSON_GetArraySize(dataItem) != NUM_GAMEPAD_HOTKEYS)) {
            return create_error_response(request.getCid(), request.getCommand(), 1,
                                         "Hotkeys section has an invalid length");
        }
        if (cJSON_IsArray(dataItem)) {
            int index = 0;
            cJSON* hotkeyItem;
            cJSON_ArrayForEach(hotkeyItem, dataItem) {
                if (index >= NUM_GAMEPAD_HOTKEYS) break;
                if (config.hotkeys[index].isLocked) {
                    index++;
                    continue;
                }
                
                cJSON* actionItem = cJSON_GetObjectItem(hotkeyItem, "action");
                cJSON* pinItem = get_hotkey_key_item(hotkeyItem);
                cJSON* holdItem = cJSON_GetObjectItem(hotkeyItem, "isHold");

                if (actionItem && cJSON_IsString(actionItem)) {
                    config.hotkeys[index].action = ConfigUtils::getGamepadHotkeyFromString(actionItem->valuestring);
                }
                if (pinItem && cJSON_IsNumber(pinItem)) {
                    config.hotkeys[index].virtualPin = pinItem->valueint;
                }
                if (holdItem) {
                    config.hotkeys[index].isHold = cJSON_IsTrue(holdItem);
                }
                index++;
            }
        }
        g_configImport.hotkeysSeen = true;
    } else if (section == "profile") {
        cJSON* profileItem = dataItem;
        cJSON* idItem = cJSON_GetObjectItem(profileItem, "id");
        if (!cJSON_IsObject(profileItem) ||
            !json_short_string(idItem, sizeof(config.profiles[0].id))) {
            return create_error_response(request.getCid(), request.getCommand(), 1,
                                         "Profile section has an invalid ID");
        }
        int matchedIndex = -1;
        for (int i = 0; i < NUM_PROFILES; i++) {
            if (strncmp(config.profiles[i].id, idItem->valuestring,
                        sizeof(config.profiles[i].id)) == 0) {
                matchedIndex = i;
                break;
            }
        }
        if (matchedIndex < 0) {
            return create_error_response(request.getCid(), request.getCommand(), 1,
                                         "Profile ID is not supported by this device");
        }
        if (g_configImport.profileSeen[matchedIndex]) {
            return create_error_response(request.getCid(), request.getCommand(), 1,
                                         "Duplicate profile section");
        }
        ProfileCommandHandler::parseProfileJSON(profileItem, &config.profiles[matchedIndex]);
        config.profiles[matchedIndex].enabled = true;
        g_configImport.profileSeen[matchedIndex] = true;
    } else if (section == "screenControl") {
        if (!cJSON_IsObject(dataItem)) {
            return create_error_response(request.getCid(), request.getCommand(), 1,
                                         "Screen section must be an object");
        }
        cJSON* screenControl = dataItem;
        cJSON* item;
        if ((item = cJSON_GetObjectItem(screenControl, "brightness")) && cJSON_IsNumber(item)) {
            int v = item->valueint;
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            config.screenControl.brightness = (uint8_t)v;
        }
        set_screen_style_from_json(config.screenControl, screenControl);
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
        if ((item = cJSON_GetObjectItem(screenControl, "standbyDisplay")) && cJSON_IsString(item)) {
            if (strcmp(item->valuestring, "backgroundImage") == 0) config.screenControl.standbyDisplay = 1;
            else if (strcmp(item->valuestring, "buttonLayout") == 0) config.screenControl.standbyDisplay = 2;
            else config.screenControl.standbyDisplay = 0;
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
        if (featuresOrder && cJSON_IsArray(featuresOrder)) {
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
        }
        g_configImport.screenSeen = true;
    } else if (section == "adcConfig") {
        // Backup schema v1/v2 carried device-local ADC mappings and per-key
        // calibration.  They are intentionally ignored: a mapping is now
        // installed from the server, and calibration must never move between
        // devices.
    } else {
        return create_error_response(request.getCid(), request.getCommand(), 1, "Unknown section");
    }

    return create_success_response(request.getCid(), request.getCommand(), nullptr);
}

DeviceCommandResponse GlobalConfigCommandHandler::handleImportConfigFinish(const DeviceCommandRequest& request) {
    ensure_config_import_initialized();
    if (!g_configImport.active) {
        return create_error_response(request.getCid(), request.getCommand(), 1,
                                     "No configuration import is active");
    }
    if (g_configImport.strict &&
        (!g_configImport.globalSeen || !g_configImport.hotkeysSeen ||
         !g_configImport.screenSeen)) {
        return create_error_response(request.getCid(), request.getCommand(), 1,
                                     "Configuration backup is missing a required section");
    }

    if (g_configImport.replaceProfiles) {
        bool anyProfile = false;
        for (uint8_t i = 0; i < NUM_PROFILES; ++i) {
            if (g_configImport.profileSeen[i]) {
                anyProfile = true;
            } else {
                g_configImport.candidate.profiles[i].enabled = false;
            }
        }
        if (!anyProfile) {
            return create_error_response(request.getCid(), request.getCommand(), 1,
                                         "Configuration backup contains no profiles");
        }
    }
    if (!valid_default_profile(g_configImport.candidate)) {
        return create_error_response(request.getCid(), request.getCommand(), 1,
                                     "Default profile is missing or disabled");
    }

    if (!ConfigUtils::save(g_configImport.candidate)) {
        reset_config_import();
        return create_error_response(request.getCid(), request.getCommand(), 1,
                                     "Failed to save configuration");
    }
    memcpy(&Storage::getInstance().config,
           &g_configImport.candidate,
           sizeof(g_configImport.candidate));
    reset_config_import();
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "message", "Configuration imported successfully");
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse GlobalConfigCommandHandler::handleImportConfigAbort(const DeviceCommandRequest& request) {
    ensure_config_import_initialized();
    const bool wasActive = g_configImport.active;
    reset_config_import();
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddBoolToObject(dataJSON, "aborted", wasActive);
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse GlobalConfigCommandHandler::handleReboot(const DeviceCommandRequest& request) {
    // LOG_INFO("DeviceCommand", "Handling reboot command, cid: %d", request.getCid());
    
    // 创建响应数据
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "message", "System is rebooting");
    
    STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
    STORAGE_MANAGER.saveConfig();

    // 设置延迟重启时间
    DeviceCommandHandler::rebootTick = HAL_GetTick() + 2000;
    DeviceCommandHandler::needReboot = true;
    
    // LOG_INFO("DeviceCommand", "reboot command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse GlobalConfigCommandHandler::handlePushLedsConfig(const DeviceCommandRequest& request) {
    // LOG_INFO("DeviceCommand", "Handling push_leds_config command, cid: %d", request.getCid());
    
    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("DeviceCommand", "push_leds_config: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    // 获取当前默认配置文件作为基础
    const GamepadProfile* currentProfile = STORAGE_MANAGER.getDefaultGamepadProfile();
    if (!currentProfile) {
        LOG_ERROR("DeviceCommand", "push_leds_config: Failed to get current profile");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to get current profile");
    }

    // 创建临时的LED配置，基于当前配置
    LEDProfile tempLedsConfig = currentProfile->ledsConfigs;
    
    // 解析前端传来的配置参数
    cJSON* item;
    
    // LED 启用状态
    if ((item = cJSON_GetObjectItem(params, "ledEnabled"))) {
        tempLedsConfig.ledEnabled = cJSON_IsTrue(item);
    }
    
    // LED 特效类型
    if ((item = cJSON_GetObjectItem(params, "ledsEffectStyle")) 
        && cJSON_IsNumber(item) 
        && item->valueint >= 0 
        && item->valueint < LEDEffect::NUM_EFFECTS) {
        tempLedsConfig.ledEffect = static_cast<LEDEffect>(item->valueint);
    }
    
    // LED 亮度
    if ((item = cJSON_GetObjectItem(params, "ledBrightness")) 
        && cJSON_IsNumber(item)
        && item->valueint >= 0 
        && item->valueint <= 100) {
        tempLedsConfig.ledBrightness = item->valueint;
    }
    
    // LED 动画速度
    if ((item = cJSON_GetObjectItem(params, "ledAnimationSpeed")) 
        && cJSON_IsNumber(item)
        && item->valueint >= 1 
        && item->valueint <= 5) {
        tempLedsConfig.ledAnimationSpeed = item->valueint;
    }
    
    // LED 颜色数组
    cJSON* ledColors = cJSON_GetObjectItem(params, "ledColors");
    if (ledColors && cJSON_IsArray(ledColors) && cJSON_GetArraySize(ledColors) >= 3) {
        cJSON* color1 = cJSON_GetArrayItem(ledColors, 0);
        cJSON* color2 = cJSON_GetArrayItem(ledColors, 1);
        cJSON* color3 = cJSON_GetArrayItem(ledColors, 2);
        
        if (color1 && cJSON_IsString(color1)) {
            sscanf(color1->valuestring, "#%lx", &tempLedsConfig.ledColor1);
        }
        if (color2 && cJSON_IsString(color2)) {
            sscanf(color2->valuestring, "#%lx", &tempLedsConfig.ledColor2);
        }
        if (color3 && cJSON_IsString(color3)) {
            sscanf(color3->valuestring, "#%lx", &tempLedsConfig.ledColor3);
        }
    }

    // 氛围灯配置
    if ((item = cJSON_GetObjectItem(params, "aroundLedEnabled"))) {
        tempLedsConfig.aroundLedEnabled = cJSON_IsTrue(item);
    }

    if ((item = cJSON_GetObjectItem(params, "aroundLedSyncToMainLed"))) {
        tempLedsConfig.aroundLedSyncToMainLed = cJSON_IsTrue(item);
    }       

    if ((item = cJSON_GetObjectItem(params, "aroundLedTriggerByButton"))) {
        tempLedsConfig.aroundLedTriggerByButton = cJSON_IsTrue(item);
    }

    if ((item = cJSON_GetObjectItem(params, "aroundLedEffectStyle"))
        && cJSON_IsNumber(item)
        && item->valueint >= 0
        && item->valueint < AroundLEDEffect::NUM_AROUND_LED_EFFECTS) {
        tempLedsConfig.aroundLedEffect = static_cast<AroundLEDEffect>(item->valueint);
    }

    cJSON* aroundLedColors = cJSON_GetObjectItem(params, "aroundLedColors");
    if (aroundLedColors && cJSON_IsArray(aroundLedColors) && cJSON_GetArraySize(aroundLedColors) >= 3) {
        cJSON* aroundColor1 = cJSON_GetArrayItem(aroundLedColors, 0);
        cJSON* aroundColor2 = cJSON_GetArrayItem(aroundLedColors, 1);
        cJSON* aroundColor3 = cJSON_GetArrayItem(aroundLedColors, 2);
        
        if (aroundColor1 && cJSON_IsString(aroundColor1)) {
            sscanf(aroundColor1->valuestring, "#%lx", &tempLedsConfig.aroundLedColor1);
        }   
        if (aroundColor2 && cJSON_IsString(aroundColor2)) {
            sscanf(aroundColor2->valuestring, "#%lx", &tempLedsConfig.aroundLedColor2);
        }
        if (aroundColor3 && cJSON_IsString(aroundColor3)) {
            sscanf(aroundColor3->valuestring, "#%lx", &tempLedsConfig.aroundLedColor3);
        }
    }

    if ((item = cJSON_GetObjectItem(params, "aroundLedBrightness"))
        && cJSON_IsNumber(item)
        && item->valueint >= 0
        && item->valueint <= LedConfigSafety::kMaxBrightnessPercent) {
        tempLedsConfig.aroundLedBrightness = item->valueint;
    }
    
    if ((item = cJSON_GetObjectItem(params, "aroundLedAnimationSpeed"))
        && cJSON_IsNumber(item)
        && item->valueint >= LedConfigSafety::kMinAnimationSpeed
        && item->valueint <= LedConfigSafety::kMaxAnimationSpeed) {
        tempLedsConfig.aroundLedAnimationSpeed = item->valueint;
    }
    
    // 通过WebConfigLedsManager应用预览配置
    WEBCONFIG_LEDS_MANAGER.applyPreviewConfig(tempLedsConfig);
    // 通过WebConfigBtnsManager启动按键工作器
    WEBCONFIG_BTNS_MANAGER.startButtonWorkers();
    
    // 返回成功响应
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "message", "LED configuration applied successfully for preview");
    
    // LOG_INFO("DeviceCommand", "push_leds_config command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse GlobalConfigCommandHandler::handleClearLedsPreview(const DeviceCommandRequest& request) {
    // LOG_INFO("DeviceCommand", "Handling clear_leds_preview command, cid: %d", request.getCid());
    
    WEBCONFIG_LEDS_MANAGER.clearPreviewConfig();
    WEBCONFIG_BTNS_MANAGER.stopButtonWorkers();
    
    // 返回成功响应
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddStringToObject(dataJSON, "message", "LED preview mode cleared successfully");
    
    // LOG_INFO("DeviceCommand", "clear_leds_preview command completed successfully");
    
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

DeviceCommandResponse GlobalConfigCommandHandler::handle(const DeviceCommandRequest& request) {
    const std::string& command = request.getCommand();
    
    if (command == "get_global_config") {
        return handleGetGlobalConfig(request);
    } else if (command == "update_global_config") {
        return handleUpdateGlobalConfig(request);
    } else if (command == "get_hotkeys_config") {
        return handleGetHotkeysConfig(request);
    } else if (command == "update_hotkeys_config") {
        return handleUpdateHotkeysConfig(request);
    } else if (command == "get_screen_control_config") {
        return handleGetScreenControlConfig(request);
    } else if (command == "update_screen_control_config") {
        return handleUpdateScreenControlConfig(request);
    } else if (command == "export_all_config") {
        return handleExportAllConfig(request);
    } else if (command == "import_all_config") {
        return handleImportAllConfig(request);
    } else if (command == "import_config_begin") {
        return handleImportConfigBegin(request);
    } else if (command == "import_config_part") {
        return handleImportConfigPart(request);
    } else if (command == "import_config_finish") {
        return handleImportConfigFinish(request);
    } else if (command == "import_config_abort") {
        return handleImportConfigAbort(request);
    } else if (command == "reboot") {
        return handleReboot(request);
    } else if (command == "push_leds_config") {
        return handlePushLedsConfig(request);
    } else if (command == "clear_leds_preview") {
        return handleClearLedsPreview(request);
    }
    
    return create_error_response(request.getCid(), command, -1, "Unknown command");
}
