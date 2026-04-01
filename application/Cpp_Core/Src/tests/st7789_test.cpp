#include "tests/st7789_test.hpp"

#include <stdio.h>
#include <string.h>

extern "C" {
#include "st7789.h"
}

#include "micro_timer.hpp"

static ST7789_Handle g_lcd;
static bool g_inited = false;

static uint8_t g_brightness = 100;
static bool g_brightnessUp = false;

static uint16_t lcd_w() { return g_lcd.cfg.width; }
static uint16_t lcd_h() { return g_lcd.cfg.height; }

static void draw_calibration_box(uint16_t x, uint16_t y, uint32_t color) {
    const uint16_t s = 18;
    ST7789_DrawRect(&g_lcd, x, y, s, s, color);
    ST7789_DrawLine(&g_lcd, x, y, (int)(x + s - 1), (int)(y + s - 1), color);
    ST7789_DrawLine(&g_lcd, (int)(x + s - 1), y, (int)x, (int)(y + s - 1), color);
}

static void draw_calibration_boxes() {
    const uint16_t w = lcd_w();
    const uint16_t h = lcd_h();
    const uint16_t s = 18;
    const uint16_t margin = 2;

    draw_calibration_box(margin, margin, ST7789_RGB(255, 0, 0));
    draw_calibration_box((uint16_t)(w - s - margin), margin, ST7789_RGB(0, 255, 0));
    draw_calibration_box(margin, (uint16_t)(h - s - margin), ST7789_RGB(0, 0, 255));
    draw_calibration_box((uint16_t)(w - s - margin), (uint16_t)(h - s - margin), ST7789_RGB(255, 255, 0));
}

static void draw_calibration_status() {
    const uint16_t w = lcd_w();
    const uint16_t h = lcd_h();
    const uint16_t y = (uint16_t)(h / 2);
    ST7789_FillRect(&g_lcd, 0, y, w, 14, 0x000000u);
    char buf[64];
    snprintf(buf, sizeof(buf), "W=%u H=%u XO=%u YO=%u", (unsigned)w, (unsigned)h, (unsigned)g_lcd.cfg.x_offset, (unsigned)g_lcd.cfg.y_offset);
    ST7789_DrawString(&g_lcd, 2, (uint16_t)(h / 2), buf, 0xFFFFFFu, 0x000000u, 1);
}

static void draw_calibration_frame() {
    ST7789_FillScreen(&g_lcd, 0x000000u);
    draw_calibration_boxes();
    draw_calibration_status();
}

void ST7789Test::setup() {
    if (g_inited) return;
    memset(&g_lcd, 0, sizeof(g_lcd));
    ST7789_Config cfg = {0};
    cfg.width = ST7789_WIDTH;
    cfg.height = ST7789_HEIGHT;
    cfg.x_offset = 0;
    cfg.y_offset = 35;                                                                                                                                                                                                                           
    cfg.color_mode = ST7789_COLOR_MODE_RGB565;
    cfg.rotation = ST7789_ROTATION_270;
    cfg.invert = true;
    cfg.fps = 12;
    cfg.use_framebuffer = true;
    cfg.bl_htim = NULL;
    cfg.bl_tim_channel = 0;
    ST7789_Init(&g_lcd, &cfg);
    g_inited = true;
    ST7789_FrameBegin(&g_lcd);
    ST7789_FillScreen(&g_lcd, 0x000000u);
    draw_calibration_boxes();
    draw_calibration_status();
    ST7789_FrameEnd(&g_lcd);
}

void ST7789Test::loop() {
    if (!g_inited) return;
    if (!ST7789_FrameBegin(&g_lcd)) return;
    if (g_brightnessUp) {
        if (g_brightness >= 100) g_brightnessUp = false;
        else g_brightness++;
    } else {
        if (g_brightness == 0) g_brightnessUp = true;
        else g_brightness--;
    }
    ST7789_SetBacklight(&g_lcd, g_brightness);
    draw_calibration_frame();
    ST7789_FrameEnd(&g_lcd);
}
