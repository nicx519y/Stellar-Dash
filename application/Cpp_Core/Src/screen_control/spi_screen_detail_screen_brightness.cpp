#include "screen_control/spi_screen_detail_entries.hpp"

#include "storagemanager.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"

static uint8_t clamp_u8_i32(int32_t v, uint8_t lo, uint8_t hi) {
    if (v < (int32_t)lo) return lo;
    if (v > (int32_t)hi) return hi;
    return (uint8_t)v;
}

uint8_t ScreenDetailScreenBrightness_InitIndex(void) {
    return STORAGE_MANAGER.config.screenControl.brightness;
}

void ScreenDetailScreenBrightness_Rotate(uint8_t* ioIndex, int8_t det) {
    if (!ioIndex) return;
    uint8_t prev = *ioIndex;
    int32_t next = (int32_t)(*ioIndex) + (int32_t)det * SPI_SCREEN_SLIDER_STEP;
    *ioIndex = clamp_u8_i32(next, 0, 100);
    if (*ioIndex == prev) return;
    STORAGE_MANAGER.config.screenControl.brightness = *ioIndex;
    ScreenUI_RequestDeferredSave(2000u);
}

void ScreenDetailScreenBrightness_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style) {
    ScreenDetailRender_Slider(lcd, "Screen Brightness", index, style);
}

void ScreenDetailScreenBrightness_OnConfirm(uint8_t index) {
    uint8_t current = STORAGE_MANAGER.config.screenControl.brightness;
    uint8_t restore = index;
    if (restore == 0) restore = 50;
    if (current > 0) STORAGE_MANAGER.config.screenControl.brightness = 0;
    else STORAGE_MANAGER.config.screenControl.brightness = restore;
    ScreenUI_RequestDeferredSave(2000u);
}
