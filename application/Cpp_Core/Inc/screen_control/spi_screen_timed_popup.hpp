#ifndef SPI_SCREEN_TIMED_POPUP_HPP
#define SPI_SCREEN_TIMED_POPUP_HPP

#include <stdint.h>

#include "screen_control/spi_screen_layout.hpp"

extern "C" {
#include "st7789.h"
}

struct ScreenTimedPopup {
    bool visible;
    uint32_t closeAtMs;
    const char* title;
    const char* const* lines;
    uint8_t lineCount;
};

void ScreenTimedPopup_Reset(ScreenTimedPopup* popup);
void ScreenTimedPopup_Show(ScreenTimedPopup* popup, const char* title, const char* const* lines, uint8_t lineCount, uint32_t durationMs, uint32_t nowMs);
void ScreenTimedPopup_Close(ScreenTimedPopup* popup);
void ScreenTimedPopup_Update(ScreenTimedPopup* popup, uint32_t nowMs);
bool ScreenTimedPopup_IsVisible(const ScreenTimedPopup* popup);
void ScreenTimedPopup_Render(const ScreenTimedPopup* popup, ST7789_Handle* lcd, const ScreenUiStyle& style);

#endif
