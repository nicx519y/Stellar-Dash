#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BQ25895_I2C_ADDRESS_7BIT 0x6Au

typedef enum {
    BQ25895_INPUT_PROFILE_5V_1P5A = 0,
    BQ25895_INPUT_PROFILE_9V_1P5A = 1,
} BQ25895_InputProfile;

typedef struct {
    I2C_HandleTypeDef* i2c;
    bool online;
    bool profile_configured;
    BQ25895_InputProfile input_profile;
} BQ25895_Handle;

typedef struct {
    uint8_t system_status;
    uint8_t fault;
    uint8_t vbus_status;
    uint8_t charge_status;
    uint16_t battery_mv;
    uint16_t vbus_mv;
    uint16_t charge_current_ma;
    uint16_t input_current_limit_ma;
    bool power_good;
    bool vbus_good;
    bool thermal_regulation;
    bool input_current_regulation;
    bool input_voltage_regulation;
} BQ25895_State;

bool BQ25895_Init(BQ25895_Handle* handle, I2C_HandleTypeDef* i2c);
bool BQ25895_ConfigureSafeProfile(BQ25895_Handle* handle,
                                  BQ25895_InputProfile input_profile);
bool BQ25895_VerifySafeProfile(BQ25895_Handle* handle);
bool BQ25895_EnableContinuousAdc(BQ25895_Handle* handle);
bool BQ25895_ReadState(BQ25895_Handle* handle, BQ25895_State* state);
bool BQ25895_IsFatalFault(uint8_t fault);

#ifdef __cplusplus
}
#endif
