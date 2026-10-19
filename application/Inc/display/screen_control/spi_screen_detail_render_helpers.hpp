#ifndef SPI_SCREEN_DETAIL_RENDER_HELPERS_HPP
#define SPI_SCREEN_DETAIL_RENDER_HELPERS_HPP

#include <stdint.h>

#include "screen_control/spi_screen_layout.hpp"

// A virtual row before item zero; persistent configuration indices stay unchanged.
constexpr uint8_t SCREEN_LIST_BACK = 255u;
inline uint8_t ScreenList_Rotate(uint8_t index, int8_t det, uint8_t count) {
    int next = (index == SCREEN_LIST_BACK ? -1 : int(index)) + det;
    if (next < -1) next = -1;
    if (next >= count) next = int(count) - 1;
    return next < 0 ? SCREEN_LIST_BACK : uint8_t(next);
}

extern "C" {
#include "st7789.h"
}

void ScreenDetailRender_List(ST7789_Handle* lcd, const char* title, const char* const* labels, uint8_t count, uint8_t index, uint8_t selectedConfigIndex, const ScreenUiStyle& style, uint16_t itemH = SPI_SCREEN_DETAIL_ITEM_H, bool animActive = false, int animDir = 0, uint32_t animStartMs = 0u, uint32_t nowMs = 0u, const bool* disabledItems = nullptr, bool showBack = true);
void ScreenDetailRender_Slider(ST7789_Handle* lcd, const char* title, uint8_t value, const ScreenUiStyle& style, uint8_t maxValue = 100u, const char* valueLabel = nullptr);
void ScreenDetailRender_Info(ST7789_Handle* lcd, const char* title, const char* text, const ScreenUiStyle& style);
void ScreenDetailRender_TitleLines(ST7789_Handle* lcd, const char* title, const char* const* lines, uint8_t lineCount, const ScreenUiStyle& style);

#endif
