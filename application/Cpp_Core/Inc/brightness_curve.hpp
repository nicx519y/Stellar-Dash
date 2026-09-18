#ifndef BRIGHTNESS_CURVE_HPP
#define BRIGHTNESS_CURVE_HPP

#include <stdint.h>

static inline uint8_t BrightnessCurve_ApplyPercent(uint8_t inputPercent)
{
    static constexpr uint8_t kGamma22Percent[101] = {
        0u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 2u,
        2u, 2u, 2u, 3u, 3u, 3u, 4u, 4u, 4u, 5u, 5u, 6u, 6u, 7u, 7u, 8u,
        8u, 9u, 9u, 10u, 11u, 11u, 12u, 13u, 13u, 14u, 15u, 16u, 16u, 17u, 18u, 19u,
        20u, 21u, 22u, 23u, 24u, 25u, 26u, 27u, 28u, 29u, 30u, 31u, 33u, 34u, 35u, 36u,
        37u, 39u, 40u, 41u, 43u, 44u, 46u, 47u, 49u, 50u, 52u, 53u, 55u, 56u, 58u, 60u,
        61u, 63u, 65u, 66u, 68u, 70u, 72u, 74u, 75u, 77u, 79u, 81u, 83u, 85u, 87u, 89u,
        91u, 94u, 96u, 98u, 100u
    };

    if (inputPercent > 100u) {
        inputPercent = 100u;
    }
    return kGamma22Percent[inputPercent];
}

#endif
