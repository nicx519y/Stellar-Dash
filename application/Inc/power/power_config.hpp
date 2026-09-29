#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Persistent layout: reuse the high half of the legacy 32-bit wake time.
struct PowerConfig {
    uint16_t wakeHoldMs;
    uint8_t autoSleepEnabled;
    uint8_t reserved;
    uint32_t autoStandbyMs;
};
static_assert(sizeof(PowerConfig) == 8, "PowerConfig storage size");
static_assert(offsetof(PowerConfig, wakeHoldMs) == 0, "legacy wake offset");
static_assert(offsetof(PowerConfig, autoSleepEnabled) == 2, "enable offset");
static_assert(offsetof(PowerConfig, reserved) == 3, "reserved offset");
static_assert(offsetof(PowerConfig, autoStandbyMs) == 4, "timeout storage offset");

inline uint16_t clamp_power_wake_hold_ms(uint32_t value) {
    if (value < 1000u) return 1000u;
    if (value > 5000u) return 5000u;
    return static_cast<uint16_t>((value / 1000u) * 1000u);
}
inline uint32_t sanitize_power_auto_standby_ms(uint32_t value) {
    switch (value) {
    case 10000u: case 30000u: case 60000u: case 120000u: case 300000u: return value;
    default: return 300000u;
    }
}
inline void init_power_defaults(PowerConfig& power) { power = {3000u, 0u, 0u, 300000u}; }
inline void sanitize_power_config(PowerConfig& power) {
    power.wakeHoldMs = clamp_power_wake_hold_ms(power.wakeHoldMs);
    power.autoSleepEnabled = power.autoSleepEnabled == 1u ? 1u : 0u;
    power.reserved = 0u;
    power.autoStandbyMs = sanitize_power_auto_standby_ms(power.autoStandbyMs);
}
inline void migrate_legacy_power_config(PowerConfig& power) {
    uint32_t legacyWake;
    memcpy(&legacyWake, &power, sizeof(legacyWake));
    power.wakeHoldMs = clamp_power_wake_hold_ms(legacyWake);
    power.autoSleepEnabled = 0u;
    power.reserved = 0u;
    power.autoStandbyMs = sanitize_power_auto_standby_ms(power.autoStandbyMs);
}
