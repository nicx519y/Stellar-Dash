#ifndef TX_USB_BOARD_LINK_H
#define TX_USB_BOARD_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "usb_board_link_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

void usb_board_link_init(usb_board_role_t locked_role);
void usb_board_link_process(void);
bool usb_board_link_is_ready(void);
uint8_t usb_board_link_last_fault(void);
bool usb_board_link_publish_bulk(usb_board_channel_t channel,
                                 const uint8_t *data,
                                 uint16_t length);
void usb_board_link_reset_channel(usb_board_channel_t channel);
void usb_board_link_webconfig_set_ready(bool ready,
                                        uint8_t available_reports);
void usb_board_link_webconfig_report_consumed(void);

/* Hardware SPI slave port, low-active event signal on PA5. */
bool usb_board_link_port_init(void);
void usb_board_link_port_shutdown(void);
void usb_board_link_port_process(void);
bool usb_board_link_port_pop_rx(uint8_t *byte);
bool usb_board_link_port_take_fault(uint8_t *fault);
bool usb_board_link_port_queue_event(const uint8_t *frame, uint8_t length);

#ifdef __cplusplus
}
#endif

#endif
