#ifndef RFM_SPI_RELIABLE_EVENT_H
#define RFM_SPI_RELIABLE_EVENT_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*rfm_spi_reliable_event_complete_cb_t)(uint8_t evt,
                                                     uint8_t seq,
                                                     uint8_t user);

void rfm_spi_reliable_event_init(rfm_spi_reliable_event_complete_cb_t complete_cb);
uint8_t rfm_spi_reliable_event_next_seq(void);
bool rfm_spi_reliable_event_schedule(uint8_t evt,
                                     uint8_t seq,
                                     uint8_t user,
                                     const uint8_t *frame,
                                     uint8_t frame_len);
bool rfm_spi_reliable_event_handle_ack(uint8_t seq);
void rfm_spi_reliable_event_poll(bool command_ack_pending);
bool rfm_spi_reliable_event_has_pending(void);

#endif
