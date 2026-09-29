#pragma once
#include "power_config.hpp"
#include "cJSON.h"
#include "board_cfg.h"

// Validate before touching any part of the containing configuration.
inline bool valid_power_json(const cJSON* global) {
    const cJSON* power = cJSON_GetObjectItemCaseSensitive(global, "power");
    if (!power) return true;
    if (!cJSON_IsObject(power)) return false;
    const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(power, "autoSleepEnabled");
    return !enabled || cJSON_IsBool(enabled);
}
inline void add_power_json(cJSON* global, const PowerConfig& power) {
    cJSON* object = cJSON_CreateObject();
    cJSON_AddNumberToObject(object, "wakeHoldMs", power.wakeHoldMs);
    cJSON_AddNumberToObject(object, "autoStandbyMs", power.autoStandbyMs);
    cJSON_AddBoolToObject(object, "autoSleepEnabled", power.autoSleepEnabled == 1u);
    cJSON_AddBoolToObject(object, "autoSleepSupported", HBOX_AUTO_SLEEP_ENABLED != 0);
    cJSON_AddItemToObject(global, "power", object);
}
inline void parse_power_json(PowerConfig& power, cJSON* global) {
    cJSON* object = cJSON_GetObjectItemCaseSensitive(global, "power");
    if (!cJSON_IsObject(object)) return;
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, "wakeHoldMs");
    if (cJSON_IsNumber(item))
        power.wakeHoldMs = clamp_power_wake_hold_ms(item->valueint > 0 ? (uint32_t)item->valueint : 3000u);
    item = cJSON_GetObjectItemCaseSensitive(object, "autoStandbyMs");
    if (cJSON_IsNumber(item))
        power.autoStandbyMs = sanitize_power_auto_standby_ms(item->valueint > 0 ? (uint32_t)item->valueint : 0u);
    item = cJSON_GetObjectItemCaseSensitive(object, "autoSleepEnabled");
    if (cJSON_IsBool(item)) power.autoSleepEnabled = cJSON_IsTrue(item) ? 1u : 0u;
    // autoSleepSupported is a firmware capability, never writable/imported.
    sanitize_power_config(power);
}
