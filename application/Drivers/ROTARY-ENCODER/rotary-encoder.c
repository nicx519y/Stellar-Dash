#include "rotary-encoder.h"

#include "board_cfg.h"

typedef struct {
    volatile bool initialized;
    volatile uint8_t lastAB;
    volatile int16_t deltaAcc;
    volatile int8_t stepAcc;
    volatile int8_t detentDeltaAcc;

    volatile bool btnDown;
    volatile bool btnPressed;
    volatile bool btnReleased;
    volatile bool btnClicked;
    volatile bool btnLongPressed;

    volatile bool btnRaw;
    volatile bool btnStable;
    volatile uint8_t btnDebounceSamples;
    volatile uint32_t btnDownStartMs;
    volatile bool btnLongFired;

    volatile bool bootIgnoreActive;
    volatile uint32_t bootIgnoreUntilMs;

#if ROTENC_DEBUG_PRINT
    volatile uint8_t debugEvents;
#endif
} RotEnc_State;

static RotEnc_State g_rotenc = {0};

#ifndef ROTENC_BOOT_IGNORE_MS
#define ROTENC_BOOT_IGNORE_MS 1000u
#endif

#ifndef ROTENC_BUTTON_DEBOUNCE_MS
#define ROTENC_BUTTON_DEBOUNCE_MS 10u
#endif

#if ROTENC_BUTTON_DEBOUNCE_MS == 0u || ROTENC_BUTTON_DEBOUNCE_MS > 255u
#error ROTENC_BUTTON_DEBOUNCE_MS must be in the range 1..255
#endif

#if ROTENC_STEPS_PER_DETENT <= 0 || ROTENC_STEPS_PER_DETENT > 127
#error ROTENC_STEPS_PER_DETENT must be in the range 1..127
#endif

#if ROTENC_DEBUG_PRINT
#define ROTENC_DEBUG_BTN_DOWN 0x01u
#define ROTENC_DEBUG_BTN_UP   0x02u
#define ROTENC_DEBUG_BTN_LONG 0x04u
#endif

/*
 * Index is (previous AB << 2) | current AB. A valid quadrature edge changes
 * exactly one bit; two-bit jumps are rejected instead of guessing a direction.
 * The sign matches the legacy decoder's 00->10->11->01->00 direction.
 */
static const int8_t kQuadratureTransition[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0,
};

static uint8_t rotenc_read_ab(void)
{
    const uint8_t a =
        (HAL_GPIO_ReadPin(ROTENC_A_PORT, ROTENC_A_PIN) == GPIO_PIN_SET) ? 1u : 0u;
    const uint8_t b =
        (HAL_GPIO_ReadPin(ROTENC_B_PORT, ROTENC_B_PIN) == GPIO_PIN_SET) ? 1u : 0u;
    return (uint8_t)((a << 1) | b);
}

static bool rotenc_read_button_raw_down(void)
{
    return HAL_GPIO_ReadPin(ROTENC_BTN_PORT, ROTENC_BTN_PIN) == GPIO_PIN_RESET;
}

static uint32_t rotenc_enter_critical(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void rotenc_leave_critical(uint32_t primask)
{
    if (primask == 0u) {
        __enable_irq();
    }
}

static void rotenc_reset_ignored_state(uint8_t ab, bool rawDown)
{
    g_rotenc.lastAB = ab;
    g_rotenc.deltaAcc = 0;
    g_rotenc.stepAcc = 0;
    g_rotenc.detentDeltaAcc = 0;
    g_rotenc.btnDown = false;
    g_rotenc.btnPressed = false;
    g_rotenc.btnReleased = false;
    g_rotenc.btnClicked = false;
    g_rotenc.btnLongPressed = false;
    g_rotenc.btnRaw = rawDown;
    g_rotenc.btnStable = false;
    g_rotenc.btnDebounceSamples = 0u;
    g_rotenc.btnLongFired = false;
}

void RotEnc_Init(void)
{
    uint32_t primask = rotenc_enter_critical();
    g_rotenc.initialized = false;
    __DMB();
    rotenc_leave_critical(primask);

    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    /*
     * PH8 shares EXTI line 8 with PI8 charger interrupt on PCB V2. Keep both
     * encoder phases as ordinary inputs; SysTick samples them every 1 ms so UI
     * rendering, QSPI writes and report catch-up cannot hide complete detents.
     */
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    gpio.Pin = ROTENC_A_PIN;
    HAL_GPIO_Init(ROTENC_A_PORT, &gpio);

    gpio.Pin = ROTENC_B_PIN;
    HAL_GPIO_Init(ROTENC_B_PORT, &gpio);

    gpio.Pin = ROTENC_BTN_PIN;
    HAL_GPIO_Init(ROTENC_BTN_PORT, &gpio);

    const uint8_t ab = rotenc_read_ab();
    const bool rawDown = rotenc_read_button_raw_down();
    const uint32_t nowMs = HAL_GetTick();
    primask = rotenc_enter_critical();

    rotenc_reset_ignored_state(ab, rawDown);
    g_rotenc.btnDownStartMs = nowMs;
    g_rotenc.bootIgnoreActive = true;
    g_rotenc.bootIgnoreUntilMs = nowMs + (uint32_t)ROTENC_BOOT_IGNORE_MS;
#if ROTENC_DEBUG_PRINT
    g_rotenc.debugEvents = 0u;
#endif
    __DMB();
    g_rotenc.initialized = true;

    rotenc_leave_critical(primask);
}

void RotEnc_Tick1msFromISR(void)
{
    if (!g_rotenc.initialized) {
        return;
    }

    const uint32_t nowMs = HAL_GetTick();
    const uint8_t ab = rotenc_read_ab();
    const bool rawDown = rotenc_read_button_raw_down();

    if (g_rotenc.bootIgnoreActive) {
        if ((int32_t)(nowMs - g_rotenc.bootIgnoreUntilMs) < 0) {
            rotenc_reset_ignored_state(ab, rawDown);
            g_rotenc.btnDownStartMs = nowMs;
            return;
        }

        g_rotenc.bootIgnoreActive = false;
        g_rotenc.lastAB = ab;
        g_rotenc.deltaAcc = 0;
        g_rotenc.stepAcc = 0;
        g_rotenc.detentDeltaAcc = 0;
        g_rotenc.btnDown = rawDown;
        g_rotenc.btnPressed = false;
        g_rotenc.btnReleased = false;
        g_rotenc.btnClicked = false;
        g_rotenc.btnLongPressed = false;
        g_rotenc.btnRaw = rawDown;
        g_rotenc.btnStable = rawDown;
        g_rotenc.btnDebounceSamples = ROTENC_BUTTON_DEBOUNCE_MS;
        g_rotenc.btnDownStartMs = nowMs;
        g_rotenc.btnLongFired = false;
    }

    if (ab != g_rotenc.lastAB) {
        const uint8_t changedBits = (uint8_t)(g_rotenc.lastAB ^ ab);
        int8_t step = kQuadratureTransition[(g_rotenc.lastAB << 2) | ab];
        g_rotenc.lastAB = ab;
        if (changedBits == 3u) {
            g_rotenc.stepAcc = 0;
        }
        if (step != 0) {
            step = (int8_t)(step * (int8_t)ROTENC_DIR);

            int32_t nextDelta = (int32_t)g_rotenc.deltaAcc + step;
            if (nextDelta > 32767) nextDelta = 32767;
            if (nextDelta < -32768) nextDelta = -32768;
            g_rotenc.deltaAcc = (int16_t)nextDelta;

            int16_t nextStepAcc = (int16_t)g_rotenc.stepAcc + step;
            if (nextStepAcc > 127) nextStepAcc = 127;
            if (nextStepAcc < -127) nextStepAcc = -127;
            g_rotenc.stepAcc = (int8_t)nextStepAcc;

            while (g_rotenc.stepAcc >= (int8_t)ROTENC_STEPS_PER_DETENT) {
                g_rotenc.stepAcc =
                    (int8_t)(g_rotenc.stepAcc - (int8_t)ROTENC_STEPS_PER_DETENT);
                if (g_rotenc.detentDeltaAcc < 127) {
                    g_rotenc.detentDeltaAcc++;
                }
            }
            while (g_rotenc.stepAcc <= -(int8_t)ROTENC_STEPS_PER_DETENT) {
                g_rotenc.stepAcc =
                    (int8_t)(g_rotenc.stepAcc + (int8_t)ROTENC_STEPS_PER_DETENT);
                if (g_rotenc.detentDeltaAcc > -127) {
                    g_rotenc.detentDeltaAcc--;
                }
            }
        }
    }

    if (rawDown != g_rotenc.btnRaw) {
        g_rotenc.btnRaw = rawDown;
        g_rotenc.btnDebounceSamples = 1u;
    } else if (g_rotenc.btnDebounceSamples < ROTENC_BUTTON_DEBOUNCE_MS) {
        g_rotenc.btnDebounceSamples++;
    }

    if (g_rotenc.btnRaw != g_rotenc.btnStable &&
        g_rotenc.btnDebounceSamples >= ROTENC_BUTTON_DEBOUNCE_MS) {
        g_rotenc.btnStable = g_rotenc.btnRaw;
        if (g_rotenc.btnStable) {
            g_rotenc.btnDown = true;
            g_rotenc.btnPressed = true;
            g_rotenc.btnDownStartMs = nowMs;
            g_rotenc.btnLongFired = false;
#if ROTENC_DEBUG_PRINT
            g_rotenc.debugEvents |= ROTENC_DEBUG_BTN_DOWN;
#endif
        } else if (g_rotenc.btnDown) {
            const uint32_t heldMs = nowMs - g_rotenc.btnDownStartMs;
            g_rotenc.btnDown = false;
            g_rotenc.btnReleased = true;
            if (!g_rotenc.btnLongFired &&
                heldMs < (uint32_t)ROTENC_LONG_PRESS_MS) {
                g_rotenc.btnClicked = true;
            }
            g_rotenc.btnLongFired = false;
#if ROTENC_DEBUG_PRINT
            g_rotenc.debugEvents |= ROTENC_DEBUG_BTN_UP;
#endif
        }
    }

    if (g_rotenc.btnDown && !g_rotenc.btnLongFired &&
        (uint32_t)(nowMs - g_rotenc.btnDownStartMs) >=
            (uint32_t)ROTENC_LONG_PRESS_MS) {
        g_rotenc.btnLongPressed = true;
        g_rotenc.btnLongFired = true;
#if ROTENC_DEBUG_PRINT
        g_rotenc.debugEvents |= ROTENC_DEBUG_BTN_LONG;
#endif
    }
}

void RotEnc_Update(void)
{
#if ROTENC_DEBUG_PRINT
    const uint32_t primask = rotenc_enter_critical();
    const uint8_t events = g_rotenc.debugEvents;
    g_rotenc.debugEvents = 0u;
    rotenc_leave_critical(primask);

    if ((events & ROTENC_DEBUG_BTN_DOWN) != 0u) APP_DBG("ROT BTN down");
    if ((events & ROTENC_DEBUG_BTN_UP) != 0u) APP_DBG("ROT BTN up");
    if ((events & ROTENC_DEBUG_BTN_LONG) != 0u) APP_DBG("ROT BTN long");
#endif
}

int16_t RotEnc_GetDelta(void)
{
    const uint32_t primask = rotenc_enter_critical();
    const int16_t value = g_rotenc.deltaAcc;
    g_rotenc.deltaAcc = 0;
    rotenc_leave_critical(primask);
    return value;
}

int8_t RotEnc_GetDetentDelta(void)
{
    const uint32_t primask = rotenc_enter_critical();
    const int8_t value = g_rotenc.detentDeltaAcc;
    g_rotenc.detentDeltaAcc = 0;
    rotenc_leave_critical(primask);
    return value;
}

bool RotEnc_IsButtonDown(void)
{
    return g_rotenc.btnDown;
}

bool RotEnc_WasButtonPressed(void)
{
    const uint32_t primask = rotenc_enter_critical();
    const bool value = g_rotenc.btnPressed;
    g_rotenc.btnPressed = false;
    rotenc_leave_critical(primask);
    return value;
}

bool RotEnc_WasButtonReleased(void)
{
    const uint32_t primask = rotenc_enter_critical();
    const bool value = g_rotenc.btnReleased;
    g_rotenc.btnReleased = false;
    rotenc_leave_critical(primask);
    return value;
}

bool RotEnc_WasButtonClicked(void)
{
    const uint32_t primask = rotenc_enter_critical();
    const bool value = g_rotenc.btnClicked;
    g_rotenc.btnClicked = false;
    rotenc_leave_critical(primask);
    return value;
}

bool RotEnc_WasButtonLongPressed(void)
{
    const uint32_t primask = rotenc_enter_critical();
    const bool value = g_rotenc.btnLongPressed;
    g_rotenc.btnLongPressed = false;
    rotenc_leave_critical(primask);
    return value;
}
