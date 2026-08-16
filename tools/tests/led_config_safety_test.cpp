#include <cassert>
#include <cstdint>
#include <iostream>

#include "brightness_curve.hpp"
#include "leds/led_config_safety.hpp"

int main()
{
    using namespace LedConfigSafety;

    assert(clampBrightnessPercent(0u) == 0u);
    assert(clampBrightnessPercent(100u) == 100u);
    assert(clampBrightnessPercent(255u) == 100u);
    assert(scaleGammaPercentToCap(0u, kKeyMaxHardwareDrivePercent) == 0u);
    assert(scaleGammaPercentToCap(100u, kKeyMaxHardwareDrivePercent) == 25u);
    assert(scaleGammaPercentToCap(100u, kAmbientMaxHardwareDrivePercent) == 40u);
    assert(scaleGammaPercentToCap(100u, kLcdMaxHardwareDrivePercent) == 60u);
    assert(scaleGammaPercentToCap(22u, kKeyMaxHardwareDrivePercent) == 6u);
    assert(scaleGammaPercentToCap(22u, kAmbientMaxHardwareDrivePercent) == 9u);
    assert(scaleGammaPercentToCap(BrightnessCurve_ApplyPercent(0u),
                                  kKeyMaxHardwareDrivePercent) == 0u);
    assert(scaleGammaPercentToCap(BrightnessCurve_ApplyPercent(50u),
                                  kKeyMaxHardwareDrivePercent) == 6u);
    assert(scaleGammaPercentToCap(BrightnessCurve_ApplyPercent(100u),
                                  kKeyMaxHardwareDrivePercent) == 25u);
    assert(scaleGammaPercentToCap(BrightnessCurve_ApplyPercent(0u),
                                  kAmbientMaxHardwareDrivePercent) == 0u);
    assert(scaleGammaPercentToCap(BrightnessCurve_ApplyPercent(50u),
                                  kAmbientMaxHardwareDrivePercent) == 9u);
    assert(scaleGammaPercentToCap(BrightnessCurve_ApplyPercent(100u),
                                  kAmbientMaxHardwareDrivePercent) == 40u);
    assert(scaleGammaPercentToCap(BrightnessCurve_ApplyPercent(0u),
                                  kLcdMaxHardwareDrivePercent) == 0u);
    assert(scaleGammaPercentToCap(BrightnessCurve_ApplyPercent(50u),
                                  kLcdMaxHardwareDrivePercent) == 13u);
    assert(scaleGammaPercentToCap(BrightnessCurve_ApplyPercent(100u),
                                  kLcdMaxHardwareDrivePercent) == 60u);

    assert(clampAnimationSpeed(0u) == 1u);
    assert(clampAnimationSpeed(1u) == 1u);
    assert(clampAnimationSpeed(5u) == 5u);
    assert(clampAnimationSpeed(255u) == 5u);

    assert(rippleDurationMs(0u) == 3000u);
    assert(rippleDurationMs(5u) == 600u);
    assert(rippleDurationMs(255u) == 600u);

    assert(aroundAnimationDurationMs(0u, false) == 3600u);
    assert(aroundAnimationDurationMs(5u, false) == 1200u);
    assert(aroundAnimationDurationMs(255u, true) == 600u);

    for (uint16_t value = 0u; value <= 255u; ++value) {
        const uint8_t input = static_cast<uint8_t>(value);
        assert(scaleGammaPercentToCap(input, kKeyMaxHardwareDrivePercent) <=
               kKeyMaxHardwareDrivePercent);
        assert(scaleGammaPercentToCap(input, kAmbientMaxHardwareDrivePercent) <=
               kAmbientMaxHardwareDrivePercent);
        assert(rippleDurationMs(input) > 0u);
        assert(aroundAnimationDurationMs(input, false) > 0u);
        assert(aroundAnimationDurationMs(input, true) > 0u);
    }

    std::cout << "LED config safety tests passed\n";
    return 0;
}
