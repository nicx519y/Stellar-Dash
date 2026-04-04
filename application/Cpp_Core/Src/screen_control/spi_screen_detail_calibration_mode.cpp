#include "screen_control/spi_screen_detail_entries.hpp"

#include "storagemanager.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"

uint8_t ScreenDetailCalibration_InitIndex(void) {
    return 0;
}

void ScreenDetailCalibration_Rotate(uint8_t* ioIndex, int8_t det) {
    (void)ioIndex;
    (void)det;
}

void ScreenDetailCalibration_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style) {
    (void)index;
    const char* lines[] = {
        "Button calibration data has been cleared.",
        "Press and hold each button fully",
        "until its indicator turns green,",
        "which means that button is calibrated."
    };
    ScreenDetailRender_TitleLines(lcd, "Calibration Mode", lines, (uint8_t)(sizeof(lines) / sizeof(lines[0])), style);
}

void ScreenDetailCalibration_OnConfirm(uint8_t index) {
    (void)index;
    STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
    STORAGE_MANAGER.saveConfig();
    NVIC_SystemReset();
}
