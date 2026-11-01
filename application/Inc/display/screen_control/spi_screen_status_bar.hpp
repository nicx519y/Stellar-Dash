#ifndef SPI_SCREEN_STATUS_BAR_HPP
#define SPI_SCREEN_STATUS_BAR_HPP

#include "screen_control/spi_screen_layout.hpp"

extern "C" {
#include "st7789.h"
}

// Pure status graphics; the screen manager supplies live state and colors.
enum class ScreenConnectionIcon : uint8_t { Usb, Wireless, Maintenance };
void ScreenUI_DrawConnectionIcon(ST7789_Handle* lcd, ScreenConnectionIcon icon, const ScreenUiStyle& style);
void ScreenUI_DrawPlatformBadge(ST7789_Handle* lcd, const char* label, uint32_t fg, uint32_t bg);
const char* ScreenUI_ReportRateLabel(uint16_t rateHz);
void ScreenUI_DrawStatusMeta(ST7789_Handle* lcd, const char* performance, uint16_t rateHz, const ScreenUiStyle& style);

#endif
