#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QSPI_W25Qxx_OK 0
#define W25Qxx_ERROR_Erase -4
#define W25Qxx_ERROR_TRANSMIT -5
#define W25Qxx_PageSize 256u
#define W25Qxx_FlashSize 0x800000u
#define W25Qxx_Mem_Addr 0x90000000u

int8_t QSPI_W25Qxx_WritePage(uint8_t *, uint32_t, uint16_t);
int8_t QSPI_W25Qxx_ReadBuffer(uint8_t *, uint32_t, uint32_t);
int8_t QSPI_W25Qxx_BufferErase(uint32_t, uint32_t);
int8_t QSPI_W25Qxx_EnterMemoryMappedMode(void);
int8_t QSPI_W25Qxx_ExitMemoryMappedMode(void);
bool QSPI_W25Qxx_IsMemoryMappedMode(void);

#ifdef __cplusplus
}
#endif

