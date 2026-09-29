#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "usb_board_link_codec.h"

static void test_empty_control_frame(void)
{
    uint8_t frame[USB_BOARD_LINK_MAX_FRAME_BYTES] = {0};
    uint8_t length = 0u;
    usb_board_link_frame_t decoded;

    assert(usb_board_link_encode(USB_BOARD_CMD_GET_CAPS,
                                 0,
                                 0u,
                                 frame,
                                 sizeof(frame),
                                 &length));
    assert(length == 4u);
    assert(frame[0] == USB_BOARD_LINK_SYNC);
    assert(frame[1] == USB_BOARD_CMD_GET_CAPS);
    assert(frame[2] == 0u);
    assert(frame[3] == 0x5Cu);
    assert(usb_board_link_decode(frame, length, &decoded));
    assert(decoded.command == USB_BOARD_CMD_GET_CAPS);
    assert(decoded.length == 0u);
}

static void test_maximum_frame(void)
{
    uint8_t payload[USB_BOARD_LINK_MAX_PAYLOAD_BYTES];
    uint8_t frame[USB_BOARD_LINK_MAX_FRAME_BYTES] = {0};
    uint8_t length = 0u;
    usb_board_link_frame_t decoded;
    uint8_t index;

    for(index = 0u; index < sizeof(payload); ++index)
    {
        payload[index] = (uint8_t)(index ^ 0xA5u);
    }
    assert(usb_board_link_encode(USB_BOARD_CMD_BULK_FRAGMENT,
                                 payload,
                                 sizeof(payload),
                                 frame,
                                 sizeof(frame),
                                 &length));
    assert(length == USB_BOARD_LINK_MAX_FRAME_BYTES);
    assert(usb_board_link_decode(frame, length, &decoded));
    assert(decoded.length == sizeof(payload));
    assert(memcmp(decoded.payload, payload, sizeof(payload)) == 0);

    frame[length - 1u] ^= 0x01u;
    assert(!usb_board_link_decode(frame, length, &decoded));
}

static void test_parser_resynchronization(void)
{
    const uint8_t payload[] = {USB_BOARD_ROLE_USB};
    uint8_t frame[USB_BOARD_LINK_MAX_FRAME_BYTES] = {0};
    uint8_t length = 0u;
    usb_board_link_parser_t parser;
    usb_board_link_frame_t decoded;
    uint8_t index;
    uint8_t complete = 0u;

    assert(usb_board_link_encode(USB_BOARD_CMD_SELECT_ROLE,
                                 payload,
                                 sizeof(payload),
                                 frame,
                                 sizeof(frame),
                                 &length));
    usb_board_link_parser_init(&parser);
    assert(!usb_board_link_parser_feed(&parser, 0x00u, &decoded));
    assert(!usb_board_link_parser_feed(&parser, 0xFFu, &decoded));
    for(index = 0u; index < length; ++index)
    {
        if(usb_board_link_parser_feed(&parser, frame[index], &decoded))
        {
            ++complete;
        }
    }
    assert(complete == 1u);
    assert(decoded.command == USB_BOARD_CMD_SELECT_ROLE);
    assert(decoded.length == sizeof(payload));
    assert(decoded.payload[0] == USB_BOARD_ROLE_USB);
}

static void test_bulk_crc_empty_message(void)
{
    const uint8_t empty_storage = 0u;

    assert(usb_board_crc16_ccitt(0, 0u) == 0xFFFFu);
    assert(usb_board_crc16_ccitt(&empty_storage, 0u) == 0xFFFFu);
    assert(usb_board_crc16_ccitt(0, 1u) == 0u);
}

static void test_usb_control_codec_boundaries(void)
{
    usb_board_control_request_v1_t request;
    usb_board_link_frame_t decoded;
    uint8_t frame[USB_BOARD_LINK_MAX_FRAME_BYTES] = {0};
    uint8_t frame_length = 0u;
    uint8_t index;

    memset(&request, 0, sizeof(request));
    request.header.opcode = USB_BOARD_CONTROL_SET_MAC;
    request.header.transaction = 0xA5u;
    request.header.status = USB_BOARD_STATUS_OK;
    request.header.data_length = USB_BOARD_CONTROL_DATA_BYTES;
    for(index = 0u; index < USB_BOARD_CONTROL_DATA_BYTES; ++index)
    {
        request.data[index] = (uint8_t)(0x5Au ^ index);
    }

    assert(usb_board_link_encode(
        USB_BOARD_CMD_USB_CONTROL,
        (const uint8_t *)&request,
        USB_BOARD_CONTROL_HEADER_BYTES + request.header.data_length,
        frame,
        sizeof(frame),
        &frame_length));
    assert(frame_length == USB_BOARD_LINK_MAX_FRAME_BYTES);
    assert(usb_board_link_decode(frame, frame_length, &decoded));
    assert(decoded.command == USB_BOARD_CMD_USB_CONTROL);
    assert(decoded.length == USB_BOARD_LINK_MAX_PAYLOAD_BYTES);
    assert(memcmp(decoded.payload,
                  &request,
                  USB_BOARD_LINK_MAX_PAYLOAD_BYTES) == 0);

    assert(!usb_board_link_encode(
        USB_BOARD_CMD_USB_CONTROL,
        (const uint8_t *)&request,
        (uint8_t)(USB_BOARD_LINK_MAX_PAYLOAD_BYTES + 1u),
        frame,
        sizeof(frame),
        &frame_length));
    assert(!usb_board_link_encode(
        USB_BOARD_CMD_USB_CONTROL,
        (const uint8_t *)&request,
        USB_BOARD_LINK_MAX_PAYLOAD_BYTES,
        frame,
        (uint8_t)(USB_BOARD_LINK_MAX_FRAME_BYTES - 1u),
        &frame_length));

    request.header.opcode = USB_BOARD_CONTROL_GET_LINK_STATE;
    request.header.transaction = 0u;
    request.header.data_length = 0u;
    assert(usb_board_link_encode(
        USB_BOARD_CMD_USB_CONTROL,
        (const uint8_t *)&request,
        USB_BOARD_CONTROL_HEADER_BYTES,
        frame,
        sizeof(frame),
        &frame_length));
    assert(frame_length ==
           USB_BOARD_LINK_HEADER_BYTES + USB_BOARD_CONTROL_HEADER_BYTES +
               USB_BOARD_LINK_CHECKSUM_BYTES);
    assert(usb_board_link_decode(frame, frame_length, &decoded));
    assert(decoded.length == USB_BOARD_CONTROL_HEADER_BYTES);
}

int main(void)
{
    _Static_assert(USB_BOARD_LINK_MAX_FRAME_BYTES == 64u,
                   "UsbBoardLink frame size changed");
    _Static_assert(USB_BOARD_LINK_MAX_PAYLOAD_BYTES == 60u,
                   "UsbBoardLink payload size changed");
    _Static_assert(sizeof(usb_board_input_v1_t) == 10u,
                   "UsbBoardLink input ABI changed");
    _Static_assert(sizeof(usb_board_fragment_header_v1_t) == 8u,
                   "UsbBoardLink fragment header changed");
    _Static_assert(USB_BOARD_FRAGMENT_DATA_BYTES == 52u,
                   "UsbBoardLink fragment data size changed");
    _Static_assert(USB_BOARD_CONTROL_HEADER_BYTES == 4u,
                   "USB_CONTROL header ABI changed");
    _Static_assert(USB_BOARD_CONTROL_DATA_BYTES == 56u,
                   "USB_CONTROL payload ceiling changed");
    _Static_assert(sizeof(usb_board_control_request_v1_t) ==
                       USB_BOARD_LINK_MAX_PAYLOAD_BYTES,
                   "USB_CONTROL request container changed");
    _Static_assert(sizeof(usb_board_control_response_v1_t) ==
                       USB_BOARD_LINK_MAX_PAYLOAD_BYTES,
                   "USB_CONTROL response container changed");
    _Static_assert(USB_BOARD_CHANNEL_WEBCONFIG == 0x06u,
                   "WebConfig channel ABI changed");
    _Static_assert(USB_BOARD_CHANNEL_SLOTS == 7u,
                   "BoardLink channel storage ABI changed");

    test_empty_control_frame();
    test_maximum_frame();
    test_parser_resynchronization();
    test_bulk_crc_empty_message();
    test_usb_control_codec_boundaries();
    return 0;
}
