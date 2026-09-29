#include "usb_ncm.h"

#include <string.h>

/*
 * CDC-NCM constants and byte layout are derived from the repository's existing
 * MIT-licensed TinyUSB implementation.  This module is deliberately transport
 * agnostic so the CH585 USB ISR can remain local and time bounded.
 */
#define USB_DESC_DEVICE                 0x01u
#define USB_DESC_CONFIGURATION          0x02u
#define USB_DESC_STRING                 0x03u
#define USB_DESC_INTERFACE              0x04u
#define USB_DESC_ENDPOINT               0x05u
#define USB_DESC_INTERFACE_ASSOCIATION  0x0Bu
#define USB_DESC_CS_INTERFACE           0x24u

#define USB_CLASS_CDC                   0x02u
#define USB_CLASS_CDC_DATA              0x0Au
#define USB_CLASS_MISC                  0xEFu
#define USB_MISC_SUBCLASS_COMMON        0x02u
#define USB_MISC_PROTOCOL_IAD           0x01u
#define USB_CDC_SUBCLASS_NCM            0x0Du
#define USB_CDC_DATA_PROTOCOL_NTB       0x01u

#define USB_REQUEST_GET_INTERFACE       0x0Au
#define USB_REQUEST_SET_INTERFACE       0x0Bu
#define USB_NCM_GET_NTB_PARAMETERS      0x80u
#define USB_REQUEST_TYPE_STANDARD_INTF  0x81u
#define USB_REQUEST_TYPE_STANDARD_OUT_INTF 0x01u
#define USB_REQUEST_TYPE_CLASS_IN_INTF  0xA1u

#define USB_CDC_NOTIFICATION_NETWORK_CONNECTION 0x00u
#define USB_CDC_NOTIFICATION_SPEED_CHANGE       0x2Au

#define USB_NTH16_SIGNATURE       0x484D434Eu
#define USB_NDP16_SIGNATURE_NCM0  0x304D434Eu
#define USB_NDP16_SIGNATURE_NCM1  0x314D434Eu
#define USB_NTH16_BYTES           12u
#define USB_NDP16_HEADER_BYTES    8u
#define USB_NDP16_ENTRY_BYTES     4u
#define USB_NDP16_SINGLE_BYTES    \
    (USB_NDP16_HEADER_BYTES + (2u * USB_NDP16_ENTRY_BYTES))
#define USB_NCM_SINGLE_FRAME_OFFSET \
    (USB_NTH16_BYTES + USB_NDP16_SINGLE_BYTES)

typedef enum
{
    USB_NCM_NOTIFY_NONE = 0,
    USB_NCM_NOTIFY_SPEED,
    USB_NCM_NOTIFY_CONNECTED
} usb_ncm_notify_state_t;

static uint8_t s_configuration_hs[USB_NCM_CONFIGURATION_BYTES];
static uint8_t s_configuration_fs[USB_NCM_CONFIGURATION_BYTES];
static uint8_t s_string[64];
static uint8_t s_mac[6] = {0x02u, 0x02u, 0x84u, 0x6Au, 0x96u, 0x00u};
static usb_ncm_speed_t s_speed = USB_NCM_SPEED_HIGH;
static uint8_t s_alt_setting;
static uint8_t s_link_up;
static usb_ncm_notify_state_t s_notification;

static const uint8_t s_device_descriptor[USB_NCM_DEVICE_DESCRIPTOR_BYTES] = {
    0x12u, USB_DESC_DEVICE, 0x00u, 0x02u,
    USB_CLASS_MISC, USB_MISC_SUBCLASS_COMMON, USB_MISC_PROTOCOL_IAD,
    USB_NCM_EP0_BYTES,
    0xFEu, 0xCAu, /* VID 0xCAFE */
    0x20u, 0x40u, /* PID 0x4020: NET bit in the former TinyUSB PID map */
    0x01u, 0x01u, /* bcdDevice 1.01 */
    0x01u, 0x02u, 0x03u, 0x01u
};

static const char *const s_ascii_strings[] = {
    "",
    "TinyUSB",
    "TinyUSB Device",
    "123456",
    "TinyUSB Network Interface"
};

static const usb_ncm_ntb_parameters_t s_ntb_parameters = {
    USB_NCM_NTB_PARAMETERS_BYTES,
    0x0001u, /* NTB16 */
    USB_NCM_NTB_MAX_BYTES,
    1u,
    0u,
    4u,
    0u,
    USB_NCM_NTB_MAX_BYTES,
    1u,
    0u,
    4u,
    USB_NCM_MAX_DATAGRAMS_PER_NTB
};

USB_BOARD_STATIC_ASSERT(sizeof(usb_ncm_setup_packet_t) == 8u);
USB_BOARD_STATIC_ASSERT(sizeof(usb_ncm_ntb_parameters_t) ==
                        USB_NCM_NTB_PARAMETERS_BYTES);
USB_BOARD_STATIC_ASSERT(USB_NCM_SINGLE_FRAME_OFFSET == 28u);
USB_BOARD_STATIC_ASSERT(USB_NCM_ETHERNET_FRAME_MAX_BYTES <=
                        USB_BOARD_BULK_MESSAGE_MAX_BYTES);
USB_BOARD_STATIC_ASSERT(USB_NCM_NTB_MAX_BYTES >=
                        USB_NCM_SINGLE_FRAME_OFFSET +
                        USB_NCM_ETHERNET_FRAME_MAX_BYTES);

static uint16_t get_u16(const uint8_t *source)
{
    return (uint16_t)source[0] | ((uint16_t)source[1] << 8);
}

static uint32_t get_u32(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

static void put_u16(uint8_t *target, uint16_t value)
{
    target[0] = (uint8_t)value;
    target[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *target, uint32_t value)
{
    target[0] = (uint8_t)value;
    target[1] = (uint8_t)(value >> 8);
    target[2] = (uint8_t)(value >> 16);
    target[3] = (uint8_t)(value >> 24);
}

static void build_configuration(uint8_t *descriptor,
                                usb_ncm_speed_t speed)
{
    const uint16_t endpoint_size =
        (speed == USB_NCM_SPEED_HIGH)
            ? USB_NCM_ENDPOINT_HS_BYTES
            : USB_NCM_ENDPOINT_FS_BYTES;
    uint8_t *p = descriptor;

#define APPEND_BYTE(value) do { *p++ = (uint8_t)(value); } while(0)
#define APPEND_U16(value) do { \
    const uint16_t append_value_ = (uint16_t)(value); \
    APPEND_BYTE(append_value_); \
    APPEND_BYTE(append_value_ >> 8); \
} while(0)

    /* Configuration: two interfaces, 100 mA. */
    APPEND_BYTE(9u); APPEND_BYTE(USB_DESC_CONFIGURATION);
    APPEND_U16(USB_NCM_CONFIGURATION_BYTES);
    APPEND_BYTE(2u); APPEND_BYTE(1u); APPEND_BYTE(0u);
    APPEND_BYTE(0x80u); APPEND_BYTE(50u);

    /* IAD and IF0 CDC-NCM control interface. */
    APPEND_BYTE(8u); APPEND_BYTE(USB_DESC_INTERFACE_ASSOCIATION);
    APPEND_BYTE(0u); APPEND_BYTE(2u); APPEND_BYTE(USB_CLASS_CDC);
    APPEND_BYTE(USB_CDC_SUBCLASS_NCM); APPEND_BYTE(0u); APPEND_BYTE(0u);
    APPEND_BYTE(9u); APPEND_BYTE(USB_DESC_INTERFACE);
    APPEND_BYTE(0u); APPEND_BYTE(0u); APPEND_BYTE(1u);
    APPEND_BYTE(USB_CLASS_CDC); APPEND_BYTE(USB_CDC_SUBCLASS_NCM);
    APPEND_BYTE(0u); APPEND_BYTE(4u);

    /* CDC header, union, Ethernet and NCM functional descriptors. */
    APPEND_BYTE(5u); APPEND_BYTE(USB_DESC_CS_INTERFACE);
    APPEND_BYTE(0x00u); APPEND_U16(0x0110u);
    APPEND_BYTE(5u); APPEND_BYTE(USB_DESC_CS_INTERFACE);
    APPEND_BYTE(0x06u); APPEND_BYTE(0u); APPEND_BYTE(1u);
    APPEND_BYTE(13u); APPEND_BYTE(USB_DESC_CS_INTERFACE);
    APPEND_BYTE(0x0Fu); APPEND_BYTE(5u);
    APPEND_BYTE(0u); APPEND_BYTE(0u); APPEND_BYTE(0u); APPEND_BYTE(0u);
    APPEND_U16(USB_NCM_ETHERNET_FRAME_MAX_BYTES);
    APPEND_U16(0u); APPEND_BYTE(0u);
    APPEND_BYTE(6u); APPEND_BYTE(USB_DESC_CS_INTERFACE);
    APPEND_BYTE(0x1Au); APPEND_U16(0x0100u); APPEND_BYTE(0u);

    /* Interrupt notification endpoint. */
    APPEND_BYTE(7u); APPEND_BYTE(USB_DESC_ENDPOINT);
    APPEND_BYTE(USB_NCM_ENDPOINT_NOTIFICATION_IN);
    APPEND_BYTE(0x03u); APPEND_U16(64u); APPEND_BYTE(50u);

    /* IF1 alt 0 (inactive), alt 1 (bulk data). */
    APPEND_BYTE(9u); APPEND_BYTE(USB_DESC_INTERFACE);
    APPEND_BYTE(1u); APPEND_BYTE(0u); APPEND_BYTE(0u);
    APPEND_BYTE(USB_CLASS_CDC_DATA); APPEND_BYTE(0u);
    APPEND_BYTE(USB_CDC_DATA_PROTOCOL_NTB); APPEND_BYTE(0u);
    APPEND_BYTE(9u); APPEND_BYTE(USB_DESC_INTERFACE);
    APPEND_BYTE(1u); APPEND_BYTE(1u); APPEND_BYTE(2u);
    APPEND_BYTE(USB_CLASS_CDC_DATA); APPEND_BYTE(0u);
    APPEND_BYTE(USB_CDC_DATA_PROTOCOL_NTB); APPEND_BYTE(0u);

    APPEND_BYTE(7u); APPEND_BYTE(USB_DESC_ENDPOINT);
    APPEND_BYTE(USB_NCM_ENDPOINT_DATA_IN); APPEND_BYTE(0x02u);
    APPEND_U16(endpoint_size); APPEND_BYTE(0u);
    APPEND_BYTE(7u); APPEND_BYTE(USB_DESC_ENDPOINT);
    APPEND_BYTE(USB_NCM_ENDPOINT_DATA_OUT); APPEND_BYTE(0x02u);
    APPEND_U16(endpoint_size); APPEND_BYTE(0u);

#undef APPEND_U16
#undef APPEND_BYTE
    (void)p;
}

void usb_ncm_init(usb_ncm_speed_t speed)
{
    build_configuration(s_configuration_hs, USB_NCM_SPEED_HIGH);
    build_configuration(s_configuration_fs, USB_NCM_SPEED_FULL);
    s_speed = (speed == USB_NCM_SPEED_FULL)
        ? USB_NCM_SPEED_FULL
        : USB_NCM_SPEED_HIGH;
    s_alt_setting = 0u;
    s_link_up = 1u; /* Matches TinyUSB's default NCM link state. */
    s_notification = USB_NCM_NOTIFY_NONE;
}

void usb_ncm_reset(void)
{
    s_alt_setting = 0u;
    s_notification = USB_NCM_NOTIFY_NONE;
}

const uint8_t *usb_ncm_device_descriptor(uint16_t *length)
{
    if(length != 0)
    {
        *length = sizeof(s_device_descriptor);
    }
    return s_device_descriptor;
}

const uint8_t *usb_ncm_configuration_descriptor(usb_ncm_speed_t speed,
                                                 uint16_t *length)
{
    if(length != 0)
    {
        *length = USB_NCM_CONFIGURATION_BYTES;
    }
    return (speed == USB_NCM_SPEED_FULL)
        ? s_configuration_fs
        : s_configuration_hs;
}

const uint8_t *usb_ncm_string_descriptor(uint8_t index, uint16_t *length)
{
    uint8_t count = 0u;
    uint8_t i;

    if(index == 0u)
    {
        s_string[0] = 4u;
        s_string[1] = USB_DESC_STRING;
        s_string[2] = 0x09u;
        s_string[3] = 0x04u;
        count = 4u;
    }
    else if(index == 5u)
    {
        static const char hex[] = "0123456789ABCDEF";
        s_string[1] = USB_DESC_STRING;
        for(i = 0u; i < 6u; ++i)
        {
            s_string[2u + (i * 4u)] = (uint8_t)hex[s_mac[i] >> 4];
            s_string[3u + (i * 4u)] = 0u;
            s_string[4u + (i * 4u)] =
                (uint8_t)hex[s_mac[i] & 0x0Fu];
            s_string[5u + (i * 4u)] = 0u;
        }
        count = 26u;
        s_string[0] = count;
    }
    else if(index < (sizeof(s_ascii_strings) / sizeof(s_ascii_strings[0])))
    {
        const char *text = s_ascii_strings[index];
        s_string[1] = USB_DESC_STRING;
        while((text[count] != '\0') && (count < 30u))
        {
            s_string[2u + (count * 2u)] = (uint8_t)text[count];
            s_string[3u + (count * 2u)] = 0u;
            ++count;
        }
        count = (uint8_t)(2u + (count * 2u));
        s_string[0] = count;
    }
    else
    {
        if(length != 0)
        {
            *length = 0u;
        }
        return 0;
    }

    if(length != 0)
    {
        *length = count;
    }
    return s_string;
}

const usb_ncm_ntb_parameters_t *usb_ncm_ntb_parameters(void)
{
    return &s_ntb_parameters;
}

bool usb_ncm_set_mac(const uint8_t mac[6])
{
    uint8_t aggregate = 0u;
    uint8_t i;
    if((mac == 0) || ((mac[0] & 0x01u) != 0u))
    {
        return false;
    }
    for(i = 0u; i < 6u; ++i)
    {
        aggregate |= mac[i];
    }
    if(aggregate == 0u)
    {
        return false;
    }
    memcpy(s_mac, mac, sizeof(s_mac));
    return true;
}

void usb_ncm_get_mac(uint8_t mac[6])
{
    if(mac != 0)
    {
        memcpy(mac, s_mac, sizeof(s_mac));
    }
}

usb_ncm_control_result_t usb_ncm_handle_setup(
    const usb_ncm_setup_packet_t *setup,
    uint8_t *response,
    uint16_t response_capacity,
    uint16_t *response_length)
{
    uint16_t length;
    if(response_length != 0)
    {
        *response_length = 0u;
    }
    if((setup == 0) || (response_length == 0))
    {
        return USB_NCM_CONTROL_STALL;
    }

    if((setup->bm_request_type == USB_REQUEST_TYPE_STANDARD_INTF) &&
       (setup->b_request == USB_REQUEST_GET_INTERFACE) &&
       (setup->w_index_le == 1u) && (setup->w_value_le == 0u))
    {
        if((response == 0) || (response_capacity == 0u))
        {
            return USB_NCM_CONTROL_STALL;
        }
        response[0] = s_alt_setting;
        *response_length = (setup->w_length_le == 0u) ? 0u : 1u;
        return USB_NCM_CONTROL_DATA;
    }

    if((setup->bm_request_type == USB_REQUEST_TYPE_STANDARD_OUT_INTF) &&
       (setup->b_request == USB_REQUEST_SET_INTERFACE) &&
       (setup->w_index_le == 1u) && (setup->w_value_le < 2u) &&
       (setup->w_length_le == 0u))
    {
        s_alt_setting = (uint8_t)setup->w_value_le;
        s_notification = (s_alt_setting == 1u)
            ? USB_NCM_NOTIFY_SPEED
            : USB_NCM_NOTIFY_NONE;
        return USB_NCM_CONTROL_STATUS;
    }

    if((setup->bm_request_type == USB_REQUEST_TYPE_CLASS_IN_INTF) &&
       (setup->b_request == USB_NCM_GET_NTB_PARAMETERS) &&
       (setup->w_index_le == 0u) && (setup->w_value_le == 0u))
    {
        length = sizeof(s_ntb_parameters);
        if(length > setup->w_length_le)
        {
            length = setup->w_length_le;
        }
        if((length != 0u) &&
           ((response == 0) || (response_capacity < length)))
        {
            return USB_NCM_CONTROL_STALL;
        }
        if(length != 0u)
        {
            memcpy(response, &s_ntb_parameters, length);
        }
        *response_length = length;
        return USB_NCM_CONTROL_DATA;
    }

    return USB_NCM_CONTROL_STALL;
}

void usb_ncm_set_link_state(bool link_up)
{
    const uint8_t value = link_up ? 1u : 0u;
    if(s_link_up == value)
    {
        return;
    }
    s_link_up = value;
    if(s_alt_setting == 1u)
    {
        s_notification = USB_NCM_NOTIFY_CONNECTED;
    }
}

bool usb_ncm_link_is_up(void)
{
    return s_link_up != 0u;
}

uint8_t usb_ncm_data_alt_setting(void)
{
    return s_alt_setting;
}

usb_ncm_speed_t usb_ncm_speed(void)
{
    return s_speed;
}

bool usb_ncm_notification_pending(void)
{
    return s_notification != USB_NCM_NOTIFY_NONE;
}

bool usb_ncm_next_notification(uint8_t *buffer,
                               uint8_t capacity,
                               uint8_t *length)
{
    uint32_t bits_per_second;
    if(length != 0)
    {
        *length = 0u;
    }
    if((buffer == 0) || (length == 0) ||
       (s_notification == USB_NCM_NOTIFY_NONE) ||
       (s_alt_setting != 1u))
    {
        return false;
    }

    if(s_notification == USB_NCM_NOTIFY_SPEED)
    {
        if(capacity < 16u)
        {
            return false;
        }
        memset(buffer, 0, 16u);
        buffer[0] = 0xA1u;
        buffer[1] = USB_CDC_NOTIFICATION_SPEED_CHANGE;
        put_u16(&buffer[4], 0u); /* IF0 */
        put_u16(&buffer[6], 8u);
        bits_per_second = (s_speed == USB_NCM_SPEED_HIGH)
            ? 480000000ul
            : 12000000ul;
        put_u32(&buffer[8], bits_per_second);
        put_u32(&buffer[12], bits_per_second);
        *length = 16u;
        s_notification = USB_NCM_NOTIFY_CONNECTED;
        return true;
    }

    if(capacity < 8u)
    {
        return false;
    }
    memset(buffer, 0, 8u);
    buffer[0] = 0xA1u;
    buffer[1] = USB_CDC_NOTIFICATION_NETWORK_CONNECTION;
    put_u16(&buffer[2], s_link_up != 0u ? 1u : 0u);
    put_u16(&buffer[4], 0u); /* IF0 */
    put_u16(&buffer[6], 0u);
    *length = 8u;
    s_notification = USB_NCM_NOTIFY_NONE;
    return true;
}

bool usb_ncm_pack_frame(const uint8_t *frame,
                        uint16_t frame_length,
                        uint16_t sequence,
                        uint8_t *ntb,
                        uint16_t ntb_capacity,
                        uint16_t *ntb_length)
{
    const uint16_t block_length =
        (uint16_t)(USB_NCM_SINGLE_FRAME_OFFSET + frame_length);
    if(ntb_length != 0)
    {
        *ntb_length = 0u;
    }
    if((frame == 0) || (ntb == 0) || (ntb_length == 0) ||
       (frame_length == 0u) ||
       (frame_length > USB_NCM_ETHERNET_FRAME_MAX_BYTES) ||
       (block_length > ntb_capacity) ||
       (block_length > USB_NCM_NTB_MAX_BYTES))
    {
        return false;
    }

    memset(ntb, 0, USB_NCM_SINGLE_FRAME_OFFSET);
    put_u32(&ntb[0], USB_NTH16_SIGNATURE);
    put_u16(&ntb[4], USB_NTH16_BYTES);
    put_u16(&ntb[6], sequence);
    put_u16(&ntb[8], block_length);
    put_u16(&ntb[10], USB_NTH16_BYTES);
    put_u32(&ntb[12], USB_NDP16_SIGNATURE_NCM0);
    put_u16(&ntb[16], USB_NDP16_SINGLE_BYTES);
    put_u16(&ntb[18], 0u);
    put_u16(&ntb[20], USB_NCM_SINGLE_FRAME_OFFSET);
    put_u16(&ntb[22], frame_length);
    put_u16(&ntb[24], 0u);
    put_u16(&ntb[26], 0u);
    memcpy(&ntb[USB_NCM_SINGLE_FRAME_OFFSET], frame, frame_length);
    *ntb_length = block_length;
    return true;
}

usb_ncm_parse_result_t usb_ncm_unpack_ntb(const uint8_t *ntb,
                                          uint16_t ntb_length,
                                          usb_ncm_frame_sink_t sink,
                                          void *context,
                                          uint8_t *frame_count)
{
    uint16_t block_length;
    uint16_t ndp_index;
    uint16_t ndp_length;
    uint16_t entry_count;
    uint16_t entry_offset;
    uint8_t count = 0u;
    uint8_t terminator_seen = 0u;
    uint16_t i;

    if(frame_count != 0)
    {
        *frame_count = 0u;
    }
    if((ntb == 0) || (sink == 0) || (frame_count == 0))
    {
        return USB_NCM_PARSE_INVALID_ARGUMENT;
    }
    if((ntb_length < USB_NCM_SINGLE_FRAME_OFFSET) ||
       (ntb_length > USB_NCM_NTB_MAX_BYTES))
    {
        return USB_NCM_PARSE_BAD_LENGTH;
    }
    if((get_u32(&ntb[0]) != USB_NTH16_SIGNATURE) ||
       (get_u16(&ntb[4]) != USB_NTH16_BYTES))
    {
        return USB_NCM_PARSE_BAD_NTH;
    }
    block_length = get_u16(&ntb[8]);
    ndp_index = get_u16(&ntb[10]);
    if((block_length > ntb_length) ||
       (block_length < USB_NCM_SINGLE_FRAME_OFFSET))
    {
        return USB_NCM_PARSE_BAD_LENGTH;
    }
    if((ndp_index < USB_NTH16_BYTES) ||
       ((uint32_t)ndp_index + USB_NDP16_SINGLE_BYTES > block_length))
    {
        return USB_NCM_PARSE_BAD_NDP;
    }
    if((get_u32(&ntb[ndp_index]) != USB_NDP16_SIGNATURE_NCM0) &&
       (get_u32(&ntb[ndp_index]) != USB_NDP16_SIGNATURE_NCM1))
    {
        return USB_NCM_PARSE_BAD_NDP;
    }
    ndp_length = get_u16(&ntb[ndp_index + 4u]);
    if((ndp_length < USB_NDP16_SINGLE_BYTES) ||
       ((ndp_length - USB_NDP16_HEADER_BYTES) %
        USB_NDP16_ENTRY_BYTES) != 0u ||
       ((uint32_t)ndp_index + ndp_length > block_length) ||
       (get_u16(&ntb[ndp_index + 6u]) != 0u))
    {
        return USB_NCM_PARSE_BAD_NDP;
    }

    entry_count =
        (uint16_t)((ndp_length - USB_NDP16_HEADER_BYTES) /
                   USB_NDP16_ENTRY_BYTES);
    entry_offset = (uint16_t)(ndp_index + USB_NDP16_HEADER_BYTES);

    /*
     * Validate the complete pointer table before publishing any frame.  A bad
     * later entry must not cause a valid-looking prefix to escape to STM32.
     */
    for(i = 0u; i < entry_count; ++i)
    {
        const uint16_t frame_index =
            get_u16(&ntb[entry_offset + (i * USB_NDP16_ENTRY_BYTES)]);
        const uint16_t frame_length =
            get_u16(&ntb[entry_offset + (i * USB_NDP16_ENTRY_BYTES) + 2u]);
        if((frame_index == 0u) && (frame_length == 0u))
        {
            terminator_seen = 1u;
            break;
        }
        if((frame_index == 0u) || (frame_length == 0u) ||
           (frame_length > USB_NCM_ETHERNET_FRAME_MAX_BYTES) ||
           ((uint32_t)frame_index + frame_length > block_length))
        {
            return USB_NCM_PARSE_BAD_DATAGRAM;
        }
        if(count >= USB_NCM_MAX_DATAGRAMS_PER_NTB)
        {
            return USB_NCM_PARSE_TOO_MANY_DATAGRAMS;
        }
        ++count;
    }
    if((terminator_seen == 0u) || (count == 0u))
    {
        return USB_NCM_PARSE_BAD_NDP;
    }

    for(i = 0u; i < count; ++i)
    {
        const uint16_t frame_index =
            get_u16(&ntb[entry_offset + (i * USB_NDP16_ENTRY_BYTES)]);
        const uint16_t frame_length =
            get_u16(&ntb[entry_offset + (i * USB_NDP16_ENTRY_BYTES) + 2u]);
        if(!sink(&ntb[frame_index], frame_length, context))
        {
            return USB_NCM_PARSE_SINK_REJECTED;
        }
    }
    *frame_count = count;
    return USB_NCM_PARSE_OK;
}

bool usb_ncm_transfer_needs_zlp(uint16_t transfer_length,
                                usb_ncm_speed_t speed)
{
    const uint16_t packet_size =
        (speed == USB_NCM_SPEED_HIGH)
            ? USB_NCM_ENDPOINT_HS_BYTES
            : USB_NCM_ENDPOINT_FS_BYTES;
    return (transfer_length != 0u) &&
           ((transfer_length % packet_size) == 0u);
}
