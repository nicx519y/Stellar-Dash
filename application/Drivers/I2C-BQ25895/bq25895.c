#include "bq25895.h"

#include <string.h>

enum {
    BQ_REG_INPUT_SOURCE_CONTROL = 0x00,
    BQ_REG_ADC_CONTROL = 0x02,
    BQ_REG_CHARGE_CURRENT = 0x04,
    BQ_REG_PRECHARGE_TERM_CURRENT = 0x05,
    BQ_REG_CHARGE_VOLTAGE = 0x06,
    BQ_REG_TIMER_CONTROL = 0x07,
    BQ_REG_IR_THERMAL_CONTROL = 0x08,
    BQ_REG_SYSTEM_STATUS = 0x0B,
    BQ_REG_FAULT = 0x0C,
    BQ_REG_VINDPM = 0x0D,
    BQ_REG_BATTERY_ADC = 0x0E,
    BQ_REG_VBUS_ADC = 0x11,
    BQ_REG_CHARGE_CURRENT_ADC = 0x12,
    BQ_REG_DPM_STATUS = 0x13,
    BQ_REG_PART_INFORMATION = 0x14,
};

enum {
    BQ_I2C_TIMEOUT_MS = 20,
    BQ_PART_INFORMATION_MASK = 0x3B,
    BQ_PART_INFORMATION_BQ25895_REV1 = 0x39,
    BQ_VINDPM_FORCE_MASK = 0x80,
    BQ_VINDPM_BASE_MV = 2600,
    BQ_VINDPM_STEP_MV = 100,
    BQ_VINDPM_5V_TARGET_MV = 4400,
    BQ_VINDPM_9V_TARGET_MV = 8200,
    BQ_VINDPM_5V_REGISTER =
        BQ_VINDPM_FORCE_MASK |
        ((BQ_VINDPM_5V_TARGET_MV - BQ_VINDPM_BASE_MV) / BQ_VINDPM_STEP_MV),
    BQ_VINDPM_9V_REGISTER =
        BQ_VINDPM_FORCE_MASK |
        ((BQ_VINDPM_9V_TARGET_MV - BQ_VINDPM_BASE_MV) / BQ_VINDPM_STEP_MV),
};

_Static_assert(BQ_VINDPM_5V_TARGET_MV >= 3900,
               "5 V VINDPM target is below the supported range");
_Static_assert(((BQ_VINDPM_5V_TARGET_MV - BQ_VINDPM_BASE_MV) %
                BQ_VINDPM_STEP_MV) == 0,
               "5 V VINDPM target must align to the 100 mV register step");
_Static_assert(((BQ_VINDPM_9V_TARGET_MV - BQ_VINDPM_BASE_MV) %
                BQ_VINDPM_STEP_MV) == 0,
               "9 V VINDPM target must align to the 100 mV register step");
_Static_assert(BQ_VINDPM_5V_REGISTER == 0x92u,
               "4.4 V FORCE_VINDPM encoding changed");
_Static_assert(BQ_VINDPM_9V_REGISTER == 0xB8u,
               "8.2 V FORCE_VINDPM encoding changed");

typedef struct {
    uint8_t reg;
    uint8_t mask;
    uint8_t value;
} BQ25895_ProfileField;

/*
 * Safe 1S2P / 8000 mAh common charging profile.  Input current and VINDPM
 * are selected separately after VBUS ADC identifies a 5 V or 9 V source.
 * BQ25895 DPDM/MaxCharge and ICO are disabled because the board's USB-C/PD
 * negotiation is owned by CH224A and BQ25895 D+/D- are not connected.
 *
 * Common profile:
 *   ICHG 1.6 A
 *   IPRECHG 256 mA, ITERM 64 mA
 *   VREG 4.192 V, BATLOWV 3.0 V, VRECHG 100 mV
 *   watchdog disabled, termination enabled, 8-hour safety timer
 *   BAT_COMP/VCLAMP disabled, TREG 80 C
 */
static const BQ25895_ProfileField k_safe_common_profile[] = {
    {BQ_REG_ADC_CONTROL, 0x1Du, 0x00u},
    {BQ_REG_CHARGE_CURRENT, 0xFFu, 0x19u},
    {BQ_REG_PRECHARGE_TERM_CURRENT, 0xFFu, 0x30u},
    {BQ_REG_CHARGE_VOLTAGE, 0xFFu, 0x5Au},
    {BQ_REG_TIMER_CONTROL, 0xBEu, 0x8Au},
    {BQ_REG_IR_THERMAL_CONTROL, 0xFFu, 0x01u},
};

/* Both qualified input profiles use IINLIM=1.5 A with hardware ILIM kept on. */
static const BQ25895_ProfileField k_input_profile_5v[] = {
    {BQ_REG_INPUT_SOURCE_CONTROL, 0xFFu, 0x5Cu},
    {BQ_REG_VINDPM, 0xFFu, BQ_VINDPM_5V_REGISTER},
};

static const BQ25895_ProfileField k_input_profile_9v[] = {
    {BQ_REG_INPUT_SOURCE_CONTROL, 0xFFu, 0x5Cu},
    {BQ_REG_VINDPM, 0xFFu, BQ_VINDPM_9V_REGISTER},
};

static const BQ25895_ProfileField* input_profile_fields(
    BQ25895_InputProfile input_profile,
    uint32_t* count)
{
    if (count == NULL) {
        return NULL;
    }
    if (input_profile == BQ25895_INPUT_PROFILE_9V_1P5A) {
        *count = (uint32_t)(sizeof(k_input_profile_9v) /
                            sizeof(k_input_profile_9v[0]));
        return k_input_profile_9v;
    }
    *count = (uint32_t)(sizeof(k_input_profile_5v) /
                        sizeof(k_input_profile_5v[0]));
    return k_input_profile_5v;
}

static bool read_register(BQ25895_Handle* handle, uint8_t reg, uint8_t* value)
{
    if (handle == NULL || handle->i2c == NULL || value == NULL) {
        return false;
    }
    return HAL_I2C_Mem_Read(
               handle->i2c,
               (uint16_t)(BQ25895_I2C_ADDRESS_7BIT << 1),
               reg,
               I2C_MEMADD_SIZE_8BIT,
               value,
               1u,
               BQ_I2C_TIMEOUT_MS) == HAL_OK;
}

static bool write_register(BQ25895_Handle* handle, uint8_t reg, uint8_t value)
{
    if (handle == NULL || handle->i2c == NULL) {
        return false;
    }
    return HAL_I2C_Mem_Write(
               handle->i2c,
               (uint16_t)(BQ25895_I2C_ADDRESS_7BIT << 1),
               reg,
               I2C_MEMADD_SIZE_8BIT,
               &value,
               1u,
               BQ_I2C_TIMEOUT_MS) == HAL_OK;
}

static bool update_register(BQ25895_Handle* handle, uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t before = 0;
    if (!read_register(handle, reg, &before)) {
        return false;
    }

    const uint8_t after = (uint8_t)((before & (uint8_t)~mask) | (value & mask));
    if (after != before && !write_register(handle, reg, after)) {
        return false;
    }

    uint8_t verify = 0;
    return read_register(handle, reg, &verify) && ((verify & mask) == (value & mask));
}

bool BQ25895_Init(BQ25895_Handle* handle, I2C_HandleTypeDef* i2c)
{
    if (handle == NULL || i2c == NULL) {
        return false;
    }

    memset(handle, 0, sizeof(*handle));
    handle->i2c = i2c;
    if (HAL_I2C_IsDeviceReady(
            i2c,
            (uint16_t)(BQ25895_I2C_ADDRESS_7BIT << 1),
            2u,
            BQ_I2C_TIMEOUT_MS) != HAL_OK) {
        return false;
    }

    uint8_t part_information = 0;
    if (!read_register(handle, BQ_REG_PART_INFORMATION, &part_information)) {
        return false;
    }
    if ((part_information & BQ_PART_INFORMATION_MASK) !=
        BQ_PART_INFORMATION_BQ25895_REV1) {
        return false;
    }

    handle->online = true;
    return true;
}

static bool apply_profile_fields(BQ25895_Handle* handle,
                                 const BQ25895_ProfileField* fields,
                                 uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        const BQ25895_ProfileField* field = &fields[i];
        if (!update_register(handle, field->reg, field->mask, field->value)) {
            return false;
        }
    }
    return true;
}

static bool verify_profile_fields(BQ25895_Handle* handle,
                                  const BQ25895_ProfileField* fields,
                                  uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        const BQ25895_ProfileField* field = &fields[i];
        uint8_t value = 0;
        if (!read_register(handle, field->reg, &value) ||
            (value & field->mask) != (field->value & field->mask)) {
            return false;
        }
    }
    return true;
}

bool BQ25895_ConfigureSafeProfile(BQ25895_Handle* handle,
                                  BQ25895_InputProfile input_profile)
{
    if (handle == NULL || !handle->online) {
        return false;
    }

    uint32_t input_field_count = 0;
    const BQ25895_ProfileField* input_fields =
        input_profile_fields(input_profile, &input_field_count);
    const bool configured =
        input_fields != NULL &&
        apply_profile_fields(
            handle,
            k_safe_common_profile,
            (uint32_t)(sizeof(k_safe_common_profile) /
                       sizeof(k_safe_common_profile[0]))) &&
        apply_profile_fields(handle, input_fields, input_field_count);
    if (!configured) {
        handle->online = false;
        handle->profile_configured = false;
        return false;
    }

    handle->input_profile = input_profile;
    handle->profile_configured = true;
    return true;
}

bool BQ25895_VerifySafeProfile(BQ25895_Handle* handle)
{
    if (handle == NULL || !handle->online || !handle->profile_configured) {
        return false;
    }

    uint32_t input_field_count = 0;
    const BQ25895_ProfileField* input_fields =
        input_profile_fields(handle->input_profile, &input_field_count);
    return input_fields != NULL &&
           verify_profile_fields(
               handle,
               k_safe_common_profile,
               (uint32_t)(sizeof(k_safe_common_profile) /
                          sizeof(k_safe_common_profile[0]))) &&
           verify_profile_fields(handle, input_fields, input_field_count);
}

bool BQ25895_EnableContinuousAdc(BQ25895_Handle* handle)
{
    /* CONV_RATE=1. No protocol/DPDM bits are changed. */
    return update_register(handle, BQ_REG_ADC_CONTROL, 0x40u, 0x40u);
}

bool BQ25895_ReadState(BQ25895_Handle* handle, BQ25895_State* state)
{
    if (handle == NULL || state == NULL || !handle->online) {
        return false;
    }

    uint8_t status = 0;
    uint8_t fault_latched = 0;
    uint8_t fault_current = 0;
    uint8_t battery_adc = 0;
    uint8_t vbus_adc = 0;
    uint8_t charge_current_adc = 0;
    uint8_t dpm_status = 0;

    /*
     * REG0C is read twice per the device fault-latch behavior.  OR'ing the
     * latched and current values ensures that a transient protection event
     * cannot be hidden before the caller has disabled CE.
     */
    if (!read_register(handle, BQ_REG_SYSTEM_STATUS, &status) ||
        !read_register(handle, BQ_REG_FAULT, &fault_latched) ||
        !read_register(handle, BQ_REG_FAULT, &fault_current) ||
        !read_register(handle, BQ_REG_BATTERY_ADC, &battery_adc) ||
        !read_register(handle, BQ_REG_VBUS_ADC, &vbus_adc) ||
        !read_register(handle, BQ_REG_CHARGE_CURRENT_ADC, &charge_current_adc) ||
        !read_register(handle, BQ_REG_DPM_STATUS, &dpm_status)) {
        handle->online = false;
        return false;
    }

    memset(state, 0, sizeof(*state));
    state->system_status = status;
    state->fault = (uint8_t)(fault_latched | fault_current);
    state->vbus_status = (uint8_t)((status >> 5) & 0x07u);
    state->charge_status = (uint8_t)((status >> 3) & 0x03u);
    state->power_good = (status & 0x04u) != 0u;
    state->vbus_good = (vbus_adc & 0x80u) != 0u;
    state->battery_mv = (uint16_t)(2304u + ((uint16_t)(battery_adc & 0x7Fu) * 20u));
    state->vbus_mv = (uint16_t)(2600u + ((uint16_t)(vbus_adc & 0x7Fu) * 100u));
    state->charge_current_ma = (uint16_t)((uint16_t)(charge_current_adc & 0x7Fu) * 50u);
    /* REG13 exposes only voltage/current DPM status plus the IDPM limit. */
    state->thermal_regulation = (battery_adc & 0x80u) != 0u;
    state->input_voltage_regulation = (dpm_status & 0x80u) != 0u;
    state->input_current_regulation = (dpm_status & 0x40u) != 0u;
    state->input_current_limit_ma =
        (uint16_t)(100u + ((uint16_t)(dpm_status & 0x3Fu) * 50u));
    return true;
}

bool BQ25895_IsFatalFault(uint8_t fault)
{
    /*
     * This first-board policy is deliberately conservative.  It includes
     * JEITA warm/cool indications: the pack requirement is 10..45 C and the
     * external TS network, not firmware derating, is the safety authority.
     */
    return fault != 0u;
}
