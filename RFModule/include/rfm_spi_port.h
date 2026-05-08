#ifndef RFM_SPI_PORT_H
#define RFM_SPI_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Stable SPI port API (RFModule side).
 * Bridge/protocol layer must only use these entry points.
 */
bool rfm_spi_port_try_read(uint8_t *buf, size_t *inout_len);
bool rfm_spi_port_try_write(const uint8_t *buf, size_t len);

#endif
