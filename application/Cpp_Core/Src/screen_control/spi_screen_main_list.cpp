#include "screen_control/spi_screen_main_list.hpp"

#include "screen_control/spi_screen_ui_common.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"

static const ScreenMenuMeta kMenuMeta[] = {
    {0, SCREEN_FEATURE_INPUT_MODE_SWITCH, "Input Mode"},
    {1, SCREEN_FEATURE_PROFILES_SWITCH, "Profiles"},
    {2, SCREEN_FEATURE_SOCD_MODE_SWITCH, "SOCD"},
    {4, SCREEN_FEATURE_LED_BRIGHTNESS_ADJUST, "Light Brightness"},
    {5, SCREEN_FEATURE_LED_EFFECT_SWITCH, "Light Effect"},
    {6, SCREEN_FEATURE_AMBIENT_BRIGHTNESS_ADJUST, "Ambient Brightness"},
    {7, SCREEN_FEATURE_AMBIENT_EFFECT_SWITCH, "Ambient Effect"},
    {8, SCREEN_FEATURE_SCREEN_BRIGHTNESS_ADJUST, "Screen Brightness"},
    {9, SCREEN_FEATURE_WEB_CONFIG_ENTRY, "Web Config"},
    {10, SCREEN_FEATURE_CALIBRATION_MODE_SWITCH, "Calibration Mode"},
};

const ScreenMenuMeta* ScreenMain_FindMenuMeta(uint8_t id) {
    for (size_t i = 0; i < sizeof(kMenuMeta) / sizeof(kMenuMeta[0]); i++) {
        if (kMenuMeta[i].id == id) return &kMenuMeta[i];
    }
    return nullptr;
}

const char* ScreenMain_InputModeAbbrev(InputMode mode) {
    switch (mode) {
        case InputMode::INPUT_MODE_PS4:
        case InputMode::INPUT_MODE_PS5:
            return "PS";
        case InputMode::INPUT_MODE_XBOX:
            return "Xbox";
        case InputMode::INPUT_MODE_SWITCH:
            return "NS";
        case InputMode::INPUT_MODE_XINPUT:
        default:
            return "PC";
    }
}

uint8_t ScreenMain_RebuildMenuIds(const ScreenControlConfig& sc, uint8_t* outIds, uint8_t outCap) {
    if (!outIds || outCap == 0) return 0;
    uint8_t count = 0;
    const uint32_t mask = sc.featuresMask;

    for (uint32_t i = 0; i < SCREEN_FEATURE_COUNT; i++) {
        uint8_t id = sc.featuresOrder[i];
        const ScreenMenuMeta* meta = ScreenMain_FindMenuMeta(id);
        if (!meta) continue;
        if ((mask & meta->bit) == 0) continue;
        if (count < outCap) outIds[count++] = id;
    }

    if (count == 0) {
        for (size_t i = 0; i < sizeof(kMenuMeta) / sizeof(kMenuMeta[0]); i++) {
            if (count < outCap) outIds[count++] = kMenuMeta[i].id;
        }
    }

    return count;
}

void ScreenMain_RenderList(ST7789_Handle* lcd,
                           const ScreenUiStyle& style,
                           const uint8_t* menuIds,
                           uint8_t menuCount,
                           uint8_t menuIndex,
                           bool animActive,
                           int animDir,
                           uint32_t animStartMs,
                           uint32_t nowMs) {
    if (!lcd || !menuIds || menuCount == 0) return;
    const char* labels[32] = {0};
    uint8_t renderCount = menuCount;
    if (renderCount > (uint8_t)(sizeof(labels) / sizeof(labels[0]))) renderCount = (uint8_t)(sizeof(labels) / sizeof(labels[0]));
    for (uint8_t i = 0; i < renderCount; i++) {
        const ScreenMenuMeta* meta = ScreenMain_FindMenuMeta(menuIds[i]);
        labels[i] = (meta && meta->label) ? meta->label : "";
    }
    ScreenDetailRender_List(lcd, "", labels, renderCount, menuIndex, menuIndex, style, SPI_SCREEN_MENU_ITEM_H, animActive, animDir, animStartMs, nowMs);
}
