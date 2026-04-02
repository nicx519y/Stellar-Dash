#include "screen_control/spi_screen_detail_entries.hpp"

#include "storagemanager.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"

static const char* kLightEffectLabels[] = {
    "Static",
    "Breathing",
    "Star",
    "Flowing",
    "Ripple",
    "Transform",
};

static GamepadProfile* default_profile(void) {
    return STORAGE_MANAGER.getDefaultGamepadProfile();
}

uint8_t ScreenDetailLightEffect_InitIndex(void) {
    GamepadProfile* p = default_profile();
    if (!p) return 0;
    uint8_t v = (uint8_t)p->ledsConfigs.ledEffect;
    if (v >= (uint8_t)NUM_EFFECTS) v = 0;
    return v;
}

void ScreenDetailLightEffect_Rotate(uint8_t* ioIndex, int8_t det) {
    if (!ioIndex) return;
    int32_t idx = (int32_t)(*ioIndex) + det;
    if (idx < 0) idx = 0;
    if (idx >= (int32_t)NUM_EFFECTS) idx = (int32_t)NUM_EFFECTS - 1;
    *ioIndex = (uint8_t)idx;
}

void ScreenDetailLightEffect_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style) {
    ScreenDetailRender_List(lcd, "Light Effect", kLightEffectLabels, (uint8_t)(sizeof(kLightEffectLabels) / sizeof(kLightEffectLabels[0])), index, style);
}

void ScreenDetailLightEffect_OnConfirm(uint8_t index) {
    GamepadProfile* p = default_profile();
    if (p && index < (uint8_t)NUM_EFFECTS) p->ledsConfigs.ledEffect = (LEDEffect)index;
}
