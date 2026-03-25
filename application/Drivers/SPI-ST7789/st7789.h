#ifndef __ST7789_H
#define __ST7789_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ST7789_WIDTH  320u
#define ST7789_HEIGHT 172u

#define ST7789_SCL_PORT GPIOK
#define ST7789_SCL_PIN  GPIO_PIN_0

#define ST7789_SDA_PORT GPIOJ
#define ST7789_SDA_PIN  GPIO_PIN_10

#define ST7789_CS_PORT  GPIOH
#define ST7789_CS_PIN   GPIO_PIN_5

#define ST7789_DC_PORT  GPIOJ
#define ST7789_DC_PIN   GPIO_PIN_11

#define ST7789_BL_PORT  GPIOH
#define ST7789_BL_PIN   GPIO_PIN_6

typedef enum
{
    ST7789_COLOR_MODE_RGB565 = 0,
    ST7789_COLOR_MODE_RGB666 = 1
} ST7789_ColorMode;

typedef enum
{
    ST7789_ROTATION_0 = 0,
    ST7789_ROTATION_90 = 1,
    ST7789_ROTATION_180 = 2,
    ST7789_ROTATION_270 = 3
} ST7789_Rotation;

typedef struct
{
    uint16_t width;
    uint16_t height;
    uint16_t x_offset;
    uint16_t y_offset;
    ST7789_ColorMode color_mode;
    ST7789_Rotation rotation;
    bool invert;

    TIM_HandleTypeDef* bl_htim;
    uint32_t bl_tim_channel;
} ST7789_Config;

typedef struct
{
    ST7789_Config cfg;
    bool inited;
} ST7789_Handle;

uint32_t ST7789_RGB(uint8_t r, uint8_t g, uint8_t b);

void ST7789_Init(ST7789_Handle* lcd, const ST7789_Config* cfg);
bool ST7789_IsInited(const ST7789_Handle* lcd);

void ST7789_AttachBacklightPWM(ST7789_Handle* lcd, TIM_HandleTypeDef* htim, uint32_t channel);

void ST7789_SetRotation(ST7789_Handle* lcd, ST7789_Rotation rotation);
void ST7789_SetOffset(ST7789_Handle* lcd, uint16_t x_offset, uint16_t y_offset);

void ST7789_SetBacklight(ST7789_Handle* lcd, uint8_t percent);

void ST7789_FillScreen(ST7789_Handle* lcd, uint32_t rgb888);
void ST7789_FillRect(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t rgb888);
void ST7789_DrawPixel(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint32_t rgb888);
void ST7789_DrawLine(ST7789_Handle* lcd, int x0, int y0, int x1, int y1, uint32_t rgb888);
void ST7789_DrawRect(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t rgb888);
void ST7789_DrawCircle(ST7789_Handle* lcd, int x0, int y0, int r, uint32_t rgb888);
void ST7789_FillCircle(ST7789_Handle* lcd, int x0, int y0, int r, uint32_t rgb888);

void ST7789_DrawChar(ST7789_Handle* lcd, uint16_t x, uint16_t y, char c, uint32_t fg_rgb888, uint32_t bg_rgb888, uint8_t scale);
void ST7789_DrawString(ST7789_Handle* lcd, uint16_t x, uint16_t y, const char* s, uint32_t fg_rgb888, uint32_t bg_rgb888, uint8_t scale);

#ifdef __cplusplus
}
#endif

#endif
