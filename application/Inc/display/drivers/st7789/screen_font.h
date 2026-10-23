#ifndef XORA_SCREEN_FONT_H
#define XORA_SCREEN_FONT_H

#include <stdint.h>

/* Legacy scale values now select native bitmap strikes, never magnification. */
enum {
    SCREEN_FONT_SMALL = 1, /* 6x12: narrow status labels */
    SCREEN_FONT_TITLE = 2, /* 8x16 bold: menus, titles and values */
    SCREEN_FONT_BODY = 3,  /* 8x14: descriptions */
    SCREEN_FONT_LARGE = 4, /* 12x24 bold: charge percentage */
    SCREEN_FONT_BADGE_BOLD = 5 /* 8x14: body strike with 1px right/down emboldening */
};

static inline uint16_t ScreenFont_CellW(uint8_t font)
{
    if (font == 0 || font == SCREEN_FONT_SMALL) return 6;
    if (font == SCREEN_FONT_LARGE) return 12;
    return 8;
}

static inline uint16_t ScreenFont_CellH(uint8_t font)
{
    if (font == 0 || font == SCREEN_FONT_SMALL) return 12;
    if (font == SCREEN_FONT_BODY || font == SCREEN_FONT_BADGE_BOLD) return 14;
    if (font == SCREEN_FONT_LARGE) return 24;
    return 16;
}

#endif
