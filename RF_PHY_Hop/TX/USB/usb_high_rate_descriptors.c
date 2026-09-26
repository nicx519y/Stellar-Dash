#include "usb_high_rate_descriptors.h"

#include <stddef.h>

#define USB_DESC_DEVICE        0x01u
#define USB_DESC_CONFIGURATION 0x02u
#define USB_DESC_STRING        0x03u
#define USB_DESC_INTERFACE     0x04u
#define USB_DESC_ENDPOINT      0x05u
#define USB_DESC_QUALIFIER     0x06u
#define USB_DESC_OTHER_SPEED   0x07u
#define USB_DESC_BOS           0x0Fu
#define USB_DESC_DEVICE_CAP    0x10u

static const uint8_t s_device_descriptor[
    USB_HIGH_RATE_DEVICE_DESCRIPTOR_BYTES] = {
    0x12u, USB_DESC_DEVICE, 0x10u, 0x02u,
    0x00u, 0x00u, 0x00u, 0x40u,
    0xFEu, 0xCAu, /* VID 0xCAFE */
    0x23u, 0x40u, /* PID 0x4023 */
    0x00u, 0x01u,
    0x01u, 0x02u, 0x03u, 0x01u
};

static const uint8_t s_qualifier_descriptor[
    USB_HIGH_RATE_QUALIFIER_DESCRIPTOR_BYTES] = {
    0x0Au, USB_DESC_QUALIFIER, 0x10u, 0x02u,
    0x00u, 0x00u, 0x00u, 0x40u, 0x01u, 0x00u
};

static const uint8_t s_configuration_descriptor[
    USB_HIGH_RATE_CONFIG_DESCRIPTOR_BYTES] = {
    0x09u, USB_DESC_CONFIGURATION, 0x20u, 0x00u,
    0x01u, 0x01u, 0x00u, 0x80u, 0x32u,
    0x09u, USB_DESC_INTERFACE, 0x00u, 0x00u, 0x02u,
    0xFFu, 0x00u, 0x00u, 0x04u,
    0x07u, USB_DESC_ENDPOINT, USB_HIGH_RATE_ENDPOINT_IN,
    0x03u, 0x40u, 0x00u, 0x01u,
    0x07u, USB_DESC_ENDPOINT, USB_HIGH_RATE_ENDPOINT_OUT,
    0x03u, 0x20u, 0x00u, 0x04u
};

static const uint8_t s_other_speed_descriptor[
    USB_HIGH_RATE_CONFIG_DESCRIPTOR_BYTES] = {
    0x09u, USB_DESC_OTHER_SPEED, 0x20u, 0x00u,
    0x01u, 0x01u, 0x00u, 0x80u, 0x32u,
    0x09u, USB_DESC_INTERFACE, 0x00u, 0x00u, 0x02u,
    0xFFu, 0x00u, 0x00u, 0x04u,
    0x07u, USB_DESC_ENDPOINT, USB_HIGH_RATE_ENDPOINT_IN,
    0x03u, 0x40u, 0x00u, 0x01u,
    0x07u, USB_DESC_ENDPOINT, USB_HIGH_RATE_ENDPOINT_OUT,
    0x03u, 0x20u, 0x00u, 0x04u
};

/* Microsoft OS 2.0 platform capability descriptor, vendor code 0x21. */
static const uint8_t s_bos_descriptor[USB_HIGH_RATE_BOS_DESCRIPTOR_BYTES] = {
    0x05u, USB_DESC_BOS, 0x21u, 0x00u, 0x01u,
    0x1Cu, USB_DESC_DEVICE_CAP, 0x05u, 0x00u,
    0xD8u, 0xDDu, 0x60u, 0xDFu, 0x45u, 0x89u, 0x4Cu, 0xC7u,
    0x9Cu, 0xD2u, 0x65u, 0x9Du, 0x9Eu, 0x64u, 0x8Au, 0x9Fu,
    0x00u, 0x00u, 0x03u, 0x06u,
    0xB2u, 0x00u, 0x21u, 0x00u
};

/*
 * MS OS 2.0 set: configuration subset, function subset, WINUSB compatible ID,
 * and a REG_MULTI_SZ DeviceInterfaceGUIDs property.
 */
static const uint8_t s_ms_os_20_descriptor[
    USB_HIGH_RATE_MS_OS_20_DESCRIPTOR_BYTES] = {
    0x0Au, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x03u, 0x06u,
    0xB2u, 0x00u,
    0x08u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0xA8u, 0x00u,
    0x08u, 0x00u, 0x02u, 0x00u, 0x00u, 0x00u, 0xA0u, 0x00u,
    0x14u, 0x00u, 0x03u, 0x00u,
    'W', 'I', 'N', 'U', 'S', 'B', 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0x84u, 0x00u, 0x04u, 0x00u, 0x07u, 0x00u,
    0x2Au, 0x00u,
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0,
    'I', 0, 'n', 0, 't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0,
    'c', 0, 'e', 0, 'G', 0, 'U', 0, 'I', 0, 'D', 0, 's', 0, 0, 0,
    0x50u, 0x00u,
    '{', 0, '5', 0, '3', 0, 'F', 0, '2', 0, 'D', 0, '8', 0, 'A', 0,
    '1', 0, '-', 0, '6', 0, 'C', 0, '1', 0, '7', 0, '-', 0,
    '4', 0, 'E', 0, 'B', 0, '5', 0, '-', 0, '9', 0, '2', 0,
    'F', 0, '1', 0, '-', 0, '4', 0, '8', 0, '4', 0, '2', 0,
    '4', 0, 'F', 0, '5', 0, '8', 0, '4', 0, '8', 0, '3', 0,
    '1', 0, '}', 0, 0, 0, 0, 0
};

static uint8_t s_string[64];
static const char *const s_ascii_strings[] = {
    "",
    "HBox",
    "HBox High Rate Input",
    "HBOX-HIGHRATE-V1",
    "High Rate WinUSB"
};

typedef char high_rate_device_descriptor_size[
    sizeof(s_device_descriptor) == USB_HIGH_RATE_DEVICE_DESCRIPTOR_BYTES ? 1 : -1];
typedef char high_rate_configuration_descriptor_size[
    sizeof(s_configuration_descriptor) == USB_HIGH_RATE_CONFIG_DESCRIPTOR_BYTES ? 1 : -1];
typedef char high_rate_bos_descriptor_size[
    sizeof(s_bos_descriptor) == USB_HIGH_RATE_BOS_DESCRIPTOR_BYTES ? 1 : -1];
typedef char high_rate_ms_os_descriptor_size[
    sizeof(s_ms_os_20_descriptor) == USB_HIGH_RATE_MS_OS_20_DESCRIPTOR_BYTES ? 1 : -1];

#define RETURN_DESCRIPTOR(array) \
    do { if(length != NULL) *length = (uint16_t)sizeof(array); return array; } while(0)

const uint8_t *usb_high_rate_device_descriptor(uint16_t *length)
{
    RETURN_DESCRIPTOR(s_device_descriptor);
}

const uint8_t *usb_high_rate_qualifier_descriptor(uint16_t *length)
{
    RETURN_DESCRIPTOR(s_qualifier_descriptor);
}

const uint8_t *usb_high_rate_configuration_descriptor(uint16_t *length)
{
    RETURN_DESCRIPTOR(s_configuration_descriptor);
}

const uint8_t *usb_high_rate_other_speed_descriptor(uint16_t *length)
{
    RETURN_DESCRIPTOR(s_other_speed_descriptor);
}

const uint8_t *usb_high_rate_bos_descriptor(uint16_t *length)
{
    RETURN_DESCRIPTOR(s_bos_descriptor);
}

const uint8_t *usb_high_rate_ms_os_20_descriptor(uint16_t *length)
{
    RETURN_DESCRIPTOR(s_ms_os_20_descriptor);
}

const uint8_t *usb_high_rate_string_descriptor(uint8_t index,
                                               uint16_t *length)
{
    const char *source;
    size_t count = 0u;
    size_t i;
    static const uint8_t language[] = {0x04u, USB_DESC_STRING,
                                       0x09u, 0x04u};

    if(index == 0u)
    {
        if(length != NULL) *length = sizeof(language);
        return language;
    }
    if(index >= (sizeof(s_ascii_strings) / sizeof(s_ascii_strings[0])))
    {
        if(length != NULL) *length = 0u;
        return NULL;
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
    if(length != NULL) *length = s_string[0];
    return s_string;
}
