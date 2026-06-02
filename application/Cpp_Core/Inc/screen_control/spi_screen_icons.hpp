#ifndef SPI_SCREEN_ICONS_HPP
#define SPI_SCREEN_ICONS_HPP

#include <stdint.h>

#include "enums.hpp"
#include "connection_manager.hpp"

extern "C" {
#include "st7789.h"
}

void ScreenIcon_DrawInputModeLogo(ST7789_Handle* lcd,
                                  uint16_t x,
                                  uint16_t y,
                                  uint16_t w,
                                  uint16_t h,
                                  InputMode mode,
                                  uint32_t fg,
                                  uint32_t bg);

void ScreenIcon_DrawConnectionMode(ST7789_Handle* lcd,
                                   uint16_t x,
                                   uint16_t y,
                                   uint16_t w,
                                   uint16_t h,
                                   ConnectionMode mode,
                                   ConnectionLinkState state,
                                   uint32_t fg,
                                   uint32_t bg,
                                   uint32_t nowMs);

#endif
