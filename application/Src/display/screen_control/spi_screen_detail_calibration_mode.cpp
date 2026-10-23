#include "screen_control/spi_screen_detail_entries.hpp"
#include "main_runtime_control.hpp"

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
        "Calibration data cleared.",
        "Press each button fully.",
        "Hold until its LED is green."
    };
    ScreenDetailRender_TitleLines(lcd, "Calibration Mode", lines, (uint8_t)(sizeof(lines) / sizeof(lines[0])), style);
}

void ScreenDetailCalibration_OnConfirm(uint8_t index) {
    (void)index;
    STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
    STORAGE_MANAGER.saveConfig();
    MainRuntime_RequestReset();
}
