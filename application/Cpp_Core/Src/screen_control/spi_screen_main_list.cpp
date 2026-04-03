#include "screen_control/spi_screen_main_list.hpp"

#include "screen_control/spi_screen_ui_common.hpp"

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
    {3, SCREEN_FEATURE_TOURNAMENT_MODE_SWITCH, "Tournament Mode"},
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

    const uint16_t w = ST7789_WIDTH;
    const uint16_t h = ST7789_HEIGHT;
    const uint16_t listX = SPI_SCREEN_LEFT_BAR_W;
    const uint16_t listW = (uint16_t)(w - SPI_SCREEN_LEFT_BAR_W - SPI_SCREEN_RIGHT_BAR_W);
    const uint16_t itemH = SPI_SCREEN_MENU_ITEM_H;
    const uint16_t centerY = (uint16_t)(h / 2u);
    const uint16_t anchorY = (uint16_t)(centerY - itemH / 2u);
    const uint32_t normalText = ScreenUI_MutedTextForBg(style.text, style.bg, 80u);

    int offsetPx = 0;
    if (animActive) {
        uint32_t eased = ScreenUI_EaseOutCubic(nowMs - animStartMs, SPI_SCREEN_ANIM_MS);
        int signedPx = (int)((int)itemH * (int)eased / 1000);
        offsetPx = -animDir * signedPx;
    }

    for (uint8_t i = 0; i < menuCount; i++) {
        int dy = (int)((int)i - (int)menuIndex) * (int)itemH;
        int y = (int)anchorY + dy + offsetPx;
        if (y < -(int)itemH || y > (int)h) continue;

        const ScreenMenuMeta* meta = ScreenMain_FindMenuMeta(menuIds[i]);
        if (!meta || !meta->label) continue;

        uint16_t yy = (uint16_t)((y < 0) ? 0 : y);
        uint16_t itemTextY = (uint16_t)(yy + (itemH - ScreenUI_CharCellH(SPI_SCREEN_MENU_TEXT_SCALE)) / 2u);
        uint16_t itemTextX = (uint16_t)(listX + 4);
        if (i == menuIndex && !animActive) {
            ST7789_FillRect(lcd, listX, yy, listW, itemH, style.selBg);
            ST7789_DrawString(lcd, itemTextX, itemTextY, meta->label, style.text, style.selBg, SPI_SCREEN_MENU_TEXT_SCALE);
        } else {
            ST7789_DrawString(lcd, itemTextX, itemTextY, meta->label, normalText, style.bg, SPI_SCREEN_MENU_TEXT_SCALE);
        }
    }
}
