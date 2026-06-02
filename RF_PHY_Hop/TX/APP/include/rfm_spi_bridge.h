#ifndef RFM_SPI_BRIDGE_H
#define RFM_SPI_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

void rfm_spi_bridge_init(void);
void rfm_spi_bridge_poll(void);
void rfm_spi_bridge_diag_emit(unsigned long elapsed_ms);
void rfm_spi_bridge_emit_state_changed(uint8_t cmd_tag);
bool rfm_spi_bridge_take_sleep_request(void);

#endif
