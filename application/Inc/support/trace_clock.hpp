#ifndef HBOX_TRACE_CLOCK_HPP
#define HBOX_TRACE_CLOCK_HPP
#include <stdint.h>

// A DWT-based microsecond clock with real uint32 microsecond wrap (71 minutes),
// rather than dividing CYCCNT before subtraction (which wraps every ~9 seconds).
// Millisecond elapsed time disambiguates multiple DWT wraps during inactivity.
class TraceClock {
    uint32_t lastCycles = 0, lastMs = 0;
    uint64_t elapsedCycles = 0;
    bool initialized = false;
public:
    uint32_t observe(uint32_t cycles, uint32_t ms, uint32_t cyclesPerUs) {
        if (!initialized) {
            initialized = true;
        } else {
            uint64_t delta = static_cast<uint32_t>(cycles - lastCycles);
            const uint64_t estimate = static_cast<uint64_t>(ms - lastMs) * 1000u * cyclesPerUs;
            if (estimate > delta + 0x80000000ULL)
                delta += ((estimate - delta + 0x80000000ULL) >> 32) << 32;
            elapsedCycles += delta;
        }
        lastCycles = cycles;
        lastMs = ms;
        return cyclesPerUs ? static_cast<uint32_t>(elapsedCycles / cyclesPerUs) : 0u;
    }
};
#endif
