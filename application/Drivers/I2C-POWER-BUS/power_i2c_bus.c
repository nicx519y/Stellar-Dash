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

    g_power_i2c_initialized = true;
    return true;
}

void PowerI2C_DeInit(void)
{
    if (!g_power_i2c_initialized) {
        return;
    }
    (void)HAL_I2C_DeInit(&g_power_i2c);
    memset(&g_power_i2c, 0, sizeof(g_power_i2c));
    g_power_i2c_initialized = false;
}
