#include "usb_legacy_descriptors.h"

#include <stddef.h>
#include <string.h>

/*
 * Descriptor byte sources:
 *   application/Cpp_Core/Inc/drivers/ps4/PS4Descriptors.hpp
 *   application/Cpp_Core/Inc/drivers/switch/SwitchDescriptors.hpp
 *   application/Cpp_Core/Inc/drivers/xbone/XBOneDescriptors.hpp
 *
 * PS4 and Xbox/GIP arrays below are literal copies; the C-compatible Switch
 * header is included directly.  Static assertions pin every legacy descriptor
 * length so accidental truncation fails the build.  RF headers and RF protocol
 * state are deliberately not included.
 */
#include "../../../application/Cpp_Core/Inc/drivers/switch/SwitchDescriptors.hpp"

#define USB_LEGACY_STRING_BUFFER_BYTES 256u

static const uint8_t s_ps4_device_descriptor[] = {
    0x12u, 0x01u, 0x00u, 0x02u, 0x00u, 0x00u, 0x00u, 0x40u,
    0xDFu, 0x33u, 0x11u, 0x00u, 0x00u, 0x01u, 0x01u, 0x02u,
    0x00u, 0x01u
};

static const uint8_t s_ps4_report_descriptor[] = {
    0X05u, 0X01u, 0X09u, 0X05u, 0XA1u, 0X01u, 0X85u, 0X01u, 0X09u, 0X30u, 0X09u, 0X31u, 0X09u, 0X32u, 0X09u, 0X35u,
    0X15u, 0X00u, 0X26u, 0XFFu, 0X00u, 0X75u, 0X08u, 0X95u, 0X04u, 0X81u, 0X02u, 0X09u, 0X39u, 0X15u, 0X00u, 0X25u,
    0X07u, 0X35u, 0X00u, 0X46u, 0X3Bu, 0X01u, 0X65u, 0X14u, 0X75u, 0X04u, 0X95u, 0X01u, 0X81u, 0X42u, 0X65u, 0X00u,
    0X05u, 0X09u, 0X19u, 0X01u, 0X29u, 0X0Eu, 0X15u, 0X00u, 0X25u, 0X01u, 0X75u, 0X01u, 0X95u, 0X0Eu, 0X81u, 0X02u,
    0X06u, 0X00u, 0XFFu, 0X09u, 0X20u, 0X75u, 0X06u, 0X95u, 0X01u, 0X81u, 0X02u, 0X05u, 0X01u, 0X09u, 0X33u, 0X09u,
    0X34u, 0X15u, 0X00u, 0X26u, 0XFFu, 0X00u, 0X75u, 0X08u, 0X95u, 0X02u, 0X81u, 0X02u, 0X06u, 0X00u, 0XFFu, 0X09u,
    0X21u, 0X95u, 0X36u, 0X81u, 0X02u, 0X85u, 0X05u, 0X09u, 0X22u, 0X95u, 0X1Fu, 0X91u, 0X02u, 0X85u, 0X03u, 0X0Au,
    0X21u, 0X27u, 0X95u, 0X2Fu, 0XB1u, 0X02u, 0X85u, 0X02u, 0X09u, 0X24u, 0X95u, 0X24u, 0XB1u, 0X02u, 0X85u, 0X08u,
    0X09u, 0X25u, 0X95u, 0X03u, 0XB1u, 0X02u, 0X85u, 0X10u, 0X09u, 0X26u, 0X95u, 0X04u, 0XB1u, 0X02u, 0X85u, 0X11u,
    0X09u, 0X27u, 0X95u, 0X02u, 0XB1u, 0X02u, 0X85u, 0X12u, 0X06u, 0X02u, 0XFFu, 0X09u, 0X21u, 0X95u, 0X0Fu, 0XB1u,
    0X02u, 0X85u, 0X13u, 0X09u, 0X22u, 0X95u, 0X16u, 0XB1u, 0X02u, 0X85u, 0X14u, 0X06u, 0X05u, 0XFFu, 0X09u, 0X20u,
    0X95u, 0X10u, 0XB1u, 0X02u, 0X85u, 0X15u, 0X09u, 0X21u, 0X95u, 0X2Cu, 0XB1u, 0X02u, 0X06u, 0X80u, 0XFFu, 0X85u,
    0X80u, 0X09u, 0X20u, 0X95u, 0X06u, 0XB1u, 0X02u, 0X85u, 0X81u, 0X09u, 0X21u, 0X95u, 0X06u, 0XB1u, 0X02u, 0X85u,
    0X82u, 0X09u, 0X22u, 0X95u, 0X05u, 0XB1u, 0X02u, 0X85u, 0X83u, 0X09u, 0X23u, 0X95u, 0X01u, 0XB1u, 0X02u, 0X85u,
    0X84u, 0X09u, 0X24u, 0X95u, 0X04u, 0XB1u, 0X02u, 0X85u, 0X85u, 0X09u, 0X25u, 0X95u, 0X06u, 0XB1u, 0X02u, 0X85u,
    0X86u, 0X09u, 0X26u, 0X95u, 0X06u, 0XB1u, 0X02u, 0X85u, 0X87u, 0X09u, 0X27u, 0X95u, 0X23u, 0XB1u, 0X02u, 0X85u,
    0X88u, 0X09u, 0X28u, 0X95u, 0X22u, 0XB1u, 0X02u, 0X85u, 0X89u, 0X09u, 0X29u, 0X95u, 0X02u, 0XB1u, 0X02u, 0X85u,
    0X90u, 0X09u, 0X30u, 0X95u, 0X05u, 0XB1u, 0X02u, 0X85u, 0X91u, 0X09u, 0X31u, 0X95u, 0X03u, 0XB1u, 0X02u, 0X85u,
    0X92u, 0X09u, 0X32u, 0X95u, 0X03u, 0XB1u, 0X02u, 0X85u, 0X93u, 0X09u, 0X33u, 0X95u, 0X0Cu, 0XB1u, 0X02u, 0X85u,
    0XA0u, 0X09u, 0X40u, 0X95u, 0X06u, 0XB1u, 0X02u, 0X85u, 0XA1u, 0X09u, 0X41u, 0X95u, 0X01u, 0XB1u, 0X02u, 0X85u,
    0XA2u, 0X09u, 0X42u, 0X95u, 0X01u, 0XB1u, 0X02u, 0X85u, 0XA3u, 0X09u, 0X43u, 0X95u, 0X30u, 0XB1u, 0X02u, 0X85u,
    0XA4u, 0X09u, 0X44u, 0X95u, 0X0Du, 0XB1u, 0X02u, 0X85u, 0XA5u, 0X09u, 0X45u, 0X95u, 0X15u, 0XB1u, 0X02u, 0X85u,
    0XA6u, 0X09u, 0X46u, 0X95u, 0X15u, 0XB1u, 0X02u, 0X85u, 0XA7u, 0X09u, 0X4Au, 0X95u, 0X01u, 0XB1u, 0X02u, 0X85u,
    0XA8u, 0X09u, 0X4Bu, 0X95u, 0X01u, 0XB1u, 0X02u, 0X85u, 0XA9u, 0X09u, 0X4Cu, 0X95u, 0X08u, 0XB1u, 0X02u, 0X85u,
    0XAAu, 0X09u, 0X4Eu, 0X95u, 0X01u, 0XB1u, 0X02u, 0X85u, 0XABu, 0X09u, 0X4Fu, 0X95u, 0X39u, 0XB1u, 0X02u, 0X85u,
    0XACu, 0X09u, 0X50u, 0X95u, 0X39u, 0XB1u, 0X02u, 0X85u, 0XADu, 0X09u, 0X51u, 0X95u, 0X0Bu, 0XB1u, 0X02u, 0X85u,
    0XAEu, 0X09u, 0X52u, 0X95u, 0X01u, 0XB1u, 0X02u, 0X85u, 0XAFu, 0X09u, 0X53u, 0X95u, 0X02u, 0XB1u, 0X02u, 0X85u,
    0XB0u, 0X09u, 0X54u, 0X95u, 0X3Fu, 0XB1u, 0X02u, 0XC0u, 0X06u, 0XF0u, 0XFFu, 0X09u, 0X40u, 0XA1u, 0X01u, 0X85u,
    0XF0u, 0X09u, 0X47u, 0X95u, 0X3Fu, 0XB1u, 0X02u, 0X85u, 0XF1u, 0X09u, 0X48u, 0X95u, 0X3Fu, 0XB1u, 0X02u, 0X85u,
    0XF2u, 0X09u, 0X49u, 0X95u, 0X0Fu, 0XB1u, 0X02u, 0X85u, 0XF3u, 0X0Au, 0X01u, 0X47u, 0X95u, 0X07u, 0XB1u, 0X02u,
    0XC0u
};

static const uint8_t s_ps4_hid_descriptor[] = {
    0x09u, 0x21u, 0x11u, 0x01u, 0x00u, 0x01u, 0x22u, 0xE1u, 0x01u
};

static const uint8_t s_ps4_configuration_descriptor[] = {
    0x09u, 0x02u, 0x29u, 0x00u, 0x01u, 0x01u, 0x00u, 0x80u, 0x32u,
    0x09u, 0x04u, 0x00u, 0x00u, 0x02u, 0x03u, 0x00u, 0x00u, 0x00u,
    0x09u, 0x21u, 0x11u, 0x01u, 0x00u, 0x01u, 0x22u, 0xE1u, 0x01u,
    0x07u, 0x05u, 0x81u, 0x03u, 0x40u, 0x00u, 0x01u,
    0x07u, 0x05u, 0x03u, 0x03u, 0x40u, 0x00u, 0x01u
};

static const char s_ps4_manufacturer[] = "Open Stick Community";
static const char s_ps4_product[] = "GP2040-CE (PS4)";
static const char s_ps4_version[] = "1.0";

static const uint8_t s_xbox_device_qualifier[] = {
    0x0Au, 0x06u, 0x00u, 0x02u, 0xFFu, 0xFFu, 0xFFu, 0x40u, 0x01u, 0x00u
};

static const uint8_t s_xbox_device_descriptor[] = {
    0x12u, 0x01u, 0x00u, 0x02u, 0xFFu, 0xFFu, 0xFFu, 0x40u,
    0x6Fu, 0x0Eu, 0xA4u, 0x02u, 0x01u, 0x01u, 0x01u, 0x02u,
    0x03u, 0x01u
};

static const uint8_t s_xbox_configuration_descriptor[] = {
    0x09u, 0x02u, 0x20u, 0x00u, 0x01u, 0x01u, 0x00u, 0xA0u, 0xFAu,
    0x09u, 0x04u, 0x00u, 0x00u, 0x02u, 0xFFu, 0x47u, 0xD0u, 0x00u,
    0x07u, 0x05u, 0x81u, 0x03u, 0x40u, 0x00u, 0x01u,
    0x07u, 0x05u, 0x02u, 0x03u, 0x40u, 0x00u, 0x01u
};

typedef struct USB_BOARD_PACKED
{
    uint32_t total_length;
    uint16_t version;
    uint16_t index;
    uint8_t total_sections;
    uint8_t reserved[7];
    uint8_t first_interface_number;
    uint8_t reserved2;
    uint8_t compatible_id[8];
    uint8_t sub_compatible_id[8];
    uint8_t reserved3[6];
} usb_legacy_xbox_compatible_id_t;

static const usb_legacy_xbox_compatible_id_t s_xbox_compatible_id = {
    .total_length = sizeof(usb_legacy_xbox_compatible_id_t),
    .version = 0x0100u,
    .index = 0x0004u,
    .total_sections = 1u,
    .reserved = {0u},
    .first_interface_number = 0u,
    .reserved2 = 0x01u,
    .compatible_id = {'X', 'G', 'I', 'P', '1', '0', 0u, 0u},
    .sub_compatible_id = {0u},
    .reserved3 = {0u}
};

static const char s_xbox_manufacturer[] = "Open Stick Community";
static const char s_xbox_product[] = "GP2040-CE (Xbox One)";
static const char s_xbox_serial[] = "012345678ABCDEFGH";
static const char s_xbox_security_method[] =
    "Xbox Security Method 3, Version 1.00, \xA9 2005 Microsoft Corporation. "
    "All rights reserved.";
static const char s_xbox_os_descriptor[] = "MSFT100\x20";

static uint8_t s_string_descriptor[USB_LEGACY_STRING_BUFFER_BYTES];

USB_BOARD_STATIC_ASSERT(sizeof(s_ps4_device_descriptor) == 18u);
USB_BOARD_STATIC_ASSERT(sizeof(s_ps4_report_descriptor) == 481u);
USB_BOARD_STATIC_ASSERT(sizeof(s_ps4_hid_descriptor) == 9u);
USB_BOARD_STATIC_ASSERT(sizeof(s_ps4_configuration_descriptor) == 41u);
USB_BOARD_STATIC_ASSERT(sizeof(switch_device_descriptor) == 18u);
USB_BOARD_STATIC_ASSERT(sizeof(switch_report_descriptor) == 86u);
USB_BOARD_STATIC_ASSERT(sizeof(switch_hid_descriptor) == 9u);
USB_BOARD_STATIC_ASSERT(sizeof(switch_configuration_descriptor) == 41u);
USB_BOARD_STATIC_ASSERT(sizeof(s_xbox_device_descriptor) == 18u);
USB_BOARD_STATIC_ASSERT(sizeof(s_xbox_device_qualifier) == 10u);
USB_BOARD_STATIC_ASSERT(sizeof(s_xbox_configuration_descriptor) == 32u);
USB_BOARD_STATIC_ASSERT(sizeof(s_xbox_compatible_id) == 40u);

static bool is_ps4_profile(usb_board_profile_t profile)
{
    return (profile == USB_BOARD_PROFILE_PS4) ||
           (profile == USB_BOARD_PROFILE_PS5_COMPAT);
}

static const uint8_t *return_descriptor(const uint8_t *data,
                                        uint16_t size,
                                        uint16_t *length)
{
    if(length != 0)
    {
        *length = size;
    }
    return data;
}

static const uint8_t *return_no_descriptor(uint16_t *length)
{
    if(length != 0)
    {
        *length = 0u;
    }
    return 0;
}

const uint8_t *usb_legacy_get_device_descriptor(
    usb_board_profile_t profile,
    uint16_t *length)
{
    if(is_ps4_profile(profile))
    {
        return return_descriptor(s_ps4_device_descriptor,
                                 sizeof(s_ps4_device_descriptor),
                                 length);
    }
    if(profile == USB_BOARD_PROFILE_SWITCH)
    {
        return return_descriptor(switch_device_descriptor,
                                 sizeof(switch_device_descriptor),
                                 length);
    }
    if(profile == USB_BOARD_PROFILE_XBOX_ONE)
    {
        return return_descriptor(s_xbox_device_descriptor,
                                 sizeof(s_xbox_device_descriptor),
                                 length);
    }
    return return_no_descriptor(length);
}

const uint8_t *usb_legacy_get_configuration_descriptor(
    usb_board_profile_t profile,
    uint16_t *length)
{
    if(is_ps4_profile(profile))
    {
        return return_descriptor(s_ps4_configuration_descriptor,
                                 sizeof(s_ps4_configuration_descriptor),
                                 length);
    }
    if(profile == USB_BOARD_PROFILE_SWITCH)
    {
        return return_descriptor(switch_configuration_descriptor,
                                 sizeof(switch_configuration_descriptor),
                                 length);
    }
    if(profile == USB_BOARD_PROFILE_XBOX_ONE)
    {
        return return_descriptor(s_xbox_configuration_descriptor,
                                 sizeof(s_xbox_configuration_descriptor),
                                 length);
    }
    return return_no_descriptor(length);
}

const uint8_t *usb_legacy_get_report_descriptor(
    usb_board_profile_t profile,
    uint16_t *length)
{
    if(is_ps4_profile(profile))
    {
        return return_descriptor(s_ps4_report_descriptor,
                                 sizeof(s_ps4_report_descriptor),
                                 length);
    }
    if(profile == USB_BOARD_PROFILE_SWITCH)
    {
        return return_descriptor(switch_report_descriptor,
                                 sizeof(switch_report_descriptor),
                                 length);
    }
    return return_no_descriptor(length);
}

const uint8_t *usb_legacy_get_hid_descriptor(
    usb_board_profile_t profile,
    uint16_t *length)
{
    if(is_ps4_profile(profile))
    {
        return return_descriptor(s_ps4_hid_descriptor,
                                 sizeof(s_ps4_hid_descriptor),
                                 length);
    }
    if(profile == USB_BOARD_PROFILE_SWITCH)
    {
        return return_descriptor(switch_hid_descriptor,
                                 sizeof(switch_hid_descriptor),
                                 length);
    }
    return return_no_descriptor(length);
}

static const char *string_source(usb_board_profile_t profile, uint8_t index)
{
    if(is_ps4_profile(profile))
    {
        switch(index)
        {
        case 1u: return s_ps4_manufacturer;
        case 2u: return s_ps4_product;
        case 3u: return s_ps4_version;
        default: return 0;
        }
    }

    if(profile == USB_BOARD_PROFILE_SWITCH)
    {
        switch(index)
        {
        case 1u: return (const char *)switch_string_manufacturer;
        case 2u: return (const char *)switch_string_product;
        case 3u: return (const char *)switch_string_version;
        default: return 0;
        }
    }

    if(profile == USB_BOARD_PROFILE_XBOX_ONE)
    {
        switch(index)
        {
        case 1u: return s_xbox_manufacturer;
        case 2u: return s_xbox_product;
        case 3u: return s_xbox_serial;
        case 4u: return s_xbox_security_method;
        case 0xEEu: return s_xbox_os_descriptor;
        default: return 0;
        }
    }
    return 0;
}

const uint8_t *usb_legacy_get_string_descriptor(
    usb_board_profile_t profile,
    uint8_t index,
    uint16_t *length)
{
    const char *source;
    size_t count;
    size_t i;

    if(index == 0u)
    {
        static const uint8_t language[] = {0x04u, 0x03u, 0x09u, 0x04u};
        return return_descriptor(language, sizeof(language), length);
    }

    source = string_source(profile, index);
    if(source == 0)
    {
        return return_no_descriptor(length);
    }

    count = strlen(source);
    if(count > ((USB_LEGACY_STRING_BUFFER_BYTES - 2u) / 2u))
    {
        count = (USB_LEGACY_STRING_BUFFER_BYTES - 2u) / 2u;
    }

    s_string_descriptor[0] = (uint8_t)(2u + (count * 2u));
    s_string_descriptor[1] = 0x03u;
    for(i = 0u; i < count; ++i)
    {
        s_string_descriptor[2u + (i * 2u)] = (uint8_t)source[i];
        s_string_descriptor[3u + (i * 2u)] = 0u;
    }
    return return_descriptor(s_string_descriptor,
                             (uint16_t)(2u + (count * 2u)),
                             length);
}

const uint8_t *usb_legacy_get_qualifier_descriptor(
    usb_board_profile_t profile,
    uint16_t *length)
{
    if(profile == USB_BOARD_PROFILE_XBOX_ONE)
    {
        return return_descriptor(s_xbox_device_qualifier,
                                 sizeof(s_xbox_device_qualifier),
                                 length);
    }
    return return_no_descriptor(length);
}

const uint8_t *usb_legacy_get_xbox_compatible_id_descriptor(
    uint16_t *length)
{
    return return_descriptor((const uint8_t *)&s_xbox_compatible_id,
                             sizeof(s_xbox_compatible_id),
                             length);
}
