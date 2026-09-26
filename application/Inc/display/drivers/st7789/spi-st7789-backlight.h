#pragma once
#include <stdint.h>
#include <stdbool.h>

// PWM1 counts 0..ARR inclusive. CCR=ARR+1, not ARR, is fully high.
// The driver's backlight timer uses ARR=999 (fits the CCR register).
static inline uint32_t SPIST7789_BacklightCompare(uint32_t period, uint8_t percent, bool active_low)
{
    if (percent > 100u) percent = 100u;
    const uint32_t ticks = period + 1u;
    const uint32_t on_ticks = ticks * percent / 100u;
    return active_low ? ticks - on_ticks : on_ticks;
}
