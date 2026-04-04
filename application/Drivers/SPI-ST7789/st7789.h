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

#define ST7789_SPI_MOSI_AF GPIO_AF5_SPI5

#define ST7789_CS_PORT  GPIOH
#define ST7789_CS_PIN   GPIO_PIN_5

#define ST7789_DC_PORT  GPIOJ
#define ST7789_DC_PIN   GPIO_PIN_11

#define ST7789_BL_PORT  GPIOH
#define ST7789_BL_PIN   GPIO_PIN_6

#define ST7789_SPI_INSTANCE SPI5
#define ST7789_SPI_DMA_STREAM DMA2_Stream2
#define ST7789_SPI_DMA_REQUEST DMA_REQUEST_SPI5_TX
#define ST7789_SPI_DMA_IRQn DMA2_Stream2_IRQn



#define ST7789_BL_ON_STATE  GPIO_PIN_RESET
#define ST7789_BL_OFF_STATE GPIO_PIN_SET

#define ST7789_DEFAULT_FPS 12u

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
    uint8_t fps;
    bool use_framebuffer;

    TIM_HandleTypeDef* bl_htim;
    uint32_t bl_tim_channel;
} ST7789_Config;

typedef struct
{
    ST7789_Config cfg;
    bool inited;
    bool frame_blocked;
    uint32_t last_frame_ms;
    bool framebuffer_enabled;
    uint16_t* fb_front;
    uint16_t* fb_back;
    bool dirty_valid;
    uint16_t dirty_x0;
    uint16_t dirty_y0;
    uint16_t dirty_x1;
    uint16_t dirty_y1;
} ST7789_Handle;

typedef enum
{
    ST7789_BITMAP_RGB565_BE = 0,
    ST7789_BITMAP_RGB565_LE = 1,
    ST7789_BITMAP_RGB888 = 2,
    ST7789_BITMAP_ARGB8888 = 3
} ST7789_BitmapFormat;

uint32_t ST7789_RGB(uint8_t r, uint8_t g, uint8_t b);

void ST7789_Init(ST7789_Handle* lcd, const ST7789_Config* cfg);
bool ST7789_IsInited(const ST7789_Handle* lcd);
bool ST7789_IsFrameBlocked(const ST7789_Handle* lcd);
bool ST7789_FrameBegin(ST7789_Handle* lcd);
void ST7789_FrameEnd(ST7789_Handle* lcd);
void ST7789_SPI_DMA_IRQHandler(void);
void ST7789_SPI_IRQHandler(void);

void ST7789_AttachBacklightPWM(ST7789_Handle* lcd, TIM_HandleTypeDef* htim, uint32_t channel);

void ST7789_SetRotation(ST7789_Handle* lcd, ST7789_Rotation rotation);
void ST7789_SetOffset(ST7789_Handle* lcd, uint16_t x_offset, uint16_t y_offset);

void ST7789_SetBacklight(ST7789_Handle* lcd, uint8_t percent);

void ST7789_FillScreen(ST7789_Handle* lcd, uint32_t rgb888);
bool ST7789_FillScreenAsync(ST7789_Handle* lcd, uint32_t rgb888);
bool ST7789_IsTransferBusy(const ST7789_Handle* lcd);
void ST7789_FillRect(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t rgb888);
void ST7789_DrawPixel(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint32_t rgb888);
void ST7789_DrawLine(ST7789_Handle* lcd, int x0, int y0, int x1, int y1, uint32_t rgb888);
void ST7789_DrawRect(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t rgb888);
void ST7789_DrawCircle(ST7789_Handle* lcd, int x0, int y0, int r, uint32_t rgb888);
void ST7789_FillCircle(ST7789_Handle* lcd, int x0, int y0, int r, uint32_t rgb888);

void ST7789_DrawChar(ST7789_Handle* lcd, uint16_t x, uint16_t y, char c, uint32_t fg_rgb888, uint32_t bg_rgb888, uint8_t scale);
void ST7789_DrawString(ST7789_Handle* lcd, uint16_t x, uint16_t y, const char* s, uint32_t fg_rgb888, uint32_t bg_rgb888, uint8_t scale);

void ST7789_DrawBitmap(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const void* pixels, ST7789_BitmapFormat format, uint32_t stride_bytes);
void ST7789_DrawBitmap1BPP(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* bits, uint32_t stride_bytes, uint32_t fg_rgb888, uint32_t bg_rgb888);

bool ST7789_GIF_GetCanvasSize(const uint8_t* gif, size_t gif_len, uint16_t* out_w, uint16_t* out_h);
bool ST7789_GIF_RenderFirstFrame(ST7789_Handle* lcd, uint16_t x, uint16_t y, const uint8_t* gif, size_t gif_len, uint32_t bg_rgb888);
bool ST7789_GIF_Play(ST7789_Handle* lcd, uint16_t x, uint16_t y, const uint8_t* gif, size_t gif_len, uint32_t bg_rgb888, uint16_t repeat);

typedef enum
{
    ST7789_ASSET_TYPE_GIF = 1,
    ST7789_ASSET_TYPE_RGB565LE = 2
} ST7789_AssetType;

typedef struct
{
    ST7789_AssetType type;
    const void* data;
    uint32_t size;
    uint16_t width;
    uint16_t height;
} ST7789_AssetInfo;

const void* ST7789_Assets_GetBaseAddress(void);
void ST7789_Assets_SetBaseAddress(const void* base);
bool ST7789_Assets_Find(const char* name, ST7789_AssetInfo* out);
bool ST7789_Assets_Draw(ST7789_Handle* lcd, uint16_t x, uint16_t y, const char* name, uint32_t bg_rgb888);
bool ST7789_Assets_Play(ST7789_Handle* lcd, uint16_t x, uint16_t y, const char* name, uint32_t bg_rgb888, uint16_t repeat);

#ifdef __cplusplus
}
#endif

#endif
