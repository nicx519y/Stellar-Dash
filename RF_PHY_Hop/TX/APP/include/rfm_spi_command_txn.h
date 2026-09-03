#ifndef RFM_SPI_COMMAND_TXN_H
#define RFM_SPI_COMMAND_TXN_H

#include <stdbool.h>
#include <stdint.h>

void rfm_spi_command_txn_init(void);
void rfm_spi_command_txn_poll(void);
bool rfm_spi_command_txn_has_pending_ack(void);
void rfm_spi_command_txn_note_command_received(uint8_t cmd, uint8_t txn, uint8_t args_len);
bool rfm_spi_command_txn_resend_if_duplicate(uint8_t cmd, uint8_t txn);
bool rfm_spi_command_txn_is_complete(uint8_t cmd, uint8_t txn);
bool rfm_spi_command_txn_schedule_response(uint8_t cmd,
                                           uint8_t txn,
                                           const uint8_t *frame,
                                           uint8_t frame_len);

#endif
