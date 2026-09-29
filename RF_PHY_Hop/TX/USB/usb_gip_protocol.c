#include "usb_gip_protocol.h"

#include <string.h>

static uint16_t gip_encoded_length(uint16_t data_length)
{
    uint16_t remaining = data_length;
    uint16_t encoded = 0u;

    while(remaining != 0u)
    {
        const uint16_t part = (remaining > USB_GIP_CHUNK_DATA_BYTES)
            ? USB_GIP_CHUNK_DATA_BYTES
            : remaining;
        if((encoded < 0x100u) && ((uint16_t)(encoded + part) > 0x80u))
        {
            encoded = (uint16_t)(encoded + part + 0x100u);
        }
        else if(((uint16_t)(encoded + part) / 0x100u) >
                (encoded / 0x100u))
        {
            encoded = (uint16_t)(encoded + (part | 0x80u));
        }
        else
        {
            encoded = (uint16_t)(encoded + part);
        }
        remaining = (uint16_t)(remaining - part);
    }
    return encoded;
}

static uint16_t gip_decoded_length(uint16_t encoded_length)
{
    uint16_t decoded = encoded_length;
    if(decoded > 0x100u)
    {
        decoded = (uint16_t)(decoded - 0x100u);
        decoded = (uint16_t)(decoded -
                             ((decoded / 0x100u) * 0x80u));
    }
    return decoded;
}

void usb_gip_rx_reset(usb_gip_rx_t *context)
{
    if(context != 0)
    {
        memset(context, 0, sizeof(*context));
    }
}

bool usb_gip_rx_consume(usb_gip_rx_t *context,
                        const uint8_t *packet,
                        uint8_t length)
{
    uint8_t payload_length;
    uint8_t flags;

    if((context == 0) || (packet == 0) || (length < 4u))
    {
        return false;
    }

    flags = packet[1];
    payload_length = packet[3];
    if(packet[0] == 0x01u)
    {
        if((length != 13u) ||
           ((flags & USB_GIP_FLAG_INTERNAL) == 0u) ||
           (payload_length != 9u))
        {
            usb_gip_rx_reset(context);
            return false;
        }
        usb_gip_rx_reset(context);
        context->command = packet[0];
        context->flags = flags;
        context->sequence = packet[2];
        context->valid = 1u;
        context->complete = 1u;
        return true;
    }

    if((flags & USB_GIP_FLAG_CHUNKED) == 0u)
    {
        if((uint16_t)payload_length + 4u > length)
        {
            usb_gip_rx_reset(context);
            return false;
        }
        usb_gip_rx_reset(context);
        context->command = packet[0];
        context->flags = flags;
        context->sequence = packet[2];
        context->valid = 1u;
        context->complete = 1u;
        context->data_length = payload_length;
        if(payload_length != 0u)
        {
            memcpy(context->data, &packet[4], payload_length);
        }
        return true;
    }

    if(payload_length == 0u)
    {
        uint16_t end_length;
        if(length < 6u)
        {
            usb_gip_rx_reset(context);
            return false;
        }
        end_length = (uint16_t)packet[4] |
                     ((uint16_t)packet[5] << 8);
        if((context->valid == 0u) ||
           (context->chunked == 0u) ||
           (end_length != context->encoded_chunk_length) ||
           (context->data_length != context->expected_data_length))
        {
            usb_gip_rx_reset(context);
            return false;
        }
        context->flags = flags;
        context->sequence = packet[2];
        context->chunk_ended = 1u;
        context->complete = 1u;
        return true;
    }

    {
        const uint8_t copy_length =
            (payload_length > USB_GIP_CHUNK_DATA_BYTES)
                ? (uint8_t)(payload_length ^ 0x80u)
                : payload_length;
        uint16_t chunk_value;

        if((uint16_t)copy_length + 6u > length)
        {
            usb_gip_rx_reset(context);
            return false;
        }
        chunk_value = (uint16_t)packet[4] |
                      ((uint16_t)packet[5] << 8);

        if((flags & USB_GIP_FLAG_CHUNK_START) != 0u)
        {
            usb_gip_rx_reset(context);
            context->command = packet[0];
            context->sequence = packet[2];
            context->chunked = 1u;
            context->encoded_chunk_length = chunk_value;
            context->expected_data_length =
                gip_decoded_length(chunk_value);
            if(context->expected_data_length > sizeof(context->data))
            {
                usb_gip_rx_reset(context);
                return false;
            }
            context->valid = 1u;
        }
        else if((context->valid == 0u) ||
                (context->chunked == 0u) ||
                (context->command != packet[0]))
        {
            usb_gip_rx_reset(context);
            return false;
        }

        if((uint32_t)context->data_length + copy_length >
           sizeof(context->data))
        {
            usb_gip_rx_reset(context);
            return false;
        }
        memcpy(&context->data[context->data_length],
               &packet[6],
               copy_length);
        context->data_length =
            (uint16_t)(context->data_length + copy_length);
        context->encoded_chunk_received =
            (uint16_t)(context->encoded_chunk_received + payload_length);
        context->flags = flags;
        context->sequence = packet[2];
        context->complete = 0u;
        return true;
    }
}

bool usb_gip_rx_ack_required(const usb_gip_rx_t *context)
{
    return (context != 0) &&
           (context->valid != 0u) &&
           ((context->flags & USB_GIP_FLAG_NEEDS_ACK) != 0u);
}

bool usb_gip_rx_make_ack(const usb_gip_rx_t *context,
                         uint8_t *packet,
                         uint8_t *length)
{
    uint16_t remaining = 0u;

    if((context == 0) || (packet == 0) || (length == 0) ||
       (context->valid == 0u))
    {
        return false;
    }
    if((context->chunked != 0u) &&
       (context->expected_data_length > context->data_length))
    {
        remaining =
            (uint16_t)(context->expected_data_length -
                       context->data_length);
    }
    packet[0] = 0x01u;
    packet[1] = USB_GIP_FLAG_INTERNAL;
    packet[2] = context->sequence;
    packet[3] = 0x09u;
    packet[4] = 0x00u;
    packet[5] = context->command;
    packet[6] = USB_GIP_FLAG_INTERNAL;
    packet[7] = (uint8_t)context->data_length;
    packet[8] = (uint8_t)(context->data_length >> 8);
    packet[9] = 0u;
    packet[10] = 0u;
    packet[11] = (uint8_t)remaining;
    packet[12] = (uint8_t)(remaining >> 8);
    *length = 13u;
    return true;
}

void usb_gip_tx_reset(usb_gip_tx_t *context)
{
    if(context != 0)
    {
        memset(context, 0, sizeof(*context));
    }
}

bool usb_gip_tx_begin(usb_gip_tx_t *context,
                      uint8_t command,
                      uint8_t sequence,
                      bool internal,
                      bool needs_ack,
                      const uint8_t *data,
                      uint16_t length)
{
    if((context == 0) ||
       (length > USB_GIP_DATA_MAX_BYTES) ||
       ((length != 0u) && (data == 0)))
    {
        return false;
    }
    usb_gip_tx_reset(context);
    context->command = command;
    context->sequence = sequence;
    context->flags = (internal ? USB_GIP_FLAG_INTERNAL : 0u) |
                     (needs_ack ? USB_GIP_FLAG_NEEDS_ACK : 0u);
    context->data_length = length;
    context->chunked = (length > USB_GIP_CHUNK_DATA_BYTES) ? 1u : 0u;
    if(context->chunked != 0u)
    {
        context->encoded_chunk_length = gip_encoded_length(length);
    }
    if(length != 0u)
    {
        memcpy(context->data, data, length);
    }
    return true;
}

bool usb_gip_tx_next(usb_gip_tx_t *context,
                     uint8_t *packet,
                     uint8_t *length)
{
    uint8_t flags;

    if((context == 0) || (packet == 0) || (length == 0) ||
       (context->chunk_ended != 0u))
    {
        return false;
    }

    flags = context->flags;
    if(context->chunked == 0u)
    {
        packet[0] = context->command;
        packet[1] = flags;
        packet[2] = context->sequence;
        packet[3] = (uint8_t)context->data_length;
        if(context->data_length != 0u)
        {
            memcpy(&packet[4], context->data, context->data_length);
        }
        *length = (uint8_t)(4u + context->data_length);
        context->data_sent = context->data_length;
        context->chunk_ended = 1u;
        return true;
    }

    if(context->data_sent == context->data_length)
    {
        packet[0] = context->command;
        packet[1] = (uint8_t)((flags | USB_GIP_FLAG_CHUNKED) &
                              (uint8_t)~USB_GIP_FLAG_NEEDS_ACK);
        packet[2] = context->sequence;
        packet[3] = 0u;
        packet[4] = (uint8_t)context->encoded_chunk_length;
        packet[5] = (uint8_t)(context->encoded_chunk_length >> 8);
        *length = 6u;
        context->chunk_ended = 1u;
        return true;
    }

    {
        uint16_t remaining =
            (uint16_t)(context->data_length - context->data_sent);
        uint8_t part = (remaining > USB_GIP_CHUNK_DATA_BYTES)
            ? USB_GIP_CHUNK_DATA_BYTES
            : (uint8_t)remaining;
        uint16_t chunk_value;
        uint8_t encoded_part = part;

        flags |= USB_GIP_FLAG_CHUNKED;
        if(context->chunks_sent == 0u)
        {
            flags |= USB_GIP_FLAG_CHUNK_START;
            chunk_value = context->encoded_chunk_length;
        }
        else
        {
            flags &= (uint8_t)~USB_GIP_FLAG_CHUNK_START;
            chunk_value = context->encoded_chunk_sent;
        }
        if((context->chunks_sent == 0u) ||
           (((context->chunks_sent + 1u) % 5u) == 0u) ||
           (part == remaining))
        {
            flags |= USB_GIP_FLAG_NEEDS_ACK;
        }
        else
        {
            flags &= (uint8_t)~USB_GIP_FLAG_NEEDS_ACK;
        }
        if((context->chunks_sent > 0u) &&
           (context->encoded_chunk_sent < 0x100u))
        {
            encoded_part |= 0x80u;
        }
        else if((context->chunks_sent == 0u) &&
                (context->data_length > USB_GIP_CHUNK_DATA_BYTES) &&
                (context->data_length < 0x80u))
        {
            encoded_part |= 0x80u;
        }

        packet[0] = context->command;
        packet[1] = flags;
        packet[2] = context->sequence;
        packet[3] = encoded_part;
        packet[4] = (uint8_t)chunk_value;
        packet[5] = (uint8_t)(chunk_value >> 8);
        memcpy(&packet[6], &context->data[context->data_sent], part);
        *length = (uint8_t)(6u + part);

        if((context->encoded_chunk_sent < 0x100u) &&
           ((uint16_t)(context->encoded_chunk_sent + part) > 0x80u))
        {
            context->encoded_chunk_sent =
                (uint16_t)(context->encoded_chunk_sent + part + 0x100u);
        }
        else if(((uint16_t)(context->encoded_chunk_sent + part) / 0x100u) >
                (context->encoded_chunk_sent / 0x100u))
        {
            context->encoded_chunk_sent =
                (uint16_t)(context->encoded_chunk_sent +
                           (part | 0x80u));
        }
        else
        {
            context->encoded_chunk_sent =
                (uint16_t)(context->encoded_chunk_sent + part);
        }
        context->data_sent = (uint16_t)(context->data_sent + part);
        ++context->chunks_sent;
        return true;
    }
}

bool usb_gip_tx_complete(const usb_gip_tx_t *context)
{
    return (context != 0) && (context->chunk_ended != 0u);
}
