#pragma once
#include "stm32h7xx_hal_jpeg.h"
constexpr uint32_t DMA2D_M2M_PFC=1,DMA2D_OUTPUT_RGB565=2,DMA2D_INPUT_YCBCR=3,DMA2D_NO_MODIF_ALPHA=0;
constexpr uint32_t DMA2D_CSS_420=0,DMA2D_CSS_422=1,DMA2D_NO_CSS=2;
constexpr uint32_t DMA2D_ISR_TEIF=2,DMA2D_ISR_CEIF=4,DMA2D_CR_START=1;
struct Registers { uint32_t ISR=0,CR=0; };
inline Registers registers; inline Registers* DMA2D=&registers;
struct DMA2D_HandleTypeDef {
 Registers* Instance;
 struct { uint32_t Mode,ColorMode,OutputOffset; } Init;
 struct { uint32_t InputColorMode,InputOffset,AlphaMode,InputAlpha,ChromaSubSampling; } LayerCfg[2];
 bool locked;
};
int HAL_DMA2D_Init(DMA2D_HandleTypeDef*);
int HAL_DMA2D_ConfigLayer(DMA2D_HandleTypeDef*,uint32_t);
int HAL_DMA2D_Start(DMA2D_HandleTypeDef*,uint32_t,uint32_t,uint32_t,uint32_t);
int HAL_DMA2D_PollForTransfer(DMA2D_HandleTypeDef*,uint32_t);
