#ifndef TX_USB_DEVICE_H
#define TX_USB_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "usb_board_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

bool usb_device_init(usb_board_profile_t profile);
void usb_device_shutdown(void);
void usb_device_process(void);
bool usb_device_set_profile(usb_board_profile_t profile);
bool usb_device_submit_input(const usb_board_input_v1_t *input);
bool usb_device_submit_telemetry(const uint8_t *data, uint16_t length);
bool usb_device_submit_webhid_report(const uint8_t *data, uint16_t length);
/* Called from process context after EP1 reports a successful WebHID IN. */
void usb_device_webhid_report_complete(void);
/* True only after the current mounted, non-suspended WebHID generation has
 * published its receiver capacity. */
bool usb_device_webhid_credit_ready(void);
/* Called by the hardware backend after disconnect/bus-reset/profile reset. */
void usb_device_transport_reset(void);
bool usb_device_control(const uint8_t *payload, uint8_t length);
bool usb_device_is_mounted(void);
bool usb_device_is_suspended(void);
usb_board_profile_t usb_device_profile(void);
uint8_t usb_device_last_fault(void);

/* Board-specific USBHS backend. It is deliberately independent of RF code. */
bool usb_device_hw_init(usb_board_profile_t profile);
void usb_device_hw_shutdown(void);
bool usb_device_hw_send_report(const uint8_t *report, uint8_t length);
void usb_device_hw_set_actions(uint32_t action_mask);
bool usb_device_hw_send_telemetry(const uint8_t *data, uint8_t length);
bool usb_device_hw_send_webhid_report(const uint8_t *data, uint8_t length);
void usb_device_hw_process(void);
bool usb_device_hw_is_mounted(void);
bool usb_device_hw_is_suspended(void);
bool usb_device_hw_control(const uint8_t *payload, uint8_t length);

#ifdef __cplusplus
}
#endif

#endif
