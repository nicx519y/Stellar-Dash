#pragma once
#include <cstdint>
constexpr int HAL_OK=0, HAL_ERROR=1, JPEG_IRQn=5;
constexpr uint32_t JPEG_YCBCR_COLORSPACE=1, JPEG_420_SUBSAMPLING=0, JPEG_422_SUBSAMPLING=1, JPEG_444_SUBSAMPLING=2;
constexpr uint32_t JPEG_PAUSE_RESUME_OUTPUT=2, JPEG_PAUSE_RESUME_INPUT=1;
inline void* JPEG=reinterpret_cast<void*>(1);
struct JPEG_HandleTypeDef { void* Instance; };
struct JPEG_ConfTypeDef { uint32_t ImageWidth, ImageHeight, ColorSpace, ChromaSubsampling; };
void resetJpeg(); void resetDma();
#define __HAL_RCC_JPEG_CLK_ENABLE() ((void)0)
#define __HAL_RCC_DMA2D_CLK_ENABLE() ((void)0)
#define __HAL_RCC_JPEG_FORCE_RESET() resetJpeg()
#define __HAL_RCC_JPEG_RELEASE_RESET() ((void)0)
#define __HAL_RCC_DMA2D_FORCE_RESET() resetDma()
#define __HAL_RCC_DMA2D_RELEASE_RESET() ((void)0)
inline void HAL_NVIC_DisableIRQ(int) {} inline void HAL_NVIC_ClearPendingIRQ(int) {}
inline void SCB_CleanDCache_by_Addr(uint32_t*,int32_t) {} inline void SCB_InvalidateDCache_by_Addr(uint32_t*,int32_t) {}
inline void __DSB() {}
uint32_t HAL_GetTick();
int HAL_JPEG_Init(JPEG_HandleTypeDef*);
int HAL_JPEG_Decode_IT(JPEG_HandleTypeDef*,uint8_t*,uint32_t,uint8_t*,uint32_t);
void HAL_JPEG_IRQHandler(JPEG_HandleTypeDef*);
void HAL_JPEG_ConfigOutputBuffer(JPEG_HandleTypeDef*,uint8_t*,uint32_t);
int HAL_JPEG_Resume(JPEG_HandleTypeDef*,uint32_t);
int HAL_JPEG_Pause(JPEG_HandleTypeDef*,uint32_t);
extern "C" {
void HAL_JPEG_InfoReadyCallback(JPEG_HandleTypeDef*,JPEG_ConfTypeDef*);
void HAL_JPEG_DataReadyCallback(JPEG_HandleTypeDef*,uint8_t*,uint32_t);
void HAL_JPEG_DecodeCpltCallback(JPEG_HandleTypeDef*);
void HAL_JPEG_ErrorCallback(JPEG_HandleTypeDef*);
}
