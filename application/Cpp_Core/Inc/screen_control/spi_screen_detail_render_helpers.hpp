#ifndef SPI_SCREEN_DETAIL_RENDER_HELPERS_HPP
#define SPI_SCREEN_DETAIL_RENDER_HELPERS_HPP

#include <stdint.h>

#include "screen_control/spi_screen_layout.hpp"

extern "C" {
#include "st7789.h"
}

void ScreenDetailRender_List(ST7789_Handle* lcd, const char* title, const char* const* labels, uint8_t count, uint8_t index, uint8_t selectedConfigIndex, const ScreenUiStyle& style, uint16_t itemH = SPI_SCREEN_DETAIL_ITEM_H, bool animActive = false, int animDir = 0, uint32_t animStartMs = 0u, uint32_t nowMs = 0u);
void ScreenDetailRender_Slider(ST7789_Handle* lcd, const char* title, uint8_t value, const ScreenUiStyle& style);
void ScreenDetailRender_Info(ST7789_Handle* lcd, const char* title, const char* text, const ScreenUiStyle& style);
void ScreenDetailRender_TitleLines(ST7789_Handle* lcd, const char* title, const char* const* lines, uint8_t lineCount, const ScreenUiStyle& style);

#endif
