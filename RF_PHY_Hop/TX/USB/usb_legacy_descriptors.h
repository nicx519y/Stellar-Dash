#ifndef TX_USB_LEGACY_DESCRIPTORS_H
#define TX_USB_LEGACY_DESCRIPTORS_H

#include <stdbool.h>
#include <stdint.h>

#include "usb_board_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * USB descriptor accessors for the legacy STM32 profiles.
 *
 * The returned storage is owned by this module and remains valid until the
 * next call to usb_legacy_get_string_descriptor().  Xbox/GIP is not HID, so
 * its report and HID descriptor getters intentionally return NULL/length 0.
 */
const uint8_t *usb_legacy_get_device_descriptor(
    usb_board_profile_t profile,
    uint16_t *length);
const uint8_t *usb_legacy_get_configuration_descriptor(
    usb_board_profile_t profile,
    uint16_t *length);
const uint8_t *usb_legacy_get_report_descriptor(
    usb_board_profile_t profile,
    uint16_t *length);
const uint8_t *usb_legacy_get_hid_descriptor(
    usb_board_profile_t profile,
    uint16_t *length);
const uint8_t *usb_legacy_get_string_descriptor(
    usb_board_profile_t profile,
    uint8_t index,
    uint16_t *length);
const uint8_t *usb_legacy_get_qualifier_descriptor(
    usb_board_profile_t profile,
    uint16_t *length);
const uint8_t *usb_legacy_get_xbox_compatible_id_descriptor(
    uint16_t *length);

#ifdef __cplusplus
}
#endif

#endif
