#include "screen_control/spi_screen_manager.hpp"

#include <string.h>

#include "storagemanager.hpp"

extern "C" {
#include "st7789.h"
}

extern "C" uint32_t HAL_GetTick(void);

#define SPI_SCREEN_LEFT_BAR_W 40u
#define SPI_SCREEN_RIGHT_BAR_W 40u
#define SPI_SCREEN_Y_OFFSET 34u
#define SPI_SCREEN_STATUS_BAR_TEXT_SCALE 2u
#define SPI_SCREEN_MENU_TEXT_SCALE 2u
#define SPI_SCREEN_MENU_ITEM_H 34u

static ST7789_Handle g_lcd;
static bool g_inited = false;

static uint32_t rgb888(uint32_t v) { return v & 0x00FFFFFFu; }

static uint32_t blend_to_white(uint32_t rgb, uint8_t alpha255) {
    uint32_t r = (rgb >> 16) & 0xFFu;
    uint32_t g = (rgb >> 8) & 0xFFu;
    uint32_t b = rgb & 0xFFu;

    r = r + ((255u - r) * alpha255) / 255u;
    g = g + ((255u - g) * alpha255) / 255u;
    b = b + ((255u - b) * alpha255) / 255u;

    return (r << 16) | (g << 8) | b;
}

static uint16_t char_cell_w(uint8_t scale) {
    if (scale == 0) scale = 1;
    if (scale == 3) return 9;
    return (uint16_t)(6u * scale);
}

static uint16_t char_cell_h(uint8_t scale) {
    if (scale == 0) scale = 1;
    if (scale == 3) return 12;
    return (uint16_t)(8u * scale);
}

static uint16_t text_px_w(const char* s, uint8_t scale) {
    if (!s || scale == 0) return 0;
    uint16_t len = (uint16_t)strlen(s);
    return (uint16_t)(len * char_cell_w(scale));
}

static uint16_t text_px_h(uint8_t scale) {
    if (scale == 0) return 0;
    return char_cell_h(scale);
}

static void draw_string_centered_in_box(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* s, uint32_t fg, uint32_t bg, uint8_t scale) {
    if (!s || scale == 0) return;
    const uint16_t tw = text_px_w(s, scale);
    const uint16_t th = text_px_h(scale);

    if (w < tw || h < th) {
        uint16_t maxChars = (uint16_t)(w / char_cell_w(scale));
        if (maxChars == 0) return;
        char tmp[32] = {0};
        if (maxChars >= sizeof(tmp)) maxChars = (uint16_t)(sizeof(tmp) - 1);
        memcpy(tmp, s, maxChars);
        tmp[maxChars] = '\0';
        const uint16_t tw2 = text_px_w(tmp, scale);
        const uint16_t th2 = th;
        const uint16_t cx = (uint16_t)(x + (w - tw2) / 2u);
        const uint16_t cy = (uint16_t)(y + (h - th2) / 2u);
        ST7789_DrawString(&g_lcd, cx, cy, tmp, fg, bg, scale);
        return;
    }

    const uint16_t cx = (uint16_t)(x + (w - tw) / 2u);
    const uint16_t cy = (uint16_t)(y + (h - th) / 2u);
    ST7789_DrawString(&g_lcd, cx, cy, s, fg, bg, scale);
}

static const char* input_mode_abbrev(InputMode m) {
    switch (m) {
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

struct MenuMeta {
    uint8_t id;
    uint32_t bit;
    const char* label;
};

static const MenuMeta kMenuMeta[] = {
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

static const MenuMeta* find_menu_meta(uint8_t id) {
    for (size_t i = 0; i < sizeof(kMenuMeta) / sizeof(kMenuMeta[0]); i++) {
        if (kMenuMeta[i].id == id) return &kMenuMeta[i];
    }
    return nullptr;
}

static uint32_t ease_out_cubic(uint32_t t, uint32_t d) {
    if (d == 0) return 1000;
    if (t >= d) return 1000;
    uint32_t u = (uint32_t)(1000u - (t * 1000u) / d);
    uint32_t u2 = (u * u) / 1000u;
    uint32_t u3 = (u2 * u) / 1000u;
    return 1000u - u3;
}

void SPIScreenManager::setup() {
    if (g_inited) return;
    memset(&g_lcd, 0, sizeof(g_lcd));

    ST7789_Config cfg = {0};
    cfg.width = ST7789_WIDTH;
    cfg.height = ST7789_HEIGHT;
    cfg.x_offset = 0;
    cfg.y_offset = SPI_SCREEN_Y_OFFSET;
    cfg.color_mode = ST7789_COLOR_MODE_RGB565;
    cfg.rotation = ST7789_ROTATION_270;
    cfg.invert = true;
    cfg.fps = 12;
    cfg.use_framebuffer = true;
    cfg.bl_htim = NULL;
    cfg.bl_tim_channel = 0;
    ST7789_Init(&g_lcd, &cfg);
    g_inited = true;

    rebuildMenu();

    ST7789_FrameBegin(&g_lcd);
    ST7789_SetBacklight(&g_lcd, 100);
    ST7789_FillScreen(&g_lcd, rgb888(STORAGE_MANAGER.config.screenControl.backgroundColor));
    renderFrame();
    ST7789_FrameEnd(&g_lcd);
}

void SPIScreenManager::rebuildMenu() {
    menuCount = 0;
    menuIndex = 0;
    const ScreenControlConfig& sc = STORAGE_MANAGER.config.screenControl;
    const uint32_t mask = sc.featuresMask;

    for (uint32_t i = 0; i < SCREEN_FEATURE_COUNT; i++) {
        uint8_t id = sc.featuresOrder[i];
        const MenuMeta* meta = find_menu_meta(id);
        if (!meta) continue;
        if ((mask & meta->bit) == 0) continue;
        if (menuCount < (uint8_t)(sizeof(menuIds) / sizeof(menuIds[0]))) {
            menuIds[menuCount++] = id;
        }
    }

    if (menuCount == 0) {
        for (size_t i = 0; i < sizeof(kMenuMeta) / sizeof(kMenuMeta[0]); i++) {
            if (menuCount < (uint8_t)(sizeof(menuIds) / sizeof(menuIds[0]))) {
                menuIds[menuCount++] = kMenuMeta[i].id;
            }
        }
    }
}

void SPIScreenManager::beginAnimation(int dir) {
    animActive = true;
    animDir = dir;
    animStartMs = HAL_GetTick();
}

void SPIScreenManager::menuPrev() {
    if (menuCount == 0) return;
    if (animActive) return;
    if (menuIndex == 0) return;
    beginAnimation(-1);
}

void SPIScreenManager::menuNext() {
    if (menuCount == 0) return;
    if (animActive) return;
    if (menuIndex + 1 >= menuCount) return;
    beginAnimation(1);
}

void SPIScreenManager::loop() {
    if (!g_inited) return;
    if (!ST7789_FrameBegin(&g_lcd)) return;

    uint32_t nowMs = HAL_GetTick();
    if (animActive) {
        uint32_t elapsed = nowMs - animStartMs;
        if (elapsed >= 160u) {
            animActive = false;
            if (animDir > 0 && menuIndex + 1 < menuCount) menuIndex++;
            else if (animDir < 0 && menuIndex > 0) menuIndex--;
            animDir = 0;
        }
    }

    uint8_t brightness = STORAGE_MANAGER.config.screenControl.brightness;
    if (brightness > 100) brightness = 100;
    ST7789_SetBacklight(&g_lcd, brightness);
    ST7789_FillScreen(&g_lcd, rgb888(STORAGE_MANAGER.config.screenControl.backgroundColor));
    renderFrame();
    ST7789_FrameEnd(&g_lcd);
}

void SPIScreenManager::renderFrame() {
    renderBars();
    renderList();
}

void SPIScreenManager::renderBars() {
    const uint16_t w = ST7789_WIDTH;
    const uint16_t h = ST7789_HEIGHT;
    const uint16_t leftW = SPI_SCREEN_LEFT_BAR_W;
    const uint16_t rightW = SPI_SCREEN_RIGHT_BAR_W;
    const uint16_t rightX = (uint16_t)(w - rightW);
    const uint32_t barBg = rgb888(STORAGE_MANAGER.config.screenControl.backgroundColor);
    const uint32_t textColor = rgb888(STORAGE_MANAGER.config.screenControl.textColor);

    ST7789_FillRect(&g_lcd, 0, 0, leftW, h, barBg);
    ST7789_FillRect(&g_lcd, rightX, 0, rightW, h, barBg);

    const char* mode = input_mode_abbrev(STORAGE_MANAGER.getInputMode());
    draw_string_centered_in_box(0, 0, leftW, h, mode, textColor, barBg, SPI_SCREEN_STATUS_BAR_TEXT_SCALE);

    const MenuMeta* prev = (menuIndex > 0) ? find_menu_meta(menuIds[menuIndex - 1]) : nullptr;
    const MenuMeta* next = (menuIndex + 1 < menuCount) ? find_menu_meta(menuIds[menuIndex + 1]) : nullptr;

    const uint16_t areaH = (uint16_t)(h / 3u);
    const uint16_t topY = 0;
    const uint16_t midY = areaH;
    const uint16_t botY = (uint16_t)(areaH * 2u);
    const uint16_t botH = (uint16_t)(h - botY);

    if (prev && prev->label) draw_string_centered_in_box(rightX, topY, rightW, areaH, prev->label, textColor, barBg, SPI_SCREEN_STATUS_BAR_TEXT_SCALE);
    draw_string_centered_in_box(rightX, midY, rightW, areaH, "OK", textColor, barBg, SPI_SCREEN_STATUS_BAR_TEXT_SCALE);
    if (next && next->label) draw_string_centered_in_box(rightX, botY, rightW, botH, next->label, textColor, barBg, SPI_SCREEN_STATUS_BAR_TEXT_SCALE);
}

void SPIScreenManager::renderList() {
    if (menuCount == 0) return;

    const uint16_t w = ST7789_WIDTH;
    const uint16_t h = ST7789_HEIGHT;
    const uint16_t listX = SPI_SCREEN_LEFT_BAR_W;
    const uint16_t listW = (uint16_t)(w - SPI_SCREEN_LEFT_BAR_W - SPI_SCREEN_RIGHT_BAR_W);
    const uint16_t itemH = SPI_SCREEN_MENU_ITEM_H;
    const uint16_t centerY = (uint16_t)(h / 2u);
    const uint16_t anchorY = (uint16_t)(centerY - itemH / 2u);
    const uint32_t textColor = rgb888(STORAGE_MANAGER.config.screenControl.textColor);
    const uint32_t bg = rgb888(STORAGE_MANAGER.config.screenControl.backgroundColor);
    const uint32_t selBg = blend_to_white(bg, 32u);

    int offsetPx = 0;
    if (animActive) {
        uint32_t eased = ease_out_cubic(HAL_GetTick() - animStartMs, 160u);
        int signedPx = (int)((int)itemH * (int)eased / 1000);
        offsetPx = -animDir * signedPx;
    }

    for (uint8_t i = 0; i < menuCount; i++) {
        int dy = (int)((int)i - (int)menuIndex) * (int)itemH;
        int y = (int)anchorY + dy + offsetPx;
        if (y < -(int)itemH || y > (int)h) continue;

        const MenuMeta* meta = find_menu_meta(menuIds[i]);
        if (!meta || !meta->label) continue;

        uint16_t yy = (uint16_t)((y < 0) ? 0 : y);
        uint16_t itemTextY = (uint16_t)(yy + (itemH - char_cell_h(SPI_SCREEN_MENU_TEXT_SCALE)) / 2u);
        uint16_t itemTextX = (uint16_t)(listX + 4);
        if (i == menuIndex && !animActive) {
            ST7789_FillRect(&g_lcd, listX, yy, listW, itemH, selBg);
            ST7789_DrawString(&g_lcd, itemTextX, itemTextY, meta->label, textColor, selBg, SPI_SCREEN_MENU_TEXT_SCALE);
        } else {
            ST7789_DrawString(&g_lcd, itemTextX, itemTextY, meta->label, textColor, bg, SPI_SCREEN_MENU_TEXT_SCALE);
        }
    }
}
