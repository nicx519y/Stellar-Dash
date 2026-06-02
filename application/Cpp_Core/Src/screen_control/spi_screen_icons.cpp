#include "screen_control/spi_screen_icons.hpp"

#include "screen_control/spi_screen_ui_common.hpp"

static uint16_t center_pos(uint16_t outer, uint16_t inner) {
    return (outer > inner) ? (uint16_t)((outer - inner) / 2u) : 0u;
}

static void draw_fallback_label(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* label, uint32_t fg, uint32_t bg) {
    uint8_t scale = 2u;
    if (ScreenUI_TextPxW(label, scale) > w || ScreenUI_TextPxH(scale) > h) {
        scale = 1u;
    }
    ScreenUI_DrawStringCenteredInBox(lcd, x, y, w, h, label, fg, bg, scale);
}

static bool draw_asset_centered(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char* name, uint32_t bg) {
    ST7789_AssetInfo info = {};
    if (!ST7789_Assets_Find(name, &info)) return false;
    if (info.type != ST7789_ASSET_TYPE_RGB565LE) return false;
    if (info.width == 0u || info.height == 0u || info.width > w || info.height > h) return false;
    const uint16_t ix = (uint16_t)(x + center_pos(w, info.width));
    const uint16_t iy = (uint16_t)(y + center_pos(h, info.height));
    (void)bg;
    const uint8_t* pixels = (const uint8_t*)info.data;
    for (uint16_t row = 0; row < info.height; row++) {
        for (uint16_t col = 0; col < info.width; col++) {
            const uint32_t off = ((uint32_t)row * info.width + col) * 2u;
            const uint16_t c = (uint16_t)(pixels[off] | ((uint16_t)pixels[off + 1u] << 8));
            if (c == 0xF81Fu) continue;
            const uint8_t r = (uint8_t)(((c >> 11) & 0x1Fu) << 3);
            const uint8_t g = (uint8_t)(((c >> 5) & 0x3Fu) << 2);
            const uint8_t b = (uint8_t)((c & 0x1Fu) << 3);
            ST7789_DrawPixel(lcd, (uint16_t)(ix + col), (uint16_t)(iy + row), ST7789_RGB(r, g, b));
        }
    }
    return true;
}

static void draw_status_slash(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t size, uint32_t color) {
    ST7789_DrawLine(lcd, (int)(x + 2u), (int)(y + size - 3u), (int)(x + size - 3u), (int)(y + 2u), color);
    ST7789_DrawLine(lcd, (int)(x + 3u), (int)(y + size - 3u), (int)(x + size - 3u), (int)(y + 3u), color);
}

static void draw_status_error(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t size, uint32_t color) {
    const uint16_t ex = (uint16_t)(x + size - 5u);
    const uint16_t ey = (uint16_t)(y + 1u);
    ST7789_DrawLine(lcd, ex, ey, ex, (int)(ey + 5u), color);
    ST7789_DrawPixel(lcd, ex, (uint16_t)(ey + 7u), color);
    ST7789_DrawPixel(lcd, (uint16_t)(ex + 1u), (uint16_t)(ey + 7u), color);
}

void ScreenIcon_DrawInputModeLogo(ST7789_Handle* lcd,
                                  uint16_t x,
                                  uint16_t y,
                                  uint16_t w,
                                  uint16_t h,
                                  InputMode mode,
                                  uint32_t fg,
                                  uint32_t bg) {
    if (!lcd) return;
    ST7789_FillRect(lcd, x, y, w, h, bg);
    switch (mode) {
    case InputMode::INPUT_MODE_PS4:
    case InputMode::INPUT_MODE_PS5:
        if (draw_asset_centered(lcd, x, y, w, h, "status_ps", bg)) return;
        draw_fallback_label(lcd, x, y, w, h, "PS", fg, bg);
        return;
    case InputMode::INPUT_MODE_XBOX:
        if (draw_asset_centered(lcd, x, y, w, h, "status_xbox", bg)) return;
        draw_fallback_label(lcd, x, y, w, h, "Xbox", fg, bg);
        return;
    case InputMode::INPUT_MODE_SWITCH:
        if (draw_asset_centered(lcd, x, y, w, h, "status_ns", bg)) return;
        draw_fallback_label(lcd, x, y, w, h, "NS", fg, bg);
        return;
    case InputMode::INPUT_MODE_XINPUT:
    default:
        if (draw_asset_centered(lcd, x, y, w, h, "status_pc", bg)) return;
        draw_fallback_label(lcd, x, y, w, h, "PC", fg, bg);
        return;
    }
}

void ScreenIcon_DrawConnectionMode(ST7789_Handle* lcd,
                                   uint16_t x,
                                   uint16_t y,
                                   uint16_t w,
                                   uint16_t h,
                                   ConnectionMode mode,
                                   ConnectionLinkState state,
                                   uint32_t fg,
                                   uint32_t bg,
                                   uint32_t nowMs) {
    if (!lcd) return;
    const uint16_t size = 24u;
    const uint16_t ix = (uint16_t)(x + center_pos(w, size));
    const uint16_t iy = (uint16_t)(y + center_pos(h, size));
    uint32_t iconColor = fg;
    const bool connected = (state == ConnectionLinkState::Connected);
    const bool connecting = (state == ConnectionLinkState::Connecting);
    const bool error = (state == ConnectionLinkState::Error);

    ST7789_FillRect(lcd, x, y, w, h, bg);

    const char* assetName = (mode == ConnectionMode::CONNECTION_MODE_USB) ? "status_usb" : "status_rf24g";
    const bool assetDrawn = draw_asset_centered(lcd, x, y, w, h, assetName, bg);
    if (assetDrawn) {
        if (!connected && !connecting && !error) {
            draw_status_slash(lcd, ix, iy, size, fg);
        }
        if (error) {
            draw_status_error(lcd, ix, iy, size, fg);
        }
        return;
    }

    if (!connected && !connecting && !error) {
        iconColor = ScreenUI_MutedTextForBg(fg, bg, 120u);
    }
    if (connecting && ((nowMs / 300u) & 1u) != 0u) {
        iconColor = ScreenUI_MutedTextForBg(fg, bg, 80u);
    }

    const char* fallback = "2.4?";
    if (mode == ConnectionMode::CONNECTION_MODE_USB) {
        fallback = connected ? "USB" : (error ? "USB!" : (connecting ? "USB~" : "USB?"));
    } else {
        fallback = connected ? "2.4G" : (error ? "2.4!" : (connecting ? "2.4~" : "2.4?"));
    }
    draw_fallback_label(lcd, x, y, w, h, fallback, iconColor, bg);

    if (!connected && !connecting && !error) {
        draw_status_slash(lcd, ix, iy, size, fg);
    }
    if (error) {
        draw_status_error(lcd, ix, iy, size, fg);
    }
}
