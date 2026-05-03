#ifndef USB_HID_IF_H
#define USB_HID_IF_H

#include <stdbool.h>
#include <stdint.h>

#include "dongle_config.h"

void usb_hid_init(void);
void usb_hid_poll(void);
bool usb_hid_ready(void);
bool usb_hid_can_send(void);
bool usb_hid_try_send_report(const xinput_report_t *report);
bool usb_hid_try_send_telemetry(const uint8_t *payload, uint16_t len);
uint32_t usb_hid_report_sent_count(void);
uint32_t usb_hid_telemetry_drop_count(void);

#endif /* USB_HID_IF_H */
