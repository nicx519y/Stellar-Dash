#ifndef TX_USB_WEBHID_H
#define TX_USB_WEBHID_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_WEBHID_EP0_BYTES                64u
#define USB_WEBHID_REPORT_BYTES             64u
#define USB_WEBHID_ENDPOINT_IN              0x81u
#define USB_WEBHID_ENDPOINT_OUT             0x02u
#define USB_WEBHID_INTERFACE                 0u
#define USB_WEBHID_DEVICE_DESCRIPTOR_BYTES  18u
#define USB_WEBHID_QUALIFIER_DESCRIPTOR_BYTES 10u
#define USB_WEBHID_CONFIG_DESCRIPTOR_BYTES  41u
#define USB_WEBHID_REPORT_DESCRIPTOR_BYTES  27u
#define USB_WEBHID_HID_DESCRIPTOR_BYTES      9u

const uint8_t *usb_webhid_device_descriptor(uint16_t *length);
const uint8_t *usb_webhid_qualifier_descriptor(uint16_t *length);
const uint8_t *usb_webhid_configuration_descriptor(uint16_t *length);
const uint8_t *usb_webhid_other_speed_descriptor(uint16_t *length);
const uint8_t *usb_webhid_report_descriptor(uint16_t *length);
const uint8_t *usb_webhid_hid_descriptor(uint16_t *length);
const uint8_t *usb_webhid_string_descriptor(uint8_t index, uint16_t *length);

#ifdef __cplusplus
}
#endif

#endif
