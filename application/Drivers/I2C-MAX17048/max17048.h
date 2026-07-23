#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX17048_I2C_ADDRESS_7BIT 0x36u

typedef struct {
    I2C_HandleTypeDef* i2c;
    bool online;
    uint16_t version;
} MAX17048_Handle;

typedef struct {
    uint16_t cell_mv;
    uint16_t soc_permille;
    uint16_t status;
    bool alert;
    bool valid;
} MAX17048_State;

bool MAX17048_Init(MAX17048_Handle* handle, I2C_HandleTypeDef* i2c);
bool MAX17048_ConfigureAlert(MAX17048_Handle* handle, uint8_t soc_threshold_percent);
bool MAX17048_ClearAlert(MAX17048_Handle* handle);
bool MAX17048_ReadState(MAX17048_Handle* handle, MAX17048_State* state);

#ifdef __cplusplus
}
#endif
