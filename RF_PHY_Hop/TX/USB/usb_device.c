#include "usb_device.h"

#include <string.h>

#include "usb_auth.h"
#include "usb_board_link.h"
#include "usb_net_bridge.h"
#include "usb_profiles.h"
#include "webhid_protocol.h"

#define USB_DEVICE_WEBHID_QUEUE_DEPTH 4u

static usb_board_profile_t s_profile;
static usb_profile_report_t s_latest_report;
static uint8_t s_latest_telemetry[USB_BOARD_TELEMETRY_FRAME_BYTES];
static uint8_t s_report_pending;
static uint8_t s_telemetry_pending;
static uint8_t s_webhid_queue[USB_DEVICE_WEBHID_QUEUE_DEPTH]
                             [WEBHID_REPORT_BYTES];
static uint8_t s_webhid_head;
static uint8_t s_webhid_tail;
static uint8_t s_webhid_count;
static uint8_t s_webhid_link_ready;
static uint8_t s_webhid_report_in_flight;
static uint8_t s_initialized;
static uint8_t s_last_fault;

static void clear_webhid_queue(void)
{
    memset(s_webhid_queue, 0, sizeof(s_webhid_queue));
    s_webhid_head = 0u;
    s_webhid_tail = 0u;
    s_webhid_count = 0u;
    s_webhid_report_in_flight = 0u;
}

static uint8_t webhid_available_reports(void)
{
    uint8_t reserved = s_webhid_count;
    if((s_webhid_report_in_flight != 0u) &&
       (reserved < USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW))
    {
        ++reserved;
    }
    if(usb_net_bridge_message_active(USB_BOARD_CHANNEL_WEBCONFIG) &&
       (reserved < USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW))
    {
        ++reserved;
    }
    return (reserved < USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW)
        ? (uint8_t)(USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW - reserved)
        : 0u;
}

void usb_device_transport_reset(void)
{
    memset(&s_latest_report, 0, sizeof(s_latest_report));
    memset(s_latest_telemetry, 0, sizeof(s_latest_telemetry));
    clear_webhid_queue();
    s_report_pending = 0u;
    s_telemetry_pending = 0u;
    s_webhid_link_ready = 0u;
}

static void sync_webhid_link_capacity(void)
{
    const uint8_t mounted =
        usb_device_hw_is_mounted() ? 1u : 0u;
    const uint8_t suspended =
        usb_device_hw_is_suspended() ? 1u : 0u;
    const uint8_t ready =
        (s_initialized != 0u) &&
        (s_profile == USB_BOARD_PROFILE_WEB_CONFIG) &&
        (mounted != 0u) &&
        (suspended == 0u);

    if(ready == s_webhid_link_ready)
    {
        return;
    }
    if(ready == 0u)
    {
        if((s_initialized != 0u) &&
           (s_profile == USB_BOARD_PROFILE_WEB_CONFIG) &&
           (mounted != 0u) && (suspended != 0u))
        {
            /* Preserve queued and partially reassembled reports on suspend. */
            usb_board_link_webconfig_pause();
        }
        else
        {
            clear_webhid_queue();
            usb_board_link_webconfig_set_ready(false, 0u);
        }
    }
    else
    {
        usb_board_link_webconfig_set_ready(
            true,
            webhid_available_reports());
    }
    s_webhid_link_ready = ready;
}

static usb_auth_scheme_t auth_scheme_for_profile(usb_board_profile_t profile)
{
    switch(profile)
    {
    case USB_BOARD_PROFILE_XINPUT:
        return USB_AUTH_SCHEME_XINPUT;
    case USB_BOARD_PROFILE_PS4:
    case USB_BOARD_PROFILE_PS5_COMPAT:
        return USB_AUTH_SCHEME_PS4;
    case USB_BOARD_PROFILE_XBOX_ONE:
        return USB_AUTH_SCHEME_XBOX_GIP;
    default:
        return USB_AUTH_SCHEME_NONE;
    }
}

bool usb_device_init(usb_board_profile_t profile)
{
    usb_auth_scheme_t auth_scheme;

    usb_device_transport_reset();
    s_initialized = 0u;
    s_last_fault = 0u;

    if(!usb_profiles_is_supported(profile) || !usb_device_hw_init(profile))
    {
        s_last_fault = USB_BOARD_STATUS_NOT_READY;
        return false;
    }
    s_profile = profile;
    auth_scheme = auth_scheme_for_profile(profile);
    usb_auth_clear();
    if((auth_scheme != USB_AUTH_SCHEME_NONE) &&
       !usb_auth_begin(auth_scheme, (uint8_t)profile))
    {
        usb_device_hw_shutdown();
        s_profile = USB_BOARD_PROFILE_NONE;
        s_last_fault = USB_BOARD_STATUS_NOT_READY;
        return false;
    }
    s_initialized = 1u;
    return true;
}

void usb_device_shutdown(void)
{
    if(s_webhid_link_ready != 0u)
    {
        usb_board_link_webconfig_set_ready(false, 0u);
        s_webhid_link_ready = 0u;
    }
    if(s_initialized != 0u)
    {
        usb_device_hw_shutdown();
    }
    s_initialized = 0u;
    usb_device_transport_reset();
    s_profile = USB_BOARD_PROFILE_NONE;
    usb_auth_clear();
}

void usb_device_process(void)
{
    if(s_initialized == 0u)
    {
        return;
    }
    usb_auth_process();
    usb_device_hw_process();
    sync_webhid_link_capacity();
    if((s_report_pending != 0u) && usb_device_hw_is_mounted() &&
       usb_device_hw_send_report(s_latest_report.bytes,
                                 s_latest_report.length))
    {
        s_report_pending = 0u;
    }
    /*
     * Telemetry uses an independent endpoint and a single latest-frame slot.
     * It never delays or consumes the XInput report endpoint.
     */
    if((s_telemetry_pending != 0u) && usb_device_hw_is_mounted() &&
       usb_device_hw_send_telemetry(s_latest_telemetry,
                                    sizeof(s_latest_telemetry)))
    {
        s_telemetry_pending = 0u;
    }
    if((s_webhid_count != 0u) &&
       (s_webhid_report_in_flight == 0u) &&
       usb_device_hw_is_mounted())
    {
        /*
         * Reserve generic ownership before arming EP1. The USB interrupt can
         * complete immediately after the hardware ACK bit is set, so marking
         * this after usb_device_hw_send_webhid_report() would race that ISR.
         */
        s_webhid_report_in_flight = 1u;
        if(usb_device_hw_send_webhid_report(
               s_webhid_queue[s_webhid_head], WEBHID_REPORT_BYTES))
        {
            memset(s_webhid_queue[s_webhid_head],
                   0,
                   WEBHID_REPORT_BYTES);
            s_webhid_head =
                (uint8_t)((s_webhid_head + 1u) %
                          USB_DEVICE_WEBHID_QUEUE_DEPTH);
            --s_webhid_count;
        }
        else
        {
            s_webhid_report_in_flight = 0u;
        }
    }
}

void usb_device_webhid_report_complete(void)
{
    /*
     * The CH585 backend calls this from usb_device_hw_process(), not the USB
     * ISR, after a real EP1 IN completion. This keeps board-link credit/dirty
     * bookkeeping in process context and makes duplicate DONE notifications
     * harmless.
     */
    if((s_initialized == 0u) ||
       (s_profile != USB_BOARD_PROFILE_WEB_CONFIG) ||
       (s_webhid_report_in_flight == 0u))
    {
        return;
    }
    s_webhid_report_in_flight = 0u;
    usb_board_link_webconfig_report_consumed();
}

bool usb_device_webhid_credit_ready(void)
{
    /*
     * Check the hardware state as well as the process-context generation bit.
     * BUS_RST and suspend update the hardware flags in the ISR before
     * usb_device_process() can retire the old BoardLink capacity.
     */
    return (s_initialized != 0u) &&
           (s_profile == USB_BOARD_PROFILE_WEB_CONFIG) &&
           (s_webhid_link_ready != 0u) &&
           usb_device_hw_is_mounted() &&
           !usb_device_hw_is_suspended();
}

bool usb_device_set_profile(usb_board_profile_t profile)
{
    if(!usb_profiles_is_supported(profile))
    {
        return false;
    }
    if((s_initialized != 0u) && (s_profile == profile))
    {
        return true;
    }
    usb_device_shutdown();
    return usb_device_init(profile);
}

bool usb_device_submit_input(const usb_board_input_v1_t *input)
{
    if((s_initialized == 0u) || (input == 0) ||
       (usb_board_input_crc8((const uint8_t *)input,
                             (uint8_t)(sizeof(*input) - 1u)) != input->crc8) ||
       !usb_profiles_build_report(s_profile, input, &s_latest_report))
    {
        s_last_fault = USB_BOARD_STATUS_CRC_ERROR;
        return false;
    }
    usb_device_hw_set_actions(input->action_mask_le);
    s_report_pending = (s_latest_report.length != 0u) ? 1u : 0u;
    return true;
}

bool usb_device_submit_telemetry(const uint8_t *data, uint16_t length)
{
    if((data == 0) || (length != USB_BOARD_TELEMETRY_FRAME_BYTES))
    {
        return false;
    }
    if((s_initialized == 0u) ||
       (s_profile != USB_BOARD_PROFILE_XINPUT))
    {
        /* No telemetry interface exists in other profiles: consume/drop. */
        return true;
    }

    memcpy(s_latest_telemetry, data, sizeof(s_latest_telemetry));
    s_telemetry_pending = 1u;
    return true;
}

bool usb_device_submit_webhid_report(const uint8_t *data, uint16_t length)
{
    /*
     * Reports covered by credit already advertised before suspend remain
     * valid. Queue them in RAM while EP1 is paused so no in-flight SPI grant
     * is revoked; usb_device_hw_send_webhid_report() gates actual USB IN.
     */
    if((s_initialized == 0u) ||
       (s_profile != USB_BOARD_PROFILE_WEB_CONFIG) ||
       !usb_device_hw_is_mounted() ||
       (data == 0) || (length != WEBHID_REPORT_BYTES) ||
       (s_webhid_count >= USB_DEVICE_WEBHID_QUEUE_DEPTH))
    {
        return false;
    }

    memcpy(s_webhid_queue[s_webhid_tail], data, WEBHID_REPORT_BYTES);
    s_webhid_tail =
        (uint8_t)((s_webhid_tail + 1u) %
                  USB_DEVICE_WEBHID_QUEUE_DEPTH);
    ++s_webhid_count;
    return true;
}

bool usb_device_control(const uint8_t *payload, uint8_t length)
{
    return (s_initialized != 0u) &&
           usb_device_hw_control(payload, length);
}

bool usb_device_is_mounted(void)
{
    return (s_initialized != 0u) && usb_device_hw_is_mounted();
}

bool usb_device_is_suspended(void)
{
    return (s_initialized != 0u) && usb_device_hw_is_suspended();
}

usb_board_profile_t usb_device_profile(void)
{
    return s_profile;
}

uint8_t usb_device_last_fault(void)
{
    return s_last_fault;
}

__attribute__((weak)) bool usb_device_hw_init(usb_board_profile_t profile)
{
    (void)profile;
    return false;
}

__attribute__((weak)) void usb_device_hw_shutdown(void)
{
}

__attribute__((weak)) bool usb_device_hw_send_report(const uint8_t *report,
                                                     uint8_t length)
{
    (void)report;
    (void)length;
    return false;
}

__attribute__((weak)) void usb_device_hw_set_actions(uint32_t action_mask)
{
    (void)action_mask;
}

__attribute__((weak)) bool usb_device_hw_send_telemetry(
    const uint8_t *data,
    uint8_t length)
{
    (void)data;
    (void)length;
    return false;
}

__attribute__((weak)) bool usb_device_hw_send_webhid_report(
    const uint8_t *data,
    uint8_t length)
{
    (void)data;
    (void)length;
    return false;
}

__attribute__((weak)) void usb_device_hw_process(void)
{
}

__attribute__((weak)) bool usb_device_hw_is_mounted(void)
{
    return false;
}

__attribute__((weak)) bool usb_device_hw_is_suspended(void)
{
    return false;
}

__attribute__((weak)) bool usb_device_hw_control(const uint8_t *payload,
                                                uint8_t length)
{
    (void)payload;
    (void)length;
    return false;
}
