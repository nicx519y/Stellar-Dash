#ifndef DELAY_TIMER_H
#define DELAY_TIMER_H

#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

void DelayTimer_Init(void);
void DelayTimer_Start(uint16_t delay_us);
void DelayTimer_Stop(void);
void DelayTimer_SetCallback(void (*callback)(void));

#ifdef __cplusplus
}
#endif

#endif
