#ifndef CYCLE_ELAPSED_HPP
#define CYCLE_ELAPSED_HPP

#include <stdint.h>

constexpr uint32_t ElapsedCycles32(uint32_t startCycles, uint32_t endCycles)
{
    return static_cast<uint32_t>(endCycles - startCycles);
}

constexpr uint32_t CyclesToMicros32(uint32_t elapsedCycles,
                                    uint32_t cyclesPerMicrosecond)
{
    return cyclesPerMicrosecond == 0u
        ? 0u
        : elapsedCycles / cyclesPerMicrosecond;
}

constexpr uint32_t ElapsedMicros32(uint32_t startCycles,
                                   uint32_t endCycles,
                                   uint32_t cyclesPerMicrosecond)
{
    return CyclesToMicros32(ElapsedCycles32(startCycles, endCycles),
                            cyclesPerMicrosecond);
}

#endif
