#ifndef __PWM_WS2812B_H
#define __PWM_WS2812B_H

#ifdef __cplusplus
extern "C" {
#endif

#include "tim.h"
#include "utils.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "board_cfg.h"
/**
  * @brief  WS2812B Status structures definition
  */
typedef enum
{
  WS2812B_STOP         = 0x00,
  WS2812B_RUNNING      = 0x01,
  WS2812B_ERROR        = 0x02
} WS2812B_StateTypeDef;

typedef enum
{
  WS2812B_STRIP_KEYS    = 0,
  WS2812B_STRIP_AMBIENT = 1
} WS2812B_Strip;

void WS2812B_InitStrip(WS2812B_Strip strip);

WS2812B_StateTypeDef WS2812B_StartStrip(WS2812B_Strip strip);

WS2812B_StateTypeDef WS2812B_StopStrip(WS2812B_Strip strip);

WS2812B_StateTypeDef WS2812B_GetStateStrip(WS2812B_Strip strip);

void WS2812B_SetAllLEDBrightnessStrip(WS2812B_Strip strip, const uint8_t brightness);

void WS2812B_SetAllLEDColorStrip(WS2812B_Strip strip, const uint8_t r, const uint8_t g, const uint8_t b);

void WS2812B_SetLEDBrightnessStrip(WS2812B_Strip strip, const uint8_t brightness, const uint16_t index, const uint16_t length);

void WS2812B_SetLEDColorStrip(WS2812B_Strip strip, const uint8_t r, const uint8_t g, const uint8_t b, const uint16_t index);

void WS2812B_RefreshStrip(WS2812B_Strip strip, const uint16_t start, const uint16_t length);

void WS2812B_Init(void);

void WS2812B_SetAllLEDBrightness(const uint8_t brightness);

void WS2812B_SetAllLEDColor(const uint8_t r, const uint8_t g, const uint8_t b);

void WS2812B_SetLEDBrightness(const uint8_t brightness, const uint16_t index, const uint8_t length);

// 便捷宏：当只需要设置单个LED亮度时
#define WS2812B_SetLEDBrightness_Single(brightness, index) WS2812B_SetLEDBrightness(brightness, index, 1)

void WS2812B_SetLEDColor(const uint8_t r, const uint8_t g, const uint8_t b, const uint16_t index);

void WS2812B_SetLEDBrightnessByMask(
  const uint8_t fontBrightness,
  const uint8_t backgroundBrightness,
  const uint32_t mask
);

void WS2812B_SetLEDColorByMask(
    const struct RGBColor frontColor, 
    const struct RGBColor backgroundColor, 
    const uint32_t mask);

WS2812B_StateTypeDef WS2812B_Start();

WS2812B_StateTypeDef WS2812B_Stop();

WS2812B_StateTypeDef WS2812B_GetState();

void WS2812B_Test();

#ifdef __cplusplus
}
#endif

#endif /* __PWM_WS2812B_H */

