#ifndef SPI_SCREEN_DETAIL_ENTRIES_HPP
#define SPI_SCREEN_DETAIL_ENTRIES_HPP

#include <stdint.h>
#include <stdbool.h>

#include "screen_control/spi_screen_layout.hpp"

extern "C" {
#include "st7789.h"
}

uint8_t ScreenDetailInputMode_InitIndex(void);
void ScreenDetailInputMode_Rotate(uint8_t* ioIndex, int8_t det);
void ScreenDetailInputMode_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style);
void ScreenDetailInputMode_OnConfirm(uint8_t index);

uint8_t ScreenDetailProfiles_InitIndex(void);
void ScreenDetailProfiles_Rotate(uint8_t* ioIndex, int8_t det);
void ScreenDetailProfiles_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style);
void ScreenDetailProfiles_OnConfirm(uint8_t index);

uint8_t ScreenDetailSocd_InitIndex(void);
void ScreenDetailSocd_Rotate(uint8_t* ioIndex, int8_t det);
void ScreenDetailSocd_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style);
void ScreenDetailSocd_OnConfirm(uint8_t index);

uint8_t ScreenDetailLightEffect_InitIndex(void);
void ScreenDetailLightEffect_Rotate(uint8_t* ioIndex, int8_t det);
void ScreenDetailLightEffect_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style);
void ScreenDetailLightEffect_OnConfirm(uint8_t index);

uint8_t ScreenDetailAmbientEffect_InitIndex(void);
void ScreenDetailAmbientEffect_Rotate(uint8_t* ioIndex, int8_t det);
void ScreenDetailAmbientEffect_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style);
void ScreenDetailAmbientEffect_OnConfirm(uint8_t index);

uint8_t ScreenDetailLightBrightness_InitIndex(void);
void ScreenDetailLightBrightness_Rotate(uint8_t* ioIndex, int8_t det);
void ScreenDetailLightBrightness_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style);
void ScreenDetailLightBrightness_OnConfirm(uint8_t index);

uint8_t ScreenDetailAmbientBrightness_InitIndex(void);
void ScreenDetailAmbientBrightness_Rotate(uint8_t* ioIndex, int8_t det);
void ScreenDetailAmbientBrightness_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style);
void ScreenDetailAmbientBrightness_OnConfirm(uint8_t index);

uint8_t ScreenDetailScreenBrightness_InitIndex(void);
void ScreenDetailScreenBrightness_Rotate(uint8_t* ioIndex, int8_t det);
void ScreenDetailScreenBrightness_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style);
void ScreenDetailScreenBrightness_OnConfirm(uint8_t index);

uint8_t ScreenDetailWebConfig_InitIndex(void);
void ScreenDetailWebConfig_Rotate(uint8_t* ioIndex, int8_t det);
void ScreenDetailWebConfig_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style);
void ScreenDetailWebConfig_OnConfirm(uint8_t index);
void ScreenDetailWebConfig_OnBack(void);

uint8_t ScreenDetailCalibration_InitIndex(void);
void ScreenDetailCalibration_Rotate(uint8_t* ioIndex, int8_t det);
void ScreenDetailCalibration_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style);
void ScreenDetailCalibration_OnConfirm(uint8_t index);

uint8_t ScreenDetailTournament_InitIndex(void);
void ScreenDetailTournament_Rotate(uint8_t* ioIndex, int8_t det);
void ScreenDetailTournament_Render(ST7789_Handle* lcd, uint8_t index, const ScreenUiStyle& style);
void ScreenDetailTournament_OnConfirm(uint8_t index);

void ScreenUI_RequestDeferredSave(uint32_t delayMs);
void ScreenUI_RequestRebootTo(uint8_t menuId, uint8_t index);

#endif
