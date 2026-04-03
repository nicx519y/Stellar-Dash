#include "screen_control/spi_screen_detail_entries.hpp"

#include "storagemanager.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"

static const char* kSocdLabels[] = {
    "Neutral",
    "Up Priority",
    "Second Input",
    "First Input",
    "Bypass",
};

static GamepadProfile* default_profile(void) {
    return STORAGE_MANAGER.getDefaultGamepadProfile();
}

uint8_t ScreenDetailSocd_InitIndex(void) {
    GamepadProfile* p = default_profile();
    if (!p) return 0;
    uint8_t v = (uint8_t)p->keysConfig.socdMode;
    if (v >= (uint8_t)NUM_SOCD_MODES) v = 0;
    return v;
}

void ScreenDetailSocd_Rotate(uint8_t* ioIndex, int8_t det) {
    if (!ioIndex) return;
    int32_t idx = (int32_t)(*ioIndex) + det;
    if (idx < 0) idx = 0;
    if (idx >= (int32_t)NUM_SOCD_MODES) idx = (int32_t)NUM_SOCD_MODES - 1;
    *ioIndex = (uint8_t)idx;
}

void ScreenDetailSocd_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style) {
    uint8_t selected = ScreenDetailSocd_InitIndex();
    ScreenDetailRender_List(lcd, "SOCD", kSocdLabels, (uint8_t)(sizeof(kSocdLabels) / sizeof(kSocdLabels[0])), index, selected, style);
}

void ScreenDetailSocd_OnConfirm(uint8_t index) {
    GamepadProfile* p = default_profile();
    if (p && index < (uint8_t)NUM_SOCD_MODES) p->keysConfig.socdMode = (SOCDMode)index;
}
