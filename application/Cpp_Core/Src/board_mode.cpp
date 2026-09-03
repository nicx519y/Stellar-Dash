#include "board_mode.hpp"

#include "board_cfg.h"
#include "stm32h7xx_hal.h"

BoardMode BoardModeManager::readRaw() const
{
    const bool usbN = HAL_GPIO_ReadPin(MODE_USB_N_PORT, MODE_USB_N_PIN) == GPIO_PIN_SET;
    const bool rfN = HAL_GPIO_ReadPin(MODE_RF_N_PORT, MODE_RF_N_PIN) == GPIO_PIN_SET;

    if (!usbN && rfN) {
        return BoardMode::Usb;
    }
    if (usbN && !rfN) {
        return BoardMode::Rf;
    }
    if (usbN && rfN) {
        return BoardMode::CenterOff;
    }
    return BoardMode::Fault;
}

void BoardModeManager::setup()
{
    __HAL_RCC_GPIOI_CLK_ENABLE();

    GPIO_InitTypeDef init = {};
    init.Pin = MODE_USB_N_PIN | MODE_RF_N_PIN;
    init.Mode = GPIO_MODE_INPUT;
    init.Pull = GPIO_PULLUP;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOI, &init);

    candidateMode = readRaw();
    candidateSinceMs = HAL_GetTick();
    stableMode = BoardMode::CenterOff;
    stable = false;
    changed = false;

    /* Establish a deterministic boot selection using the same 20 ms rule. */
    HAL_Delay(BOARD_MODE_DEBOUNCE_MS);
    update(HAL_GetTick());
}

void BoardModeManager::update(uint32_t nowMs)
{
    const BoardMode raw = readRaw();
    if (raw != candidateMode) {
        candidateMode = raw;
        candidateSinceMs = nowMs;
        return;
    }

    if ((uint32_t)(nowMs - candidateSinceMs) < BOARD_MODE_DEBOUNCE_MS) {
        return;
    }

    if (!stable || stableMode != candidateMode) {
        stableMode = candidateMode;
        stable = true;
        changed = true;
    }
}

bool BoardModeManager::consumeChanged()
{
    const bool result = changed;
    changed = false;
    return result;
}
