#ifndef SPI_SCREEN_UI_COMMON_HPP
#define SPI_SCREEN_UI_COMMON_HPP

#include <stdint.h>
#include "screen_control/spi_screen_layout.hpp"

extern "C" {
#include "st7789.h"
}

uint32_t ScreenUI_RGB888(uint32_t v);
uint32_t ScreenUI_BlendToWhite(uint32_t rgb, uint8_t alpha255);
uint32_t ScreenUI_BlendToBlack(uint32_t rgb, uint8_t alpha255);
uint32_t ScreenUI_HighlightFromBg(uint32_t bg, uint8_t amount);
uint32_t ScreenUI_MutedTextForBg(uint32_t fg, uint32_t bg, uint8_t amount);

uint16_t ScreenUI_CharCellW(uint8_t scale);
uint16_t ScreenUI_CharCellH(uint8_t scale);
uint16_t ScreenUI_TextPxW(const char* s, uint8_t scale);
uint16_t ScreenUI_TextPxH(uint8_t scale);

void ScreenUI_DrawStringCenteredInBox(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* s, uint32_t fg, uint32_t bg, uint8_t scale);
void ScreenUI_DrawScrollArrow(ST7789_Handle* lcd, uint16_t cx, uint16_t cy, bool up, uint32_t color);
void ScreenUI_DrawBackIcon(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint32_t color);

enum class ScreenActionIcon : uint8_t {
    None, Open, Back, Confirm, TurnOn, TurnOff, Edit, Close, Exit, Start,
    Retry, Wait, Unavailable, Check
};
ScreenActionIcon ScreenUI_ActionIcon(const char* action);
void ScreenUI_DrawActionIcon(ST7789_Handle* lcd, uint16_t cx, uint16_t cy, ScreenActionIcon icon, uint32_t fg, uint32_t bg);
void ScreenUI_DrawListArrows(ST7789_Handle* lcd, bool canUp, bool canDown, uint32_t fg, uint32_t bg);
ScreenStatusBarLayout ScreenUI_StatusBarLayout(void);

#endif
