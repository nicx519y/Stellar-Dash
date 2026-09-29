#ifndef HBOX_FIRMWARE_MANAGER_TEST_QSPI_W25Q64_H
#define HBOX_FIRMWARE_MANAGER_TEST_QSPI_W25Q64_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QSPI_W25Qxx_OK 0

int8_t QSPI_W25Qxx_WriteBuffer_WithXIPOrNot(
    uint8_t *data,
    uint32_t address,
    uint32_t length);
int8_t QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(
    uint8_t *data,
    uint32_t address,
    uint32_t length);
int8_t QSPI_W25Qxx_BufferErase(uint32_t address, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif
