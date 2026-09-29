#pragma once
#include <stdint.h>

// LPTIM2 runs continuously until clocks have recovered. Leave several seconds
// of counter headroom after the longest sleep for the bounded recovery waits.
namespace StopTimerTiming {
constexpr uint32_t prescaler = 4u;
constexpr uint32_t maxSleepMs = 5000u;

constexpr uint32_t countsForMs(uint32_t sourceHz, uint32_t ms) {
    return static_cast<uint32_t>((static_cast<uint64_t>(sourceHz) * ms +
                                 prescaler * 1000u - 1u) / (prescaler * 1000u));
}

inline uint32_t elapsedMs(uint16_t begin, uint16_t end, uint32_t sourceHz,
                          uint32_t& fractional) {
    const uint32_t counts = static_cast<uint16_t>(end - begin);
    const uint32_t scaled = fractional + counts * prescaler * 1000u;
    fractional = scaled % sourceHz;
    return scaled / sourceHz;
}
}
