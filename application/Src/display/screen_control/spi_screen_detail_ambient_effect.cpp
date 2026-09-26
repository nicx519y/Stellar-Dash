#include "screen_control/spi_screen_detail_entries.hpp"

#include "storagemanager.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"

static const char* kAmbientEffectLabels[] = {
    "Follow Button LED",
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
    if (p->ledsConfigs.aroundLedSyncToMainLed) return 0;
    uint8_t v = (uint8_t)p->ledsConfigs.aroundLedEffect;
    if (v >= (uint8_t)NUM_AROUND_LED_EFFECTS) v = 0;
    return (uint8_t)(v + 1u);
}

void ScreenDetailAmbientEffect_Rotate(uint8_t* ioIndex, int8_t det) {
    if (!ioIndex) return;
    int32_t idx = (int32_t)(*ioIndex) + det;
    if (idx < 0) idx = 0;
    const int32_t max = (int32_t)(NUM_AROUND_LED_EFFECTS + 1u);
    if (idx >= max) idx = max - 1;
    *ioIndex = (uint8_t)idx;
}

void ScreenDetailAmbientEffect_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style) {
    uint8_t selected = ScreenDetailAmbientEffect_InitIndex();
    ScreenDetailRender_List(lcd, "Ambient Effect", kAmbientEffectLabels, (uint8_t)(sizeof(kAmbientEffectLabels) / sizeof(kAmbientEffectLabels[0])), index, selected, style);
}

void ScreenDetailAmbientEffect_OnConfirm(uint8_t index) {
    GamepadProfile* p = default_profile();
    if (!p) return;
    if (index == 0u) {
        p->ledsConfigs.aroundLedSyncToMainLed = true;
        return;
    }
    p->ledsConfigs.aroundLedSyncToMainLed = false;
    uint8_t eff = (uint8_t)(index - 1u);
    if (eff < (uint8_t)NUM_AROUND_LED_EFFECTS) {
        p->ledsConfigs.aroundLedEffect = (AroundLEDEffect)eff;
    }
}
