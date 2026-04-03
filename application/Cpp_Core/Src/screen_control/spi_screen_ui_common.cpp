#include "screen_control/spi_screen_ui_common.hpp"

#include <string.h>

#include "screen_control/spi_screen_layout.hpp"

uint32_t ScreenUI_RGB888(uint32_t v) {
    return v & 0x00FFFFFFu;
}

uint32_t ScreenUI_BlendToWhite(uint32_t rgb, uint8_t alpha255) {
    uint32_t r = (rgb >> 16) & 0xFFu;
    uint32_t g = (rgb >> 8) & 0xFFu;
    uint32_t b = rgb & 0xFFu;

    r = r + ((255u - r) * alpha255) / 255u;
    g = g + ((255u - g) * alpha255) / 255u;
    b = b + ((255u - b) * alpha255) / 255u;

    return (r << 16) | (g << 8) | b;
}

uint32_t ScreenUI_BlendToBlack(uint32_t rgb, uint8_t alpha255) {
    uint32_t r = (rgb >> 16) & 0xFFu;
    uint32_t g = (rgb >> 8) & 0xFFu;
    uint32_t b = rgb & 0xFFu;

    r = (r * (255u - alpha255)) / 255u;
    g = (g * (255u - alpha255)) / 255u;
    b = (b * (255u - alpha255)) / 255u;

    return (r << 16) | (g << 8) | b;
}

static uint32_t screenui_luma8(uint32_t rgb) {
    uint32_t r = (rgb >> 16) & 0xFFu;
    uint32_t g = (rgb >> 8) & 0xFFu;
    uint32_t b = rgb & 0xFFu;
    return (uint32_t)((r * 54u + g * 183u + b * 19u) >> 8);
}

uint32_t ScreenUI_HighlightFromBg(uint32_t bg, uint8_t amount) {
    return (screenui_luma8(bg) < 128u) ? ScreenUI_BlendToWhite(bg, amount) : ScreenUI_BlendToBlack(bg, amount);
}

uint32_t ScreenUI_MutedTextForBg(uint32_t fg, uint32_t bg, uint8_t amount) {
    uint32_t fr = (fg >> 16) & 0xFFu;
    uint32_t fgc = (fg >> 8) & 0xFFu;
    uint32_t fb = fg & 0xFFu;
    uint32_t br = (bg >> 16) & 0xFFu;
    uint32_t bgc = (bg >> 8) & 0xFFu;
    uint32_t bb = bg & 0xFFu;
    uint32_t r = (fr * (255u - amount) + br * amount) / 255u;
    uint32_t g = (fgc * (255u - amount) + bgc * amount) / 255u;
    uint32_t b = (fb * (255u - amount) + bb * amount) / 255u;
    return (r << 16) | (g << 8) | b;
}

uint16_t ScreenUI_CharCellW(uint8_t scale) {
    if (scale == 0) scale = 1;
    if (scale == 3) return 9;
    return (uint16_t)(6u * scale);
}

uint16_t ScreenUI_CharCellH(uint8_t scale) {
    if (scale == 0) scale = 1;
    if (scale == 3) return 12;
    return (uint16_t)(8u * scale);
}

uint16_t ScreenUI_TextPxW(const char* s, uint8_t scale) {
    if (!s || scale == 0) return 0;
    uint16_t len = (uint16_t)strlen(s);
    return (uint16_t)(len * ScreenUI_CharCellW(scale));
}

uint16_t ScreenUI_TextPxH(uint8_t scale) {
    if (scale == 0) return 0;
    return ScreenUI_CharCellH(scale);
}

void ScreenUI_DrawStringCenteredInBox(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* s, uint32_t fg, uint32_t bg, uint8_t scale) {
    if (!lcd || !s || scale == 0) return;
    const uint16_t tw = ScreenUI_TextPxW(s, scale);
    const uint16_t th = ScreenUI_TextPxH(scale);

    if (w < tw || h < th) {
        uint16_t maxChars = (uint16_t)(w / ScreenUI_CharCellW(scale));
        if (maxChars == 0) return;
        char tmp[32] = {0};
        if (maxChars >= sizeof(tmp)) maxChars = (uint16_t)(sizeof(tmp) - 1);
        memcpy(tmp, s, maxChars);
        tmp[maxChars] = '\0';
        const uint16_t tw2 = ScreenUI_TextPxW(tmp, scale);
        const uint16_t cx = (uint16_t)(x + (w - tw2) / 2u);
        const uint16_t cy = (uint16_t)(y + (h - th) / 2u);
        ST7789_DrawString(lcd, cx, cy, tmp, fg, bg, scale);
        return;
    }

    const uint16_t cx = (uint16_t)(x + (w - tw) / 2u);
    const uint16_t cy = (uint16_t)(y + (h - th) / 2u);
    ST7789_DrawString(lcd, cx, cy, s, fg, bg, scale);
}

uint32_t ScreenUI_EaseOutCubic(uint32_t t, uint32_t d) {
    if (d == 0) return 1000;
    if (t >= d) return 1000;
    uint32_t u = (uint32_t)(1000u - (t * 1000u) / d);
    uint32_t u2 = (u * u) / 1000u;
    uint32_t u3 = (u2 * u) / 1000u;
    return 1000u - u3;
}
