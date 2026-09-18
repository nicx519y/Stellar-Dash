#include "adc_sampling_policy.h"

#include <cassert>
#include <cstdint>

int main()
{
    // H750 Rev.V: 45 MHz kernel / 2, six 16-bit channels, 32.5 + 8.5 cycles.
    constexpr uint32_t adcClockHz = 22500000u;
    constexpr uint32_t scanCycles = 6u * 41u;
    const uint16_t transitions[] = {1000u, 8000u, 4000u, 8000u, 2000u, 1000u};
    for (const uint16_t rate : transitions) {
        const uint32_t ratio = ADC_InputOversamplingRatio(rate);
        const uint8_t shift = ADC_InputOversamplingShift(rate);
        // Reserve at least 20 us for completion/IRQ handling in each period.
        const uint32_t conversionUs =
            (scanCycles * ratio * 1000000ull + adcClockHz - 1u) / adcClockHz;
        assert(conversionUs + 20u < 1000000u / rate);
        assert(ratio == (rate == 8000u ? 8u : 16u));
        // A constant input must retain its exact 16-bit scale in every mode.
        for (uint32_t sample = 0u; sample <= 65535u; ++sample) {
            assert(((sample * ratio) >> shift) == sample);
        }
    }
    // Regression: the former fixed 16x setting misses the 8-kHz deadline.
    assert(scanCycles * 16ull * 8000u > adcClockHz);
}
