#include "screen_control/spi_screen_status_bar.hpp"
#include "screen_control/spi_screen_ui_common.hpp"
#include <string.h>

// CPU-only status drawing stays in ordinary executable RAM; the spi_screen*
// constants use the existing DTCM UI-constant placement, without QSPI reads.
const char* ScreenUI_ReportRateLabel(uint16_t rateHz) {
    switch (rateHz) {
        case 125u: return "125";
        case 250u: return "250";
        case 500u: return "500";
        case 1000u: return "1K";
        case 2000u: return "2K";
        case 4000u: return "4K";
        case 8000u: return "8K";
        default: return "--";
    }
}

__attribute__((optimize("Os"))) void ScreenUI_DrawStatusMeta(ST7789_Handle* lcd, const char* performance, uint16_t rateHz, const ScreenUiStyle& style) {
    if (!lcd) return;
    const auto layout = ScreenUI_StatusBarLayout();
    const uint16_t textH = ScreenUI_CharCellH(SPI_SCREEN_STATUS_META_TEXT_SCALE);
    const uint16_t x = (SPI_SCREEN_LEFT_BAR_W - SPI_SCREEN_STATUS_BADGE_W) / 2u;
    const char* labels[] = {"FST", "BAL", "STB", "CUS"};
    uint8_t selected = 3u;
    for (uint8_t i = 0; i < 4u; ++i) {
        if (performance && strcmp(performance, labels[i]) == 0) { selected = i; break; }
    }
    // Erase the complete group before drawing a different preset.
    ST7789_FillRect(lcd, x, layout.performanceY, SPI_SCREEN_STATUS_BADGE_W, SPI_SCREEN_STATUS_PERFORMANCE_H, style.bg);
    const uint16_t labelW = ScreenUI_TextPxW(labels[selected], SPI_SCREEN_STATUS_META_TEXT_SCALE) - 1u;
    ST7789_DrawString(lcd, (SPI_SCREEN_LEFT_BAR_W - labelW) / 2u, layout.performanceY,
        labels[selected], style.icon, style.bg, SPI_SCREEN_STATUS_META_TEXT_SCALE);
    for (uint8_t i = 0; i < 4u; ++i) {
        ST7789_FillRect(lcd, x + i * 8u, layout.performanceY + 12u, 4u, 3u,
            i == selected ? style.accent : style.icon);
    }
    // Crop the font's blank rows: the 8px bar touches the visible ink above/below.
    ST7789_FillRect(lcd, x, layout.reportRateY, SPI_SCREEN_STATUS_BADGE_W, textH, style.icon);
    const char* rate = ScreenUI_ReportRateLabel(rateHz);
    const uint16_t rateW = ScreenUI_TextPxW(rate, SPI_SCREEN_STATUS_META_TEXT_SCALE) - 1u;
    ST7789_DrawString(lcd, (SPI_SCREEN_LEFT_BAR_W - rateW) / 2u, layout.reportRateY,
        rate, style.bg, style.icon, SPI_SCREEN_STATUS_META_TEXT_SCALE);
}

__attribute__((optimize("Os"))) void ScreenUI_DrawConnectionIcon(ST7789_Handle* lcd, ScreenConnectionIcon icon, const ScreenUiStyle& style) {
    if (!lcd) return;
    // Native 28x11 one-bit rows; no QSPI assets, scaling or antialiasing.
    static const uint32_t radio[] = {
        0x2003804u, 0x4007002u, 0x8807011u, 0x920e049u, 0x941fc29u,
        0x941f829u, 0x943f029u, 0x920e049u, 0x480c012u, 0x4018002u, 0x2010004u
    };
    static const uint32_t usb[] = {
        0x0000400u, 0x000fe00u, 0x0010420u, 0x0020010u, 0x4040008u,
        0xffffffcu, 0x4004008u, 0x0002010u, 0x00010e0u, 0x0000fe0u, 0x00000e0u
    };
    const auto layout = ScreenUI_StatusBarLayout();
    const uint16_t x = (SPI_SCREEN_LEFT_BAR_W - SPI_SCREEN_STATUS_BADGE_W) / 2u;
    ST7789_FillRect(lcd, x, layout.connectionY, SPI_SCREEN_STATUS_BADGE_W, SPI_SCREEN_CONNECTION_ICON_H, style.bg);
    if (icon == ScreenConnectionIcon::Maintenance) {
        ScreenUI_DrawStringCenteredInBox(lcd, x, layout.connectionY, SPI_SCREEN_STATUS_BADGE_W,
            SPI_SCREEN_CONNECTION_ICON_H, "ISP", style.icon, style.bg, SPI_SCREEN_STATUS_META_TEXT_SCALE);
        return;
    }
    const uint32_t* rows = icon == ScreenConnectionIcon::Wireless ? radio : usb;
    for (uint16_t y = 0; y < SPI_SCREEN_CONNECTION_ICON_H; ++y) {
        uint16_t col = 0;
        while (col < SPI_SCREEN_STATUS_BADGE_W) {
            if ((rows[y] & (1u << (27u - col))) == 0u) { ++col; continue; }
            const uint16_t start = col++;
            while (col < SPI_SCREEN_STATUS_BADGE_W && (rows[y] & (1u << (27u - col)))) ++col;
            ST7789_FillRect(lcd, x + start, layout.connectionY + y, col - start, 1u, style.icon);
        }
    }
}

void ScreenUI_DrawPlatformBadge(ST7789_Handle* lcd, const char* label, uint32_t fg, uint32_t bg) {
    const uint8_t font = SCREEN_FONT_BADGE_BOLD;
    const uint16_t badgeW = 28u; // Battery body (26px) plus terminal (2px).
    const uint16_t badgeH = SPI_SCREEN_BATTERY_BODY_H;
    const uint16_t x = (SPI_SCREEN_LEFT_BAR_W - badgeW) / 2u;
    const uint16_t y = ScreenUI_StatusBarLayout().platformY + (SPI_SCREEN_STATUS_ICON_H - badgeH) / 2u;
    ST7789_FillRect(lcd, x, y, badgeW, badgeH, fg);
    ScreenUI_DrawStringCenteredInBox(lcd, x, y, badgeW, badgeH, label, bg, fg, font);
}
