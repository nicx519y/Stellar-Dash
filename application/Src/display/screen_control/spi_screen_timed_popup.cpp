#include "screen_control/spi_screen_timed_popup.hpp"

#include "screen_control/spi_screen_ui_common.hpp"

static bool tick_reached(uint32_t nowMs, uint32_t targetMs) {
    return (int32_t)(nowMs - targetMs) >= 0;
}

void ScreenTimedPopup_Reset(ScreenTimedPopup* popup) {
    if (!popup) return;
    popup->visible = false;
    popup->persistent = false;
    popup->closeAtMs = 0u;
    popup->title = "";
    popup->lines = nullptr;
    popup->lineCount = 0u;
}

void ScreenTimedPopup_Show(ScreenTimedPopup* popup, const char* title, const char* const* lines, uint8_t lineCount, uint32_t durationMs, uint32_t nowMs) {
    if (!popup) return;
    popup->visible = true;
    popup->persistent = durationMs == 0;
    popup->closeAtMs = nowMs + durationMs;
    popup->title = title ? title : "";
    popup->lines = lines;
    popup->lineCount = lineCount;
}

void ScreenTimedPopup_Close(ScreenTimedPopup* popup) {
    if (!popup) return;
    popup->visible = false;
    popup->persistent = false;
}

void ScreenTimedPopup_Update(ScreenTimedPopup* popup, uint32_t nowMs) {
    if (!popup || !popup->visible || popup->persistent) return;
    if (tick_reached(nowMs, popup->closeAtMs)) {
        popup->visible = false;
    popup->persistent = false;
    }
}

bool ScreenTimedPopup_IsVisible(const ScreenTimedPopup* popup) {
    return popup ? popup->visible : false;
}

__attribute__((optimize("Os"))) void ScreenTimedPopup_Render(const ScreenTimedPopup* popup, ST7789_Handle* lcd, const ScreenUiStyle& style) {
    if (!popup || !popup->visible) return;
    if (!lcd) return;

    const uint16_t w = ST7789_WIDTH;
    const uint16_t h = ST7789_HEIGHT;
    const uint16_t listX = SPI_SCREEN_LEFT_BAR_W;
    const uint16_t listW = (uint16_t)(w - SPI_SCREEN_LEFT_BAR_W - SPI_SCREEN_RIGHT_BAR_W);

    const uint8_t titleScale = SCREEN_FONT_TITLE;
    const uint8_t textScale = SCREEN_FONT_BODY;
    const uint16_t titleH = ScreenUI_CharCellH(titleScale);
    const uint16_t lineH = ScreenUI_CharCellH(textScale);
    const uint16_t titleGap = 10u;
    const uint16_t lineGap = 4u;
    char row[32];
    const uint16_t availableChars = (listW - 16u) / ScreenUI_CharCellW(textScale);
    const uint16_t maxChars = availableChars < sizeof(row) - 1u ? availableChars : sizeof(row) - 1u;
    if (maxChars == 0u) return;
    uint16_t rows = 0;
    for (uint8_t i = 0; i < popup->lineCount; ++i) {
        const char* text = popup->lines && popup->lines[i] ? popup->lines[i] : "";
        do { text = ScreenUI_NextTextRow(text, row, maxChars); ++rows; } while (*text);
    }
    const uint16_t linesH = rows ? rows * lineH + (rows - 1u) * lineGap : 0u;
    const uint16_t blockH = (uint16_t)(titleH + titleGap + linesH);
    const uint16_t startY = (blockH < h) ? (uint16_t)((h - blockH) / 2u) : 0u;
    const uint32_t mutedText = ScreenUI_MutedTextForBg(style.text, style.bg, 96u);

    ST7789_FillRect(lcd, listX, 0, listW, h, style.bg);
    ScreenUI_DrawStringCenteredInBox(lcd, listX, startY, listW, titleH, popup->title ? popup->title : "", style.text, style.bg, titleScale);

    uint16_t y = (uint16_t)(startY + titleH + titleGap);
    for (uint8_t i = 0; i < popup->lineCount; i++) {
        const char* line = (popup->lines && popup->lines[i]) ? popup->lines[i] : "";
        do {
            if (y + lineH > h) return;
            line = ScreenUI_NextTextRow(line, row, maxChars);
            ScreenUI_DrawStringCenteredInBox(lcd, listX, y, listW, lineH, row, mutedText, style.bg, textScale);
            y = (uint16_t)(y + lineH + lineGap);
        } while (*line);
    }
}
