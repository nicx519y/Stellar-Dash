#ifndef SPI_SCREEN_LAYOUT_HPP
#define SPI_SCREEN_LAYOUT_HPP

#include <stdint.h>
#include "screen_font.h"

#define SPI_SCREEN_LEFT_BAR_W 40u
#define SPI_SCREEN_RIGHT_BAR_W 40u
#define SPI_SCREEN_Y_OFFSET 34u
#define SPI_SCREEN_STATUS_BAR_TEXT_SCALE SCREEN_FONT_TITLE
// Native strikes share one metric definition with the LCD renderer.
#define SPI_SCREEN_PROFILE_TEXT_SCALE SCREEN_FONT_BADGE_BOLD
#define SPI_SCREEN_MENU_TEXT_SCALE SCREEN_FONT_TITLE
#define SPI_SCREEN_MENU_ITEM_H 34u
#define SPI_SCREEN_OK_FLASH_MS 200u
#define SPI_SCREEN_SLIDER_STEP 10u
#define SPI_SCREEN_DETAIL_ITEM_H 28u
#define SPI_SCREEN_ANIM_MS 160u
#define SPI_SCREEN_STATUS_ICON_H 32u
#define SPI_SCREEN_BATTERY_BODY_H 14u

struct ScreenStatusBarLayout {
    uint16_t profileY;
    uint16_t platformY;
    uint16_t connectionY;
    uint16_t batteryY;
    uint16_t topArrowY;
    uint16_t bottomArrowY;
};

struct ScreenUiStyle {
    uint32_t bg;
    uint32_t text;
    uint32_t selBg;
    uint32_t okBg;
    uint32_t mutedText = text;
    uint32_t icon = text;
    uint32_t accent = text;
    uint32_t track = selBg;
};

uint32_t ScreenUI_EaseOutCubic(uint32_t t, uint32_t d);

#endif
