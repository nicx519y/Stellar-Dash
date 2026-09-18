#include "usb_xbox_device.h"

#include <string.h>

#include "usb_auth.h"
#include "usb_gip_protocol.h"

#define XBOX_ANNOUNCE_DELAY_MS       500u
#define XBOX_ACK_TIMEOUT_MS         2000u
#define XBOX_KEEPALIVE_MS          15000u
#define XBOX_ACK_QUEUE_SLOTS           4u

#define GIP_ACK_RESPONSE              0x01u
#define GIP_ANNOUNCE                  0x02u
#define GIP_KEEPALIVE                 0x03u
#define GIP_DEVICE_DESCRIPTOR         0x04u
#define GIP_POWER_MODE                0x05u
#define GIP_AUTH                      0x06u
#define GIP_VIRTUAL_KEYCODE           0x07u
#define GIP_RUMBLE                    0x09u
#define GIP_LED                       0x0Au
#define GIP_FINAL_AUTH                0x1Eu
#define GIP_INPUT_REPORT              0x20u

#define XBOX_GUIDE_ACTION             (1ul << 16)
#define XBOX_REGULAR_ACTIONS          0x0000FFFFul

typedef struct
{
    uint8_t data[USB_GIP_PACKET_MAX_BYTES];
    uint8_t length;
} xbox_frame_t;

typedef struct
{
    xbox_frame_t frames[XBOX_ACK_QUEUE_SLOTS];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} xbox_frame_queue_t;

/*
 * Source: application/Cpp_Core/Src/drivers/xbone/XBOneDriver.cpp,
 * xboxOneDescriptor[].  This is the console-facing GIP device descriptor,
 * not the USB configuration descriptor.
 */
static const uint8_t s_xbox_gip_descriptor[] = {
    0x10u,0x00u,0x01u,0x00u,0x00u,0x00u,0x00u,0x00u,
    0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0xCAu,0x00u,
    0x8Bu,0x00u,0x16u,0x00u,0x1Fu,0x00u,0x20u,0x00u,
    0x27u,0x00u,0x2Du,0x00u,0x4Au,0x00u,0x00u,0x00u,
    0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x02u,0x01u,
    0x00u,0x00u,0x00u,0x01u,0x00u,0x01u,0x00u,0x00u,
    0x06u,0x01u,0x02u,0x03u,0x04u,0x06u,0x07u,0x05u,
    0x01u,0x04u,0x05u,0x06u,0x0Au,0x01u,0x1Au,0x00u,
    0x57u,0x69u,0x6Eu,0x64u,0x6Fu,0x77u,0x73u,0x2Eu,
    0x58u,0x62u,0x6Fu,0x78u,0x2Eu,0x49u,0x6Eu,0x70u,
    0x75u,0x74u,0x2Eu,0x47u,0x61u,0x6Du,0x65u,0x70u,
    0x61u,0x64u,0x04u,0x56u,0xFFu,0x76u,0x97u,0xFDu,
    0x9Bu,0x81u,0x45u,0xADu,0x45u,0xB6u,0x45u,0xBBu,
    0xA5u,0x26u,0xD6u,0x2Cu,0x40u,0x2Eu,0x08u,0xDFu,
    0x07u,0xE1u,0x45u,0xA5u,0xABu,0xA3u,0x12u,0x7Au,
    0xF1u,0x97u,0xB5u,0xE7u,0x1Fu,0xF3u,0xB8u,0x86u,
    0x73u,0xE9u,0x40u,0xA9u,0xF8u,0x2Fu,0x21u,0x26u,
    0x3Au,0xCFu,0xB7u,0xFEu,0xD2u,0xDDu,0xECu,0x87u,
    0xD3u,0x94u,0x42u,0xBDu,0x96u,0x1Au,0x71u,0x2Eu,
    0x3Du,0xC7u,0x7Du,0x02u,0x17u,0x00u,0x20u,0x20u,
    0x00u,0x01u,0x00u,0x10u,0x00u,0x00u,0x00u,0x00u,
    0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
    0x00u,0x00u,0x00u,0x17u,0x00u,0x09u,0x3Cu,0x00u,
    0x01u,0x00u,0x08u,0x00u,0x00u,0x00u,0x00u,0x00u,
    0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
    0x00u,0x00u
};

static const uint8_t s_idle_payload[32] = {
    0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0xFFu,0xFFu,
    0x00u,0x00u,0xFFu,0xFFu,0x00u,0x00u,0x00u,0x00u,
    0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
    0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u
};

typedef char xbox_gip_descriptor_must_be_202_bytes[
    (sizeof(s_xbox_gip_descriptor) == 202u) ? 1 : -1];
typedef char xbox_input_report_must_be_36_bytes[
    (USB_XBOX_DEVICE_INPUT_BYTES == 36u) ? 1 : -1];

static xbox_frame_queue_t s_ack_queue;
static usb_gip_rx_t s_rx;
static usb_gip_tx_t s_descriptor_tx;
static uint8_t s_descriptor_active;
static uint8_t s_waiting_ack;
static uint32_t s_ack_started_ms;

static uint8_t s_mounted;
static uint8_t s_announce_due;
static uint8_t s_announce_sent;
static uint32_t s_mounted_ms;
static uint32_t s_now_ms;

static uint32_t s_actions;
static uint8_t s_input_dirty;
static uint8_t s_input_sequence;
static uint8_t s_guide_sent;
static uint8_t s_guide_sequence;
static uint8_t s_guide_pending;
static uint8_t s_authenticated;
static uint8_t s_auth_session_seen;

static uint8_t s_powered_on;
static uint8_t s_led_mode;
static uint8_t s_led_brightness;
static uint8_t s_rumble[USB_GIP_CHUNK_DATA_BYTES];
static uint8_t s_rumble_length;
static uint8_t s_keepalive_sequence;
static uint32_t s_keepalive_ms;

static bool elapsed(uint32_t started, uint32_t interval)
{
    return (uint32_t)(s_now_ms - started) >= interval;
}

static void queue_reset(xbox_frame_queue_t *queue)
{
    memset(queue, 0, sizeof(*queue));
}

static bool queue_push(xbox_frame_queue_t *queue,
                       const uint8_t *data,
                       uint8_t length)
{
    xbox_frame_t *frame;

    if((queue == 0) || (data == 0) || (length == 0u) ||
       (length > USB_GIP_PACKET_MAX_BYTES) ||
       (queue->count >= XBOX_ACK_QUEUE_SLOTS))
    {
        return false;
    }
    frame = &queue->frames[queue->head];
    memcpy(frame->data, data, length);
    frame->length = length;
    queue->head = (uint8_t)((queue->head + 1u) % XBOX_ACK_QUEUE_SLOTS);
    ++queue->count;
    return true;
}

static bool queue_take(xbox_frame_queue_t *queue,
                       uint8_t *data,
                       uint8_t capacity,
                       uint8_t *length)
{
    const xbox_frame_t *frame;

    if((queue == 0) || (data == 0) || (length == 0) ||
       (queue->count == 0u))
    {
        return false;
    }
    frame = &queue->frames[queue->tail];
    if(capacity < frame->length)
    {
        return false;
    }
    memcpy(data, frame->data, frame->length);
    *length = frame->length;
    queue->tail = (uint8_t)((queue->tail + 1u) % XBOX_ACK_QUEUE_SLOTS);
    --queue->count;
    return true;
}

static bool emit_message(uint8_t command,
                         uint8_t sequence,
                         bool internal,
                         const uint8_t *payload,
                         uint8_t payload_length,
                         uint8_t *packet,
                         uint8_t capacity,
                         uint8_t *length)
{
    if((packet == 0) || (length == 0) ||
       ((payload_length != 0u) && (payload == 0)) ||
       ((uint16_t)payload_length + 4u > capacity))
    {
        return false;
    }
    packet[0] = command;
    packet[1] = internal ? USB_GIP_FLAG_INTERNAL : 0u;
    packet[2] = sequence;
    packet[3] = payload_length;
    if(payload_length != 0u)
    {
        memcpy(&packet[4], payload, payload_length);
    }
    *length = (uint8_t)(payload_length + 4u);
    return true;
}

static bool emit_announce(uint8_t *packet,
                          uint8_t capacity,
                          uint8_t *length)
{
    uint8_t announce[] = {
        0x00u,0x2Au,0x00u,0xFFu,0xFFu,0xFFu,0x00u,0x00u,
        0xDFu,0x33u,0x14u,0x00u,0x01u,0x00u,0x01u,0x00u,
        0x17u,0x01u,0x02u,0x00u,0x01u,0x00u,0x01u,0x00u,
        0x01u,0x00u,0x01u,0x00u
    };

    announce[3] = (uint8_t)s_now_ms;
    announce[4] = (uint8_t)(s_now_ms >> 8);
    announce[5] = (uint8_t)(s_now_ms >> 16);
    if(!emit_message(GIP_ANNOUNCE, 1u, true,
                     announce, sizeof(announce),
                     packet, capacity, length))
    {
        return false;
    }
    s_announce_due = 0u;
    s_announce_sent = 1u;
    return true;
}

static bool emit_guide(uint8_t *packet,
                       uint8_t capacity,
                       uint8_t *length)
{
    uint8_t payload[2];

    ++s_guide_sequence;
    if(s_guide_sequence == 0u)
    {
        s_guide_sequence = 1u;
    }
    payload[0] = ((s_actions & XBOX_GUIDE_ACTION) != 0u) ? 0x01u : 0x00u;
    payload[1] = 0x5Bu;
    if(!emit_message(GIP_VIRTUAL_KEYCODE, s_guide_sequence, true,
                     payload, sizeof(payload),
                     packet, capacity, length))
    {
        --s_guide_sequence;
        return false;
    }
    s_guide_sent = payload[0];
    s_guide_pending = 0u;
    return true;
}

static bool emit_keepalive(uint8_t *packet,
                           uint8_t capacity,
                           uint8_t *length)
{
    static const uint8_t payload[] = {0x80u,0x00u,0x00u,0x00u};

    ++s_keepalive_sequence;
    if(s_keepalive_sequence == 0u)
    {
        s_keepalive_sequence = 1u;
    }
    if(!emit_message(GIP_KEEPALIVE, s_keepalive_sequence, true,
                     payload, sizeof(payload),
                     packet, capacity, length))
    {
        --s_keepalive_sequence;
        return false;
    }
    s_keepalive_ms = s_now_ms;
    return true;
}

static void store_u16_le(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

static bool emit_input(bool idle,
                       uint8_t *packet,
                       uint8_t capacity,
                       uint8_t *length)
{
    uint8_t payload[32];
    uint8_t sequence = 0u;
    uint8_t buttons0 = 0u;
    uint8_t buttons1 = 0u;

    if(idle)
    {
        memcpy(payload, s_idle_payload, sizeof(payload));
    }
    else
    {
        memset(payload, 0, sizeof(payload));
        ++s_input_sequence;
        if(s_input_sequence == 0u)
        {
            s_input_sequence = 1u;
        }
        sequence = s_input_sequence;

        buttons0 |= (s_actions & (1ul << 13)) ? (1u << 2) : 0u;
        buttons0 |= (s_actions & (1ul << 12)) ? (1u << 3) : 0u;
        buttons0 |= (s_actions & (1ul << 4)) ? (1u << 4) : 0u;
        buttons0 |= (s_actions & (1ul << 5)) ? (1u << 5) : 0u;
        buttons0 |= (s_actions & (1ul << 6)) ? (1u << 6) : 0u;
        buttons0 |= (s_actions & (1ul << 7)) ? (1u << 7) : 0u;
        buttons1 |= (s_actions & (1ul << 0)) ? (1u << 0) : 0u;
        buttons1 |= (s_actions & (1ul << 1)) ? (1u << 1) : 0u;
        buttons1 |= (s_actions & (1ul << 2)) ? (1u << 2) : 0u;
        buttons1 |= (s_actions & (1ul << 3)) ? (1u << 3) : 0u;
        buttons1 |= (s_actions & (1ul << 8)) ? (1u << 4) : 0u;
        buttons1 |= (s_actions & (1ul << 9)) ? (1u << 5) : 0u;
        buttons1 |= (s_actions & (1ul << 14)) ? (1u << 6) : 0u;
        buttons1 |= (s_actions & (1ul << 15)) ? (1u << 7) : 0u;
        payload[0] = buttons0;
        payload[1] = buttons1;
        store_u16_le(&payload[2],
                     (s_actions & (1ul << 10)) ? 0x03FFu : 0u);
        store_u16_le(&payload[4],
                     (s_actions & (1ul << 11)) ? 0x03FFu : 0u);
    }

    if(!emit_message(GIP_INPUT_REPORT, sequence, false,
                     payload, sizeof(payload),
                     packet, capacity, length))
    {
        return false;
    }
    if(!idle)
    {
        s_input_dirty = 0u;
    }
    return true;
}

void usb_xbox_device_reset(void)
{
    queue_reset(&s_ack_queue);
    usb_gip_rx_reset(&s_rx);
    usb_gip_tx_reset(&s_descriptor_tx);
    s_descriptor_active = 0u;
    s_waiting_ack = 0u;
    s_ack_started_ms = 0u;
    s_mounted = 0u;
    s_announce_due = 0u;
    s_announce_sent = 0u;
    s_mounted_ms = 0u;
    s_now_ms = 0u;
    s_actions = 0u;
    s_input_dirty = 1u;
    s_input_sequence = 0u;
    s_guide_sent = 0u;
    s_guide_sequence = 0u;
    s_guide_pending = 0u;
    s_authenticated = 0u;
    s_auth_session_seen = 0u;
    s_powered_on = 0u;
    s_led_mode = 0u;
    s_led_brightness = 0u;
    memset(s_rumble, 0, sizeof(s_rumble));
    s_rumble_length = 0u;
    s_keepalive_sequence = 0u;
    s_keepalive_ms = 0u;
}

void usb_xbox_device_init(void)
{
    usb_xbox_device_reset();
}

void usb_xbox_device_set_mounted(bool mounted, uint32_t now_ms)
{
    if(!mounted)
    {
        usb_xbox_device_reset();
        s_now_ms = now_ms;
        return;
    }
    if(s_mounted == 0u)
    {
        s_mounted = 1u;
        s_now_ms = now_ms;
        s_mounted_ms = now_ms;
        s_keepalive_ms = now_ms;
        s_input_dirty = 1u;
    }
}

void usb_xbox_device_set_actions(uint32_t action_mask)
{
    const uint32_t changed = s_actions ^ action_mask;
    s_actions = action_mask;
    if((changed & XBOX_REGULAR_ACTIONS) != 0u)
    {
        s_input_dirty = 1u;
    }
    if((changed & XBOX_GUIDE_ACTION) != 0u)
    {
        s_guide_pending = 1u;
    }
}

void usb_xbox_device_process(uint32_t now_ms)
{
    const uint8_t authenticated =
        ((s_auth_session_seen != 0u) &&
         usb_auth_is_authenticated(USB_AUTH_SCHEME_XBOX_GIP)) ? 1u : 0u;

    s_now_ms = now_ms;
    if(s_mounted == 0u)
    {
        return;
    }
    if((s_announce_sent == 0u) &&
       elapsed(s_mounted_ms, XBOX_ANNOUNCE_DELAY_MS))
    {
        s_announce_due = 1u;
    }
    if((s_waiting_ack != 0u) &&
       elapsed(s_ack_started_ms, XBOX_ACK_TIMEOUT_MS))
    {
        /* Legacy behavior continues after the two-second ACK window. */
        s_waiting_ack = 0u;
    }
    if((authenticated != 0u) && (s_authenticated == 0u))
    {
        s_authenticated = 1u;
        s_input_dirty = 1u;
        s_keepalive_ms = now_ms;
        if((((s_actions & XBOX_GUIDE_ACTION) != 0u) ? 1u : 0u) !=
           s_guide_sent)
        {
            s_guide_pending = 1u;
        }
    }
    else if(authenticated == 0u)
    {
        s_authenticated = 0u;
    }
}

bool usb_xbox_device_out(const uint8_t *packet, uint8_t length)
{
    uint8_t ack[USB_GIP_PACKET_MAX_BYTES];
    uint8_t ack_length = 0u;

    if((s_mounted == 0u) || (packet == 0) || (length < 4u))
    {
        return false;
    }

    if((packet[0] == GIP_AUTH) || (packet[0] == GIP_FINAL_AUTH))
    {
        if(!usb_auth_gip_device_out(packet, length))
        {
            return false;
        }
        s_auth_session_seen = 1u;
        s_authenticated = 0u;
        return true;
    }

    if(packet[0] == GIP_ACK_RESPONSE)
    {
        s_waiting_ack = 0u;
        /*
         * Authentication responses are independently chunked.  Let the auth
         * engine observe ACKs as well; absence of an active auth exchange is
         * not an error for the console-facing state machine.
         */
        (void)usb_auth_gip_device_out(packet, length);
        return true;
    }

    if(!usb_gip_rx_consume(&s_rx, packet, length))
    {
        return false;
    }
    if(usb_gip_rx_ack_required(&s_rx))
    {
        if(!usb_gip_rx_make_ack(&s_rx, ack, &ack_length) ||
           !queue_push(&s_ack_queue, ack, ack_length))
        {
            return false;
        }
    }
    if(s_rx.complete == 0u)
    {
        return true;
    }

    switch(s_rx.command)
    {
    case GIP_DEVICE_DESCRIPTOR:
        usb_gip_tx_reset(&s_descriptor_tx);
        s_descriptor_active =
            usb_gip_tx_begin(&s_descriptor_tx,
                             GIP_DEVICE_DESCRIPTOR,
                             s_rx.sequence,
                             true,
                             false,
                             s_xbox_gip_descriptor,
                             sizeof(s_xbox_gip_descriptor))
                ? 1u
                : 0u;
        s_waiting_ack = 0u;
        break;

    case GIP_POWER_MODE:
        s_powered_on = 1u;
        break;

    case GIP_LED:
        if(s_rx.data_length >= 3u)
        {
            s_led_mode = s_rx.data[1];
            s_led_brightness = s_rx.data[2];
        }
        break;

    case GIP_RUMBLE:
        s_rumble_length =
            (s_rx.data_length > sizeof(s_rumble))
                ? sizeof(s_rumble)
                : (uint8_t)s_rx.data_length;
        if(s_rumble_length != 0u)
        {
            memcpy(s_rumble, s_rx.data, s_rumble_length);
        }
        break;

    case GIP_KEEPALIVE:
    default:
        break;
    }
    usb_gip_rx_reset(&s_rx);
    return true;
}

bool usb_xbox_device_next_in(uint8_t *packet,
                             uint8_t capacity,
                             uint8_t *length)
{
    if(length != 0)
    {
        *length = 0u;
    }
    if((s_mounted == 0u) || (packet == 0) || (length == 0) ||
       (capacity < USB_GIP_PACKET_MAX_BYTES))
    {
        return false;
    }

    if(queue_take(&s_ack_queue, packet, capacity, length))
    {
        return true;
    }
    if(s_announce_due != 0u)
    {
        return emit_announce(packet, capacity, length);
    }
    if((s_descriptor_active != 0u) && (s_waiting_ack == 0u))
    {
        if(usb_gip_tx_next(&s_descriptor_tx, packet, length))
        {
            if((packet[1] & USB_GIP_FLAG_NEEDS_ACK) != 0u)
            {
                s_waiting_ack = 1u;
                s_ack_started_ms = s_now_ms;
            }
            if(usb_gip_tx_complete(&s_descriptor_tx))
            {
                s_descriptor_active = 0u;
            }
            return true;
        }
        s_descriptor_active = 0u;
    }
    if(usb_auth_gip_device_in_pending() &&
       usb_auth_gip_device_in(packet, capacity, length))
    {
        return true;
    }
    if((s_authenticated != 0u) && (s_guide_pending != 0u))
    {
        return emit_guide(packet, capacity, length);
    }
    if((s_authenticated != 0u) &&
       elapsed(s_keepalive_ms, XBOX_KEEPALIVE_MS))
    {
        return emit_keepalive(packet, capacity, length);
    }
    if(s_authenticated == 0u)
    {
        return emit_input(true, packet, capacity, length);
    }
    if(s_input_dirty != 0u)
    {
        return emit_input(false, packet, capacity, length);
    }
    return false;
}
