#include "screen_control/spi_screen_detail_render_helpers.hpp"

#include <stdio.h>
#include <string.h>

#include "screen_control/spi_screen_ui_common.hpp"

void ScreenDetailRender_List(ST7789_Handle* lcd, const char* title, const char* const* labels, uint8_t count, uint8_t index, uint8_t selectedConfigIndex, const ScreenUiStyle& style, uint16_t itemH, bool animActive, int animDir, uint32_t animStartMs, uint32_t nowMs, const bool* disabledItems, bool showBack) {
    (void)title;
    if (!lcd || !labels) return;

    const uint16_t w = ST7789_WIDTH;
    const uint16_t h = ST7789_HEIGHT;
    const uint16_t listX = SPI_SCREEN_LEFT_BAR_W;
    const uint16_t listW = (uint16_t)(w - SPI_SCREEN_LEFT_BAR_W - SPI_SCREEN_RIGHT_BAR_W);
    const uint16_t centerY = (uint16_t)(h / 2u);
    const uint16_t anchorY = (uint16_t)(centerY - itemH / 2u);
    const uint32_t normalText = ScreenUI_MutedTextForBg(style.text, style.bg, 80u);
    const uint32_t disabledText = ScreenUI_MutedTextForBg(style.text, style.bg, 180u);
    const uint16_t charH = ScreenUI_CharCellH(SPI_SCREEN_MENU_TEXT_SCALE);

    ST7789_FillRect(lcd, listX, 0, listW, h, style.bg);

    int offsetPx = 0;
    if (animActive) {
        uint32_t eased = ScreenUI_EaseOutCubic(nowMs - animStartMs, SPI_SCREEN_ANIM_MS);
        int signedPx = (int)((int)itemH * (int)eased / 1000);
        offsetPx = -animDir * signedPx;
    }

    const int cursor = index == SCREEN_LIST_BACK ? -1 : int(index);
    bool canUp = showBack && cursor >= 0, canDown = false;
    for (int i = 0; i < count; ++i) {
        if (disabledItems && disabledItems[i]) continue;
        if (i < cursor) canUp = true;
        if (i > cursor) canDown = true;
    }
    ScreenUI_DrawListArrows(lcd, canUp, canDown, style.text, style.bg);
    for (int i = showBack ? -1 : 0; i < count; i++) {
        int dy = (i - cursor) * (int)itemH;
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
        const bool back = i == -1;
        const char* label = back ? "Back" : (labels[i] ? labels[i] : "");
        const bool disabled = !back && disabledItems && disabledItems[i];
        const uint32_t itemText = disabled ? disabledText : ((back || i == selectedConfigIndex) ? style.text : normalText);
        const uint16_t textX = listX + (back ? 24u : 4u);
        if (i == cursor) {
            ST7789_FillRect(lcd, listX, yy, listW, hh, style.selBg);
            if (textVisible) {
                if (back) ScreenUI_DrawBackIcon(lcd, listX + 5u, ty + (charH - 13u) / 2u, itemText);
                ST7789_DrawString(lcd, textX, ty, label, itemText, style.selBg, SPI_SCREEN_MENU_TEXT_SCALE);
            }
        } else {
            if (textVisible) {
                if (back) ScreenUI_DrawBackIcon(lcd, listX + 5u, ty + (charH - 13u) / 2u, itemText);
                ST7789_DrawString(lcd, textX, ty, label, itemText, style.bg, SPI_SCREEN_MENU_TEXT_SCALE);
            }
        }
    }
}

void ScreenDetailRender_Slider(ST7789_Handle* lcd, const char* title, uint8_t value, const ScreenUiStyle& style, uint8_t maxValue, const char* valueLabel) {
    if (!lcd || maxValue == 0u) return;
    if (value > maxValue) value = maxValue;

    const uint16_t w = ST7789_WIDTH;
    const uint16_t h = ST7789_HEIGHT;
    const uint16_t listX = SPI_SCREEN_LEFT_BAR_W;
    const uint16_t listW = (uint16_t)(w - SPI_SCREEN_LEFT_BAR_W - SPI_SCREEN_RIGHT_BAR_W);
    const uint8_t titleScale = SCREEN_FONT_TITLE;
    const uint8_t valueScale = 2u;
    const uint16_t titleH = ScreenUI_CharCellH(titleScale);
    const uint16_t valueH = ScreenUI_CharCellH(valueScale);
    const uint16_t barW = (uint16_t)(listW - 20u);
    const uint16_t barH = (uint16_t)(14u * 2u);
    const uint16_t barX = (uint16_t)(listX + 10u);
    const uint16_t barY = (uint16_t)(h / 2u - barH / 2u);
    const uint16_t fillW = (uint16_t)((barW * (uint16_t)value) / maxValue);

    ST7789_FillRect(lcd, listX, 0, listW, h, style.bg);
    ST7789_DrawString(lcd, barX, (uint16_t)(barY - titleH - 10u), title, style.text, style.bg, titleScale);

    ST7789_FillRect(lcd, barX, barY, barW, barH, style.selBg);
    ST7789_FillRect(lcd, barX, barY, fillW, barH, style.text);

    char val[8];
    snprintf(val, sizeof(val), "%u%%", (unsigned)value);
    ScreenUI_DrawStringCenteredInBox(lcd, listX, (uint16_t)(barY + barH + 8u), listW, valueH, valueLabel ? valueLabel : val, style.text, style.bg, valueScale);
}

void ScreenDetailRender_Info(ST7789_Handle* lcd, const char* title, const char* text, const ScreenUiStyle& style) {
    if (!lcd) return;
    const uint16_t w = ST7789_WIDTH;
    const uint16_t h = ST7789_HEIGHT;
    const uint16_t listX = SPI_SCREEN_LEFT_BAR_W;
    const uint16_t listW = (uint16_t)(w - SPI_SCREEN_LEFT_BAR_W - SPI_SCREEN_RIGHT_BAR_W);
    const uint16_t titleH = 20u;

    ST7789_FillRect(lcd, listX, 0, listW, h, style.bg);
    ScreenUI_DrawStringCenteredInBox(lcd, listX, 0, listW, titleH, title, style.text, style.bg, SCREEN_FONT_TITLE);
    ScreenUI_DrawStringCenteredInBox(lcd, listX, titleH, listW, (uint16_t)(h - titleH), text, style.text, style.bg, SPI_SCREEN_MENU_TEXT_SCALE);
}

void ScreenDetailRender_TitleLines(ST7789_Handle* lcd, const char* title, const char* const* lines, uint8_t lineCount, const ScreenUiStyle& style) {
    if (!lcd) return;
    const uint16_t w = ST7789_WIDTH;
    const uint16_t h = ST7789_HEIGHT;
    const uint16_t listX = SPI_SCREEN_LEFT_BAR_W;
    const uint16_t listW = (uint16_t)(w - SPI_SCREEN_LEFT_BAR_W - SPI_SCREEN_RIGHT_BAR_W);
    const uint16_t x = (uint16_t)(listX + 8u);
    const uint8_t titleScale = SCREEN_FONT_LARGE;
    // Native 8x14 body glyphs; never resample a smaller strike.
    const uint8_t textScale = SCREEN_FONT_BODY;
    const uint16_t titleY = 12u;
    const uint16_t titleH = ScreenUI_CharCellH(titleScale);
    const uint16_t textStartY = (uint16_t)(titleY + titleH + 14u);
    const uint16_t lineH = ScreenUI_CharCellH(textScale);
    const uint16_t gap = 6u;
    char row[32];
    const uint16_t availableChars = (uint16_t)((listW - 16u) / ScreenUI_CharCellW(textScale));
    const uint16_t maxChars = availableChars < sizeof(row) - 1u ? availableChars : sizeof(row) - 1u;

    ST7789_FillRect(lcd, listX, 0, listW, h, style.bg);
    ST7789_DrawString(lcd, x, titleY, title ? title : "", style.text, style.bg, titleScale);

    uint16_t y = textStartY;
    for (uint8_t i = 0; i < lineCount; i++) {
        if (!lines || !lines[i]) continue;
        const char* remaining = lines[i];
        while (*remaining && maxChars > 0u) {
            if (y + lineH > h) return;
            remaining = ScreenUI_NextTextRow(remaining, row, maxChars);
            ST7789_DrawString(lcd, x, y, row, style.text, style.bg, textScale);
            y = (uint16_t)(y + lineH + gap);
        }
    }
}
