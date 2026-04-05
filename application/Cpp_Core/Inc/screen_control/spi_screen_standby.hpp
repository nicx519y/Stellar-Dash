#ifndef SPI_SCREEN_STANDBY_HPP
#define SPI_SCREEN_STANDBY_HPP

#include <stdint.h>

extern "C" {
#include "st7789.h"
}

void ScreenStandby_Init(uint32_t nowMs, uint32_t inputMask);
void ScreenStandby_Configure(uint8_t standbyDisplay, const char* backgroundImageId, uint32_t bgRgb888, uint32_t fgRgb888);
void ScreenStandby_NotifyInput(uint32_t nowMs, uint32_t inputMask, bool activityEvent, bool wakeEvent);
void ScreenStandby_Tick(uint32_t nowMs);
bool ScreenStandby_IsActive(void);
bool ScreenStandby_Deactivate(void);
void ScreenStandby_Render(ST7789_Handle* lcd, uint32_t inputMask);

#endif
