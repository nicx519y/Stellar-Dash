#ifndef SPI_SCREEN_DETAIL_PAGES_HPP
#define SPI_SCREEN_DETAIL_PAGES_HPP

#include <stdint.h>
#include <stdbool.h>

#include "screen_control/spi_screen_layout.hpp"
#include "screen_control/spi_screen_main_list.hpp"

extern "C" {
#include "st7789.h"
}

enum ScreenDetailKind {
    SCREEN_DETAIL_NONE = 0,
    SCREEN_DETAIL_LIST = 1,
    SCREEN_DETAIL_SLIDER = 2,
    SCREEN_DETAIL_INFO = 3,
};

ScreenDetailKind ScreenDetail_Kind(uint8_t menuId);
uint8_t ScreenDetail_InitIndex(uint8_t menuId);
void ScreenDetail_OnRotate(uint8_t menuId, uint8_t* ioIndex, int8_t det);
bool ScreenDetail_OnConfirm(uint8_t menuId, uint8_t index);
void ScreenDetail_Render(ST7789_Handle* lcd, uint8_t menuId, uint8_t index, const ScreenUiStyle& style);

#endif
