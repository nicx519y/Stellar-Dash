#include "rotary-encoder.h"

typedef struct {
    uint8_t prevAB;
    int16_t deltaAcc;
    int16_t detentAcc;
    int8_t detentDeltaAcc;

    bool btnDown;
    bool btnPressed;
    bool btnReleased;
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
    GPIO_Init.Mode = GPIO_MODE_INPUT;
    GPIO_Init.Pull = GPIO_PULLUP;
    GPIO_Init.Speed = GPIO_SPEED_FREQ_LOW;

    GPIO_Init.Pin = ROTENC_A_PIN;
    HAL_GPIO_Init(ROTENC_A_PORT, &GPIO_Init);

    GPIO_Init.Pin = ROTENC_B_PIN;
    HAL_GPIO_Init(ROTENC_B_PORT, &GPIO_Init);

    GPIO_Init.Pin = ROTENC_BTN_PIN;
    HAL_GPIO_Init(ROTENC_BTN_PORT, &GPIO_Init);

    g_rotenc.prevAB = rotenc_read_ab();
    g_rotenc.deltaAcc = 0;
    g_rotenc.detentAcc = 0;
    g_rotenc.detentDeltaAcc = 0;

    g_rotenc.btnDown = rotenc_read_button_raw_down();
    g_rotenc.btnPressed = false;
    g_rotenc.btnReleased = false;
    g_rotenc.btnLongPressed = false;
    g_rotenc.btnStable = g_rotenc.btnDown;
    g_rotenc.btnLastChangeMs = HAL_GetTick();
    g_rotenc.btnDownStartMs = g_rotenc.btnLastChangeMs;
    g_rotenc.btnLongFired = false;
}

void RotEnc_Update(void) {
    static const int8_t kDeltaLUT[16] = {
        0, -1, 1, 0,
        1, 0, 0, -1,
        -1, 0, 0, 1,
        0, 1, -1, 0,
    };

    uint8_t ab = rotenc_read_ab();
    uint8_t idx = (uint8_t)((g_rotenc.prevAB << 2) | ab);
    int8_t d = kDeltaLUT[idx];
    if (d != 0) {
        int16_t dd = (int16_t)(d * (int16_t)ROTENC_DIR);
        g_rotenc.deltaAcc = (int16_t)(g_rotenc.deltaAcc + dd);
        g_rotenc.detentAcc = (int16_t)(g_rotenc.detentAcc + dd);
        while (g_rotenc.detentAcc >= (int16_t)ROTENC_STEPS_PER_DETENT) {
            g_rotenc.detentAcc = (int16_t)(g_rotenc.detentAcc - (int16_t)ROTENC_STEPS_PER_DETENT);
            if (g_rotenc.detentDeltaAcc < 127) g_rotenc.detentDeltaAcc++;
        }
        while (g_rotenc.detentAcc <= -(int16_t)ROTENC_STEPS_PER_DETENT) {
            g_rotenc.detentAcc = (int16_t)(g_rotenc.detentAcc + (int16_t)ROTENC_STEPS_PER_DETENT);
            if (g_rotenc.detentDeltaAcc > -127) g_rotenc.detentDeltaAcc--;
        }
    }
    g_rotenc.prevAB = ab;

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
            } else if (!g_rotenc.btnStable && g_rotenc.btnDown) {
                g_rotenc.btnDown = false;
                g_rotenc.btnReleased = true;
                g_rotenc.btnLongFired = false;
            }
        }
    } else {
        g_rotenc.btnLastChangeMs = nowMs;
    }

    if (g_rotenc.btnDown && !g_rotenc.btnLongFired) {
        if ((uint32_t)(nowMs - g_rotenc.btnDownStartMs) >= (uint32_t)ROTENC_LONG_PRESS_MS) {
            g_rotenc.btnLongPressed = true;
            g_rotenc.btnLongFired = true;
        }
    }
}

int16_t RotEnc_GetDelta(void) {
    int16_t v = g_rotenc.deltaAcc;
    g_rotenc.deltaAcc = 0;
    return v;
}

int8_t RotEnc_GetDetentDelta(void) {
    int8_t v = g_rotenc.detentDeltaAcc;
    g_rotenc.detentDeltaAcc = 0;
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

bool RotEnc_WasButtonLongPressed(void) {
    bool v = g_rotenc.btnLongPressed;
    g_rotenc.btnLongPressed = false;
    return v;
}
