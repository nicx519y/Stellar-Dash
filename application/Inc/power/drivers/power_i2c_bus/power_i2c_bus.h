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

/* One persistent, main-loop-owned operation; IRQs only advance the HAL transfer.
 * Values are host-order; two-byte device registers use big-endian wire order. */
typedef enum { POWER_I2C_PENDING, POWER_I2C_DONE, POWER_I2C_FAILED } PowerI2C_Result;
typedef enum { POWER_I2C_READ, POWER_I2C_UPDATE, POWER_I2C_VERIFY } PowerI2C_Kind;
typedef struct {
    uint8_t address, reg, bytes;
    PowerI2C_Kind kind;
    uint16_t value, mask, verify_mask;
} PowerI2C_Register;
PowerI2C_Result PowerI2C_StepRegister(const PowerI2C_Register* operation, uint16_t* value);
bool PowerI2C_AsyncBusy(void);

#ifdef __cplusplus
}
#endif
