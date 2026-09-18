#pragma once

#include <stdint.h>

#define QSPI_W25Qxx_OK 0

#ifdef __cplusplus
extern "C" {
#endif
int8_t QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(uint8_t *buffer,
                                           uint32_t address,
                                           uint32_t length);
#ifdef __cplusplus
}
#endif
