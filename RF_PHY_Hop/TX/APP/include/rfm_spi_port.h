#ifndef RFM_SPI_PORT_H
#define RFM_SPI_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void rfm_spi_port_init(void);
void rfm_spi_port_set_irq(bool asserted);
bool rfm_spi_port_try_read(uint8_t *buf, size_t *inout_len);
bool rfm_spi_port_try_write(const uint8_t *buf, size_t len);

#endif
