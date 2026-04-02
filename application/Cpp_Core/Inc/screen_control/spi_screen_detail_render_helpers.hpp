#ifndef SPI_SCREEN_DETAIL_RENDER_HELPERS_HPP
#define SPI_SCREEN_DETAIL_RENDER_HELPERS_HPP

#include <stdint.h>

#include "screen_control/spi_screen_layout.hpp"

extern "C" {
#include "st7789.h"
}

void ScreenDetailRender_List(ST7789_Handle* lcd, const char* title, const char* const* labels, uint8_t count, uint8_t index, const ScreenUiStyle& style);
void ScreenDetailRender_Slider(ST7789_Handle* lcd, const char* title, uint8_t value, const ScreenUiStyle& style);
void ScreenDetailRender_Info(ST7789_Handle* lcd, const char* title, const char* text, const ScreenUiStyle& style);

#endif
