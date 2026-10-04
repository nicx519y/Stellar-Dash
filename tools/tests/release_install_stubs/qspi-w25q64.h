#pragma once
#include <stdint.h>
#define QSPI_W25Qxx_OK 0
#define W25Qxx_FlashSize 0x800000u
bool QSPI_W25Qxx_IsMemoryMappedMode();
int8_t QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(uint8_t*,uint32_t,uint32_t);
int8_t QSPI_W25Qxx_WriteBuffer_WithXIPOrNot(uint8_t*,uint32_t,uint32_t);
int8_t QSPI_W25Qxx_ExitMemoryMappedMode();
int8_t QSPI_W25Qxx_EnterMemoryMappedMode();
int8_t QSPI_W25Qxx_SectorErase(uint32_t);
int8_t QSPI_W25Qxx_WritePage(uint8_t*,uint32_t,uint16_t);
int8_t QSPI_W25Qxx_ReadBuffer(uint8_t*,uint32_t,uint32_t);
