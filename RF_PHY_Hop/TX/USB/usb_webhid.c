#include "usb_webhid.h"

#include <stddef.h>

#include "webhid_protocol.h"

#define USB_DESC_DEVICE        0x01u
#define USB_DESC_CONFIGURATION 0x02u
#define USB_DESC_STRING        0x03u
#define USB_DESC_INTERFACE     0x04u
#define USB_DESC_ENDPOINT      0x05u
#define USB_DESC_QUALIFIER     0x06u
#define USB_DESC_OTHER_SPEED   0x07u
#define USB_DESC_HID           0x21u
#define USB_DESC_REPORT        0x22u

static const uint8_t s_device_descriptor[
    USB_WEBHID_DEVICE_DESCRIPTOR_BYTES] = {
    0x12u, USB_DESC_DEVICE, 0x00u, 0x02u,
    0x00u, 0x00u, 0x00u, USB_WEBHID_EP0_BYTES,
    0xFEu, 0xCAu, /* VID 0xCAFE */
    0x21u, 0x40u, /* PID 0x4021: dedicated HBox WebHID profile */
    0x00u, 0x02u, /* bcdDevice 2.00 */
    0x01u, 0x02u, 0x03u, 0x01u
};

static const uint8_t s_qualifier_descriptor[
    USB_WEBHID_QUALIFIER_DESCRIPTOR_BYTES] = {
    USB_WEBHID_QUALIFIER_DESCRIPTOR_BYTES, USB_DESC_QUALIFIER,
    0x00u, 0x02u, 0x00u, 0x00u, 0x00u,
    USB_WEBHID_EP0_BYTES, 0x01u, 0x00u
};

/*
 * Vendor-defined, report-ID-free HID descriptor.  WebHID sends and receives
 * one complete SecureHidReportV1 per interrupt transaction.
 */
static const uint8_t s_report_descriptor[
    USB_WEBHID_REPORT_DESCRIPTOR_BYTES] = {
    0x06u, 0x00u, 0xFFu,       /* Usage Page (Vendor 0xFF00) */
    0x09u, 0x01u,              /* Usage 1 */
    0xA1u, 0x01u,              /* Collection (Application) */
    0x15u, 0x00u,              /* Logical Minimum 0 */
    0x26u, 0xFFu, 0x00u,       /* Logical Maximum 255 */
    0x75u, 0x08u,              /* Report Size 8 */
    0x95u, 0x40u,              /* Report Count 64 */
    0x09u, 0x02u, 0x81u, 0x02u, /* Input: Data,Var,Abs */
    0x95u, 0x40u,              /* Report Count 64 */
    0x09u, 0x03u, 0x91u, 0x02u, /* Output: Data,Var,Abs */
    0xC0u
};

static const uint8_t s_configuration_descriptor[
    USB_WEBHID_CONFIG_DESCRIPTOR_BYTES] = {
    /* Configuration */
    0x09u, USB_DESC_CONFIGURATION,
    USB_WEBHID_CONFIG_DESCRIPTOR_BYTES, 0x00u,
    0x01u, 0x01u, 0x00u, 0x80u, 0x32u,

    /* One vendor HID interface. */
    0x09u, USB_DESC_INTERFACE,
    USB_WEBHID_INTERFACE, 0x00u, 0x02u,
    0x03u, 0x00u, 0x00u, 0x04u,

    /* HID 1.11 */
    USB_WEBHID_HID_DESCRIPTOR_BYTES, USB_DESC_HID,
    0x11u, 0x01u, 0x00u, 0x01u, USB_DESC_REPORT,
    USB_WEBHID_REPORT_DESCRIPTOR_BYTES, 0x00u,

    /* Interrupt IN and OUT, both fixed at one 64-byte report. */
    0x07u, USB_DESC_ENDPOINT, USB_WEBHID_ENDPOINT_IN,
    0x03u, USB_WEBHID_REPORT_BYTES, 0x00u, 0x01u,
    0x07u, USB_DESC_ENDPOINT, USB_WEBHID_ENDPOINT_OUT,
    0x03u, USB_WEBHID_REPORT_BYTES, 0x00u, 0x01u
};

static const uint8_t s_other_speed_descriptor[
    USB_WEBHID_CONFIG_DESCRIPTOR_BYTES] = {
    /* Same 64-byte interrupt endpoints at the other negotiated bus speed. */
    0x09u, USB_DESC_OTHER_SPEED,
    USB_WEBHID_CONFIG_DESCRIPTOR_BYTES, 0x00u,
    0x01u, 0x01u, 0x00u, 0x80u, 0x32u,
    0x09u, USB_DESC_INTERFACE,
    USB_WEBHID_INTERFACE, 0x00u, 0x02u,
    0x03u, 0x00u, 0x00u, 0x04u,
    USB_WEBHID_HID_DESCRIPTOR_BYTES, USB_DESC_HID,
    0x11u, 0x01u, 0x00u, 0x01u, USB_DESC_REPORT,
    USB_WEBHID_REPORT_DESCRIPTOR_BYTES, 0x00u,
    0x07u, USB_DESC_ENDPOINT, USB_WEBHID_ENDPOINT_IN,
    0x03u, USB_WEBHID_REPORT_BYTES, 0x00u, 0x01u,
    0x07u, USB_DESC_ENDPOINT, USB_WEBHID_ENDPOINT_OUT,
    0x03u, USB_WEBHID_REPORT_BYTES, 0x00u, 0x01u
};

static uint8_t s_string[64];
static const char *const s_ascii_strings[] = {
    "",
    "HBox",
    "HBox WebConfig",
    "HBOX-WEBCONFIG-V2",
    "WebConfig"
};

typedef char webhid_report_size_must_match_protocol[
    (USB_WEBHID_REPORT_BYTES == WEBHID_REPORT_BYTES) ? 1 : -1];
typedef char webhid_device_descriptor_size[
    (sizeof(s_device_descriptor) ==
     USB_WEBHID_DEVICE_DESCRIPTOR_BYTES) ? 1 : -1];
typedef char webhid_qualifier_descriptor_size[
    (sizeof(s_qualifier_descriptor) ==
     USB_WEBHID_QUALIFIER_DESCRIPTOR_BYTES) ? 1 : -1];
typedef char webhid_config_descriptor_size[
    (sizeof(s_configuration_descriptor) ==
     USB_WEBHID_CONFIG_DESCRIPTOR_BYTES) ? 1 : -1];
typedef char webhid_other_speed_descriptor_size[
    (sizeof(s_other_speed_descriptor) ==
     USB_WEBHID_CONFIG_DESCRIPTOR_BYTES) ? 1 : -1];
typedef char webhid_report_descriptor_size[
    (sizeof(s_report_descriptor) ==
     USB_WEBHID_REPORT_DESCRIPTOR_BYTES) ? 1 : -1];

const uint8_t *usb_webhid_device_descriptor(uint16_t *length)
{
    if(length != 0)
    {
        *length = sizeof(s_device_descriptor);
    }
    return s_device_descriptor;
}

const uint8_t *usb_webhid_qualifier_descriptor(uint16_t *length)
{
    if(length != 0)
    {
        *length = sizeof(s_qualifier_descriptor);
    }
    return s_qualifier_descriptor;
}

const uint8_t *usb_webhid_configuration_descriptor(uint16_t *length)
{
    if(length != 0)
    {
        *length = sizeof(s_configuration_descriptor);
    }
    return s_configuration_descriptor;
}

const uint8_t *usb_webhid_other_speed_descriptor(uint16_t *length)
{
    if(length != 0)
    {
        *length = sizeof(s_other_speed_descriptor);
    }
    return s_other_speed_descriptor;
}

const uint8_t *usb_webhid_report_descriptor(uint16_t *length)
{
    if(length != 0)
    {
        *length = sizeof(s_report_descriptor);
    }
    return s_report_descriptor;
}

const uint8_t *usb_webhid_hid_descriptor(uint16_t *length)
{
    if(length != 0)
    {
        *length = USB_WEBHID_HID_DESCRIPTOR_BYTES;
    }
    return &s_configuration_descriptor[18];
}

const uint8_t *usb_webhid_string_descriptor(uint8_t index, uint16_t *length)
{
    const char *source;
    size_t count = 0u;
    size_t i;

    if(index == 0u)
    {
        static const uint8_t language[] = {0x04u, USB_DESC_STRING,
                                           0x09u, 0x04u};
        if(length != 0)
        {
            *length = sizeof(language);
        }
        return language;
    }
    if(index >= (sizeof(s_ascii_strings) / sizeof(s_ascii_strings[0])))
    {
        if(length != 0)
        {
            *length = 0u;
        }
        return 0;
    }

    source = s_ascii_strings[index];
    while((source[count] != '\0') &&
          (count < ((sizeof(s_string) - 2u) / 2u)))
    {
        ++count;
    }
    s_string[0] = (uint8_t)(2u + count * 2u);
    s_string[1] = USB_DESC_STRING;
    for(i = 0u; i < count; ++i)
    {
        s_string[2u + i * 2u] = (uint8_t)source[i];
        s_string[3u + i * 2u] = 0u;
    }
    if(length != 0)
    {
        *length = (uint16_t)s_string[0];
    }
    return s_string;
}
