#ifndef TX_USB_MANAGEMENT_CONTROL_H
#define TX_USB_MANAGEMENT_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "usb_board_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

void usb_management_control_init(void);

/*
 * Handles one variable-length USB_BOARD_CMD_USB_CONTROL payload and emits the
 * corresponding USB_BOARD_EVT_USB_CONTROL payload.  true means a syntactically
 * valid response was produced; operation success is response.header.status.
 */
bool usb_management_control_handle(const uint8_t *request,
                                   uint8_t request_length,
                                   uint8_t *response,
                                   uint8_t response_capacity,
                                   uint8_t *response_length);

bool usb_management_control_is_connected(void);
uint8_t usb_management_control_last_fault(void);

/* Board hooks remain local to the CH585 USB role. */
bool usb_management_control_hw_connect(void);
void usb_management_control_hw_disconnect(void);
void usb_management_control_hw_clear_fault(void);

#ifdef __cplusplus
}
#endif

#endif
