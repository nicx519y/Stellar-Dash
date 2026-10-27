#include "screen_control/spi_screen_ui_common.hpp"

// Theme work is CPU-only and stays in ordinary executable RAM, outside scarce ITCM.
// Existing linker/startup rules also keep this module's constants available without QSPI.
static uint32_t screenui_luma8(uint32_t rgb) {
    return ((((rgb >> 16u) & 255u) * 54u) + (((rgb >> 8u) & 255u) * 183u) +
            ((rgb & 255u) * 19u)) >> 8u;
}

__attribute__((optimize("Os"))) void ScreenUI_DrawSliderControls(ST7789_Handle* lcd, uint32_t fg, uint32_t bg) {
    // Clear the same arrow regions, including when returning from a list.
    ScreenUI_DrawListArrows(lcd, false, false, fg, bg);
    const auto layout = ScreenUI_StatusBarLayout();
    const uint16_t cx = ST7789_WIDTH - SPI_SCREEN_RIGHT_BAR_W / 2u;
    ST7789_FillRect(lcd, cx - 6u, layout.topArrowY - 1u, 12u, 2u, fg);
    ST7789_FillRect(lcd, cx - 1u, layout.topArrowY - 6u, 2u, 12u, fg);
    ST7789_FillRect(lcd, cx - 6u, layout.bottomArrowY - 1u, 12u, 2u, fg);
}

__attribute__((optimize("Os"))) void ScreenUI_DrawTintedIcon(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            const uint8_t* rgb565, uint32_t fg, uint32_t bg) {
    if (!lcd || !rgb565 || !w || !h) return;
    for (uint16_t row = 0; row < h; ++row) {
        uint16_t start = 0;
        uint32_t previous = bg;
        for (uint16_t col = 0; col < w; ++col) {
            const uint32_t offset = ((uint32_t)row * w + col) * 2u;
            const uint16_t pixel = rgb565[offset] | ((uint16_t)rgb565[offset + 1u] << 8u);
            const uint32_t r = ((pixel >> 11u) & 31u) * 255u / 31u;
            const uint32_t g = ((pixel >> 5u) & 63u) * 255u / 63u;
            const uint32_t b = (pixel & 31u) * 255u / 31u;
            const uint8_t alpha = (r * 54u + g * 183u + b * 19u) >> 8u;
            const uint32_t color = ScreenUI_MutedTextForBg(fg, bg, 255u - alpha);
            if (col && color != previous) {
                ST7789_FillRect(lcd, x + start, y + row, col - start, 1u, previous);
                start = col;
            }
            previous = color;
        }
        ST7789_FillRect(lcd, x + start, y + row, w - start, 1u, previous);
    }
}

__attribute__((optimize("Os"))) ScreenUiStyle ScreenUI_ColdSteelStyle(bool light, uint32_t accent) {
    // A fixed palette owns surfaces and neutral content; only emphasis follows the user color.
    ScreenUiStyle style = light
        ? ScreenUiStyle{0xEDF3F8u, 0x182C3Cu, 0xCDDCE8u, 0xBDCFDFu,
                        0x435D73u, 0x386786u, 0u, 0xD7E2EBu}
        : ScreenUiStyle{0x101923u, 0xDCE6F0u, 0x30495Eu, 0x3B566Eu,
                        0xA9B9CCu, 0x88B4D7u, 0u, 0x263B4Cu};
    style.accent = ScreenUI_RGB888(accent);
    // Keep arbitrary user colors visible against both selection and slider backgrounds.
    const uint32_t luma = screenui_luma8(style.accent);
    if (!light && luma < 120u) style.accent = ScreenUI_BlendToWhite(style.accent, 128u);
    if (light && luma > 120u) style.accent = ScreenUI_BlendToBlack(style.accent, 160u);
    return style;
}
