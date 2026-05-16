#ifndef RF_BRIDGE_PORT_INTERNAL_H
#define RF_BRIDGE_PORT_INTERNAL_H

#include "stm32h7xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

void RFBridgePort_DMA_IRQHandler(void);
void RFBridgePort_SPI_IRQHandler(void);
void RFBridgePort_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi);
void RFBridgePort_SPI_ErrorCallback(SPI_HandleTypeDef *hspi);

#ifdef __cplusplus
}
#endif

#endif
