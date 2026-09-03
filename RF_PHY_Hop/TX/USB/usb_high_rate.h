#ifndef TX_USB_HIGH_RATE_H
#define TX_USB_HIGH_RATE_H

#include <stdbool.h>
#include <stdint.h>

#include "hbox_high_rate_protocol.h"
#include "usb_board_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    USB_HIGH_RATE_EVENT_NONE = 0,
    USB_HIGH_RATE_EVENT_SEND_NEUTRAL,
    USB_HIGH_RATE_EVENT_DETACH_FOR_TURBO,
    USB_HIGH_RATE_EVENT_ATTACH_TURBO,
    USB_HIGH_RATE_EVENT_DETACH_FOR_NATIVE,
    USB_HIGH_RATE_EVENT_ATTACH_NATIVE
} usb_high_rate_event_t;

void usb_high_rate_init(void);
void usb_high_rate_reset(void);
bool usb_high_rate_is_turbo_presentation(void);
bool usb_high_rate_is_streaming(void);
uint16_t usb_high_rate_effective_rate_hz(void);

bool usb_high_rate_handle_control(const hbox_client_control_v1_t *request,
                                  hbox_client_control_v1_t *response,
                                  uint32_t now_ms,
                                  bool usb_high_speed,
                                  bool from_turbo_transport);
void usb_high_rate_get_status(hbox_client_control_v1_t *response,
                              uint32_t transaction,
                              bool usb_high_speed);
usb_high_rate_event_t usb_high_rate_process(uint32_t now_ms);
void usb_high_rate_neutral_sent(uint32_t now_ms);
void usb_high_rate_note_board_link_fault(void);

void usb_high_rate_submit_input(const usb_board_input_v1_t *input,
                                uint16_t xinput_buttons,
                                uint32_t producer_time_us);
bool usb_high_rate_peek_input(hbox_client_input_v1_t *packet);
void usb_high_rate_commit_input(void);

#ifdef __cplusplus
}
#endif

#endif /* TX_USB_HIGH_RATE_H */
