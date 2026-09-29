#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "stm32h7xx_hal.h"
#include "power_i2c_bus.h"

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

typedef enum {
    BQ25895_JOB_READ, BQ25895_JOB_INIT,
    BQ25895_JOB_CONFIGURE, BQ25895_JOB_VERIFY
} BQ25895_JobKind;
typedef struct {
    BQ25895_JobKind kind;
    BQ25895_InputProfile profile;
    uint8_t index;
    uint8_t values[7];
} BQ25895_Job;
/* One cooperative step; keep job and state alive until DONE/FAILED. */
PowerI2C_Result BQ25895_Step(BQ25895_Handle* handle, BQ25895_Job* job,
                           BQ25895_State* state);

#ifdef __cplusplus
}
#endif
