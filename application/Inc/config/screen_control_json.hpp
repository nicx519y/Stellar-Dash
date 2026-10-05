#pragma once
#include "screen_control_config.hpp"
#include "cJSON.h"
#include <cmath>

namespace ScreenConfig {
struct Feature { uint8_t id; const char* key; };
constexpr Feature kFeatures[] = {
    {3, "connectionModeSwitch"}, {0, "inputModeSwitch"}, {1, "profilesSwitch"},
    {2, "socdModeSwitch"}, {11, "buttonsPerformanceQuickSet"}, {13, "ledSetting"},
    {8, "screenBrightnessAdjust"}, {14, "power"}, {9, "webConfigEntry"}, {10, "calibrationModeSwitch"}
};
constexpr const char* kLegacyLedKeys[] = {
    "ledBrightnessAdjust", "ledEffectSwitch", "ambientBrightnessAdjust", "ambientEffectSwitch"
};
inline uint8_t featureId(const char* key) {
    for (auto f : kFeatures) if (!strcmp(key, f.key)) return f.id;
    for (uint8_t i = 0; i < 4; ++i) if (!strcmp(key, kLegacyLedKeys[i])) return uint8_t(4 + i);
    return 255;
}
inline cJSON* toJson(const ScreenControlConfig& value) {
    auto sc = value; normalize(sc);
    auto* json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "brightness", sc.brightness);
    cJSON_AddBoolToObject(json, "standbyEnabled", enabled(sc));
    cJSON_AddStringToObject(json, "standbyDisplay", sc.standbyDisplay == 1 ? "backgroundImage" : sc.standbyDisplay == 2 ? "buttonLayout" : "screenOff");
    cJSON_AddNumberToObject(json, "standbyTimeoutSeconds", sc.standbyTimeoutSeconds);
    cJSON_AddStringToObject(json, "screenStyle", sc.screenStyle == SCREEN_STYLE_LIGHT ? "light" : "dark");
    cJSON_AddStringToObject(json, "backgroundImageId", sc.backgroundImageId);
    cJSON_AddNumberToObject(json, "currentPageId", sc.currentPageId);
    auto* features = cJSON_AddObjectToObject(json, "features");
    for (auto f : kFeatures) cJSON_AddBoolToObject(features, f.key, (sc.featuresMask & (1u << f.id)) != 0);
    auto* order = cJSON_AddArrayToObject(json, "featuresOrder");
    for (auto id : sc.featuresOrder) for (auto f : kFeatures) if (id == f.id) cJSON_AddItemToArray(order, cJSON_CreateString(f.key));
    return json;
}
// Apply atomically; legacy backups may omit the independent switch.
inline const char* applyJson(ScreenControlConfig& target, const cJSON* json, bool importing = false) {
    if (!cJSON_IsObject(json)) return "Invalid screenControl";
    auto sc = target; normalize(sc);
    auto* item = cJSON_GetObjectItemCaseSensitive(json, "standbyEnabled");
    if (item && !cJSON_IsBool(item)) return "Invalid standbyEnabled";
    const auto* enable = item;
    item = cJSON_GetObjectItemCaseSensitive(json, "standbyDisplay");
    bool legacyNone = false;
    if (item) {
        if (!cJSON_IsString(item)) return "Invalid standby display";
        if (!strcmp(item->valuestring, "backgroundImage")) sc.standbyDisplay = 1;
        else if (!strcmp(item->valuestring, "buttonLayout")) sc.standbyDisplay = 2;
        else if (!strcmp(item->valuestring, "screenOff")) sc.standbyDisplay = 0;
        else if (!strcmp(item->valuestring, "none")) { sc.standbyDisplay = 0; legacyNone = true; }
        else return "Invalid standby display";
    }
    if (item && importing && !enable) setEnabled(sc, sc.standbyDisplay != 0);
    if (legacyNone) setEnabled(sc, false);
    item = cJSON_GetObjectItemCaseSensitive(json, "standbyTimeoutSeconds");
    if (item) {
        if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) || item->valuedouble != item->valueint ||
            item->valueint < 0 || item->valueint > 300 || normalizeScreenStandbyTimeoutSeconds(uint16_t(item->valueint)) != item->valueint)
            return "Invalid standby timeout";
        sc.standbyTimeoutSeconds = uint16_t(item->valueint);
    }
    item = cJSON_GetObjectItemCaseSensitive(json, "brightness");
    if (cJSON_IsNumber(item)) sc.brightness = uint8_t(item->valueint < 0 ? 0 : item->valueint > 100 ? 100 : item->valueint);
    item = cJSON_GetObjectItemCaseSensitive(json, "screenStyle");
    if (cJSON_IsString(item)) sc.screenStyle = !strcmp(item->valuestring, "light") ? SCREEN_STYLE_LIGHT : SCREEN_STYLE_DARK;
    else if (importing) {
        const auto* bg = cJSON_GetObjectItemCaseSensitive(json, "backgroundColor");
        const auto* fg = cJSON_GetObjectItemCaseSensitive(json, "textColor");
        auto luma = [](uint32_t rgb) { return ((rgb >> 16) & 255u) * 299u + ((rgb >> 8) & 255u) * 587u + (rgb & 255u) * 114u; };
        if (cJSON_IsNumber(bg) && cJSON_IsNumber(fg)) sc.screenStyle = luma(bg->valueint) > luma(fg->valueint) ? SCREEN_STYLE_LIGHT : SCREEN_STYLE_DARK;
    }
    item = cJSON_GetObjectItemCaseSensitive(json, "backgroundImageId");
    if (cJSON_IsString(item)) { strncpy(sc.backgroundImageId, item->valuestring, sizeof(sc.backgroundImageId) - 1); sc.backgroundImageId[31] = 0; }
    item = cJSON_GetObjectItemCaseSensitive(json, "currentPageId");
    if (cJSON_IsNumber(item)) sc.currentPageId = uint16_t(item->valueint < 0 ? 0 : item->valueint > 65535 ? 65535 : item->valueint);
    const auto* features = cJSON_GetObjectItemCaseSensitive(json, "features");
    bool legacyFeatures = false;
    if (cJSON_IsObject(features)) {
        for (uint8_t i = 0; i < 4; ++i) {
            const auto* b = cJSON_GetObjectItemCaseSensitive(features, kLegacyLedKeys[i]);
            if (b) legacyFeatures = true;
            if (cJSON_IsTrue(b)) sc.featuresMask |= 1u << (4 + i); else sc.featuresMask &= ~(1u << (4 + i));
        }
        legacyFeatures = legacyFeatures && !cJSON_GetObjectItemCaseSensitive(features, "ledSetting");
        for (auto f : kFeatures) {
            const auto* b = cJSON_GetObjectItemCaseSensitive(features, f.key);
            if (b && !cJSON_IsBool(b)) return "Invalid screen feature";
            if (b) { if (cJSON_IsTrue(b)) sc.featuresMask |= 1u << f.id; else sc.featuresMask &= ~(1u << f.id); }
        }
    }
    const auto* order = cJSON_GetObjectItemCaseSensitive(json, "featuresOrder");
    if (order && !cJSON_IsArray(order)) return "Invalid featuresOrder";
    if (order) {
        memset(sc.featuresOrder, 255, sizeof(sc.featuresOrder));
        bool seen[15] = {}; unsigned pos = 0;
        const cJSON* entry;
        cJSON_ArrayForEach(entry, order) if (cJSON_IsString(entry)) {
            auto id = featureId(entry->valuestring);
            if (id < 15 && !seen[id] && pos < SCREEN_FEATURE_COUNT) { sc.featuresOrder[pos++] = id; seen[id] = true; }
        }
    }
    if (sc.currentPageId >= 4u && sc.currentPageId <= 7u) sc.currentPageId = kLed;
    const bool switchValue = legacyNone ? false : enable ? cJSON_IsTrue(enable) : enabled(sc);
    if (legacyFeatures) sc.reservedStyle[0] = 0;
    normalize(sc);
    setEnabled(sc, switchValue);
    target = sc;
    return nullptr;
}
}
