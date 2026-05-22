#ifndef RFM_SPI_BRIDGE_H
#define RFM_SPI_BRIDGE_H

void rfm_spi_bridge_init(void);
void rfm_spi_bridge_poll(void);
void rfm_spi_bridge_diag_emit(unsigned long elapsed_ms);

#endif
