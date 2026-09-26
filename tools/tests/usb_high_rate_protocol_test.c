#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hbox_high_rate_protocol.h"
#include "usb_high_rate.h"
#include "usb_high_rate_descriptors.h"

static hbox_client_control_v1_t make_control(uint8_t opcode,
                                             uint32_t transaction,
                                             const uint8_t token[16])
{
    hbox_client_control_v1_t request;
    memset(&request, 0, sizeof(request));
    request.magic_le = HBOX_CLIENT_CONTROL_MAGIC;
    request.version = HBOX_CLIENT_PROTOCOL_VERSION;
    request.opcode = opcode;
    request.transaction_le = transaction;
    if(token != NULL)
    {
        memcpy(request.lease_token, token, sizeof(request.lease_token));
    }
    request.crc16_le = hbox_client_crc16_ccitt(
        (const uint8_t *)&request,
        offsetof(hbox_client_control_v1_t, crc16_le));
    return request;
}

static void submit_rate(uint16_t rate_hz, uint32_t actions)
{
    usb_board_input_v1_t input;
    memset(&input, 0, sizeof(input));
    input.flags = (uint8_t)(
        (USB_BOARD_INPUT_FORMAT_VERSION << USB_BOARD_INPUT_VERSION_SHIFT) |
        USB_BOARD_INPUT_FLAG_PROCESSED |
        usb_board_input_rate_flags(rate_hz));
    input.action_mask_le = actions;
    input.age_us_le = 17u;
    input.battery_code = 9u;
    usb_high_rate_submit_input(&input, 0x1001u, 1234u);
}

int main(void)
{
    static const uint8_t golden[] = "123456789";
    static const uint8_t token[16] = {
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u,
        9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u
    };
    hbox_client_control_v1_t request;
    hbox_client_control_v1_t response;
    hbox_client_input_v1_t packet;
    uint16_t length;

    _Static_assert(sizeof(hbox_client_control_v1_t) == 32u,
                   "control ABI changed");
    _Static_assert(sizeof(hbox_client_input_v1_t) == 64u,
                   "input ABI changed");
    _Static_assert(offsetof(hbox_client_input_v1_t, lease_token) == 16u,
                   "token offset changed");
    _Static_assert(offsetof(hbox_client_input_v1_t, crc32_le) == 60u,
                   "CRC offset changed");
    _Static_assert(sizeof(usb_board_input_v1_t) == 10u,
                   "BoardLink input ABI changed");
    assert(hbox_client_crc16_ccitt(golden, 9u) == 0x29B1u);
    assert(hbox_client_crc32(golden, 9u) == 0xCBF43926u);

    assert(usb_board_input_rate_hz(usb_board_input_rate_flags(1000u)) == 1000u);
    assert(usb_board_input_rate_hz(usb_board_input_rate_flags(2000u)) == 2000u);
    assert(usb_board_input_rate_hz(usb_board_input_rate_flags(4000u)) == 4000u);
    assert(usb_board_input_rate_hz(usb_board_input_rate_flags(8000u)) == 8000u);

    assert(usb_high_rate_device_descriptor(&length) != NULL && length == 18u);
    assert(usb_high_rate_configuration_descriptor(&length) != NULL &&
           length == 32u);
    assert(usb_high_rate_bos_descriptor(&length) != NULL && length == 33u);
    assert(usb_high_rate_ms_os_20_descriptor(&length) != NULL &&
           length == 178u);

    usb_high_rate_init();
    submit_rate(8000u, 0u);
    assert(usb_high_rate_effective_rate_hz() == 8000u);

    request = make_control(HBOX_CLIENT_CONTROL_ACQUIRE, 40u, token);
    request.version = 2u;
    request.crc16_le = hbox_client_crc16_ccitt(
        (const uint8_t *)&request,
        offsetof(hbox_client_control_v1_t, crc16_le));
    assert(!usb_high_rate_handle_control(
        &request, &response, 90u, true, false));
    assert(response.status == HBOX_CLIENT_STATUS_BAD_VERSION);

    request = make_control(HBOX_CLIENT_CONTROL_ACQUIRE, 41u, token);
    assert(!usb_high_rate_handle_control(
        &request, &response, 95u, false, false));
    assert(response.status == HBOX_CLIENT_STATUS_BAD_STATE);

    request = make_control(HBOX_CLIENT_CONTROL_ACQUIRE, 42u, token);
    assert(usb_high_rate_handle_control(&request, &response, 100u, true, false));
    assert(response.status == HBOX_CLIENT_STATUS_OK);
    assert((response.flags & HBOX_CLIENT_FLAG_ENABLED) != 0u);
    assert(usb_high_rate_process(100u) == USB_HIGH_RATE_EVENT_SEND_NEUTRAL);
    usb_high_rate_neutral_sent(100u);
    assert(usb_high_rate_process(199u) == USB_HIGH_RATE_EVENT_NONE);
    assert(usb_high_rate_process(200u) ==
           USB_HIGH_RATE_EVENT_DETACH_FOR_TURBO);
    assert(usb_high_rate_process(449u) == USB_HIGH_RATE_EVENT_NONE);
    assert(usb_high_rate_process(450u) == USB_HIGH_RATE_EVENT_ATTACH_TURBO);

    request = make_control(HBOX_CLIENT_CONTROL_HEARTBEAT, 42u, NULL);
    assert(!usb_high_rate_handle_control(&request, &response, 450u, true, true));
    assert(response.status == HBOX_CLIENT_STATUS_BAD_TOKEN);

    request = make_control(HBOX_CLIENT_CONTROL_HEARTBEAT, 43u, token);
    assert(usb_high_rate_handle_control(&request, &response, 451u, true, true));
    assert(usb_high_rate_is_streaming());

    usb_high_rate_note_board_link_fault();
    submit_rate(8000u, 1u);
    submit_rate(8000u, 2u);
    assert(usb_high_rate_peek_input(&packet));
    assert(packet.stream_sequence_le == 1u);
    assert(packet.action_mask_le == 2u);
    assert(packet.device_overwrite_count_le == 1u);
    assert(packet.board_link_fault_count_le == 1u);
    assert(packet.crc32_le == hbox_client_crc32(
        (const uint8_t *)&packet,
        offsetof(hbox_client_input_v1_t, crc32_le)));
    usb_high_rate_commit_input();
    assert(!usb_high_rate_peek_input(&packet));

    assert(usb_high_rate_process(1450u) == USB_HIGH_RATE_EVENT_NONE);
    assert(usb_high_rate_process(1451u) ==
           USB_HIGH_RATE_EVENT_DETACH_FOR_NATIVE);
    assert(usb_high_rate_process(1701u) == USB_HIGH_RATE_EVENT_ATTACH_NATIVE);
    assert(!usb_high_rate_is_turbo_presentation());
    return 0;
}
