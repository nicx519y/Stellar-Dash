#include "screen_control/spi_screen_detail_entries.hpp"

#include "storagemanager.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"

uint8_t ScreenDetailWebConfig_InitIndex(void) {
    return 0;
}

void ScreenDetailWebConfig_Rotate(uint8_t* ioIndex, int8_t det) {
    (void)ioIndex;
    (void)det;
}

void ScreenDetailWebConfig_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style) {
    (void)index;
    const char* lines[] = {
        "Open a browser and visit",
        "http://192.168.7.1 for Web Config.",
        "Press Quit or Back to exit."
    };
    ScreenDetailRender_TitleLines(lcd, "Web Config", lines, (uint8_t)(sizeof(lines) / sizeof(lines[0])), style);
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
