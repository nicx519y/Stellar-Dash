#ifndef USB_BOARD_LINK_CODEC_H
#define USB_BOARD_LINK_CODEC_H

#include <stdbool.h>
#include <stdint.h>

#include "usb_board_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t command;
    uint8_t length;
    uint8_t payload[USB_BOARD_LINK_MAX_PAYLOAD_BYTES];
} usb_board_link_frame_t;

typedef enum
{
    USB_BOARD_PARSE_WAIT_SYNC = 0,
    USB_BOARD_PARSE_COMMAND,
    USB_BOARD_PARSE_LENGTH,
    USB_BOARD_PARSE_PAYLOAD,
    USB_BOARD_PARSE_CHECKSUM
} usb_board_link_parse_state_t;

typedef struct
{
    usb_board_link_parse_state_t state;
    usb_board_link_frame_t frame;
    uint8_t payload_index;
    uint8_t checksum;
} usb_board_link_parser_t;

uint8_t usb_board_link_frame_size(uint8_t payload_length);
bool usb_board_link_encode(uint8_t command,
                           const void *payload,
                           uint8_t payload_length,
                           uint8_t *output,
                           uint8_t output_capacity,
                           uint8_t *output_length);
bool usb_board_link_decode(const uint8_t *data,
                           uint8_t data_length,
                           usb_board_link_frame_t *frame);

void usb_board_link_parser_init(usb_board_link_parser_t *parser);
bool usb_board_link_parser_feed(usb_board_link_parser_t *parser,
                                uint8_t byte,
                                usb_board_link_frame_t *completed_frame);

#ifdef __cplusplus
}
#endif

#endif
