#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

void SPIST7789_Init(void);
void SPIST7789_SetBacklight100(void);
bool SPIST7789_FillBlueAsync(void);
bool SPIST7789_IsBusy(void);
void SPIST7789_DMA_IRQHandler(void);
void SPIST7789_SPI_IRQHandler(void);
void SPIST7789_Service(void);
void SPIST7789_OnSpiTxCplt(SPI_HandleTypeDef* hspi);
bool SPIST7789_ConsumeDmaIrqFlag(void);
bool SPIST7789_ConsumeDmaDoneFlag(void);
bool SPIST7789_ConsumeDmaErrFlag(void);
void SPIST7789_GetDebug(uint32_t* out_ndtr, uint32_t* out_dma_cr, uint32_t* out_dma_isr, uint32_t* out_spi_sr, uint32_t* out_spi_cfg1, uint32_t* out_spi_cr1, uint32_t* out_spi_cr2);

#ifdef __cplusplus
}
#endif
