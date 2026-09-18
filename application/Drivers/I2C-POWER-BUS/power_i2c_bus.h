#pragma once

#include <stdbool.h>

#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Dedicated power-management bus.
 *
 * The latest board connects I2C1 to PB8/PB9 through external 4.7 kOhm
 * pull-ups.  The bus intentionally runs at 100 kHz: neither the charger nor
 * the fuel gauge is on a latency-sensitive path, and the lower rate gives the
 * first PCB spin comfortable rise-time margin.
 */
bool PowerI2C_Init(void);
void PowerI2C_DeInit(void);
I2C_HandleTypeDef* PowerI2C_GetHandle(void);

#ifdef __cplusplus
}
#endif
