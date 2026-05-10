#ifndef RFM_SPI_BRIDGE_H
#define RFM_SPI_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    SPI_CMD_GET_STATUS = 0x01,
    SPI_CMD_START_PAIR = 0x02,
    SPI_CMD_STOP_PAIR = 0x03,
    SPI_CMD_UNBIND = 0x04,
    SPI_CMD_SET_RATE = 0x05,
    SPI_CMD_INPUT_DATA = 0x06
} spi_cmd_t;

typedef enum {
    SPI_EVT_STATUS = 0x81,
    SPI_EVT_STATE_CHANGED = 0x82,
    SPI_EVT_RATE_APPLIED = 0x83,
    SPI_EVT_LINK_WARN = 0x84,
    SPI_EVT_ERROR = 0x85
} spi_evt_t;

void rfm_spi_bridge_init(void);
void rfm_spi_bridge_poll(void);

#endif
