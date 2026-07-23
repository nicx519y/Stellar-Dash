#include "usb_device.h"

#include <string.h>

#include "usb_auth.h"
#include "usb_profiles.h"

static usb_board_profile_t s_profile;
static usb_profile_report_t s_latest_report;
static uint8_t s_latest_telemetry[USB_BOARD_TELEMETRY_FRAME_BYTES];
static uint8_t s_report_pending;
static uint8_t s_telemetry_pending;
static uint8_t s_initialized;
static uint8_t s_last_fault;

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

    memset(&s_latest_report, 0, sizeof(s_latest_report));
    memset(s_latest_telemetry, 0, sizeof(s_latest_telemetry));
    s_report_pending = 0u;
    s_telemetry_pending = 0u;
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
    if(s_initialized != 0u)
    {
        usb_device_hw_shutdown();
    }
    s_initialized = 0u;
    s_report_pending = 0u;
    s_telemetry_pending = 0u;
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

bool usb_device_control(const uint8_t *payload, uint8_t length)
{
    return (s_initialized != 0u) &&
           usb_device_hw_control(payload, length);
}

bool usb_device_send_network_frame(const uint8_t *data, uint16_t length)
{
    return (s_initialized != 0u) &&
           (s_profile == USB_BOARD_PROFILE_WEB_CONFIG) &&
           usb_device_hw_send_network_frame(data, length);
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

__attribute__((weak)) bool usb_device_hw_send_network_frame(
    const uint8_t *data,
    uint16_t length)
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
