#include "screen_control/spi_screen_detail_entries.hpp"

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
    ScreenDetailRender_Info(lcd, "Calibration Mode", "Calibration Mode", style);
}

void ScreenDetailCalibration_OnConfirm(uint8_t index) {
    (void)index;
}
