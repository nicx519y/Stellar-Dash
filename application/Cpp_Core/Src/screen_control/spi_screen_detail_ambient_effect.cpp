#include "screen_control/spi_screen_detail_entries.hpp"

#include "storagemanager.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"

static const char* kAmbientEffectLabels[] = {
    "Static",
    "Breathing",
    "Quake",
    "Meteor",
};

static GamepadProfile* default_profile(void) {
    return STORAGE_MANAGER.getDefaultGamepadProfile();
}

uint8_t ScreenDetailAmbientEffect_InitIndex(void) {
    GamepadProfile* p = default_profile();
    if (!p) return 0;
    uint8_t v = (uint8_t)p->ledsConfigs.aroundLedEffect;
    if (v >= (uint8_t)NUM_AROUND_LED_EFFECTS) v = 0;
    return v;
}

void ScreenDetailAmbientEffect_Rotate(uint8_t* ioIndex, int8_t det) {
    if (!ioIndex) return;
    int32_t idx = (int32_t)(*ioIndex) + det;
    if (idx < 0) idx = 0;
    if (idx >= (int32_t)NUM_AROUND_LED_EFFECTS) idx = (int32_t)NUM_AROUND_LED_EFFECTS - 1;
    *ioIndex = (uint8_t)idx;
}

void ScreenDetailAmbientEffect_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style) {
    ScreenDetailRender_List(lcd, "Ambient Effect", kAmbientEffectLabels, (uint8_t)(sizeof(kAmbientEffectLabels) / sizeof(kAmbientEffectLabels[0])), index, style);
}

void ScreenDetailAmbientEffect_OnConfirm(uint8_t index) {
    GamepadProfile* p = default_profile();
    if (p && index < (uint8_t)NUM_AROUND_LED_EFFECTS) p->ledsConfigs.aroundLedEffect = (AroundLEDEffect)index;
}
