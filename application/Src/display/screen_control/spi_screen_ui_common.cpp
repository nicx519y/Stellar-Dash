#include "screen_control/spi_screen_ui_common.hpp"
#include "screen_font.h"
#include <string.h>

void ScreenUI_DrawScrollArrow(ST7789_Handle* lcd, uint16_t cx, uint16_t cy, bool up, uint32_t color) {
    for (uint16_t row = 0; row < 6; ++row) {
        const uint16_t y = up ? cy - 3 + row : cy + 3 - row;
        ST7789_FillRect(lcd, cx - row, y, row * 2 + 1, 1, color);
    }
}

void ScreenUI_DrawBackIcon(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint32_t color) {
    for (uint16_t row = 0; row < 9; ++row) {
        const uint16_t dx = row < 5 ? 4 - row : row - 4;
        ST7789_FillRect(lcd, x + dx, y + row, 2, 1, color);
    }
    ST7789_FillRect(lcd, x + 2, y + 4, 12, 2, color);
    ST7789_FillRect(lcd, x + 12, y + 4, 2, 9, color);
    ST7789_FillRect(lcd, x + 7, y + 11, 7, 2, color);
}

#include <string.h>

#include "screen_control/spi_screen_layout.hpp"

ScreenStatusBarLayout ScreenUI_StatusBarLayout(void) {
    const uint16_t areaH = ST7789_HEIGHT / 3u;
    const uint16_t bottom = areaH * 2u;
    const uint16_t profileH = ScreenUI_CharCellH(SPI_SCREEN_PROFILE_TEXT_SCALE);
    ScreenStatusBarLayout layout = {};
    layout.profileY = (areaH - profileH) / 2u - 5u;
    layout.batteryY = bottom + (ST7789_HEIGHT - bottom - SPI_SCREEN_BATTERY_BODY_H) / 2u + 5u;
    layout.platformY = (ST7789_HEIGHT - SPI_SCREEN_STATUS_ICON_H) / 2u;
    layout.topArrowY = layout.profileY + profileH / 2u;
    layout.bottomArrowY = layout.batteryY + SPI_SCREEN_BATTERY_BODY_H / 2u;
    const uint16_t platformCenter = layout.platformY + SPI_SCREEN_STATUS_ICON_H / 2u;
    layout.connectionY = (platformCenter + layout.bottomArrowY) / 2u - SPI_SCREEN_STATUS_ICON_H / 2u;
    return layout;
}

void ScreenUI_DrawListArrows(ST7789_Handle* lcd, bool canUp, bool canDown, uint32_t fg, uint32_t bg) {
    const uint16_t x = ST7789_WIDTH - SPI_SCREEN_RIGHT_BAR_W;
    const uint16_t areaH = ST7789_HEIGHT / 3u;
    const uint16_t bottom = areaH * 2u;
    const auto layout = ScreenUI_StatusBarLayout();
    // Clear both directions as well, including when the cursor just reached an end.
    ST7789_FillRect(lcd, x, 0, SPI_SCREEN_RIGHT_BAR_W, areaH, bg);
    ST7789_FillRect(lcd, x, bottom, SPI_SCREEN_RIGHT_BAR_W, ST7789_HEIGHT - bottom, bg);
    if (canUp) ScreenUI_DrawScrollArrow(lcd, x + SPI_SCREEN_RIGHT_BAR_W / 2u, layout.topArrowY, true, fg);
    if (canDown) ScreenUI_DrawScrollArrow(lcd, x + SPI_SCREEN_RIGHT_BAR_W / 2u, layout.bottomArrowY, false, fg);
}

ScreenActionIcon ScreenUI_ActionIcon(const char* action) {
    if (!action || !action[0]) return ScreenActionIcon::None;
    struct Mapping { const char* label; ScreenActionIcon icon; };
    static const Mapping mappings[] = {
        {"Open", ScreenActionIcon::Open}, {"Back", ScreenActionIcon::Back},
        {"Apply", ScreenActionIcon::Confirm}, {"OK", ScreenActionIcon::Confirm},
        {"Done", ScreenActionIcon::Confirm}, {"Finish", ScreenActionIcon::Confirm},
        {"ON", ScreenActionIcon::TurnOn}, {"OFF", ScreenActionIcon::TurnOff},
        {"Edit", ScreenActionIcon::Edit}, {"Close", ScreenActionIcon::Close},
        {"Cancel", ScreenActionIcon::Close}, {"Quit", ScreenActionIcon::Exit},
        {"Exit", ScreenActionIcon::Exit}, {"Start", ScreenActionIcon::Start},
        {"Retry", ScreenActionIcon::Retry}, {"Wait", ScreenActionIcon::Wait},
        {"N/A", ScreenActionIcon::Unavailable}, {"Check", ScreenActionIcon::Check},
    };
    for (const auto& mapping : mappings)
        if (strcmp(action, mapping.label) == 0) return mapping.icon;
    return ScreenActionIcon::None;
}

namespace {
// Small integer strokes avoid font/resource dependencies and stay inside 24x24.
void iconLine(ST7789_Handle* lcd, int x, int y, int x1, int y1, uint32_t color) {
    const int dx = x1 > x ? x1 - x : x - x1;
    const int dy = y1 > y ? y - y1 : y1 - y;
    const int sx = x < x1 ? 1 : -1, sy = y < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        ST7789_FillRect(lcd, x, y, 2, 2, color);
        if (x == x1 && y == y1) break;
        const int twice = 2 * error;
        if (twice >= dy) { error += dy; x += sx; }
        if (twice <= dx) { error += dx; y += sy; }
    }
}
}

void ScreenUI_DrawActionIcon(ST7789_Handle* lcd, uint16_t cx, uint16_t cy, ScreenActionIcon icon, uint32_t fg, uint32_t bg) {
    if (!lcd || icon == ScreenActionIcon::None) return;
    const int x = int(cx) - 12, y = int(cy) - 12;
    ST7789_FillRect(lcd, x, y, 24, 24, bg);
    if (icon == ScreenActionIcon::Unavailable || icon == ScreenActionIcon::Wait)
        fg = ScreenUI_MutedTextForBg(fg, bg, 96u);
    auto line = [&](int ax, int ay, int bx, int by) { iconLine(lcd, x + ax, y + ay, x + bx, y + by, fg); };
    auto arrow = [&] { line(5, 11, 19, 11); line(14, 6, 19, 11); line(19, 11, 14, 16); };
    switch (icon) {
        case ScreenActionIcon::Open: arrow(); break;
        case ScreenActionIcon::Back: ScreenUI_DrawBackIcon(lcd, cx - 7u, cy - 6u, fg); break;
        case ScreenActionIcon::Confirm: line(3, 11, 9, 17); line(9, 17, 20, 5); break;
        case ScreenActionIcon::TurnOn:
        case ScreenActionIcon::TurnOff:
            // The slash distinguishes the action to turn off from the action to turn on.
            line(11, 2, 11, 11);
            line(6, 5, 3, 9); line(3, 9, 3, 15); line(3, 15, 7, 19);
            line(7, 19, 15, 19); line(15, 19, 19, 15); line(19, 15, 19, 9); line(19, 9, 16, 5);
            if (icon == ScreenActionIcon::TurnOff) line(2, 2, 21, 21);
            break;
        case ScreenActionIcon::Edit:
            line(4, 15, 15, 4); line(15, 4, 19, 8); line(19, 8, 8, 19);
            line(8, 19, 3, 20); line(3, 20, 4, 15); line(12, 7, 16, 11);
            break;
        case ScreenActionIcon::Close: line(5, 5, 18, 18); line(18, 5, 5, 18); break;
        case ScreenActionIcon::Exit:
            line(9, 3, 2, 3); line(2, 3, 2, 20); line(2, 20, 9, 20); arrow(); break;
        case ScreenActionIcon::Start:
            for (int col = 0; col < 12; ++col) ST7789_FillRect(lcd, x + 6 + col, y + 3 + col * 3 / 4, 1, 19 - 2 * (col * 3 / 4), fg);
            break;
        case ScreenActionIcon::Retry:
            line(18, 7, 14, 3); line(14, 3, 7, 3); line(7, 3, 3, 7);
            line(3, 7, 3, 15); line(3, 15, 7, 19); line(7, 19, 15, 19); line(15, 19, 19, 15);
            line(18, 2, 18, 8); line(18, 8, 12, 8); break;
        case ScreenActionIcon::Wait:
            line(5, 3, 18, 3); line(5, 20, 18, 20);
            line(6, 4, 6, 7); line(17, 4, 17, 7); line(6, 7, 17, 16); line(17, 7, 6, 16);
            line(6, 16, 6, 19); line(17, 16, 17, 19); break;
        case ScreenActionIcon::Unavailable:
            line(7, 3, 15, 3); line(15, 3, 20, 8); line(20, 8, 20, 15); line(20, 15, 15, 20);
            line(15, 20, 7, 20); line(7, 20, 3, 15); line(3, 15, 3, 8); line(3, 8, 7, 3);
            line(5, 18, 18, 5); break;
        case ScreenActionIcon::Check:
            line(6, 3, 12, 3); line(12, 3, 16, 7); line(16, 7, 16, 12); line(16, 12, 12, 16);
            line(12, 16, 6, 16); line(6, 16, 2, 12); line(2, 12, 2, 7); line(2, 7, 6, 3);
            line(15, 15, 21, 21); break;
        default: break;
    }
}

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
    return ScreenFont_CellW(scale);
}

uint16_t ScreenUI_CharCellH(uint8_t scale) {
    return ScreenFont_CellH(scale);
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

__attribute__((optimize("Os"))) const char* ScreenUI_NextTextRow(const char* text, char* row, uint16_t maxChars) {
    uint16_t length = 0;
    while (length < maxChars && text[length] && text[length] != '\n') ++length;
    if (length == maxChars && text[length] && text[length] != ' ' && text[length] != '\n') {
        uint16_t wordEnd = length;
        while (wordEnd > 0 && text[wordEnd - 1] != ' ') --wordEnd;
        if (wordEnd > 0) length = wordEnd;
    }
    memcpy(row, text, length);
    row[length] = '\0';
    text += length;
    while (*text == ' ') ++text;
    if (*text == '\n') ++text;
    return text;
}

__attribute__((optimize("Os"))) void ScreenUI_DrawStringCenteredInBox(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* s, uint32_t fg, uint32_t bg, uint8_t scale) {
    if (!lcd || !s || scale == 0) return;
    const uint16_t cellW = ScreenUI_CharCellW(scale);
    uint16_t tw = (uint16_t)(strlen(s) * cellW);
    const uint16_t th = ScreenUI_CharCellH(scale);
    char tmp[32];
    if (h < th) return;
    if (w < tw) {
        uint16_t maxChars = (uint16_t)(w / cellW);
        if (maxChars == 0) return;
        if (maxChars >= sizeof(tmp)) maxChars = (uint16_t)(sizeof(tmp) - 1);
        memcpy(tmp, s, maxChars);
        tmp[maxChars] = '\0';
        tw = maxChars * cellW;
        s = tmp;
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
