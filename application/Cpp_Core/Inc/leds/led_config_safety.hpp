#ifndef HBOX_LED_CONFIG_SAFETY_HPP
#define HBOX_LED_CONFIG_SAFETY_HPP

#include <stdint.h>

namespace LedConfigSafety {

constexpr uint8_t kMinAnimationSpeed = 1u;
constexpr uint8_t kMaxAnimationSpeed = 5u;
constexpr uint8_t kMaxBrightnessPercent = 100u;
constexpr uint8_t kKeyMaxHardwareDrivePercent = 25u;
constexpr uint8_t kAmbientMaxHardwareDrivePercent = 40u;
constexpr uint8_t kLcdMaxHardwareDrivePercent = 60u;
constexpr uint32_t kStartupRampDurationMs = 5000u;
constexpr uint32_t kRippleBaseDurationMs = 3000u;

static inline uint8_t clampBrightnessPercent(uint8_t value)
{
    return value > kMaxBrightnessPercent ? kMaxBrightnessPercent : value;
}

static inline uint8_t scaleGammaPercentToCap(uint8_t gammaPercent,
                                             uint8_t capPercent)
{
    if (gammaPercent > 100u) gammaPercent = 100u;
    if (capPercent > 100u) capPercent = 100u;
    return static_cast<uint8_t>(
        ((uint16_t)gammaPercent * (uint16_t)capPercent + 50u) / 100u);
}

static inline uint8_t interpolateDrive8(uint8_t targetDrive,
                                        uint32_t elapsedMs,
                                        uint32_t durationMs)
{
    if (durationMs == 0u || elapsedMs >= durationMs) {
        return targetDrive;
    }

    return static_cast<uint8_t>(
        ((uint32_t)targetDrive * elapsedMs) / durationMs);
}

static inline uint8_t clampAnimationSpeed(uint8_t value)
{
    if (value < kMinAnimationSpeed) return kMinAnimationSpeed;
    if (value > kMaxAnimationSpeed) return kMaxAnimationSpeed;
    return value;
}

static inline uint32_t rippleDurationMs(uint8_t configuredSpeed)
{
    return kRippleBaseDurationMs / clampAnimationSpeed(configuredSpeed);
}

static inline uint32_t aroundAnimationDurationMs(uint8_t configuredSpeed,
                                                 bool quake)
{
    const uint8_t speed = clampAnimationSpeed(configuredSpeed);
    uint32_t duration = 600u * (7u - (uint32_t)speed);
    if (quake) duration /= 2u;
    return duration;
}

} // namespace LedConfigSafety

#endif // HBOX_LED_CONFIG_SAFETY_HPP
