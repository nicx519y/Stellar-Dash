#include "usb_net_bridge.h"

#include <string.h>

#define USB_NET_MESSAGE_BYTES USB_BOARD_BULK_MESSAGE_MAX_BYTES
#define USB_NET_INITIAL_CREDITS USB_BOARD_BULK_CREDIT_WINDOW

typedef struct
{
    uint8_t transaction;
    uint8_t expected_fragment;
    uint16_t expected_length;
    uint16_t length;
    uint16_t crc;
    uint8_t active;
    uint8_t data[USB_NET_MESSAGE_BYTES];
} usb_net_reassembly_t;

static usb_net_reassembly_t s_slots[6];
static uint8_t s_credits[6];
static usb_net_bridge_sink_t s_sink;

void usb_net_bridge_init(void)
{
    memset(s_slots, 0, sizeof(s_slots));
    memset(s_credits, 0, sizeof(s_credits));
    s_credits[USB_BOARD_CHANNEL_USB_DEVICE] = USB_NET_INITIAL_CREDITS;
    s_credits[USB_BOARD_CHANNEL_USB_HOST] = USB_NET_INITIAL_CREDITS;
    s_credits[USB_BOARD_CHANNEL_NETWORK] = USB_NET_INITIAL_CREDITS;
    s_credits[USB_BOARD_CHANNEL_TELEMETRY] = USB_NET_INITIAL_CREDITS;
    s_credits[USB_BOARD_CHANNEL_AUTH] = USB_NET_INITIAL_CREDITS;
    s_sink = 0;
}

void usb_net_bridge_set_sink(usb_net_bridge_sink_t sink)
{
    s_sink = sink;
}

bool usb_net_bridge_fragment(const usb_board_fragment_header_v1_t *header,
                             const uint8_t *data,
                             uint8_t data_length)
{
    usb_net_reassembly_t *slot;
    uint8_t channel;

    if((header == 0) || ((data_length != 0u) && (data == 0)) ||
       (data_length > USB_BOARD_FRAGMENT_DATA_BYTES))
    {
        return false;
    }
    channel = header->channel;
    if((channel == 0u) || (channel >= 6u) ||
       (header->total_length_le > USB_NET_MESSAGE_BYTES))
    {
        return false;
    }
    slot = &s_slots[channel];

    if((header->flags & USB_BOARD_FRAGMENT_FLAG_FIRST) != 0u)
    {
        memset(slot, 0, sizeof(*slot));
        slot->transaction = header->transaction;
        slot->expected_length = header->total_length_le;
        slot->crc = header->message_crc16_le;
        slot->active = 1u;
    }
    if((slot->active == 0u) ||
       (slot->transaction != header->transaction) ||
       (slot->expected_fragment != header->fragment_index) ||
       ((uint32_t)slot->length + data_length > slot->expected_length))
    {
        slot->active = 0u;
        return false;
    }

    if(data_length != 0u)
    {
        memcpy(&slot->data[slot->length], data, data_length);
        slot->length = (uint16_t)(slot->length + data_length);
    }
    slot->expected_fragment++;

    if((header->flags & USB_BOARD_FRAGMENT_FLAG_LAST) != 0u)
    {
        bool accepted;
        if((slot->length != slot->expected_length) ||
           (usb_board_crc16_ccitt(slot->data, slot->length) != slot->crc))
        {
            slot->active = 0u;
            return false;
        }
        accepted = (s_sink != 0)
            ? s_sink((usb_board_channel_t)channel, slot->data, slot->length)
            : true;
        slot->active = 0u;
        return accepted;
    }
    return true;
}

uint8_t usb_net_bridge_credit(usb_board_channel_t channel)
{
    const uint8_t index = (uint8_t)channel;
    return (index < sizeof(s_credits)) ? s_credits[index] : 0u;
}

bool usb_net_bridge_take_credit(usb_board_channel_t channel)
{
    const uint8_t index = (uint8_t)channel;
    if((index == 0u) || (index >= sizeof(s_credits)) ||
       (s_credits[index] == 0u))
    {
        return false;
    }
    --s_credits[index];
    return true;
}

void usb_net_bridge_return_credit(usb_board_channel_t channel)
{
    const uint8_t index = (uint8_t)channel;
    if((index != 0u) && (index < sizeof(s_credits)) &&
       (s_credits[index] < USB_NET_INITIAL_CREDITS))
    {
        ++s_credits[index];
    }
}
