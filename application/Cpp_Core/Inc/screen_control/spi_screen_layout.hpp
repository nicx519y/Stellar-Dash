#ifndef SPI_SCREEN_LAYOUT_HPP
#define SPI_SCREEN_LAYOUT_HPP

#include <stdint.h>

#define SPI_SCREEN_LEFT_BAR_W 40u
#define SPI_SCREEN_RIGHT_BAR_W 40u
#define SPI_SCREEN_Y_OFFSET 34u
#define SPI_SCREEN_STATUS_BAR_TEXT_SCALE 2u
#define SPI_SCREEN_MENU_TEXT_SCALE 2u
#define SPI_SCREEN_MENU_ITEM_H 34u
#define SPI_SCREEN_OK_FLASH_MS 200u
#define SPI_SCREEN_SLIDER_STEP 2
#define SPI_SCREEN_DETAIL_ITEM_H 28u
#define SPI_SCREEN_ANIM_MS 160u

struct ScreenUiStyle {
    uint32_t bg;
    uint32_t text;
    uint32_t selBg;
    uint32_t okBg;
};

uint32_t ScreenUI_EaseOutCubic(uint32_t t, uint32_t d);

#endif
