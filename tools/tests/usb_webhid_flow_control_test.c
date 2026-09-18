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
static uint8_t s_published_credit;
static uint8_t s_reset_before_ep1_arm;

static void observe_credit(void)
{
    const uint8_t credit =
        usb_net_bridge_credit(USB_BOARD_CHANNEL_WEBCONFIG);
    assert(credit <= USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);
    if(credit > s_credit_peak)
    {
        s_credit_peak = credit;
    }
    s_published_credit = credit;
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

void usb_board_link_webconfig_pause(void)
{
    /* Withhold publication without revoking grants already seen by STM32. */
    s_published_credit = 0u;
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
    if(s_reset_before_ep1_arm != 0u)
    {
        /* Inject BUS_RST winning the old check-to-arm race. */
        s_reset_before_ep1_arm = 0u;
        s_mounted = 0u;
        s_ep1_busy = 0u;
        usb_device_transport_reset();
        return false;
    }
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

static void complete_ep1(void)
{
    assert(s_ep1_busy != 0u);
    s_ep1_busy = 0u;
    usb_device_webhid_report_complete();
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
    s_published_credit = 0u;
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
        assert(usb_net_bridge_credit(
                   USB_BOARD_CHANNEL_WEBCONFIG) == 0u);

        /*
         * Merely arming EP1 must neither pop another queue entry nor return a
         * credit. Only the successful IN-complete event releases it.
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

        complete_ep1();
        assert(usb_net_bridge_credit(
                   USB_BOARD_CHANNEL_WEBCONFIG) ==
               USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);

        /* A duplicate completion and a completion with no in-flight report
         * must not manufacture a second credit. */
        usb_device_webhid_report_complete();
        assert(usb_net_bridge_credit(
                   USB_BOARD_CHANNEL_WEBCONFIG) ==
               USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);

        if(submitted < TEST_REPORT_COUNT)
        {
            assert(submit_complete_report(submitted));
            ++submitted;
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

static void test_suspend_preserves_old_transport_generation(void)
{
    uint8_t submitted = 0u;
    uint8_t sequence;

    memset(s_sent, 0, sizeof(s_sent));
    s_sent_count = 0u;
    s_credit_peak = 0u;
    s_published_credit = 0u;
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
    assert(s_published_credit == 0u);

    /* Suspend advertises zero credit but retains every queued report. */
    s_suspended = 1u;
    usb_device_process();
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) == 0u);
    assert(s_sent_count == 0u);
    assert(!submit_complete_report(100u));

    s_suspended = 0u;
    usb_device_process();
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) == 0u);
    assert(s_sent_count == 0u);

    for(sequence = 0u;
        sequence < USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW;
        ++sequence)
    {
        uint8_t expected[WEBHID_REPORT_BYTES];
        s_ep1_busy = 0u;
        usb_device_process();
        assert(s_sent_count == (uint8_t)(sequence + 1u));
        assert(usb_net_bridge_credit(
                   USB_BOARD_CHANNEL_WEBCONFIG) == 0u);
        build_report(sequence, expected);
        assert(memcmp(s_sent[sequence],
                      expected,
                      sizeof(expected)) == 0);
        complete_ep1();
    }
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);
    usb_device_shutdown();
}

static void test_resume_capacity_reserves_partial_reassembly(void)
{
    uint8_t report[WEBHID_REPORT_BYTES];
    usb_board_fragment_header_v1_t header;

    memset(s_sent, 0, sizeof(s_sent));
    s_sent_count = 0u;
    s_credit_peak = 0u;
    s_published_credit = 0u;
    s_mounted = 1u;
    s_suspended = 0u;
    s_ep1_busy = 1u;

    usb_net_bridge_init();
    usb_net_bridge_set_sink(sink);
    assert(usb_device_init(USB_BOARD_PROFILE_WEB_CONFIG));
    usb_device_process();
    assert(usb_net_bridge_take_credit(
        USB_BOARD_CHANNEL_WEBCONFIG));

    build_report(55u, report);
    memset(&header, 0, sizeof(header));
    header.channel = USB_BOARD_CHANNEL_WEBCONFIG;
    header.transaction = 55u;
    header.flags = USB_BOARD_FRAGMENT_FLAG_FIRST;
    header.total_length_le = WEBHID_REPORT_BYTES;
    header.message_crc16_le =
        usb_board_crc16_ccitt(report, sizeof(report));
    assert(usb_net_bridge_fragment(
        &header, report, USB_BOARD_FRAGMENT_DATA_BYTES));
    assert(usb_net_bridge_message_active(
        USB_BOARD_CHANNEL_WEBCONFIG));

    s_suspended = 1u;
    usb_device_process();
    assert(s_published_credit == 0u);
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) ==
           (uint8_t)(USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW - 1u));
    s_suspended = 0u;
    usb_device_process();
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) ==
           (uint8_t)(USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW - 1u));

    header.fragment_index = 1u;
    header.flags = USB_BOARD_FRAGMENT_FLAG_LAST;
    assert(usb_net_bridge_fragment(
        &header,
        &report[USB_BOARD_FRAGMENT_DATA_BYTES],
        (uint8_t)(sizeof(report) - USB_BOARD_FRAGMENT_DATA_BYTES)));
    assert(!usb_net_bridge_message_active(
        USB_BOARD_CHANNEL_WEBCONFIG));

    s_ep1_busy = 0u;
    usb_device_process();
    assert(s_sent_count == 1u);
    assert(memcmp(s_sent[0], report, sizeof(report)) == 0);
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) == 0u);
    complete_ep1();
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);
    usb_device_shutdown();
}

static void test_suspend_accepts_previously_published_credits(void)
{
    uint8_t sequence;

    memset(s_sent, 0, sizeof(s_sent));
    s_sent_count = 0u;
    s_credit_peak = 0u;
    s_published_credit = 0u;
    s_mounted = 1u;
    s_suspended = 0u;
    s_ep1_busy = 1u;

    usb_net_bridge_init();
    usb_net_bridge_set_sink(sink);
    assert(usb_device_init(USB_BOARD_PROFILE_WEB_CONFIG));
    usb_device_process();
    assert(s_published_credit ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);

    s_suspended = 1u;
    usb_device_process();
    assert(s_published_credit == 0u);
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);

    /* These reports were covered by the last credit already seen by STM32. */
    for(sequence = 0u;
        sequence < USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW;
        ++sequence)
    {
        assert(submit_complete_report(sequence));
    }
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) == 0u);
    assert(s_sent_count == 0u);

    s_suspended = 0u;
    usb_device_process();
    assert(s_published_credit == 0u);
    for(sequence = 0u;
        sequence < USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW;
        ++sequence)
    {
        uint8_t expected[WEBHID_REPORT_BYTES];
        s_ep1_busy = 0u;
        usb_device_process();
        build_report(sequence, expected);
        assert(memcmp(s_sent[sequence],
                      expected,
                      sizeof(expected)) == 0);
        assert(usb_net_bridge_credit(
                   USB_BOARD_CHANNEL_WEBCONFIG) == 0u);
        complete_ep1();
    }
    assert(s_published_credit ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);
    usb_device_shutdown();
}

static void test_in_flight_survives_suspend_until_completion(void)
{
    memset(s_sent, 0, sizeof(s_sent));
    s_sent_count = 0u;
    s_credit_peak = 0u;
    s_published_credit = 0u;
    s_mounted = 1u;
    s_suspended = 0u;
    s_ep1_busy = 0u;

    usb_net_bridge_init();
    usb_net_bridge_set_sink(sink);
    assert(usb_device_init(USB_BOARD_PROFILE_WEB_CONFIG));
    usb_device_process();
    assert(submit_complete_report(77u));
    usb_device_process();
    assert(s_sent_count == 1u);
    assert(s_ep1_busy != 0u);
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) == 0u);

    s_suspended = 1u;
    usb_device_process();
    assert(s_published_credit == 0u);
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) == 0u);

    s_suspended = 0u;
    usb_device_process();
    /* Resume must still reserve the report owned by EP1. */
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) == 0u);
    complete_ep1();
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);
    usb_device_shutdown();
}

static void test_transport_reset_drops_in_flight_completion(void)
{
    memset(s_sent, 0, sizeof(s_sent));
    s_sent_count = 0u;
    s_credit_peak = 0u;
    s_published_credit = 0u;
    s_mounted = 1u;
    s_suspended = 0u;
    s_ep1_busy = 0u;

    usb_net_bridge_init();
    usb_net_bridge_set_sink(sink);
    assert(usb_device_init(USB_BOARD_PROFILE_WEB_CONFIG));
    usb_device_process();
    assert(submit_complete_report(88u));
    usb_device_process();
    assert(s_sent_count == 1u);
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) == 0u);

    /* A bus/profile reset cancels ownership; a late DONE is stale. */
    s_mounted = 0u;
    s_ep1_busy = 0u;
    usb_device_transport_reset();
    usb_device_webhid_report_complete();
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) == 0u);

    /* A fresh mount advertises exactly one new-generation report. */
    s_mounted = 1u;
    usb_device_process();
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);
    usb_device_shutdown();
}

static void test_reset_in_pre_arm_window_discards_old_report(void)
{
    memset(s_sent, 0, sizeof(s_sent));
    s_sent_count = 0u;
    s_credit_peak = 0u;
    s_published_credit = 0u;
    s_reset_before_ep1_arm = 0u;
    s_mounted = 1u;
    s_suspended = 0u;
    s_ep1_busy = 0u;

    usb_net_bridge_init();
    usb_net_bridge_set_sink(sink);
    assert(usb_device_init(USB_BOARD_PROFILE_WEB_CONFIG));
    usb_device_process();
    assert(submit_complete_report(99u));
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) == 0u);

    s_reset_before_ep1_arm = 1u;
    usb_device_process();
    assert(s_sent_count == 0u);
    assert(s_ep1_busy == 0u);
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) == 0u);
    usb_device_webhid_report_complete();
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) == 0u);

    /* Only a fresh post-reset mount may advertise the next report. */
    s_mounted = 1u;
    usb_device_process();
    assert(usb_net_bridge_credit(
               USB_BOARD_CHANNEL_WEBCONFIG) ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);
    usb_device_shutdown();
}

static void test_channel_specific_credit_caps(void)
{
    uint8_t count;

    usb_net_bridge_init();
    assert(usb_net_bridge_credit(USB_BOARD_CHANNEL_USB_DEVICE) ==
           USB_BOARD_BULK_CREDIT_WINDOW);
    for(count = 0u; count < USB_BOARD_BULK_CREDIT_WINDOW; ++count)
    {
        assert(usb_net_bridge_take_credit(
            USB_BOARD_CHANNEL_USB_DEVICE));
    }
    assert(!usb_net_bridge_take_credit(
        USB_BOARD_CHANNEL_USB_DEVICE));
    for(count = 0u; count < (USB_BOARD_BULK_CREDIT_WINDOW + 2u); ++count)
    {
        usb_net_bridge_return_credit(USB_BOARD_CHANNEL_USB_DEVICE);
    }
    assert(usb_net_bridge_credit(USB_BOARD_CHANNEL_USB_DEVICE) ==
           USB_BOARD_BULK_CREDIT_WINDOW);

    usb_net_bridge_set_credit(
        USB_BOARD_CHANNEL_WEBCONFIG, USB_BOARD_BULK_CREDIT_WINDOW);
    assert(usb_net_bridge_credit(USB_BOARD_CHANNEL_WEBCONFIG) ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);
    assert(usb_net_bridge_take_credit(
        USB_BOARD_CHANNEL_WEBCONFIG));
    assert(!usb_net_bridge_take_credit(
        USB_BOARD_CHANNEL_WEBCONFIG));
    usb_net_bridge_return_credit(USB_BOARD_CHANNEL_WEBCONFIG);
    usb_net_bridge_return_credit(USB_BOARD_CHANNEL_WEBCONFIG);
    assert(usb_net_bridge_credit(USB_BOARD_CHANNEL_WEBCONFIG) ==
           USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW);
}

static void test_credit_readiness_tracks_live_usb_generation(void)
{
    s_mounted = 1u;
    s_suspended = 0u;
    s_ep1_busy = 0u;

    usb_net_bridge_init();
    usb_net_bridge_set_sink(sink);
    assert(usb_device_init(USB_BOARD_PROFILE_WEB_CONFIG));
    usb_device_process();
    assert(usb_device_webhid_credit_ready());

    /* BUS_RST updates the hardware mount flag in the ISR before process
     * context retires the old BoardLink generation. */
    s_mounted = 0u;
    assert(!usb_device_webhid_credit_ready());
    usb_device_process();
    assert(!usb_device_webhid_credit_ready());

    s_mounted = 1u;
    usb_device_process();
    assert(usb_device_webhid_credit_ready());

    s_suspended = 1u;
    assert(!usb_device_webhid_credit_ready());
    usb_device_process();
    assert(!usb_device_webhid_credit_ready());
    s_suspended = 0u;
    /* Resume cannot publish old capacity before the generic generation sync. */
    assert(!usb_device_webhid_credit_ready());
    usb_device_process();
    assert(usb_device_webhid_credit_ready());
    usb_device_shutdown();
}

int main(void)
{
    _Static_assert(
        USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW == 1u,
        "WebConfig complete-report credit window changed");
    _Static_assert(
        USB_BOARD_BULK_CREDIT_WINDOW == 4u,
        "Other BoardLink bulk channel credit window changed");
    test_ep1_hold_and_ten_report_fifo();
    test_suspend_preserves_old_transport_generation();
    test_resume_capacity_reserves_partial_reassembly();
    test_suspend_accepts_previously_published_credits();
    test_in_flight_survives_suspend_until_completion();
    test_transport_reset_drops_in_flight_completion();
    test_reset_in_pre_arm_window_discards_old_report();
    test_channel_specific_credit_caps();
    test_credit_readiness_tracks_live_usb_generation();
    return 0;
}
