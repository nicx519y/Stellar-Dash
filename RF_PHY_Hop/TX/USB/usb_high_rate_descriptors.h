#ifndef TX_USB_HIGH_RATE_DESCRIPTORS_H
#define TX_USB_HIGH_RATE_DESCRIPTORS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_HIGH_RATE_INTERFACE                    0u
#define USB_HIGH_RATE_ENDPOINT_IN                0x81u
#define USB_HIGH_RATE_ENDPOINT_OUT               0x02u
#define USB_HIGH_RATE_DEVICE_DESCRIPTOR_BYTES      18u
#define USB_HIGH_RATE_QUALIFIER_DESCRIPTOR_BYTES   10u
#define USB_HIGH_RATE_CONFIG_DESCRIPTOR_BYTES      32u
#define USB_HIGH_RATE_BOS_DESCRIPTOR_BYTES         33u
#define USB_HIGH_RATE_MS_OS_20_DESCRIPTOR_BYTES   178u

const uint8_t *usb_high_rate_device_descriptor(uint16_t *length);
const uint8_t *usb_high_rate_qualifier_descriptor(uint16_t *length);
const uint8_t *usb_high_rate_configuration_descriptor(uint16_t *length);
const uint8_t *usb_high_rate_other_speed_descriptor(uint16_t *length);
const uint8_t *usb_high_rate_bos_descriptor(uint16_t *length);
const uint8_t *usb_high_rate_ms_os_20_descriptor(uint16_t *length);
const uint8_t *usb_high_rate_string_descriptor(uint8_t index,
                                               uint16_t *length);

#ifdef __cplusplus
}
#endif

#endif /* TX_USB_HIGH_RATE_DESCRIPTORS_H */
