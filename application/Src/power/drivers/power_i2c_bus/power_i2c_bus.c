#include "power_i2c_bus.h"

#include <string.h>

#include "board_cfg.h"
#include "stm32h7xx_hal_i2c_ex.h"
#include "stm32h7xx_hal_rcc_ex.h"

/*
 * Cube timing for a 120 MHz I2C123 kernel clock, Standard-mode (100 kHz),
 * analog filter enabled and digital filter disabled.
 *
 * I2C1 is sourced from D2PCLK1 below.  The board clock configuration keeps
 * D2PCLK1 at 120 MHz for both the boot and application clock plans.
 */
#define POWER_I2C_TIMING_120MHZ_100KHZ 0x307075B1u

static I2C_HandleTypeDef g_power_i2c;
static bool g_power_i2c_initialized;
static bool g_async_active;
static uint8_t g_async_phase, g_async_bytes[2];
static uint16_t g_async_written;
static uint32_t g_async_started;
static PowerI2C_Register g_async_register;

static void enable_gpio_clock(GPIO_TypeDef* port)
{
    if (port == GPIOA) { __HAL_RCC_GPIOA_CLK_ENABLE(); }
    else if (port == GPIOB) { __HAL_RCC_GPIOB_CLK_ENABLE(); }
    else if (port == GPIOC) { __HAL_RCC_GPIOC_CLK_ENABLE(); }
    else if (port == GPIOD) { __HAL_RCC_GPIOD_CLK_ENABLE(); }
    else if (port == GPIOE) { __HAL_RCC_GPIOE_CLK_ENABLE(); }
    else if (port == GPIOF) { __HAL_RCC_GPIOF_CLK_ENABLE(); }
    else if (port == GPIOG) { __HAL_RCC_GPIOG_CLK_ENABLE(); }
    else if (port == GPIOH) { __HAL_RCC_GPIOH_CLK_ENABLE(); }
    else if (port == GPIOI) { __HAL_RCC_GPIOI_CLK_ENABLE(); }
    else if (port == GPIOJ) { __HAL_RCC_GPIOJ_CLK_ENABLE(); }
    else if (port == GPIOK) { __HAL_RCC_GPIOK_CLK_ENABLE(); }
}

I2C_HandleTypeDef* PowerI2C_GetHandle(void)
{
    return &g_power_i2c;
}

void HAL_I2C_MspInit(I2C_HandleTypeDef* hi2c)
{
    if (hi2c == NULL || hi2c->Instance != POWER_I2C_INSTANCE) {
        return;
    }

    enable_gpio_clock(POWER_I2C_SCL_PORT);
    enable_gpio_clock(POWER_I2C_SDA_PORT);
    __HAL_RCC_I2C1_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = POWER_I2C_GPIO_AF;
    gpio.Pin = POWER_I2C_SCL_PIN;
    HAL_GPIO_Init(POWER_I2C_SCL_PORT, &gpio);
    gpio.Pin = POWER_I2C_SDA_PIN;
    HAL_GPIO_Init(POWER_I2C_SDA_PORT, &gpio);
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef* hi2c)
{
    if (hi2c == NULL || hi2c->Instance != POWER_I2C_INSTANCE) {
        return;
    }

    __HAL_RCC_I2C1_CLK_DISABLE();
    HAL_GPIO_DeInit(POWER_I2C_SCL_PORT, POWER_I2C_SCL_PIN);
    HAL_GPIO_DeInit(POWER_I2C_SDA_PORT, POWER_I2C_SDA_PIN);
}

bool PowerI2C_Init(void)
{
    if (g_power_i2c_initialized) {
        return true;
    }

    RCC_PeriphCLKInitTypeDef peripheral_clock = {0};
    peripheral_clock.PeriphClockSelection = RCC_PERIPHCLK_I2C1;
    peripheral_clock.I2c123ClockSelection = RCC_I2C123CLKSOURCE_D2PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&peripheral_clock) != HAL_OK) {
        return false;
    }

    memset(&g_power_i2c, 0, sizeof(g_power_i2c));
    g_power_i2c.Instance = POWER_I2C_INSTANCE;
    g_power_i2c.Init.Timing = POWER_I2C_TIMING_120MHZ_100KHZ;
    g_power_i2c.Init.OwnAddress1 = 0;
    g_power_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    g_power_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    g_power_i2c.Init.OwnAddress2 = 0;
    g_power_i2c.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    g_power_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    g_power_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&g_power_i2c) != HAL_OK) {
        return false;
    }
    if (HAL_I2CEx_ConfigAnalogFilter(&g_power_i2c, I2C_ANALOGFILTER_ENABLE) != HAL_OK) {
        HAL_I2C_DeInit(&g_power_i2c);
        return false;
    }
    if (HAL_I2CEx_ConfigDigitalFilter(&g_power_i2c, 0u) != HAL_OK) {
        HAL_I2C_DeInit(&g_power_i2c);
        return false;
    }

    HAL_NVIC_SetPriority(I2C1_EV_IRQn, 7u, 0u);
    HAL_NVIC_SetPriority(I2C1_ER_IRQn, 7u, 0u);
    HAL_NVIC_ClearPendingIRQ(I2C1_EV_IRQn);
    HAL_NVIC_ClearPendingIRQ(I2C1_ER_IRQn);
    HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
    g_power_i2c_initialized = true;
    return true;
}

void PowerI2C_DeInit(void)
{
    if (!g_power_i2c_initialized) {
        return;
    }
    HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
    HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);
    g_async_active = false;
    (void)HAL_I2C_DeInit(&g_power_i2c);
    __HAL_RCC_I2C1_FORCE_RESET();
    __HAL_RCC_I2C1_RELEASE_RESET();
    HAL_NVIC_ClearPendingIRQ(I2C1_EV_IRQn);
    HAL_NVIC_ClearPendingIRQ(I2C1_ER_IRQn);
    memset(&g_power_i2c, 0, sizeof(g_power_i2c));
    g_power_i2c_initialized = false;
}

void I2C1_EV_IRQHandler(void) { HAL_I2C_EV_IRQHandler(&g_power_i2c); }
void I2C1_ER_IRQHandler(void) { HAL_I2C_ER_IRQHandler(&g_power_i2c); }

bool PowerI2C_AsyncBusy(void) { return g_async_active; }

static PowerI2C_Result async_failed(void)
{
    /* Disable/flush IRQ ownership before reusing the persistent buffer. No
     * polling abort, bus-clear pulses or blocking retry on an unhealthy bus. */
    PowerI2C_DeInit();
    (void)PowerI2C_Init();
    return POWER_I2C_FAILED;
}

static PowerI2C_Result async_start(bool write)
{
    g_async_started = HAL_GetTick();
    const HAL_StatusTypeDef result = write
        ? HAL_I2C_Mem_Write_IT(&g_power_i2c, g_async_register.address << 1,
              g_async_register.reg, I2C_MEMADD_SIZE_8BIT,
              g_async_bytes, g_async_register.bytes)
        : HAL_I2C_Mem_Read_IT(&g_power_i2c, g_async_register.address << 1,
              g_async_register.reg, I2C_MEMADD_SIZE_8BIT,
              g_async_bytes, g_async_register.bytes);
    return result == HAL_OK ? POWER_I2C_PENDING : async_failed();
}

PowerI2C_Result PowerI2C_StepRegister(const PowerI2C_Register* op, uint16_t* value)
{
    if (op == NULL || value == NULL || (op->bytes != 1u && op->bytes != 2u)) {
        return POWER_I2C_FAILED;
    }
    if (!g_async_active) {
        if (!g_power_i2c_initialized) { return POWER_I2C_FAILED; }
        g_async_register = *op;
        g_async_phase = 0u;
        g_async_active = true;
        return async_start(false);
    }
    /* A caller may not replace an in-flight register operation. */
    if (op->address != g_async_register.address || op->reg != g_async_register.reg ||
        op->bytes != g_async_register.bytes || op->kind != g_async_register.kind ||
        op->value != g_async_register.value || op->mask != g_async_register.mask ||
        op->verify_mask != g_async_register.verify_mask) {
        return async_failed();
    }
    if (HAL_I2C_GetState(&g_power_i2c) != HAL_I2C_STATE_READY) {
        if ((uint32_t)(HAL_GetTick() - g_async_started) >= 20u) { return async_failed(); }
        return POWER_I2C_PENDING;
    }
    if (HAL_I2C_GetError(&g_power_i2c) != HAL_I2C_ERROR_NONE) { return async_failed(); }
    const uint16_t got = op->bytes == 1u ? g_async_bytes[0] :
        ((uint16_t)g_async_bytes[0] << 8) | g_async_bytes[1];
    if (g_async_phase == 1u) {
        if (op->verify_mask != 0u) {
            g_async_phase = 2u;
            return async_start(false);
        }
        *value = g_async_written;
        g_async_active = false;
        return POWER_I2C_DONE;
    }
    if (g_async_phase == 2u || op->kind == POWER_I2C_VERIFY) {
        const uint16_t mask = g_async_phase == 2u ? op->verify_mask : op->mask;
        g_async_active = false;
        *value = got;
        return (got & mask) == (op->value & mask) ? POWER_I2C_DONE : POWER_I2C_FAILED;
    }
    if (op->kind == POWER_I2C_UPDATE) {
        g_async_written = (got & (uint16_t)~op->mask) | (op->value & op->mask);
        if (g_async_written != got) {
            if (op->bytes == 1u) { g_async_bytes[0] = (uint8_t)g_async_written; }
            else {
                g_async_bytes[0] = (uint8_t)(g_async_written >> 8);
                g_async_bytes[1] = (uint8_t)g_async_written;
            }
            g_async_phase = 1u;
            return async_start(true);
        }
        /* Preserve readback verification even when the register was correct. */
        if (op->verify_mask != 0u) {
            g_async_phase = 2u;
            return async_start(false);
        }
    }
    *value = got;
    g_async_active = false;
    return POWER_I2C_DONE;
}
