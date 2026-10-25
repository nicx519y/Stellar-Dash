#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void WS2812B_SetPowerLimit(uint8_t percent);
void SPIST7789_SetPowerLimit(uint8_t percent);
#ifdef __cplusplus
}
#endif
