#ifndef ROTARY_ENCODER_H
#define ROTARY_ENCODER_H

#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROTENC_A_PORT GPIOH
#define ROTENC_A_PIN GPIO_PIN_8

#define ROTENC_B_PORT GPIOH
#define ROTENC_B_PIN GPIO_PIN_9

#define ROTENC_BTN_PORT GPIOH
#define ROTENC_BTN_PIN GPIO_PIN_7

#ifndef ROTENC_DIR
#define ROTENC_DIR 1
#endif

#ifndef ROTENC_STEPS_PER_DETENT
#define ROTENC_STEPS_PER_DETENT 2
#endif

#ifndef ROTENC_LONG_PRESS_MS
#define ROTENC_LONG_PRESS_MS 1000u
#endif

#ifndef ROTENC_DEBUG_PRINT
#define ROTENC_DEBUG_PRINT 1
#endif

void RotEnc_Init(void);
void RotEnc_Update(void);
void RotEnc_OnEdgeIRQ(void);

int16_t RotEnc_GetDelta(void);
int8_t RotEnc_GetDetentDelta(void);

bool RotEnc_IsButtonDown(void);
bool RotEnc_WasButtonPressed(void);
bool RotEnc_WasButtonReleased(void);
bool RotEnc_WasButtonClicked(void);
bool RotEnc_WasButtonLongPressed(void);

#ifdef __cplusplus
}
#endif

#endif
