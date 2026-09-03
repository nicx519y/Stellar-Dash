#include <cassert>
#include <cstdint>

#include "cycle_elapsed.hpp"

int main()
{
    constexpr uint32_t cyclesPerUs = 480u;
    static_assert(ElapsedCycles32(0xFFFFFF00u, 0x000000E0u) == 480u,
                  "32-bit cycle wrap must use unsigned subtraction");
    static_assert(ElapsedMicros32(0xFFFFFF00u,
                                 0x000000E0u,
                                 cyclesPerUs) == 1u,
                  "wrapped DWT elapsed time must convert correctly");

    const uint32_t trigger = 0xFFFFFF00u;
    const uint32_t complete = trigger + 960u;
    const uint32_t ready = complete + 1440u;
    const uint32_t submitted = ready + 1920u;
    const uint32_t adc = ElapsedMicros32(trigger, complete, cyclesPerUs);
    const uint32_t mapping = ElapsedMicros32(complete, ready, cyclesPerUs);
    const uint32_t submit = ElapsedMicros32(ready, submitted, cyclesPerUs);
    assert(adc == 2u && mapping == 3u && submit == 4u);
    assert(adc + mapping + submit == 9u);
    return 0;
}
