#include "screen_control/spi_screen_detail_render_helpers.hpp"

#include <stdio.h>

#include "screen_control/spi_screen_ui_common.hpp"

void ScreenDetailRender_List(ST7789_Handle* lcd, const char* title, const char* const* labels, uint8_t count, uint8_t index, uint8_t selectedConfigIndex, const ScreenUiStyle& style, uint16_t itemH, bool animActive, int animDir, uint32_t animStartMs, uint32_t nowMs) {
    (void)title;
    if (!lcd || !labels) return;

    const uint16_t w = ST7789_WIDTH;
    const uint16_t h = ST7789_HEIGHT;
    const uint16_t listX = SPI_SCREEN_LEFT_BAR_W;
    const uint16_t listW = (uint16_t)(w - SPI_SCREEN_LEFT_BAR_W - SPI_SCREEN_RIGHT_BAR_W);
    const uint16_t centerY = (uint16_t)(h / 2u);
    const uint16_t anchorY = (uint16_t)(centerY - itemH / 2u);
    const uint32_t normalText = ScreenUI_MutedTextForBg(style.text, style.bg, 80u);
    const uint16_t charH = ScreenUI_CharCellH(SPI_SCREEN_MENU_TEXT_SCALE);

    ST7789_FillRect(lcd, listX, 0, listW, h, style.bg);

    int offsetPx = 0;
    if (animActive) {
        uint32_t eased = ScreenUI_EaseOutCubic(nowMs - animStartMs, SPI_SCREEN_ANIM_MS);
        int signedPx = (int)((int)itemH * (int)eased / 1000);
        offsetPx = -animDir * signedPx;
    }

    for (uint8_t i = 0; i < count; i++) {
        int dy = (int)((int)i - (int)index) * (int)itemH;
        int y0 = (int)anchorY + dy + offsetPx;
        int y1 = y0 + (int)itemH;
        if (y1 <= 0 || y0 >= (int)h) continue;

        int clipY0 = (y0 < 0) ? 0 : y0;
        int clipY1 = (y1 > (int)h) ? (int)h : y1;
        uint16_t yy = (uint16_t)clipY0;
        uint16_t hh = (uint16_t)(clipY1 - clipY0);

        int textY = y0 + (int)((itemH - charH) / 2u);
        bool textVisible = (textY >= 0) && ((textY + (int)charH) <= (int)h);
        uint16_t ty = (uint16_t)((textY < 0) ? 0 : textY);
        const char* label = labels[i] ? labels[i] : "";
        const uint32_t itemText = (i == selectedConfigIndex) ? style.text : normalText;
        if (i == index) {
            ST7789_FillRect(lcd, listX, yy, listW, hh, style.selBg);
            if (textVisible) {
                ST7789_DrawString(lcd, (uint16_t)(listX + 4), ty, label, itemText, style.selBg, SPI_SCREEN_MENU_TEXT_SCALE);
            }
        } else {
            if (textVisible) {
                ST7789_DrawString(lcd, (uint16_t)(listX + 4), ty, label, itemText, style.bg, SPI_SCREEN_MENU_TEXT_SCALE);
            }
        }
    }
}

void ScreenDetailRender_Slider(ST7789_Handle* lcd, const char* title, uint8_t value, const ScreenUiStyle& style) {
    if (!lcd) return;

    const uint16_t w = ST7789_WIDTH;
    const uint16_t h = ST7789_HEIGHT;
    const uint16_t listX = SPI_SCREEN_LEFT_BAR_W;
    const uint16_t listW = (uint16_t)(w - SPI_SCREEN_LEFT_BAR_W - SPI_SCREEN_RIGHT_BAR_W);
    const uint8_t titleScale = 2u;
    const uint8_t valueScale = 2u;
    const uint16_t titleH = ScreenUI_CharCellH(titleScale);
    const uint16_t valueH = ScreenUI_CharCellH(valueScale);
    const uint16_t barW = (uint16_t)(listW - 20u);
    const uint16_t barH = (uint16_t)(14u * 2u);
    const uint16_t barX = (uint16_t)(listX + 10u);
    const uint16_t barY = (uint16_t)(h / 2u - barH / 2u);
    const uint16_t fillW = (uint16_t)((barW * (uint16_t)value) / 100u);

    ST7789_FillRect(lcd, listX, 0, listW, h, style.bg);
    ST7789_DrawString(lcd, barX, (uint16_t)(barY - titleH - 10u), title, style.text, style.bg, titleScale);

    ST7789_FillRect(lcd, barX, barY, barW, barH, style.selBg);
    ST7789_FillRect(lcd, barX, barY, fillW, barH, style.text);

    char val[8];
    snprintf(val, sizeof(val), "%u%%", (unsigned)value);
    ScreenUI_DrawStringCenteredInBox(lcd, listX, (uint16_t)(barY + barH + 8u), listW, valueH, val, style.text, style.bg, valueScale);
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

void ScreenDetailRender_TitleLines(ST7789_Handle* lcd, const char* title, const char* const* lines, uint8_t lineCount, const ScreenUiStyle& style) {
    if (!lcd) return;
    const uint16_t w = ST7789_WIDTH;
    const uint16_t h = ST7789_HEIGHT;
    const uint16_t listX = SPI_SCREEN_LEFT_BAR_W;
    const uint16_t listW = (uint16_t)(w - SPI_SCREEN_LEFT_BAR_W - SPI_SCREEN_RIGHT_BAR_W);
    const uint16_t x = (uint16_t)(listX + 8u);
    const uint8_t titleScale = 2u;
    const uint8_t textScale = 1u;
    const uint16_t titleY = 8u;
    const uint16_t titleH = ScreenUI_CharCellH(titleScale);
    const uint16_t textStartY = (uint16_t)(titleY + titleH + 10u);
    const uint16_t lineH = ScreenUI_CharCellH(textScale);
    const uint16_t gap = 2u;

    ST7789_FillRect(lcd, listX, 0, listW, h, style.bg);
    ST7789_DrawString(lcd, x, titleY, title ? title : "", style.text, style.bg, titleScale);

    uint16_t y = textStartY;
    for (uint8_t i = 0; i < lineCount; i++) {
        if (!lines || !lines[i]) continue;
        if (y + lineH > h) break;
        ST7789_DrawString(lcd, x, y, lines[i], style.text, style.bg, textScale);
        y = (uint16_t)(y + lineH + gap);
    }
}
