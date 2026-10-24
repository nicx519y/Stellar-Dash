#pragma once
#include "config.hpp"
#include <cstring>
#include <cstddef>

// Keep the twelve-byte on-device order and the old color storage unchanged.
// reservedStyle[0..2]: schema, standby enable, LCD language (0 English, 1 Chinese).
namespace ScreenConfig {
constexpr uint8_t kPreviousSchema = 0xA1, kSchema = 0xA3;
constexpr uint8_t kLanguage = 20;
constexpr uint8_t kConnection = 15;
constexpr uint8_t kLed = 13, kPower = 14;
constexpr uint8_t kOrder[] = {kConnection, 3, 0, 1, 2, 11, kLed, 8, kPower, 9, 10};
inline void setDefaultOrder(ScreenControlConfig& sc) {
    memset(sc.featuresOrder, 0xFF, sizeof(sc.featuresOrder));
    memcpy(sc.featuresOrder, kOrder, sizeof(kOrder));
}
constexpr uint32_t kLegacyLedMask = 0xF0u;
inline bool enabled(const ScreenControlConfig& sc) { return sc.reservedStyle[1] == 1u; }
inline void setEnabled(ScreenControlConfig& sc, bool value) { sc.reservedStyle[1] = value ? 1u : 0u; }
inline uint8_t language(const ScreenControlConfig& sc) { return sc.reservedStyle[0] == kSchema && sc.reservedStyle[2] == 1u ? 1u : 0u; }
inline void setLanguage(ScreenControlConfig& sc, uint8_t value) { sc.reservedStyle[2] = value == 1u ? 1u : 0u; }
constexpr uint16_t kTopLevelMask = [] {
    uint16_t mask = 0;
    for (auto id : kOrder) mask |= uint16_t(1u << id);
    return mask;
}();
inline bool isTopLevel(uint16_t id) {
    return id == kLanguage || (id < 16u && (kTopLevelMask & (1u << id)) != 0u);
}
inline void normalize(ScreenControlConfig& sc) {
    const uint8_t savedLanguage = language(sc);
    const bool legacy = sc.reservedStyle[0] != kSchema && sc.reservedStyle[0] != 0xA2 && sc.reservedStyle[0] != kPreviousSchema;
    if (legacy) {
        setEnabled(sc, sc.standbyDisplay == 1u || sc.standbyDisplay == 2u);
        if (sc.featuresMask & kLegacyLedMask) sc.featuresMask |= 1u << kLed;
        else sc.featuresMask &= ~(1u << kLed);
        sc.featuresMask |= 1u << kPower;
        if (sc.currentPageId >= 4u && sc.currentPageId <= 7u) sc.currentPageId = kLed;
    }
    sc.reservedStyle[0] = kSchema;
    setEnabled(sc, enabled(sc));
    setLanguage(sc, savedLanguage);
    memset(sc.reservedStyle + 3, 0, sizeof(sc.reservedStyle) - 3);
    if (sc.standbyDisplay > 2u) { sc.standbyDisplay = 0; setEnabled(sc, false); }
    sc.standbyTimeoutSeconds = normalizeScreenStandbyTimeoutSeconds(sc.standbyTimeoutSeconds);
    sc.featuresMask &= ~kLegacyLedMask;
    sc.featuresMask |= SCREEN_FEATURE_WEB_CONFIG_ENTRY | SCREEN_FEATURE_TRANSPORT_MODE_SWITCH;
    uint8_t order[SCREEN_FEATURE_COUNT];
    memset(order, 0xFF, sizeof(order));
    uint16_t seen = 0;
    uint8_t count = 0;
    auto add = [&](uint8_t id) {
        if (id != kLanguage && isTopLevel(id) && !(seen & (1u << id)) && count < sizeof(order)) {
            order[count++] = id; seen |= uint16_t(1u << id);
        }
    };
    for (auto id : sc.featuresOrder) {
        if (legacy && id >= 4u && id <= 7u) id = kLed;
        add(id);
        if (legacy && id == 8u) add(kPower);
    }
    for (auto id : kOrder) add(id);
    memcpy(sc.featuresOrder, order, sizeof(order));
    if (sc.currentPageId != kLanguage && (!isTopLevel(sc.currentPageId) || !(sc.featuresMask & (1u << sc.currentPageId)))) {
        for (auto id : order) if (isTopLevel(id) && (sc.featuresMask & (1u << id))) {
            sc.currentPageId = id; break;
        }
    }
}
static_assert(sizeof(ScreenControlConfig) == 68, "Screen configuration storage layout");
static_assert(offsetof(ScreenControlConfig, backgroundImageId) == 12, "Screen image offset");
static_assert(offsetof(ScreenControlConfig, featuresOrder) == 52, "Screen order offset");
}
