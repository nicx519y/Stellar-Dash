#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "usb_net_bridge.h"
#include "webhid_protocol.h"

static uint8_t s_received[WEBHID_REPORT_BYTES];
static uint16_t s_received_length;
static uint8_t s_sink_calls;
static uint8_t s_sink_busy;

static bool sink(usb_board_channel_t channel,
                 const uint8_t *data,
                 uint16_t length)
{
    assert(channel == USB_BOARD_CHANNEL_WEBCONFIG);
    assert(data != NULL);
    assert(length == WEBHID_REPORT_BYTES);
    ++s_sink_calls;
    if(s_sink_busy != 0u)
    {
        return false;
    }
    memcpy(s_received, data, length);
    s_received_length = length;
    return true;
}

static void begin_report(const usb_board_fragment_header_v1_t *header,
                         const uint8_t *data,
                         uint8_t length)
{
    assert(usb_net_bridge_take_credit(USB_BOARD_CHANNEL_WEBCONFIG));
    assert(usb_net_bridge_fragment(header, data, length));
}

static void continue_report(const usb_board_fragment_header_v1_t *header,
                            const uint8_t *data,
                            uint8_t length)
{
    assert(usb_net_bridge_fragment(header, data, length));
}

static void reset_fixture(void)
{
    memset(s_received, 0, sizeof(s_received));
    s_received_length = 0u;
    s_sink_calls = 0u;
    s_sink_busy = 0u;
    usb_net_bridge_init();
    usb_net_bridge_set_sink(sink);
    usb_net_bridge_set_credit(
        USB_BOARD_CHANNEL_WEBCONFIG,
        USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);
}

static void test_opaque_report_round_trip(void)
{
    uint8_t report[WEBHID_REPORT_BYTES];
    usb_board_fragment_header_v1_t header;
    uint8_t index;

    reset_fixture();
    assert(usb_net_bridge_credit(USB_BOARD_CHANNEL_NETWORK) == 0u);
    assert(usb_net_bridge_credit(USB_BOARD_CHANNEL_WEBCONFIG) ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);

    for(index = 0u; index < sizeof(report); ++index)
    {
        report[index] = (uint8_t)(0xD3u ^ index);
    }
    memset(&header, 0, sizeof(header));
    header.channel = USB_BOARD_CHANNEL_WEBCONFIG;
    header.transaction = 7u;
    header.flags = USB_BOARD_FRAGMENT_FLAG_FIRST;
    header.total_length_le = sizeof(report);
    header.message_crc16_le =
        usb_board_crc16_ccitt(report, sizeof(report));
    begin_report(&header, report, USB_BOARD_FRAGMENT_DATA_BYTES);
    assert(usb_net_bridge_credit(USB_BOARD_CHANNEL_WEBCONFIG) ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW - 1u);

    header.fragment_index = 1u;
    header.flags = USB_BOARD_FRAGMENT_FLAG_LAST;
    continue_report(
        &header,
        &report[USB_BOARD_FRAGMENT_DATA_BYTES],
        (uint8_t)(sizeof(report) - USB_BOARD_FRAGMENT_DATA_BYTES));

    assert(s_sink_calls == 1u);
    assert(s_received_length == sizeof(report));
    assert(memcmp(s_received, report, sizeof(report)) == 0);
    assert(usb_net_bridge_credit(USB_BOARD_CHANNEL_WEBCONFIG) ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW - 1u);
    usb_net_bridge_return_credit(USB_BOARD_CHANNEL_WEBCONFIG);
    assert(usb_net_bridge_credit(USB_BOARD_CHANNEL_WEBCONFIG) ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);
}

static void test_reset_discards_partial_report(void)
{
    uint8_t report[WEBHID_REPORT_BYTES] = {0};
    usb_board_fragment_header_v1_t header;

    reset_fixture();
    memset(&header, 0, sizeof(header));
    header.channel = USB_BOARD_CHANNEL_WEBCONFIG;
    header.transaction = 8u;
    header.flags = USB_BOARD_FRAGMENT_FLAG_FIRST;
    header.total_length_le = sizeof(report);
    header.message_crc16_le =
        usb_board_crc16_ccitt(report, sizeof(report));
    begin_report(&header, report, USB_BOARD_FRAGMENT_DATA_BYTES);

    usb_net_bridge_reset_channel(USB_BOARD_CHANNEL_WEBCONFIG);
    assert(usb_net_bridge_credit(USB_BOARD_CHANNEL_WEBCONFIG) ==
           0u);

    header.fragment_index = 1u;
    header.flags = USB_BOARD_FRAGMENT_FLAG_LAST;
    assert(!usb_net_bridge_fragment(
        &header,
        &report[USB_BOARD_FRAGMENT_DATA_BYTES],
        (uint8_t)(sizeof(report) - USB_BOARD_FRAGMENT_DATA_BYTES)));
    assert(s_sink_calls == 0u);
}

static void test_busy_sink_retains_complete_report(void)
{
    uint8_t report[WEBHID_REPORT_BYTES];
    usb_board_fragment_header_v1_t header;
    uint8_t index;

    reset_fixture();
    s_sink_busy = 1u;
    for(index = 0u; index < sizeof(report); ++index)
    {
        report[index] = (uint8_t)(0xA7u + index);
    }
    memset(&header, 0, sizeof(header));
    header.channel = USB_BOARD_CHANNEL_WEBCONFIG;
    header.transaction = 9u;
    header.flags = USB_BOARD_FRAGMENT_FLAG_FIRST;
    header.total_length_le = sizeof(report);
    header.message_crc16_le =
        usb_board_crc16_ccitt(report, sizeof(report));
    begin_report(&header, report, USB_BOARD_FRAGMENT_DATA_BYTES);
    header.fragment_index = 1u;
    header.flags = USB_BOARD_FRAGMENT_FLAG_LAST;
    continue_report(
        &header,
        &report[USB_BOARD_FRAGMENT_DATA_BYTES],
        (uint8_t)(sizeof(report) - USB_BOARD_FRAGMENT_DATA_BYTES));

    assert(s_sink_calls == 1u);
    assert(usb_net_bridge_message_active(
        USB_BOARD_CHANNEL_WEBCONFIG));
    assert(usb_net_bridge_credit(USB_BOARD_CHANNEL_WEBCONFIG) ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW - 1u);

    usb_net_bridge_process();
    assert(s_sink_calls == 2u);
    assert(usb_net_bridge_message_active(
        USB_BOARD_CHANNEL_WEBCONFIG));

    s_sink_busy = 0u;
    usb_net_bridge_process();
    assert(s_sink_calls == 3u);
    assert(!usb_net_bridge_message_active(
        USB_BOARD_CHANNEL_WEBCONFIG));
    assert(s_received_length == sizeof(report));
    assert(memcmp(s_received, report, sizeof(report)) == 0);
    /*
     * Sink acceptance is not final USB consumption; credit remains held.
     */
    assert(usb_net_bridge_credit(USB_BOARD_CHANNEL_WEBCONFIG) ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW - 1u);
}

int main(void)
{
    _Static_assert(USB_BOARD_CHANNEL_WEBCONFIG == 0x06u,
                   "WebConfig BoardLink channel changed");
    _Static_assert(USB_BOARD_CHANNEL_SLOTS == 7u,
                   "BoardLink channel storage was not expanded");
    test_opaque_report_round_trip();
    test_reset_discards_partial_report();
    test_busy_sink_retains_complete_report();
    return 0;
}
