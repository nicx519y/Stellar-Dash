#include "rf_boot_ready.hpp"

#include <stdio.h>

#include "board_cfg.h"
#include "stm32h7xx_hal.h"

namespace {

#ifndef RF_BOOT_READY_STABLE_MS
#define RF_BOOT_READY_STABLE_MS 2u
#endif

#ifndef RF_BOOT_READY_SETTLE_MS
#define RF_BOOT_READY_SETTLE_MS 10u
#endif

static void enableGpioClock(GPIO_TypeDef* port) {
    if (port == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
    else if (port == GPIOE) __HAL_RCC_GPIOE_CLK_ENABLE();
    else if (port == GPIOF) __HAL_RCC_GPIOF_CLK_ENABLE();
    else if (port == GPIOG) __HAL_RCC_GPIOG_CLK_ENABLE();
    else if (port == GPIOH) __HAL_RCC_GPIOH_CLK_ENABLE();
    else if (port == GPIOI) __HAL_RCC_GPIOI_CLK_ENABLE();
    else if (port == GPIOJ) __HAL_RCC_GPIOJ_CLK_ENABLE();
    else if (port == GPIOK) __HAL_RCC_GPIOK_CLK_ENABLE();
}

static void configureReadyInputPullup() {
    enableGpioClock(RF_BRIDGE_IRQ_GPIO_PORT);

    GPIO_InitTypeDef init = {};
    init.Mode = GPIO_MODE_INPUT;
    init.Pull = GPIO_PULLUP;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    init.Alternate = 0u;
    init.Pin = RF_BRIDGE_IRQ_PIN;
    HAL_GPIO_Init(RF_BRIDGE_IRQ_GPIO_PORT, &init);
}

}

namespace RFBootReady {

bool waitForModuleReady(uint32_t timeoutMs) {
    static bool readySeen = false;
    if (readySeen) {
        return true;
    }

    configureReadyInputPullup();

    const uint32_t start = HAL_GetTick();
    uint32_t lowSince = 0u;

    while ((HAL_GetTick() - start) <= timeoutMs) {
        if (HAL_GPIO_ReadPin(RF_BRIDGE_IRQ_GPIO_PORT, RF_BRIDGE_IRQ_PIN) == GPIO_PIN_RESET) {
            if (lowSince == 0u) {
                lowSince = HAL_GetTick();
            }
            if ((HAL_GetTick() - lowSince) >= RF_BOOT_READY_STABLE_MS) {
                printf("[RF_BOOT][READY] low stable after %u ms\r\n",
                       (unsigned int)(HAL_GetTick() - start));
                HAL_Delay(RF_BOOT_READY_SETTLE_MS);
                readySeen = true;
                return true;
            }
        } else {
            lowSince = 0u;
        }
        HAL_Delay(1u);
    }

    printf("[RF_BOOT][READY_TIMEOUT] timeout=%u ms pin=%u\r\n",
           (unsigned int)timeoutMs,
           (unsigned int)HAL_GPIO_ReadPin(RF_BRIDGE_IRQ_GPIO_PORT, RF_BRIDGE_IRQ_PIN));
    return false;
}

}
