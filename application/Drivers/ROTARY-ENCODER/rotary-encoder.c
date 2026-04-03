#include "rotary-encoder.h"

#include <stdio.h>

#include "board_cfg.h"

typedef struct {
    volatile uint8_t lastA;
    volatile uint8_t lastB;
    volatile int16_t deltaAcc;
    volatile int8_t stepAcc;
    volatile int8_t detentDeltaAcc;

    bool btnDown;
    bool btnPressed;
    bool btnReleased;
    bool btnClicked;
    bool btnLongPressed;

    bool btnStable;
    uint32_t btnLastChangeMs;
    uint32_t btnDownStartMs;
    bool btnLongFired;
} RotEnc_State;

static RotEnc_State g_rotenc = {0};

static uint8_t rotenc_read_ab(void) {

    uint8_t a = (HAL_GPIO_ReadPin(ROTENC_A_PORT, ROTENC_A_PIN) == GPIO_PIN_SET) ? 1u : 0u;
    uint8_t b = (HAL_GPIO_ReadPin(ROTENC_B_PORT, ROTENC_B_PIN) == GPIO_PIN_SET) ? 1u : 0u;

    return (uint8_t)((a << 1) | b);
}

static bool rotenc_read_button_raw_down(void) {
    return (HAL_GPIO_ReadPin(ROTENC_BTN_PORT, ROTENC_BTN_PIN) == GPIO_PIN_RESET);
}

void RotEnc_Init(void) {
    __HAL_RCC_GPIOH_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_Init = {0};
    GPIO_Init.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_Init.Pull = GPIO_PULLUP;
    GPIO_Init.Speed = GPIO_SPEED_FREQ_LOW;

    GPIO_Init.Pin = ROTENC_A_PIN;
    HAL_GPIO_Init(ROTENC_A_PORT, &GPIO_Init);

    GPIO_Init.Pin = ROTENC_B_PIN;
    HAL_GPIO_Init(ROTENC_B_PORT, &GPIO_Init);

    GPIO_Init.Pin = ROTENC_BTN_PIN;
    GPIO_Init.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(ROTENC_BTN_PORT, &GPIO_Init);

    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

    uint8_t ab = rotenc_read_ab();
    g_rotenc.lastA = (uint8_t)((ab >> 1) & 1u);
    g_rotenc.lastB = (uint8_t)(ab & 1u);
    g_rotenc.deltaAcc = 0;
    g_rotenc.stepAcc = 0;
    g_rotenc.detentDeltaAcc = 0;

    g_rotenc.btnDown = rotenc_read_button_raw_down();
    g_rotenc.btnPressed = false;
    g_rotenc.btnReleased = false;
    g_rotenc.btnClicked = false;
    g_rotenc.btnLongPressed = false;
    g_rotenc.btnStable = g_rotenc.btnDown;
    g_rotenc.btnLastChangeMs = HAL_GetTick();
    g_rotenc.btnDownStartMs = g_rotenc.btnLastChangeMs;
    g_rotenc.btnLongFired = false;
}

void RotEnc_OnEdgeIRQ(void) {
    uint8_t ab = rotenc_read_ab();
    uint8_t a = (uint8_t)((ab >> 1) & 1u);
    uint8_t b = (uint8_t)(ab & 1u);

    if (a != g_rotenc.lastA || b != g_rotenc.lastB) {
        int8_t step = (a != g_rotenc.lastB) ? 1 : -1;
        step = (int8_t)(step * (int8_t)ROTENC_DIR);
        g_rotenc.lastA = a;
        g_rotenc.lastB = b;
        g_rotenc.deltaAcc = (int16_t)(g_rotenc.deltaAcc + step);
        int16_t nextStepAcc = (int16_t)g_rotenc.stepAcc + step;
        if (nextStepAcc > 127) nextStepAcc = 127;
        if (nextStepAcc < -127) nextStepAcc = -127;
        g_rotenc.stepAcc = (int8_t)nextStepAcc;

        while (g_rotenc.stepAcc >= (int8_t)ROTENC_STEPS_PER_DETENT) {
            g_rotenc.stepAcc = (int8_t)(g_rotenc.stepAcc - (int8_t)ROTENC_STEPS_PER_DETENT);
            if (g_rotenc.detentDeltaAcc < 127) g_rotenc.detentDeltaAcc++;
        }
        while (g_rotenc.stepAcc <= -(int8_t)ROTENC_STEPS_PER_DETENT) {
            g_rotenc.stepAcc = (int8_t)(g_rotenc.stepAcc + (int8_t)ROTENC_STEPS_PER_DETENT);
            if (g_rotenc.detentDeltaAcc > -127) g_rotenc.detentDeltaAcc--;
        }
    }
}

void RotEnc_Update(void) {
    const uint32_t nowMs = HAL_GetTick();
    const bool rawDown = rotenc_read_button_raw_down();
    if (rawDown != g_rotenc.btnStable) {
        if ((uint32_t)(nowMs - g_rotenc.btnLastChangeMs) >= 10u) {
            g_rotenc.btnStable = rawDown;
            g_rotenc.btnLastChangeMs = nowMs;

            if (g_rotenc.btnStable && !g_rotenc.btnDown) {
                g_rotenc.btnDown = true;
                g_rotenc.btnPressed = true;
                g_rotenc.btnDownStartMs = nowMs;
                g_rotenc.btnLongFired = false;
#if ROTENC_DEBUG_PRINT
                APP_DBG("ROT BTN down");
#endif
            } else if (!g_rotenc.btnStable && g_rotenc.btnDown) {
                uint32_t heldMs = nowMs - g_rotenc.btnDownStartMs;
                g_rotenc.btnDown = false;
                g_rotenc.btnReleased = true;
                if (!g_rotenc.btnLongFired && heldMs < (uint32_t)ROTENC_LONG_PRESS_MS) {
                    g_rotenc.btnClicked = true;
                }
                g_rotenc.btnLongFired = false;
#if ROTENC_DEBUG_PRINT
                APP_DBG("ROT BTN up");
#endif
            }
        }
    } else {
        g_rotenc.btnLastChangeMs = nowMs;
    }

    if (g_rotenc.btnDown && !g_rotenc.btnLongFired) {
        if ((uint32_t)(nowMs - g_rotenc.btnDownStartMs) >= (uint32_t)ROTENC_LONG_PRESS_MS) {
            g_rotenc.btnLongPressed = true;
            g_rotenc.btnLongFired = true;
#if ROTENC_DEBUG_PRINT
            APP_DBG("ROT BTN long");
#endif
        }
    }
}

int16_t RotEnc_GetDelta(void) {
    __disable_irq();
    int16_t v = g_rotenc.deltaAcc;
    g_rotenc.deltaAcc = 0;
    __enable_irq();
    return v;
}

int8_t RotEnc_GetDetentDelta(void) {
    __disable_irq();
    int8_t v = g_rotenc.detentDeltaAcc;
    g_rotenc.detentDeltaAcc = 0;
    __enable_irq();
    return v;
}

bool RotEnc_IsButtonDown(void) {
    return g_rotenc.btnDown;
}

bool RotEnc_WasButtonPressed(void) {
    bool v = g_rotenc.btnPressed;
    g_rotenc.btnPressed = false;
    return v;
}

bool RotEnc_WasButtonReleased(void) {
    bool v = g_rotenc.btnReleased;
    g_rotenc.btnReleased = false;
    return v;
}

bool RotEnc_WasButtonClicked(void) {
    bool v = g_rotenc.btnClicked;
    g_rotenc.btnClicked = false;
    return v;
}

bool RotEnc_WasButtonLongPressed(void) {
    bool v = g_rotenc.btnLongPressed;
    g_rotenc.btnLongPressed = false;
    return v;
}
