#ifndef ADC_SAMPLING_POLICY_H
#define ADC_SAMPLING_POLICY_H

#include <stdint.h>

/* H750 Rev.V divides the 45 MHz kernel clock by two. Six channels at
 * 32.5 sampling + 8.5 conversion cycles take ~175 us with 16x averaging,
 * so the 125-us (8 kHz) period needs 8x averaging (~88 us before IRQ work).
 * Shift by log2(ratio) to retain the calibrated ADC value scale. */
static inline uint8_t ADC_InputOversamplingShift(uint16_t rateHz)
{
    return rateHz == 8000u ? 3u : 4u;
}

static inline uint32_t ADC_InputOversamplingRatio(uint16_t rateHz)
{
    return 1u << ADC_InputOversamplingShift(rateHz);
}

#endif
