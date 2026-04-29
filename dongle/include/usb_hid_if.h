#ifndef USB_HID_IF_H
#define USB_HID_IF_H

#include <stdbool.h>

#include "dongle_config.h"

void usb_hid_init(void);
bool usb_hid_ready(void);
bool usb_hid_can_send(void);
bool usb_hid_try_send_report(const xinput_report_t *report);

#endif /* USB_HID_IF_H */
