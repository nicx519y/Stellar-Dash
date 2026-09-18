#ifndef RFM_SPI_PORT_INTERNAL_H
#define RFM_SPI_PORT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void rfm_spi_port_init(void);
void rfm_spi_port_sleep_until_nss_wake(void);
bool rfm_spi_port_sleep_ready(uint16_t stable_us);
uint8_t rfm_spi_port_sleep_block_flags(void);
void rfm_spi_port_set_irq(bool asserted);
void rfm_spi_port_service(void);
size_t rfm_spi_port_drain(uint8_t *buf, size_t max_len);
bool rfm_spi_port_peek_latest_input(uint8_t *payload, uint8_t len);
bool rfm_spi_port_peek_latest_control_frame(uint8_t *frame, uint8_t *inout_len);
void rfm_spi_port_discard_control_frames(void);
uint32_t rfm_spi_port_rx_peek_ok_count(void);
uint32_t rfm_spi_port_rx_peek_miss_count(void);
uint32_t rfm_spi_port_rx_ring_overrun_count(void);
uint32_t rfm_spi_port_rx_backlog_drop_count(void);
uint32_t rfm_spi_port_rx_backlog_drop_bytes(void);
uint32_t rfm_spi_port_rx_byte_count(void);
uint32_t rfm_spi_port_rx_dma_pos(void);
uint32_t rfm_spi_port_rx_fifo_ov_count(void);
uint32_t rfm_spi_port_rx_max_available(void);
uint32_t rfm_spi_port_rx_take_max_available(void);
uint32_t rfm_spi_port_rx_take_near_full_count(void);
uint32_t rfm_spi_port_rx_take_full_clip_count(void);
uint32_t rfm_spi_port_rx_bad_irq_count(void);
uint32_t rfm_spi_port_rx_isr_count(void);
uint32_t rfm_spi_port_rx_done_count(void);
uint32_t rfm_spi_port_rx_valid_frame_count(void);
uint32_t rfm_spi_port_rx_bad_frame_count(void);
uint32_t rfm_spi_port_rx_last_flags(void);
uint32_t rfm_spi_port_rx_direct_count(void);
uint8_t rfm_spi_port_tx_pending(void);
uint32_t rfm_spi_port_tx_recover_count(void);
uint32_t rfm_spi_port_tx_done_count(void);
bool rfm_spi_port_try_read(uint8_t *buf, size_t *inout_len);
bool rfm_spi_port_try_write(const uint8_t *buf, size_t len);

#endif
