#include "screen_control/spi_screen_detail_entries.hpp"

#include "storagemanager.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"
#include "screen_control/spi_screen_ui_common.hpp"

uint8_t ScreenDetailWebConfig_InitIndex(void) {
    return 0;
}

void ScreenDetailWebConfig_Rotate(uint8_t* ioIndex, int8_t det) {
    (void)ioIndex;
    (void)det;
}

void ScreenDetailWebConfig_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style) {
    (void)index;
    if (!lcd) return;

    const uint16_t w = ST7789_WIDTH;
    const uint16_t h = ST7789_HEIGHT;
    const uint16_t listX = SPI_SCREEN_LEFT_BAR_W;
    const uint16_t listW = (uint16_t)(w - SPI_SCREEN_LEFT_BAR_W - SPI_SCREEN_RIGHT_BAR_W);
    const uint16_t x = (uint16_t)(listX + 8u);
    const uint16_t y = 8u;

    ST7789_FillRect(lcd, listX, 0, listW, h, style.bg);
    ST7789_DrawString(lcd, x, y, "Web Config", style.text, style.bg, 2);

    const uint16_t lineH = ScreenUI_CharCellH(1);
    const uint16_t gap = 2u;
    const uint16_t blockH = (uint16_t)(lineH * 3u + gap * 2u);
    uint16_t ty = (blockH + 8u <= h) ? (uint16_t)(h - 8u - blockH) : (uint16_t)(y + ScreenUI_CharCellH(2) + 8u);

    ST7789_DrawString(lcd, x, ty, "Open a browser and visit", style.text, style.bg, 1);
    ty = (uint16_t)(ty + lineH + gap);
    ST7789_DrawString(lcd, x, ty, "http://192.168.7.1 for Web Config.", style.text, style.bg, 1);
    ty = (uint16_t)(ty + lineH + gap);
    ST7789_DrawString(lcd, x, ty, "Press Quit or Back to exit.", style.text, style.bg, 1);
}

void ScreenDetailWebConfig_OnConfirm(uint8_t index) {
    (void)index;
    STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
    STORAGE_MANAGER.saveConfig();
    NVIC_SystemReset();
}

void ScreenDetailWebConfig_OnBack(void) {
    STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
    STORAGE_MANAGER.saveConfig();
    NVIC_SystemReset();
}
