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
    uint8_t complete;
    uint8_t data[USB_NET_MESSAGE_BYTES];
} usb_net_reassembly_t;

static usb_net_reassembly_t s_slots[USB_BOARD_CHANNEL_SLOTS];
static uint8_t s_credits[USB_BOARD_CHANNEL_SLOTS];
static usb_net_bridge_sink_t s_sink;

static uint8_t credit_limit(usb_board_channel_t channel)
{
    return (channel == USB_BOARD_CHANNEL_WEBCONFIG)
        ? USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW
        : USB_NET_INITIAL_CREDITS;
}

static void clear_active_slot(usb_net_reassembly_t *slot)
{
    uint16_t used;

    if(slot == 0)
    {
        return;
    }
    used = (slot->length <= USB_NET_MESSAGE_BYTES)
        ? slot->length
        : USB_NET_MESSAGE_BYTES;
    if(used != 0u)
    {
        memset(slot->data, 0, used);
    }
    memset(slot, 0, sizeof(*slot) - sizeof(slot->data));
}

void usb_net_bridge_init(void)
{
    memset(s_slots, 0, sizeof(s_slots));
    memset(s_credits, 0, sizeof(s_credits));
    s_credits[USB_BOARD_CHANNEL_USB_DEVICE] = USB_NET_INITIAL_CREDITS;
    s_credits[USB_BOARD_CHANNEL_USB_HOST] = USB_NET_INITIAL_CREDITS;
    s_credits[USB_BOARD_CHANNEL_TELEMETRY] = USB_NET_INITIAL_CREDITS;
    s_credits[USB_BOARD_CHANNEL_AUTH] = USB_NET_INITIAL_CREDITS;
    /*
     * WebConfig is fail-closed until the vendor HID interface is configured.
     * usb_device_process() publishes the actual free complete-report slots.
     */
    s_credits[USB_BOARD_CHANNEL_WEBCONFIG] = 0u;
    s_sink = 0;
}

void usb_net_bridge_reset_channel(usb_board_channel_t channel)
{
    const uint8_t index = (uint8_t)channel;
    if((index == 0u) || (index >= USB_BOARD_CHANNEL_SLOTS))
    {
        return;
    }

    memset(&s_slots[index], 0, sizeof(s_slots[index]));
    s_credits[index] =
        ((channel == USB_BOARD_CHANNEL_NETWORK) ||
         (channel == USB_BOARD_CHANNEL_WEBCONFIG))
            ? 0u
            : USB_NET_INITIAL_CREDITS;
}

void usb_net_bridge_set_sink(usb_net_bridge_sink_t sink)
{
    s_sink = sink;
}

void usb_net_bridge_process(void)
{
    usb_net_reassembly_t *slot;

    if(s_sink == 0)
    {
        return;
    }
    slot = &s_slots[USB_BOARD_CHANNEL_WEBCONFIG];
    if((slot->active != 0u) && (slot->complete != 0u) &&
       s_sink(USB_BOARD_CHANNEL_WEBCONFIG,
              slot->data,
              slot->length))
    {
        clear_active_slot(slot);
    }
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
    if((channel == 0u) || (channel >= USB_BOARD_CHANNEL_SLOTS) ||
       (header->total_length_le > USB_NET_MESSAGE_BYTES))
    {
        return false;
    }
    slot = &s_slots[channel];

    if((header->flags & USB_BOARD_FRAGMENT_FLAG_FIRST) != 0u)
    {
        /*
         * A complete report whose sink is temporarily busy owns this slot.
         * Never replace it with a newer FIRST fragment.
         */
        if(slot->active != 0u)
        {
            if(channel == USB_BOARD_CHANNEL_WEBCONFIG)
            {
                return false;
            }
            clear_active_slot(slot);
        }
        slot->transaction = header->transaction;
        slot->expected_length = header->total_length_le;
        slot->crc = header->message_crc16_le;
        slot->active = 1u;
    }
    if(slot->complete != 0u)
    {
        return false;
    }
    if((slot->active == 0u) ||
       (slot->transaction != header->transaction) ||
       (slot->expected_fragment != header->fragment_index) ||
       ((uint32_t)slot->length + data_length > slot->expected_length))
    {
        clear_active_slot(slot);
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
            clear_active_slot(slot);
            return false;
        }
        slot->complete = 1u;
        accepted = (s_sink != 0) &&
                   s_sink((usb_board_channel_t)channel,
                          slot->data,
                          slot->length);
        if(accepted)
        {
            clear_active_slot(slot);
            return true;
        }
        /*
         * A busy or absent sink is still an accepted, retained report. The
         * process hook retries it without clearing or reordering the payload.
         */
        if(channel == USB_BOARD_CHANNEL_WEBCONFIG)
        {
            return true;
        }
        clear_active_slot(slot);
        return false;
    }
    return true;
}

bool usb_net_bridge_message_active(usb_board_channel_t channel)
{
    const uint8_t index = (uint8_t)channel;
    return (index != 0u) &&
           (index < USB_BOARD_CHANNEL_SLOTS) &&
           (s_slots[index].active != 0u);
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
    const uint8_t limit = credit_limit(channel);
    if((index != 0u) && (index < sizeof(s_credits)) &&
       (s_credits[index] < limit))
    {
        ++s_credits[index];
    }
}

void usb_net_bridge_set_credit(usb_board_channel_t channel,
                               uint8_t credits)
{
    const uint8_t index = (uint8_t)channel;
    const uint8_t limit = credit_limit(channel);
    if((index == 0u) || (index >= sizeof(s_credits)))
    {
        return;
    }
    s_credits[index] =
        (credits > limit)
            ? limit
            : credits;
}
