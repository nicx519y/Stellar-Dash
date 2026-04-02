#include "screen_control/spi_screen_detail_render_helpers.hpp"

#include <stdio.h>

#include "screen_control/spi_screen_ui_common.hpp"

void ScreenDetailRender_List(ST7789_Handle* lcd, const char* title, const char* const* labels, uint8_t count, uint8_t index, const ScreenUiStyle& style) {
    if (!lcd || !labels) return;

    const uint16_t w = ST7789_WIDTH;
    const uint16_t h = ST7789_HEIGHT;
    const uint16_t listX = SPI_SCREEN_LEFT_BAR_W;
    const uint16_t listW = (uint16_t)(w - SPI_SCREEN_LEFT_BAR_W - SPI_SCREEN_RIGHT_BAR_W);
    const uint16_t titleH = 20u;
    const uint16_t itemH = SPI_SCREEN_DETAIL_ITEM_H;
    const uint16_t listTop = titleH;
    const uint16_t centerY = (uint16_t)(listTop + (h - listTop) / 2u);
    const uint16_t anchorY = (uint16_t)(centerY - itemH / 2u);

    ST7789_FillRect(lcd, listX, 0, listW, h, style.bg);
    ScreenUI_DrawStringCenteredInBox(lcd, listX, 0, listW, titleH, title, style.text, style.bg, 1);

    for (uint8_t i = 0; i < count; i++) {
        int dy = (int)((int)i - (int)index) * (int)itemH;
        int y = (int)anchorY + dy;
        if (y < -(int)itemH || y > (int)h) continue;
        uint16_t yy = (uint16_t)((y < 0) ? 0 : y);
        uint16_t ty = (uint16_t)(yy + (itemH - ScreenUI_CharCellH(SPI_SCREEN_MENU_TEXT_SCALE)) / 2u);
        const char* label = labels[i] ? labels[i] : "";
        if (i == index) {
            ST7789_FillRect(lcd, listX, yy, listW, itemH, style.selBg);
            ST7789_DrawString(lcd, (uint16_t)(listX + 4), ty, label, style.text, style.selBg, SPI_SCREEN_MENU_TEXT_SCALE);
        } else {
            ST7789_DrawString(lcd, (uint16_t)(listX + 4), ty, label, style.text, style.bg, SPI_SCREEN_MENU_TEXT_SCALE);
        }
    }
}

void ScreenDetailRender_Slider(ST7789_Handle* lcd, const char* title, uint8_t value, const ScreenUiStyle& style) {
    if (!lcd) return;

    const uint16_t w = ST7789_WIDTH;
    const uint16_t h = ST7789_HEIGHT;
    const uint16_t listX = SPI_SCREEN_LEFT_BAR_W;
    const uint16_t listW = (uint16_t)(w - SPI_SCREEN_LEFT_BAR_W - SPI_SCREEN_RIGHT_BAR_W);
    const uint16_t titleH = 20u;
    const uint16_t barW = (uint16_t)(listW - 20u);
    const uint16_t barH = 14u;
    const uint16_t barX = (uint16_t)(listX + (listW - barW) / 2u);
    const uint16_t barY = (uint16_t)(h / 2u - barH / 2u);
    const uint16_t fillW = (uint16_t)((barW * (uint16_t)value) / 100u);
    const uint32_t trackBg = ScreenUI_HighlightFromBg(style.bg, 20u);

    ST7789_FillRect(lcd, listX, 0, listW, h, style.bg);
    ScreenUI_DrawStringCenteredInBox(lcd, listX, 0, listW, titleH, title, style.text, style.bg, 1);

    ST7789_FillRect(lcd, barX, barY, barW, barH, trackBg);
    ST7789_FillRect(lcd, barX, barY, fillW, barH, style.selBg);

    char val[8];
    snprintf(val, sizeof(val), "%u%%", (unsigned)value);
    ScreenUI_DrawStringCenteredInBox(lcd, listX, (uint16_t)(barY + barH + 8u), listW, 16u, val, style.text, style.bg, 1);
}

void ScreenDetailRender_Info(ST7789_Handle* lcd, const char* title, const char* text, const ScreenUiStyle& style) {
    if (!lcd) return;
    const uint16_t w = ST7789_WIDTH;
    const uint16_t h = ST7789_HEIGHT;
    const uint16_t listX = SPI_SCREEN_LEFT_BAR_W;
    const uint16_t listW = (uint16_t)(w - SPI_SCREEN_LEFT_BAR_W - SPI_SCREEN_RIGHT_BAR_W);
    const uint16_t titleH = 20u;

    ST7789_FillRect(lcd, listX, 0, listW, h, style.bg);
    ScreenUI_DrawStringCenteredInBox(lcd, listX, 0, listW, titleH, title, style.text, style.bg, 1);
    ScreenUI_DrawStringCenteredInBox(lcd, listX, titleH, listW, (uint16_t)(h - titleH), text, style.text, style.bg, SPI_SCREEN_MENU_TEXT_SCALE);
}
