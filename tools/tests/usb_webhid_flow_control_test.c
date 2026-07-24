#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "usb_auth.h"
#include "usb_board_link.h"
#include "usb_device.h"
#include "usb_net_bridge.h"
#include "usb_profiles.h"
#include "webhid_protocol.h"

#define TEST_REPORT_COUNT 10u

static uint8_t s_mounted;
static uint8_t s_suspended;
static uint8_t s_ep1_busy;
static uint8_t s_sent[TEST_REPORT_COUNT][WEBHID_REPORT_BYTES];
static uint8_t s_sent_count;
static uint8_t s_credit_peak;

static void observe_credit(void)
{
    const uint8_t credit =
        usb_net_bridge_credit(USB_BOARD_CHANNEL_WEBCONFIG);
    assert(credit <= USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);
    if(credit > s_credit_peak)
    {
        s_credit_peak = credit;
    }
}

void usb_board_link_webconfig_set_ready(bool ready,
                                        uint8_t available_reports)
{
    if(!ready)
    {
        usb_net_bridge_reset_channel(
            USB_BOARD_CHANNEL_WEBCONFIG);
    }
    else
    {
        usb_net_bridge_set_credit(
            USB_BOARD_CHANNEL_WEBCONFIG, available_reports);
    }
    observe_credit();
}

void usb_board_link_webconfig_report_consumed(void)
{
    usb_net_bridge_return_credit(USB_BOARD_CHANNEL_WEBCONFIG);
    observe_credit();
}

bool usb_profiles_is_supported(usb_board_profile_t profile)
{
    return profile == USB_BOARD_PROFILE_WEB_CONFIG;
}

bool usb_profiles_build_report(usb_board_profile_t profile,
                               const usb_board_input_v1_t *input,
                               usb_profile_report_t *report)
{
    (void)profile;
    (void)input;
    (void)report;
    return false;
}

void usb_auth_clear(void)
{
}

bool usb_auth_begin(usb_auth_scheme_t scheme, uint8_t transaction)
{
    (void)scheme;
    (void)transaction;
    return true;
}

void usb_auth_process(void)
{
}

bool usb_device_hw_init(usb_board_profile_t profile)
{
    return profile == USB_BOARD_PROFILE_WEB_CONFIG;
}

void usb_device_hw_shutdown(void)
{
}

bool usb_device_hw_send_report(const uint8_t *report, uint8_t length)
{
    (void)report;
    (void)length;
    return false;
}

void usb_device_hw_set_actions(uint32_t action_mask)
{
    (void)action_mask;
}

bool usb_device_hw_send_telemetry(const uint8_t *data, uint8_t length)
{
    (void)data;
    (void)length;
    return false;
}

bool usb_device_hw_send_webhid_report(const uint8_t *data,
                                      uint8_t length)
{
    if((data == NULL) || (length != WEBHID_REPORT_BYTES) ||
       (s_mounted == 0u) || (s_suspended != 0u) ||
       (s_ep1_busy != 0u) ||
       (s_sent_count >= TEST_REPORT_COUNT))
    {
        return false;
    }
    memcpy(s_sent[s_sent_count], data, WEBHID_REPORT_BYTES);
    ++s_sent_count;
    s_ep1_busy = 1u;
    return true;
}

void usb_device_hw_process(void)
{
}

bool usb_device_hw_is_mounted(void)
{
    return s_mounted != 0u;
}

bool usb_device_hw_is_suspended(void)
{
    return s_suspended != 0u;
}

bool usb_device_hw_control(const uint8_t *payload, uint8_t length)
{
    (void)payload;
    (void)length;
    return false;
}

static bool sink(usb_board_channel_t channel,
                 const uint8_t *data,
                 uint16_t length)
{
    assert(channel == USB_BOARD_CHANNEL_WEBCONFIG);
    return usb_device_submit_webhid_report(data, length);
}

static void build_report(uint8_t sequence,
                         uint8_t report[WEBHID_REPORT_BYTES])
{
    uint8_t index;
    memset(report, 0, WEBHID_REPORT_BYTES);
    report[0] = sequence;
    /*
     * Alternate protected control responses and reliable PERF_EDGE frames.
     * Both must retain FIFO order while EP1 is unavailable.
     */
    report[1] = (sequence & 1u) == 0u
        ? WEBHID_REPORT_SECURE_RESPONSE
        : WEBHID_REPORT_PERF_EDGE;
    for(index = 2u; index < WEBHID_REPORT_BYTES; ++index)
    {
        report[index] = (uint8_t)(sequence ^ index);
    }
}

static bool submit_complete_report(uint8_t sequence)
{
    uint8_t report[WEBHID_REPORT_BYTES];
    usb_board_fragment_header_v1_t header;

    if(!usb_net_bridge_take_credit(
           USB_BOARD_CHANNEL_WEBCONFIG))
    {
        return false;
    }
    build_report(sequence, report);
    memset(&header, 0, sizeof(header));
    header.channel = USB_BOARD_CHANNEL_WEBCONFIG;
    header.transaction = sequence;
    header.flags = USB_BOARD_FRAGMENT_FLAG_FIRST;
    header.total_length_le = WEBHID_REPORT_BYTES;
    header.message_crc16_le =
        usb_board_crc16_ccitt(report, sizeof(report));
    assert(usb_net_bridge_fragment(
        &header, report, USB_BOARD_FRAGMENT_DATA_BYTES));
    header.fragment_index = 1u;
    header.flags = USB_BOARD_FRAGMENT_FLAG_LAST;
    assert(usb_net_bridge_fragment(
        &header,
        &report[USB_BOARD_FRAGMENT_DATA_BYTES],
        (uint8_t)(sizeof(report) -
                  USB_BOARD_FRAGMENT_DATA_BYTES)));
    observe_credit();
    return true;
}

static void test_ep1_hold_and_ten_report_fifo(void)
{
    uint8_t submitted = 0u;
    uint8_t expected;

    memset(s_sent, 0, sizeof(s_sent));
    s_sent_count = 0u;
    s_credit_peak = 0u;
    s_mounted = 1u;
    s_suspended = 0u;
    s_ep1_busy = 1u;

    usb_net_bridge_init();
    usb_net_bridge_set_sink(sink);
    assert(usb_device_init(USB_BOARD_PROFILE_WEB_CONFIG));
    usb_device_process();
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);

    while(submitted < USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW)
    {
        assert(submit_complete_report(submitted));
        ++submitted;
    }
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) == 0u);
    assert(!submit_complete_report(submitted));

    while(s_sent_count < TEST_REPORT_COUNT)
    {
        const uint8_t sent_before = s_sent_count;
        s_ep1_busy = 0u;
        usb_device_process();
        assert(s_sent_count == (uint8_t)(sent_before + 1u));

        if(submitted < TEST_REPORT_COUNT)
        {
            assert(submit_complete_report(submitted));
            ++submitted;
        }

        /*
         * Holding EP1 busy must neither pop another queue entry nor return a
         * second credit.
         */
        {
            const uint8_t credit_before =
                usb_net_bridge_credit(
                    USB_BOARD_CHANNEL_WEBCONFIG);
            usb_device_process();
            assert(s_sent_count == (uint8_t)(sent_before + 1u));
            assert(usb_net_bridge_credit(
                       USB_BOARD_CHANNEL_WEBCONFIG) ==
                   credit_before);
        }
    }

    assert(submitted == TEST_REPORT_COUNT);
    assert(s_credit_peak ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);
    for(expected = 0u; expected < TEST_REPORT_COUNT; ++expected)
    {
        uint8_t original[WEBHID_REPORT_BYTES];
        build_report(expected, original);
        assert(memcmp(s_sent[expected],
                      original,
                      WEBHID_REPORT_BYTES) == 0);
    }
    usb_device_shutdown();
}

static void test_suspend_discards_old_transport_generation(void)
{
    uint8_t submitted = 0u;
    uint8_t expected[WEBHID_REPORT_BYTES];

    memset(s_sent, 0, sizeof(s_sent));
    s_sent_count = 0u;
    s_credit_peak = 0u;
    s_mounted = 1u;
    s_suspended = 0u;
    s_ep1_busy = 1u;

    usb_net_bridge_init();
    usb_net_bridge_set_sink(sink);
    assert(usb_device_init(USB_BOARD_PROFILE_WEB_CONFIG));
    usb_device_process();

    while(submitted < USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW)
    {
        assert(submit_complete_report(submitted));
        ++submitted;
    }
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) == 0u);

    /*
     * STM32 destroys the cryptographic session on suspend.  CH585 must clear
     * all reports from that generation instead of replaying them on resume.
     */
    s_suspended = 1u;
    usb_device_process();
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) == 0u);
    assert(s_sent_count == 0u);

    s_suspended = 0u;
    usb_device_process();
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);
    assert(s_sent_count == 0u);

    assert(submit_complete_report(100u));
    s_ep1_busy = 0u;
    usb_device_process();
    assert(s_sent_count == 1u);
    build_report(100u, expected);
    assert(memcmp(s_sent[0], expected, sizeof(expected)) == 0);
    usb_device_shutdown();
}

int main(void)
{
    _Static_assert(
        USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW == 4u,
        "WebConfig complete-report credit window changed");
    test_ep1_hold_and_ten_report_fifo();
    test_suspend_discards_old_transport_generation();
    return 0;
}
