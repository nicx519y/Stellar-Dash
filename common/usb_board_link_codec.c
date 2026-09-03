#include "usb_board_link_codec.h"

#include <string.h>

uint8_t usb_board_link_frame_size(uint8_t payload_length)
{
    if(payload_length > USB_BOARD_LINK_MAX_PAYLOAD_BYTES)
    {
        return 0u;
    }
    return (uint8_t)(USB_BOARD_LINK_HEADER_BYTES + payload_length +
                     USB_BOARD_LINK_CHECKSUM_BYTES);
}

bool usb_board_link_encode(uint8_t command,
                           const void *payload,
                           uint8_t payload_length,
                           uint8_t *output,
                           uint8_t output_capacity,
                           uint8_t *output_length)
{
    uint8_t frame_length = usb_board_link_frame_size(payload_length);

    if(output_length != (uint8_t *)0)
    {
        *output_length = 0u;
    }
    if((output == (uint8_t *)0) || (output_length == (uint8_t *)0) ||
       (frame_length == 0u) || (frame_length > output_capacity) ||
       ((payload_length != 0u) && (payload == (const void *)0)))
    {
        return false;
    }

    output[0] = USB_BOARD_LINK_SYNC;
    output[1] = command;
    output[2] = payload_length;
    if(payload_length != 0u)
    {
        memcpy(&output[3], payload, payload_length);
    }
    output[frame_length - 1u] =
        usb_board_link_checksum(output, (uint16_t)(frame_length - 1u));
    *output_length = frame_length;
    return true;
}

bool usb_board_link_decode(const uint8_t *data,
                           uint8_t data_length,
                           usb_board_link_frame_t *frame)
{
    uint8_t payload_length;
    uint8_t expected_length;

    if((data == (const uint8_t *)0) || (frame == (usb_board_link_frame_t *)0) ||
       (data_length < 4u) || (data[0] != USB_BOARD_LINK_SYNC))
    {
        return false;
    }

    payload_length = data[2];
    expected_length = usb_board_link_frame_size(payload_length);
    if((expected_length == 0u) || (data_length != expected_length) ||
       (usb_board_link_checksum(data, (uint16_t)(data_length - 1u)) !=
        data[data_length - 1u]))
    {
        return false;
    }

    frame->command = data[1];
    frame->length = payload_length;
    if(payload_length != 0u)
    {
        memcpy(frame->payload, &data[3], payload_length);
    }
    return true;
}

void usb_board_link_parser_init(usb_board_link_parser_t *parser)
{
    if(parser == (usb_board_link_parser_t *)0)
    {
        return;
    }
    memset(parser, 0, sizeof(*parser));
    parser->state = USB_BOARD_PARSE_WAIT_SYNC;
}

static void usb_board_link_parser_restart(usb_board_link_parser_t *parser)
{
    parser->state = USB_BOARD_PARSE_COMMAND;
    parser->frame.command = 0u;
    parser->frame.length = 0u;
    parser->payload_index = 0u;
    parser->checksum = USB_BOARD_LINK_SYNC;
}

bool usb_board_link_parser_feed(usb_board_link_parser_t *parser,
                                uint8_t byte,
                                usb_board_link_frame_t *completed_frame)
{
    if((parser == (usb_board_link_parser_t *)0) ||
       (completed_frame == (usb_board_link_frame_t *)0))
    {
        return false;
    }

    switch(parser->state)
    {
    case USB_BOARD_PARSE_WAIT_SYNC:
        if(byte == USB_BOARD_LINK_SYNC)
        {
            usb_board_link_parser_restart(parser);
        }
        break;

    case USB_BOARD_PARSE_COMMAND:
        parser->frame.command = byte;
        parser->checksum = (uint8_t)(parser->checksum + byte);
        parser->state = USB_BOARD_PARSE_LENGTH;
        break;

    case USB_BOARD_PARSE_LENGTH:
        if(byte > USB_BOARD_LINK_MAX_PAYLOAD_BYTES)
        {
            parser->state = USB_BOARD_PARSE_WAIT_SYNC;
            break;
        }
        parser->frame.length = byte;
        parser->payload_index = 0u;
        parser->checksum = (uint8_t)(parser->checksum + byte);
        parser->state = (byte == 0u)
            ? USB_BOARD_PARSE_CHECKSUM
            : USB_BOARD_PARSE_PAYLOAD;
        break;

    case USB_BOARD_PARSE_PAYLOAD:
        parser->frame.payload[parser->payload_index++] = byte;
        parser->checksum = (uint8_t)(parser->checksum + byte);
        if(parser->payload_index >= parser->frame.length)
        {
            parser->state = USB_BOARD_PARSE_CHECKSUM;
        }
        break;

    case USB_BOARD_PARSE_CHECKSUM:
        if(byte == parser->checksum)
        {
            *completed_frame = parser->frame;
            parser->state = USB_BOARD_PARSE_WAIT_SYNC;
            return true;
        }
        if(byte == USB_BOARD_LINK_SYNC)
        {
            usb_board_link_parser_restart(parser);
        }
        else
        {
            parser->state = USB_BOARD_PARSE_WAIT_SYNC;
        }
        break;

    default:
        parser->state = USB_BOARD_PARSE_WAIT_SYNC;
        break;
    }
    return false;
}
