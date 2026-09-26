#include "screen_control/spi_screen_detail_entries.hpp"

#include "leds/leds_manager.hpp"
#include "storagemanager.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"

static uint8_t clamp_u8_i32(int32_t v, uint8_t lo, uint8_t hi) {
    if (v < (int32_t)lo) return lo;
    if (v > (int32_t)hi) return hi;
    return (uint8_t)v;
}

static GamepadProfile* default_profile(void) {
    return STORAGE_MANAGER.getDefaultGamepadProfile();
}

uint8_t ScreenDetailLightBrightness_InitIndex(void) {
    GamepadProfile* p = default_profile();
    return p ? p->ledsConfigs.ledBrightness : 0;
}

void ScreenDetailLightBrightness_Rotate(uint8_t* ioIndex, int8_t det) {
    if (!ioIndex) return;
    uint8_t prev = *ioIndex;
    int32_t next = (int32_t)(*ioIndex) + (int32_t)det * SPI_SCREEN_SLIDER_STEP;
    *ioIndex = clamp_u8_i32(next, 0, 100);
    if (*ioIndex == prev) return;

    GamepadProfile* p = default_profile();
    if (p) p->ledsConfigs.ledBrightness = *ioIndex;
    if (p && !p->ledsConfigs.ledEnabled) LEDS_MANAGER.enableSwitch();
    LEDS_MANAGER.setLedsBrightness(*ioIndex);
    ScreenUI_RequestDeferredSave(2000u);
}

void ScreenDetailLightBrightness_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style) {
    ScreenDetailRender_Slider(lcd, "Light Brightness", index, style);
}

void ScreenDetailLightBrightness_OnConfirm(uint8_t index) {
    (void)index;
    LEDS_MANAGER.enableSwitch();
    ScreenUI_RequestDeferredSave(2000u);
}
