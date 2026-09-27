#include "max17048.h"

#include <string.h>

enum {
    MAX_REG_VCELL = 0x02,
    MAX_REG_SOC = 0x04,
    MAX_REG_VERSION = 0x08,
    MAX_REG_CONFIG = 0x0C,
    MAX_REG_STATUS = 0x1A,
    MAX_I2C_TIMEOUT_MS = 20,
};

static bool read_register16(MAX17048_Handle* handle, uint8_t reg, uint16_t* value)
{
    uint8_t bytes[2] = {0, 0};
    if (handle == NULL || handle->i2c == NULL || value == NULL) {
        return false;
    }
    if (HAL_I2C_Mem_Read(
            handle->i2c,
            (uint16_t)(MAX17048_I2C_ADDRESS_7BIT << 1),
            reg,
            I2C_MEMADD_SIZE_8BIT,
            bytes,
            sizeof(bytes),
            MAX_I2C_TIMEOUT_MS) != HAL_OK) {
        return false;
    }
    *value = (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
    return true;
}

static bool write_register16(MAX17048_Handle* handle, uint8_t reg, uint16_t value)
{
    uint8_t bytes[2] = {
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFFu),
    };
    if (handle == NULL || handle->i2c == NULL) {
        return false;
    }
    return HAL_I2C_Mem_Write(
               handle->i2c,
               (uint16_t)(MAX17048_I2C_ADDRESS_7BIT << 1),
               reg,
               I2C_MEMADD_SIZE_8BIT,
               bytes,
               sizeof(bytes),
               MAX_I2C_TIMEOUT_MS) == HAL_OK;
}

bool MAX17048_Init(MAX17048_Handle* handle, I2C_HandleTypeDef* i2c)
{
    if (handle == NULL || i2c == NULL) {
        return false;
    }

    memset(handle, 0, sizeof(*handle));
    handle->i2c = i2c;
    if (HAL_I2C_IsDeviceReady(
            i2c,
            (uint16_t)(MAX17048_I2C_ADDRESS_7BIT << 1),
            2u,
            MAX_I2C_TIMEOUT_MS) != HAL_OK) {
        return false;
    }

    uint16_t version = 0;
    if (!read_register16(handle, MAX_REG_VERSION, &version) ||
        version == 0u || version == 0xFFFFu) {
        return false;
    }

    handle->version = version;
    handle->online = true;
    return true;
}

bool MAX17048_ConfigureAlert(MAX17048_Handle* handle, uint8_t soc_threshold_percent)
{
    if (handle == NULL || !handle->online) {
        return false;
    }
    if (soc_threshold_percent < 1u) {
        soc_threshold_percent = 1u;
    } else if (soc_threshold_percent > 32u) {
        soc_threshold_percent = 32u;
    }

    uint16_t config = 0;
    if (!read_register16(handle, MAX_REG_CONFIG, &config)) {
        handle->online = false;
        return false;
    }

    /* Preserve RCOMP; clear ALRT and program ATHD=(32-threshold). */
    const uint16_t athd = (uint16_t)(32u - soc_threshold_percent);
    config = (uint16_t)((config & (uint16_t)~0x003Fu) | athd);
    if (!write_register16(handle, MAX_REG_CONFIG, config)) {
        handle->online = false;
        return false;
    }

    uint16_t verify = 0;
    if (!read_register16(handle, MAX_REG_CONFIG, &verify) ||
        (verify & 0x001Fu) != athd) {
        handle->online = false;
        return false;
    }
    return true;
}

bool MAX17048_ClearAlert(MAX17048_Handle* handle)
{
    if (handle == NULL || !handle->online) {
        return false;
    }
    uint16_t config = 0;
    if (!read_register16(handle, MAX_REG_CONFIG, &config)) {
        handle->online = false;
        return false;
    }
    config &= (uint16_t)~0x0020u;
    if (!write_register16(handle, MAX_REG_CONFIG, config)) {
        handle->online = false;
        return false;
    }

    /*
     * CONFIG.ALRT releases the active-low pin.  Clear the STATUS cause bits as
     * well so a later threshold crossing can generate a fresh alert; preserve
     * only EnVR, which is a configuration bit in the otherwise W1/W0 status
     * field.
     */
    uint16_t status = 0;
    if (!read_register16(handle, MAX_REG_STATUS, &status) ||
        !write_register16(handle, MAX_REG_STATUS, (uint16_t)(status & 0x4000u))) {
        handle->online = false;
        return false;
    }
    return true;
}

static void decode_state(uint16_t raw_vcell, uint16_t raw_soc,
                         uint16_t status, uint16_t config, MAX17048_State* state)
{
    memset(state, 0, sizeof(*state));
    /* VCELL LSB is 78.125 uV, exactly 5/64 mV. */
    state->cell_mv = (uint16_t)(((uint32_t)raw_vcell * 5u + 32u) / 64u);
    /* SOC is unsigned 8.8 fixed point. */
    uint32_t soc_permille = ((uint32_t)raw_soc * 10u + 128u) / 256u;
    if (soc_permille > 1000u) {
        soc_permille = 1000u;
    }
    state->soc_permille = (uint16_t)soc_permille;
    state->status = status;
    state->alert = (config & 0x0020u) != 0u;
    state->valid = state->cell_mv >= 2500u && state->cell_mv <= 5000u;
}

bool MAX17048_ReadState(MAX17048_Handle* handle, MAX17048_State* state)
{
    if (handle == NULL || state == NULL || !handle->online) {
        return false;
    }

    uint16_t raw_vcell = 0;
    uint16_t raw_soc = 0;
    uint16_t status = 0;
    uint16_t config = 0;
    if (!read_register16(handle, MAX_REG_VCELL, &raw_vcell) ||
        !read_register16(handle, MAX_REG_SOC, &raw_soc) ||
        !read_register16(handle, MAX_REG_STATUS, &status) ||
        !read_register16(handle, MAX_REG_CONFIG, &config)) {
        handle->online = false;
        return false;
    }

    decode_state(raw_vcell, raw_soc, status, config, state);
    return true;
}

PowerI2C_Result MAX17048_Step(MAX17048_Handle* handle, MAX17048_Job* job,
                            MAX17048_State* state)
{
    static const uint8_t state_regs[] = {MAX_REG_VCELL, MAX_REG_SOC, MAX_REG_STATUS, MAX_REG_CONFIG};
    if (handle == NULL || handle->i2c == NULL || job == NULL || state == NULL ||
        (job->kind != MAX17048_JOB_INIT && !handle->online)) {
        return POWER_I2C_FAILED;
    }
    PowerI2C_Register op = {MAX17048_I2C_ADDRESS_7BIT, 0, 2, POWER_I2C_READ, 0, 0, 0};
    const uint8_t count = job->kind == MAX17048_JOB_READ ? 4 : 2;
    if (job->index >= count) { return POWER_I2C_FAILED; }
    if (job->kind == MAX17048_JOB_READ) {
        op.reg = state_regs[job->index];
    } else if (job->kind == MAX17048_JOB_INIT) {
        op.reg = job->index == 0 ? MAX_REG_VERSION : MAX_REG_CONFIG;
        if (job->index == 1) {
            uint8_t threshold = job->alert_soc_percent;
            if (threshold < 1) { threshold = 1; }
            if (threshold > 32) { threshold = 32; }
            op.kind = POWER_I2C_UPDATE;
            op.mask = 0x003Fu;
            op.value = 32u - threshold;
            op.verify_mask = 0x001Fu;
        }
    } else {
        op.reg = job->index == 0 ? MAX_REG_CONFIG : MAX_REG_STATUS;
        op.kind = POWER_I2C_UPDATE;
        op.mask = job->index == 0 ? 0x0020u : (uint16_t)~0x4000u;
    }
    uint16_t value = 0;
    PowerI2C_Result result = PowerI2C_StepRegister(&op, &value);
    if (result == POWER_I2C_DONE && job->kind == MAX17048_JOB_INIT && job->index == 0) {
        if (value == 0 || value == 0xFFFFu) { result = POWER_I2C_FAILED; }
        else { handle->version = value; }
    }
    if (result == POWER_I2C_FAILED) { handle->online = false; return result; }
    if (result == POWER_I2C_PENDING) { return result; }
    if (job->kind == MAX17048_JOB_READ) { job->values[job->index] = value; }
    if (++job->index < count) { return POWER_I2C_PENDING; }
    if (job->kind == MAX17048_JOB_READ) {
        const uint16_t* v = job->values;
        decode_state(v[0], v[1], v[2], v[3], state);
    } else if (job->kind == MAX17048_JOB_INIT) { handle->online = true; }
    return POWER_I2C_DONE;
}
