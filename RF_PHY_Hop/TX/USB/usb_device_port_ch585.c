#include "usb_device.h"

#include <string.h>

#include "CH58x_common.h"
#include "usb_auth.h"
#include "usb_board_link.h"
#include "usb_endpoint_reset_control.h"
#include "usb_legacy_descriptors.h"
#include "usb_management_control.h"
#include "usb_ps4_features.h"
#include "usb_webhid.h"
#include "usb_xbox_device.h"
#include "webhid_protocol.h"

/*
 * The XInput descriptors are the authoritative bytes used by the current
 * STM32 TinyUSB implementation.  The header is C-compatible and including it
 * here prevents the CH585 copy from silently drifting.
 */
#include "../../../application/Cpp_Core/Inc/drivers/xinput/XInputDescriptors.hpp"

#define USBDEV_EP0_BYTES                  64u
#define USBDEV_ENDPOINT_BYTES            512u
#define USBDEV_INTERRUPT_BYTES            64u
#define USBDEV_CONTROL_BUFFER_BYTES       256u
#define USBDEV_XINPUT_HID_INTERFACE         4u
#define USBDEV_XINPUT_INPUT_ENDPOINT        1u
#define USBDEV_XINPUT_OUTPUT_ENDPOINT       2u
#define USBDEV_XINPUT_TELEMETRY_ENDPOINT    7u
#define USBDEV_PS4_OUTPUT_ENDPOINT          3u
#define USBDEV_OTHER_SPEED_BYTES           256u
#define USBDEV_WEBHID_OUT_QUEUE_DEPTH        4u
#define USBDEV_HID_REPORT_INPUT              1u
#define USBDEV_HID_REPORT_OUTPUT             2u
#define USBDEV_HID_REPORT_FEATURE            3u
#define USBDEV_XBOX_OS_VENDOR_CODE         0x20u
#define USBDEV_XBOX_COMPAT_ID_INDEX      0x0004u
#define USBDEV_SIE_QUIESCE_TIMEOUT_MS         1u

typedef enum
{
    USBDEV_EP0_IDLE = 0,
    USBDEV_EP0_IN_DATA,
    USBDEV_EP0_WAIT_OUT_STATUS,
    USBDEV_EP0_OUT_DATA,
    USBDEV_EP0_IN_STATUS
} usbdev_ep0_flow_t;

typedef enum
{
    USBDEV_CONTROL_OUT_NONE = 0,
    USBDEV_CONTROL_OUT_HID_REPORT,
    USBDEV_CONTROL_OUT_XINPUT_VENDOR
} usbdev_control_out_t;

__attribute__((aligned(4))) static uint8_t s_ep0[USBDEV_EP0_BYTES];
__attribute__((aligned(4))) static uint8_t s_ep1_tx[USBDEV_ENDPOINT_BYTES];
__attribute__((aligned(4))) static uint8_t s_ep2_rx[USBDEV_ENDPOINT_BYTES];
__attribute__((aligned(4))) static uint8_t s_ep3_rx[USBDEV_INTERRUPT_BYTES];
__attribute__((aligned(4))) static uint8_t s_ep4_rx[USBDEV_INTERRUPT_BYTES];
__attribute__((aligned(4))) static uint8_t s_ep6_rx[USBDEV_INTERRUPT_BYTES];
__attribute__((aligned(4))) static uint8_t s_ep7_tx[USBDEV_INTERRUPT_BYTES];
__attribute__((aligned(4))) static uint8_t s_unused_tx[USBDEV_INTERRUPT_BYTES];

static uint8_t s_control_response[USBDEV_CONTROL_BUFFER_BYTES];
static uint8_t s_control_out[USBDEV_CONTROL_BUFFER_BYTES];
static uint8_t s_other_speed[USBDEV_OTHER_SPEED_BYTES];
static uint8_t s_xinput_string[256];
static uint8_t s_last_report[USBDEV_INTERRUPT_BYTES];
static uint8_t s_last_telemetry[USB_BOARD_TELEMETRY_FRAME_BYTES];
static uint8_t s_xbox_out[USB_XBOX_DEVICE_PACKET_BYTES];
static uint8_t s_webhid_out[USBDEV_WEBHID_OUT_QUEUE_DEPTH]
                           [WEBHID_REPORT_BYTES];

static volatile uint8_t s_mounted;
static volatile uint8_t s_suspended;
static volatile uint8_t s_connected;
static volatile uint8_t s_initialized;
static volatile uint8_t s_ep1_busy;
static volatile uint8_t s_ep7_busy;
static volatile uint8_t s_xbox_out_ready;
static volatile uint8_t s_xbox_out_length;
static volatile uint8_t s_address;
static volatile uint8_t s_configuration;
static volatile uint8_t s_webhid_out_head;
static volatile uint8_t s_webhid_out_tail;
static volatile uint8_t s_webhid_out_count;
static volatile uint8_t s_transport_reset_pending;
static volatile uint8_t s_webhid_ep1_complete_pending;
static volatile uint8_t s_webhid_ep2_blocked;
static volatile uint8_t s_webhid_transport_reset_complete;
static uint8_t s_hid_idle;
static uint8_t s_hid_protocol;
static uint8_t s_remote_wakeup;
static uint8_t s_last_report_length;
static uint8_t s_last_telemetry_length;
static usb_board_profile_t s_profile;

static usbdev_ep0_flow_t s_ep0_flow;
static usbdev_control_out_t s_control_out_kind;
static uint8_t s_setup_request;
static uint8_t s_setup_type;
static uint8_t s_setup_report_id;
static uint8_t s_setup_report_type;
static uint16_t s_setup_value;
static uint16_t s_setup_index;
static uint16_t s_setup_length;
static const uint8_t *s_control_data;
static uint16_t s_control_remaining;
static uint16_t s_control_out_received;
static uint8_t s_control_need_zlp;

static uint32_t s_clock_last_cycles;
static uint32_t s_clock_remainder;
static uint32_t s_clock_millis;

USB_BOARD_STATIC_ASSERT(sizeof(xinput_device_descriptor) == 18u);
USB_BOARD_STATIC_ASSERT(sizeof(xinput_configuration_descriptor) == 0xB2u);
USB_BOARD_STATIC_ASSERT(sizeof(xinput_telemetry_hid_report_descriptor) == 21u);
USB_BOARD_STATIC_ASSERT(USB_XBOX_DEVICE_PACKET_BYTES <= USBDEV_INTERRUPT_BYTES);

static uint8_t profile_interface_count(void)
{
    if(s_profile == USB_BOARD_PROFILE_XINPUT)
    {
        return 5u;
    }
    if(s_profile == USB_BOARD_PROFILE_WEB_CONFIG)
    {
        return 1u;
    }
    return 1u;
}

static bool profile_supports_remote_wakeup(void)
{
    return (s_profile == USB_BOARD_PROFILE_XINPUT) ||
           (s_profile == USB_BOARD_PROFILE_XBOX_ONE);
}

static uint32_t device_now_ms(void)
{
    uint32_t cycles_per_ms = GetSysClock() / 1000u;
    const uint32_t now = SysTick->CNTL;
    const uint32_t elapsed = now - s_clock_last_cycles;
    uint32_t accumulated;

    if(cycles_per_ms == 0u)
    {
        cycles_per_ms = 1u;
    }
    s_clock_last_cycles = now;
    accumulated = s_clock_remainder + elapsed;
    s_clock_millis += accumulated / cycles_per_ms;
    s_clock_remainder = accumulated % cycles_per_ms;
    return s_clock_millis;
}

static bool wait_for_sie_idle(uint32_t timeout_ms)
{
    uint32_t cycles_per_ms = GetSysClock() / 1000u;
    uint32_t cycle_budget;
    uint32_t remaining_spins;
    const uint32_t start_cycles = SysTick->CNTL;

    if(cycles_per_ms == 0u)
    {
        cycles_per_ms = 1u;
    }
    cycle_budget = cycles_per_ms * timeout_ms;
    if(cycle_budget == 0u)
    {
        cycle_budget = 1u;
    }
    remaining_spins = cycle_budget;

    do
    {
        if((R8_USB2_MIS_ST & USBHS_UDMS_SIE_FREE) != 0u)
        {
            return true;
        }
        --remaining_spins;
    } while((remaining_spins != 0u) &&
            ((uint32_t)(SysTick->CNTL - start_cycles) < cycle_budget));

    return false;
}

static bool data_path_reset(bool settle_same_bus)
{
    uint16_t saved_tx_enable = 0u;
    uint16_t saved_rx_enable = 0u;
    const uint8_t saved_webhid_ep2_blocked =
        s_webhid_ep2_blocked;
    const uint8_t saved_webhid_transport_reset_complete =
        s_webhid_transport_reset_complete;
    const bool settle_webhid_endpoints =
        settle_same_bus &&
        (s_profile == USB_BOARD_PROFILE_WEB_CONFIG);

    if(s_profile == USB_BOARD_PROFILE_WEB_CONFIG)
    {
        s_webhid_ep2_blocked = 1u;
        s_webhid_transport_reset_complete = 0u;
    }

    /*
     * A bus reset or explicit CLEAR_FAULT ends the authenticated WebHID
     * transport generation. Cancel an IN report already owned by EP1 as well
     * as the software queues so old ciphertext cannot cross that boundary.
     *
     * IRQ masking alone does not stop the SIE. Disable the WebHID endpoints
     * and wait for an in-flight token to finish before examining DONE. A
     * completed transfer is discarded, but its DATA toggle must still advance
     * exactly as it would in the completion ISR. A bus reset first resets all
     * endpoint controls, so no stale DONE is consumed on that path.
     */
    if(settle_webhid_endpoints)
    {
        saved_tx_enable = R16_U2EP_TX_EN;
        saved_rx_enable = R16_U2EP_RX_EN;
        R16_U2EP_TX_EN =
            (uint16_t)(saved_tx_enable & (uint16_t)~RB_EP1_EN);
        R16_U2EP_RX_EN =
            (uint16_t)(saved_rx_enable & (uint16_t)~RB_EP2_EN);
        if(!wait_for_sie_idle(USBDEV_SIE_QUIESCE_TIMEOUT_MS))
        {
            /*
             * CLEAR_FAULT is recoverable. If the SIE cannot be quiesced in
             * time, leave the current transport generation completely intact
             * and report failure so STM32 can retry later. In particular, do
             * not detach, change endpoint toggles, or discard queued reports.
             */
            s_webhid_ep2_blocked = saved_webhid_ep2_blocked;
            s_webhid_transport_reset_complete =
                saved_webhid_transport_reset_complete;
            /* Restore software ownership before exposing endpoints again. */
            R16_U2EP_TX_EN = saved_tx_enable;
            R16_U2EP_RX_EN = saved_rx_enable;
            return false;
        }
    }

    /* The failure path returned before any queue or endpoint state changed. */
    R16_U2EP1_T_LEN = 0u;
    if(settle_webhid_endpoints)
    {
        R8_U2EP1_TX_CTRL = usb_endpoint_reset_control(
            R8_U2EP1_TX_CTRL,
            USBHS_UEP_T_DONE,
            0u,
            USBHS_UEP_T_TOG_DATA1,
            USBHS_UEP_T_RES_NAK);
    }
    else
    {
        R8_U2EP1_TX_CTRL =
            (uint8_t)((R8_U2EP1_TX_CTRL &
                       USBHS_UEP_T_TOG_DATA1) |
                      USBHS_UEP_T_RES_NAK);
    }
    s_ep1_busy = 0u;
    s_webhid_ep1_complete_pending = 0u;
    s_last_report_length = 0u;
    memset(s_ep1_tx, 0, sizeof(s_ep1_tx));
    memset(s_last_report, 0, sizeof(s_last_report));
    s_xbox_out_ready = 0u;
    s_xbox_out_length = 0u;
    s_webhid_out_head = 0u;
    s_webhid_out_tail = 0u;
    s_webhid_out_count = 0u;
    memset(s_webhid_out, 0, sizeof(s_webhid_out));
    if(s_profile == USB_BOARD_PROFILE_WEB_CONFIG)
    {
        /*
         * Drop a completed OUT payload while still consuming its DONE toggle.
         * Re-open EP2 only after both endpoint controls and queues represent
         * the new bootstrap generation.
         */
        memset(s_ep2_rx, 0, WEBHID_REPORT_BYTES);
        if(settle_webhid_endpoints)
        {
            R8_U2EP2_RX_CTRL = usb_endpoint_reset_control(
                R8_U2EP2_RX_CTRL,
                USBHS_UEP_R_DONE,
                USBHS_UEP_R_TOG_MATCH,
                USBHS_UEP_R_TOG_DATA1,
                USBHS_UEP_R_RES_NAK);
        }
        else if(!settle_webhid_endpoints)
        {
            R8_U2EP2_RX_CTRL =
                (uint8_t)((R8_U2EP2_RX_CTRL &
                           USBHS_UEP_R_TOG_DATA1) |
                          USBHS_UEP_R_RES_NAK);
        }
    }
    s_transport_reset_pending = 1u;

    if(settle_webhid_endpoints)
    {
        R16_U2EP_TX_EN = saved_tx_enable;
        R16_U2EP_RX_EN = saved_rx_enable;
    }
    return true;
}

/*
 * EP2 stays NAK after data_path_reset until both lower transport layers have
 * discarded the old generation. This function is called only with the USB
 * IRQ masked, so ACK cannot become visible between the two reset operations.
 */
static void webhid_try_reopen_out_endpoint(void)
{
    if((s_profile != USB_BOARD_PROFILE_WEB_CONFIG) ||
       (s_webhid_ep2_blocked == 0u) ||
       (s_webhid_transport_reset_complete == 0u) ||
       (s_connected == 0u) ||
       (s_mounted == 0u) ||
       (s_suspended != 0u))
    {
        return;
    }

    R8_U2EP2_RX_CTRL =
        (uint8_t)((R8_U2EP2_RX_CTRL & USBHS_UEP_R_TOG_DATA1) |
                  USBHS_UEP_R_RES_ACK);
    s_webhid_ep2_blocked = 0u;
    s_webhid_transport_reset_complete = 0u;
}

static bool webhid_out_enqueue(const uint8_t *data, uint16_t length)
{
    uint8_t tail;

    if((data == 0) || (length != WEBHID_REPORT_BYTES) ||
       (s_webhid_out_count >= USBDEV_WEBHID_OUT_QUEUE_DEPTH))
    {
        return false;
    }
    tail = s_webhid_out_tail;
    memcpy(s_webhid_out[tail], data, WEBHID_REPORT_BYTES);
    s_webhid_out_tail =
        (uint8_t)((tail + 1u) % USBDEV_WEBHID_OUT_QUEUE_DEPTH);
    ++s_webhid_out_count;
    return true;
}

static void ep0_stall(void)
{
    s_ep0_flow = USBDEV_EP0_IDLE;
    s_control_out_kind = USBDEV_CONTROL_OUT_NONE;
    R8_U2EP0_TX_CTRL =
        USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_STALL;
    R8_U2EP0_RX_CTRL =
        USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_STALL;
}

static void ep0_arm_out_status(void)
{
    s_ep0_flow = USBDEV_EP0_WAIT_OUT_STATUS;
    R16_U2EP0_T_LEN = 0u;
    R8_U2EP0_TX_CTRL = USBHS_UEP_T_RES_NAK;
    R8_U2EP0_RX_CTRL =
        USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_ACK;
}

static void ep0_status_in(void)
{
    s_ep0_flow = USBDEV_EP0_IN_STATUS;
    R16_U2EP0_T_LEN = 0u;
    R8_U2EP0_TX_CTRL =
        USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_ACK;
}

static void ep0_tx(const uint8_t *data,
                   uint16_t available,
                   uint16_t requested)
{
    uint16_t total = available;
    uint16_t packet;

    if(total > requested)
    {
        total = requested;
    }
    if((requested == 0u) && (total == 0u))
    {
        ep0_arm_out_status();
        return;
    }

    s_ep0_flow = USBDEV_EP0_IN_DATA;
    s_control_data = data;
    s_control_remaining = total;
    s_control_need_zlp =
        ((total != 0u) && (total < requested) &&
         ((total % USBDEV_EP0_BYTES) == 0u)) ? 1u : 0u;

    packet = (total > USBDEV_EP0_BYTES)
        ? USBDEV_EP0_BYTES
        : total;
    if((packet != 0u) && (data != 0))
    {
        memcpy(s_ep0, data, packet);
        s_control_data += packet;
        s_control_remaining -= packet;
    }
    R16_U2EP0_T_LEN = packet;
    R8_U2EP0_TX_CTRL =
        USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_ACK;
}

static void ep0_receive(usbdev_control_out_t kind, uint16_t length)
{
    if((length == 0u) || (length > sizeof(s_control_out)))
    {
        ep0_stall();
        return;
    }
    s_ep0_flow = USBDEV_EP0_OUT_DATA;
    s_control_out_kind = kind;
    s_control_out_received = 0u;
    R16_U2EP0_T_LEN = 0u;
    R8_U2EP0_TX_CTRL = USBHS_UEP_T_RES_NAK;
    R8_U2EP0_RX_CTRL =
        USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_ACK;
}

static const uint8_t *find_descriptor(const uint8_t *configuration,
                                      uint16_t configuration_length,
                                      uint8_t type,
                                      uint8_t interface_number,
                                      uint16_t *length)
{
    uint16_t offset = 0u;
    uint8_t current_interface = 0xFFu;

    while((offset + 2u) <= configuration_length)
    {
        const uint8_t descriptor_length = configuration[offset];
        const uint8_t descriptor_type = configuration[offset + 1u];
        if((descriptor_length < 2u) ||
           ((uint32_t)offset + descriptor_length >
            configuration_length))
        {
            break;
        }
        if((descriptor_type == USB_DESCR_TYP_INTERF) &&
           (descriptor_length >= 4u))
        {
            current_interface = configuration[offset + 2u];
        }
        else if((descriptor_type == type) &&
                (current_interface == interface_number))
        {
            if(length != 0)
            {
                *length = descriptor_length;
            }
            return &configuration[offset];
        }
        offset = (uint16_t)(offset + descriptor_length);
    }
    if(length != 0)
    {
        *length = 0u;
    }
    return 0;
}

static const uint8_t *xinput_string_descriptor(uint8_t index,
                                                uint16_t *length)
{
    const char *source = 0;
    size_t count;
    size_t i;

    if(index == 0u)
    {
        static const uint8_t language[] = {0x04u, 0x03u, 0x09u, 0x04u};
        if(length != 0)
        {
            *length = sizeof(language);
        }
        return language;
    }
    switch(index)
    {
    case 1u:
        source = (const char *)xinput_string_manfacturer;
        break;
    case 2u:
        source = (const char *)xinput_string_product;
        break;
    case 3u:
        source = (const char *)xinput_string_version;
        break;
    case 4u:
        source = (const char *)xinput_string_xsm3;
        break;
    default:
        break;
    }
    if(source == 0)
    {
        if(length != 0)
        {
            *length = 0u;
        }
        return 0;
    }

    count = strlen(source);
    if(count > ((sizeof(s_xinput_string) - 2u) / 2u))
    {
        count = (sizeof(s_xinput_string) - 2u) / 2u;
    }
    s_xinput_string[0] = (uint8_t)(2u + count * 2u);
    s_xinput_string[1] = USB_DESCR_TYP_STRING;
    for(i = 0u; i < count; ++i)
    {
        s_xinput_string[2u + i * 2u] = (uint8_t)source[i];
        s_xinput_string[3u + i * 2u] = 0u;
    }
    if(length != 0)
    {
        *length = (uint16_t)(2u + count * 2u);
    }
    return s_xinput_string;
}

static const uint8_t *descriptor_for_setup(uint16_t value,
                                           uint16_t index,
                                           uint16_t *length)
{
    const uint8_t type = (uint8_t)(value >> 8);
    const uint8_t number = (uint8_t)value;
    const uint8_t interface_number = (uint8_t)index;
    const uint8_t *descriptor = 0;
    uint16_t descriptor_length = 0u;

    if(s_profile == USB_BOARD_PROFILE_WEB_CONFIG)
    {
        switch(type)
        {
        case USB_DESCR_TYP_DEVICE:
            descriptor =
                usb_webhid_device_descriptor(&descriptor_length);
            break;
        case USB_DESCR_TYP_CONFIG:
            descriptor =
                usb_webhid_configuration_descriptor(
                    &descriptor_length);
            break;
        case USB_DESCR_TYP_QUALIF:
            descriptor =
                usb_webhid_qualifier_descriptor(&descriptor_length);
            break;
        case USB_DESCR_TYP_SPEED:
            descriptor =
                usb_webhid_other_speed_descriptor(&descriptor_length);
            break;
        case USB_DESCR_TYP_REPORT:
            if(interface_number == USB_WEBHID_INTERFACE)
            {
                descriptor =
                    usb_webhid_report_descriptor(&descriptor_length);
            }
            break;
        case USB_DESCR_TYP_HID:
            if(interface_number == USB_WEBHID_INTERFACE)
            {
                descriptor =
                    usb_webhid_hid_descriptor(&descriptor_length);
            }
            break;
        case USB_DESCR_TYP_STRING:
            descriptor =
                usb_webhid_string_descriptor(number, &descriptor_length);
            break;
        default:
            break;
        }
    }
    else if(s_profile == USB_BOARD_PROFILE_XINPUT)
    {
        switch(type)
        {
        case USB_DESCR_TYP_DEVICE:
            descriptor = xinput_device_descriptor;
            descriptor_length = sizeof(xinput_device_descriptor);
            break;
        case USB_DESCR_TYP_CONFIG:
            descriptor = xinput_configuration_descriptor;
            descriptor_length = sizeof(xinput_configuration_descriptor);
            break;
        case USB_DESCR_TYP_STRING:
            descriptor =
                xinput_string_descriptor(number, &descriptor_length);
            break;
        case USB_DESCR_TYP_REPORT:
            if(interface_number == USBDEV_XINPUT_HID_INTERFACE)
            {
                descriptor = xinput_telemetry_hid_report_descriptor;
                descriptor_length =
                    sizeof(xinput_telemetry_hid_report_descriptor);
            }
            break;
        case USB_DESCR_TYP_HID:
            if(interface_number == USBDEV_XINPUT_HID_INTERFACE)
            {
                descriptor = find_descriptor(
                    xinput_configuration_descriptor,
                    sizeof(xinput_configuration_descriptor),
                    USB_DESCR_TYP_HID,
                    USBDEV_XINPUT_HID_INTERFACE,
                    &descriptor_length);
            }
            break;
        default:
            break;
        }
    }
    else
    {
        switch(type)
        {
        case USB_DESCR_TYP_DEVICE:
            descriptor =
                usb_legacy_get_device_descriptor(s_profile,
                                                 &descriptor_length);
            break;
        case USB_DESCR_TYP_CONFIG:
            descriptor =
                usb_legacy_get_configuration_descriptor(
                    s_profile, &descriptor_length);
            break;
        case USB_DESCR_TYP_STRING:
            descriptor =
                usb_legacy_get_string_descriptor(
                    s_profile, number, &descriptor_length);
            break;
        case USB_DESCR_TYP_QUALIF:
            descriptor =
                usb_legacy_get_qualifier_descriptor(
                    s_profile, &descriptor_length);
            break;
        case USB_DESCR_TYP_REPORT:
            descriptor =
                usb_legacy_get_report_descriptor(
                    s_profile, &descriptor_length);
            break;
        case USB_DESCR_TYP_HID:
            descriptor =
                usb_legacy_get_hid_descriptor(
                    s_profile, &descriptor_length);
            break;
        case USB_DESCR_TYP_SPEED:
            if(s_profile == USB_BOARD_PROFILE_XBOX_ONE)
            {
                descriptor =
                    usb_legacy_get_configuration_descriptor(
                        s_profile, &descriptor_length);
                if((descriptor != 0) &&
                   (descriptor_length <= sizeof(s_other_speed)))
                {
                    memcpy(s_other_speed, descriptor, descriptor_length);
                    s_other_speed[1] = USB_DESCR_TYP_SPEED;
                    descriptor = s_other_speed;
                }
                else
                {
                    descriptor = 0;
                }
            }
            break;
        default:
            break;
        }
    }

    if(length != 0)
    {
        *length = descriptor_length;
    }
    return descriptor;
}

static void endpoint_controls_reset(void)
{
    R8_U2EP0_TX_CTRL = USBHS_UEP_T_RES_NAK;
    R8_U2EP0_RX_CTRL = USBHS_UEP_R_RES_ACK;
    R8_U2EP1_TX_CTRL = USBHS_UEP_T_RES_NAK;
    R8_U2EP1_RX_CTRL = USBHS_UEP_R_RES_NAK;
    R8_U2EP2_TX_CTRL = USBHS_UEP_T_RES_NAK;
    R8_U2EP2_RX_CTRL = USBHS_UEP_R_RES_NAK;
    R8_U2EP3_TX_CTRL = USBHS_UEP_T_RES_NAK;
    R8_U2EP3_RX_CTRL = USBHS_UEP_R_RES_NAK;
    R8_U2EP4_TX_CTRL = USBHS_UEP_T_RES_NAK;
    R8_U2EP4_RX_CTRL = USBHS_UEP_R_RES_NAK;
    R8_U2EP5_TX_CTRL = USBHS_UEP_T_RES_NAK;
    R8_U2EP5_RX_CTRL = USBHS_UEP_R_RES_NAK;
    R8_U2EP6_TX_CTRL = USBHS_UEP_T_RES_NAK;
    R8_U2EP6_RX_CTRL = USBHS_UEP_R_RES_NAK;
    R8_U2EP7_TX_CTRL = USBHS_UEP_T_RES_NAK;
    R8_U2EP7_RX_CTRL = USBHS_UEP_R_RES_NAK;

    if(s_profile == USB_BOARD_PROFILE_XINPUT)
    {
        R8_U2EP2_RX_CTRL = USBHS_UEP_R_RES_ACK;
        R8_U2EP4_RX_CTRL = USBHS_UEP_R_RES_ACK;
        R8_U2EP6_RX_CTRL = USBHS_UEP_R_RES_ACK;
    }
    else if((s_profile == USB_BOARD_PROFILE_PS4) ||
            (s_profile == USB_BOARD_PROFILE_PS5_COMPAT))
    {
        R8_U2EP3_RX_CTRL = USBHS_UEP_R_RES_ACK;
    }
    else if((s_profile == USB_BOARD_PROFILE_SWITCH) ||
            (s_profile == USB_BOARD_PROFILE_XBOX_ONE))
    {
        R8_U2EP2_RX_CTRL = USBHS_UEP_R_RES_ACK;
    }
    else if(s_profile == USB_BOARD_PROFILE_WEB_CONFIG)
    {
        R8_U2EP2_RX_CTRL = USBHS_UEP_R_RES_ACK;
    }
}

static void endpoints_init(void)
{
    uint16_t tx_enable = RB_EP0_EN;
    uint16_t rx_enable = RB_EP0_EN;
    uint16_t interrupt_max = USBDEV_INTERRUPT_BYTES;
    uint16_t ep2_max = USBDEV_INTERRUPT_BYTES;

    if(s_profile == USB_BOARD_PROFILE_XINPUT)
    {
        interrupt_max = 32u;
        ep2_max = 32u;
        tx_enable |= RB_EP1_EN | RB_EP3_EN | RB_EP5_EN |
                     RB_EP6_EN | RB_EP7_EN;
        rx_enable |= RB_EP2_EN | RB_EP4_EN | RB_EP6_EN;
    }
    else if((s_profile == USB_BOARD_PROFILE_PS4) ||
            (s_profile == USB_BOARD_PROFILE_PS5_COMPAT))
    {
        tx_enable |= RB_EP1_EN;
        rx_enable |= RB_EP3_EN;
    }
    else if((s_profile == USB_BOARD_PROFILE_SWITCH) ||
            (s_profile == USB_BOARD_PROFILE_XBOX_ONE))
    {
        tx_enable |= RB_EP1_EN;
        rx_enable |= RB_EP2_EN;
    }
    else if(s_profile == USB_BOARD_PROFILE_WEB_CONFIG)
    {
        ep2_max = WEBHID_REPORT_BYTES;
        tx_enable |= RB_EP1_EN;
        rx_enable |= RB_EP2_EN;
    }

    R16_U2EP_TX_EN = tx_enable;
    R16_U2EP_RX_EN = rx_enable;
    R32_U2EP0_MAX_LEN = USBDEV_EP0_BYTES;
    R32_U2EP1_MAX_LEN = interrupt_max;
    R32_U2EP2_MAX_LEN = ep2_max;
    R32_U2EP3_MAX_LEN = interrupt_max;
    R32_U2EP4_MAX_LEN = interrupt_max;
    R32_U2EP5_MAX_LEN = interrupt_max;
    R32_U2EP6_MAX_LEN = interrupt_max;
    R32_U2EP7_MAX_LEN = interrupt_max;

    R32_U2EP0_DMA = (uint32_t)s_ep0;
    R32_U2EP1_TX_DMA = (uint32_t)s_ep1_tx;
    R32_U2EP2_RX_DMA = (uint32_t)s_ep2_rx;
    R32_U2EP3_RX_DMA = (uint32_t)s_ep3_rx;
    R32_U2EP3_TX_DMA = (uint32_t)s_unused_tx;
    R32_U2EP4_RX_DMA = (uint32_t)s_ep4_rx;
    R32_U2EP5_TX_DMA = (uint32_t)s_unused_tx;
    R32_U2EP6_RX_DMA = (uint32_t)s_ep6_rx;
    R32_U2EP6_TX_DMA = (uint32_t)s_unused_tx;
    R32_U2EP7_TX_DMA = (uint32_t)s_ep7_tx;

    R16_U2EP1_T_LEN = 0u;
    R16_U2EP2_T_LEN = 0u;
    R16_U2EP3_T_LEN = 0u;
    R16_U2EP4_T_LEN = 0u;
    R16_U2EP5_T_LEN = 0u;
    R16_U2EP6_T_LEN = 0u;
    R16_U2EP7_T_LEN = 0u;
    endpoint_controls_reset();

    s_ep0_flow = USBDEV_EP0_IDLE;
    s_control_out_kind = USBDEV_CONTROL_OUT_NONE;
    s_ep1_busy = 0u;
    s_ep7_busy = 0u;
    (void)data_path_reset(false);
}

static bool ep1_send(const uint8_t *data, uint8_t length)
{
    bool armed = false;
    uint8_t irq_was_enabled;

    if((data == 0) || (length == 0u) ||
       (length > sizeof(s_ep1_tx)))
    {
        return false;
    }

    /*
     * BUS_RST clears s_mounted, EP1 ownership and the WebHID generation in
     * the USB2 ISR. Keep that reset indivisible from the final readiness
     * check through arming EP1; otherwise the ISR can clear the generation
     * after the check and this function can re-arm its old ciphertext.
     * The critical section copies at most one 64-byte interrupt report.
     */
    irq_was_enabled =
        (PFIC_GetStatusIRQ(USB2_DEVICE_IRQn) != 0u) ? 1u : 0u;
    if(irq_was_enabled != 0u)
    {
        PFIC_DisableIRQ(USB2_DEVICE_IRQn);
    }
    if((s_mounted != 0u) && (s_suspended == 0u) &&
       (s_ep1_busy == 0u))
    {
        memcpy(s_ep1_tx, data, length);
        s_ep1_busy = 1u;
        R16_U2EP1_T_LEN = length;
        R8_U2EP1_TX_CTRL =
            (uint8_t)((R8_U2EP1_TX_CTRL &
                       (uint8_t)~USBHS_UEP_T_RES_MASK) |
                      USBHS_UEP_T_RES_ACK);
        armed = true;
    }
    if(irq_was_enabled != 0u)
    {
        PFIC_EnableIRQ(USB2_DEVICE_IRQn);
    }
    return armed;
}

static bool process_hid_get_report(void)
{
    uint16_t payload_length = 0u;
    uint16_t total;
    uint8_t offset = 0u;

    if(s_setup_report_type == USBDEV_HID_REPORT_FEATURE)
    {
        if(s_setup_report_id != 0u)
        {
            s_control_response[0] = s_setup_report_id;
            offset = 1u;
        }
        if(!usb_ps4_feature_get(
                s_profile,
                s_setup_report_id,
                &s_control_response[offset],
                (uint16_t)(sizeof(s_control_response) - offset),
                &payload_length) &&
           !usb_auth_device_hid_get_feature(
                s_setup_report_id,
                &s_control_response[offset],
                (uint16_t)(sizeof(s_control_response) - offset),
                &payload_length))
        {
            return false;
        }
        total = (uint16_t)(payload_length + offset);
        ep0_tx(s_control_response, total, s_setup_length);
        return true;
    }

    if(s_setup_report_type == USBDEV_HID_REPORT_INPUT)
    {
        if((s_profile == USB_BOARD_PROFILE_XINPUT) &&
           ((uint8_t)s_setup_index == USBDEV_XINPUT_HID_INTERFACE))
        {
            if(s_last_telemetry_length == 0u)
            {
                return false;
            }
            ep0_tx(s_last_telemetry,
                   s_last_telemetry_length,
                   s_setup_length);
            return true;
        }
        if(s_last_report_length == 0u)
        {
            return false;
        }
        ep0_tx(s_last_report, s_last_report_length, s_setup_length);
        return true;
    }
    return false;
}

static bool process_control_out(void)
{
    const uint8_t *data = s_control_out;
    uint16_t length = s_control_out_received;

    if((s_control_out_kind == USBDEV_CONTROL_OUT_HID_REPORT) &&
       (s_setup_report_id != 0u) && (length > 1u) &&
       (data[0] == s_setup_report_id))
    {
        ++data;
        --length;
    }

    switch(s_control_out_kind)
    {
    case USBDEV_CONTROL_OUT_HID_REPORT:
        if(s_setup_report_type == USBDEV_HID_REPORT_OUTPUT)
        {
            if(s_profile == USB_BOARD_PROFILE_WEB_CONFIG)
            {
                return webhid_out_enqueue(data, length);
            }
            return (s_profile == USB_BOARD_PROFILE_PS4) ||
                   (s_profile == USB_BOARD_PROFILE_PS5_COMPAT) ||
                   (s_profile == USB_BOARD_PROFILE_SWITCH);
        }
        if(s_setup_report_type == USBDEV_HID_REPORT_FEATURE)
        {
            return usb_ps4_feature_set(
                       s_setup_report_id, data, length) ||
                   usb_auth_device_hid_set_feature(
                       s_setup_report_id, data, length);
        }
        return false;

    case USBDEV_CONTROL_OUT_XINPUT_VENDOR:
        return usb_auth_device_vendor_out(
            s_setup_request,
            s_setup_value,
            s_setup_index,
            s_control_out,
            s_control_out_received);

    default:
        return false;
    }
}

static void handle_setup(void)
{
    PUSB_SETUP_REQ setup = (PUSB_SETUP_REQ)s_ep0;
    const uint8_t *descriptor;
    uint16_t descriptor_length = 0u;
    uint16_t response_length = 0u;
    const uint8_t request_type =
        setup->bRequestType & USB_REQ_TYP_MASK;

    s_setup_type = setup->bRequestType;
    s_setup_request = setup->bRequest;
    s_setup_value = setup->wValue;
    s_setup_index = setup->wIndex;
    s_setup_length = setup->wLength;
    s_setup_report_id = (uint8_t)setup->wValue;
    s_setup_report_type = (uint8_t)(setup->wValue >> 8);
    s_control_data = 0;
    s_control_remaining = 0u;
    s_control_need_zlp = 0u;
    s_control_out_kind = USBDEV_CONTROL_OUT_NONE;
    s_ep0_flow = USBDEV_EP0_IDLE;

    if(request_type == USB_REQ_TYP_STANDARD)
    {
        switch(setup->bRequest)
        {
        case USB_GET_DESCRIPTOR:
            descriptor = descriptor_for_setup(
                setup->wValue, setup->wIndex, &descriptor_length);
            if(descriptor == 0)
            {
                ep0_stall();
                return;
            }
            ep0_tx(descriptor, descriptor_length, setup->wLength);
            return;

        case USB_SET_ADDRESS:
            if((setup->bRequestType != 0u) ||
               (setup->wIndex != 0u) ||
               (setup->wLength != 0u) ||
               (setup->wValue > 127u))
            {
                ep0_stall();
                return;
            }
            s_address = (uint8_t)setup->wValue;
            ep0_status_in();
            return;

        case USB_SET_CONFIGURATION:
            if((setup->bRequestType != 0u) ||
               (setup->wIndex != 0u) ||
               (setup->wLength != 0u) ||
               (setup->wValue > 1u))
            {
                ep0_stall();
                return;
            }
            if(s_profile == USB_BOARD_PROFILE_XBOX_ONE)
            {
                usb_xbox_device_set_mounted(false, device_now_ms());
            }
            /*
             * USB 2.0 requires non-control endpoint toggles and alternate
             * settings to return to their defaults on SET_CONFIGURATION.
             */
            endpoints_init();
            s_configuration = (uint8_t)setup->wValue;
            s_mounted = (s_configuration != 0u) ? 1u : 0u;
            if(s_profile == USB_BOARD_PROFILE_XBOX_ONE)
            {
                usb_xbox_device_set_mounted(
                    s_mounted != 0u, device_now_ms());
            }
            ep0_status_in();
            return;

        case USB_GET_CONFIGURATION:
            if((setup->bRequestType != 0x80u) ||
               (setup->wValue != 0u) ||
               (setup->wIndex != 0u) ||
               (setup->wLength != 1u))
            {
                ep0_stall();
                return;
            }
            s_control_response[0] = s_configuration;
            ep0_tx(s_control_response, 1u, setup->wLength);
            return;

        case USB_GET_STATUS:
            if((setup->bRequestType & 0x80u) == 0u ||
               (setup->wValue != 0u) ||
               (setup->wLength != 2u))
            {
                ep0_stall();
                return;
            }
            s_control_response[0] = 0u;
            s_control_response[1] = 0u;
            switch(setup->bRequestType & 0x1Fu)
            {
            case 0u: /* Device */
                if(setup->wIndex != 0u)
                {
                    ep0_stall();
                    return;
                }
                if(s_remote_wakeup != 0u)
                {
                    s_control_response[0] = 0x02u;
                }
                break;
            case 1u: /* Interface */
                if(setup->wIndex >= profile_interface_count())
                {
                    ep0_stall();
                    return;
                }
                break;
            case 2u: /* Endpoint: no endpoint is halted by this backend. */
                if((setup->wIndex & 0x70u) != 0u)
                {
                    ep0_stall();
                    return;
                }
                break;
            default:
                ep0_stall();
                return;
            }
            ep0_tx(s_control_response, 2u, setup->wLength);
            return;

        case USB_CLEAR_FEATURE:
        case USB_SET_FEATURE:
            if((setup->bRequestType != 0u) ||
               (setup->wValue != 1u) ||
               (setup->wIndex != 0u) ||
               (setup->wLength != 0u) ||
               !profile_supports_remote_wakeup())
            {
                ep0_stall();
                return;
            }
            s_remote_wakeup =
                (setup->bRequest == USB_SET_FEATURE) ? 1u : 0u;
            ep0_status_in();
            return;

        case USB_SET_INTERFACE:
            if((setup->bRequestType != 0x01u) ||
               (setup->wValue != 0u) ||
               (setup->wIndex >= profile_interface_count()) ||
               (setup->wLength != 0u))
            {
                ep0_stall();
                return;
            }
            ep0_status_in();
            return;

        case USB_GET_INTERFACE:
            if((setup->bRequestType != 0x81u) ||
               (setup->wValue != 0u) ||
               (setup->wIndex >= profile_interface_count()) ||
               (setup->wLength != 1u))
            {
                ep0_stall();
                return;
            }
            s_control_response[0] = 0u;
            ep0_tx(s_control_response, 1u, setup->wLength);
            return;

        default:
            ep0_stall();
            return;
        }
    }

    if(request_type == USB_REQ_TYP_CLASS)
    {
        switch(setup->bRequest)
        {
        case HID_GET_REPORT:
            if(!process_hid_get_report())
            {
                ep0_stall();
            }
            return;

        case HID_SET_REPORT:
            ep0_receive(USBDEV_CONTROL_OUT_HID_REPORT,
                        setup->wLength);
            return;

        case HID_GET_IDLE:
            s_control_response[0] = s_hid_idle;
            ep0_tx(s_control_response, 1u, setup->wLength);
            return;

        case HID_SET_IDLE:
            s_hid_idle = (uint8_t)(setup->wValue >> 8);
            ep0_status_in();
            return;

        case HID_GET_PROTOCOL:
            s_control_response[0] = s_hid_protocol;
            ep0_tx(s_control_response, 1u, setup->wLength);
            return;

        case HID_SET_PROTOCOL:
            s_hid_protocol = (uint8_t)setup->wValue;
            ep0_status_in();
            return;

        default:
            ep0_stall();
            return;
        }
    }

    if(request_type == USB_REQ_TYP_VENDOR)
    {
        if((s_profile == USB_BOARD_PROFILE_XBOX_ONE) &&
           ((setup->bRequestType & 0x80u) != 0u) &&
           (setup->bRequest == USBDEV_XBOX_OS_VENDOR_CODE) &&
           (setup->wIndex == USBDEV_XBOX_COMPAT_ID_INDEX))
        {
            descriptor =
                usb_legacy_get_xbox_compatible_id_descriptor(
                    &descriptor_length);
            ep0_tx(descriptor, descriptor_length, setup->wLength);
            return;
        }

        if(s_profile == USB_BOARD_PROFILE_XINPUT)
        {
            if((setup->bRequestType & 0x80u) != 0u)
            {
                if(!usb_auth_device_vendor_in(
                        setup->bRequest,
                        setup->wValue,
                        setup->wIndex,
                        s_control_response,
                        sizeof(s_control_response),
                        &response_length))
                {
                    ep0_stall();
                    return;
                }
                ep0_tx(s_control_response,
                       response_length,
                       setup->wLength);
                return;
            }
            ep0_receive(USBDEV_CONTROL_OUT_XINPUT_VENDOR,
                        setup->wLength);
            return;
        }
    }
    ep0_stall();
}

static void handle_ep0_out_data(void)
{
    const uint16_t packet = R16_U2EP0_RX_LEN;

    if(s_ep0_flow == USBDEV_EP0_WAIT_OUT_STATUS)
    {
        if(packet != 0u)
        {
            ep0_stall();
            return;
        }
        s_ep0_flow = USBDEV_EP0_IDLE;
        R8_U2EP0_RX_CTRL = USBHS_UEP_R_RES_ACK;
        return;
    }
    if((s_ep0_flow != USBDEV_EP0_OUT_DATA) ||
       ((uint32_t)s_control_out_received + packet >
        sizeof(s_control_out)) ||
       ((uint32_t)s_control_out_received + packet >
        s_setup_length))
    {
        ep0_stall();
        return;
    }

    if(packet != 0u)
    {
        memcpy(&s_control_out[s_control_out_received], s_ep0, packet);
        s_control_out_received =
            (uint16_t)(s_control_out_received + packet);
    }
    if(s_control_out_received == s_setup_length)
    {
        if(process_control_out())
        {
            s_control_out_kind = USBDEV_CONTROL_OUT_NONE;
            ep0_status_in();
        }
        else
        {
            ep0_stall();
        }
    }
    else
    {
        R8_U2EP0_RX_CTRL ^= USBHS_UEP_R_TOG_DATA1;
        R8_U2EP0_RX_CTRL =
            (uint8_t)((R8_U2EP0_RX_CTRL &
                       (uint8_t)~USBHS_UEP_R_RES_MASK) |
                      USBHS_UEP_R_RES_ACK);
    }
}

static void handle_ep0_in_complete(void)
{
    if(s_ep0_flow == USBDEV_EP0_IN_STATUS)
    {
        if(s_setup_request == USB_SET_ADDRESS)
        {
            R8_USB2_DEV_AD = s_address;
        }
        s_ep0_flow = USBDEV_EP0_IDLE;
        R8_U2EP0_RX_CTRL =
            USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_ACK;
        return;
    }

    if(s_ep0_flow != USBDEV_EP0_IN_DATA)
    {
        R16_U2EP0_T_LEN = 0u;
        R8_U2EP0_TX_CTRL = USBHS_UEP_T_RES_NAK;
        return;
    }

    if(s_control_remaining != 0u)
    {
        const uint16_t packet =
            (s_control_remaining > USBDEV_EP0_BYTES)
                ? USBDEV_EP0_BYTES
                : s_control_remaining;
        memcpy(s_ep0, s_control_data, packet);
        s_control_data += packet;
        s_control_remaining -= packet;
        R16_U2EP0_T_LEN = packet;
        R8_U2EP0_TX_CTRL ^= USBHS_UEP_T_TOG_DATA1;
        R8_U2EP0_TX_CTRL =
            (uint8_t)((R8_U2EP0_TX_CTRL &
                       (uint8_t)~USBHS_UEP_T_RES_MASK) |
                      USBHS_UEP_T_RES_ACK);
        return;
    }

    if(s_control_need_zlp != 0u)
    {
        s_control_need_zlp = 0u;
        R16_U2EP0_T_LEN = 0u;
        R8_U2EP0_TX_CTRL ^= USBHS_UEP_T_TOG_DATA1;
        R8_U2EP0_TX_CTRL =
            (uint8_t)((R8_U2EP0_TX_CTRL &
                       (uint8_t)~USBHS_UEP_T_RES_MASK) |
                      USBHS_UEP_T_RES_ACK);
        return;
    }
    ep0_arm_out_status();
}

bool usb_device_hw_init(usb_board_profile_t profile)
{
    s_profile = profile;
    s_mounted = 0u;
    s_suspended = 0u;
    s_connected = 0u;
    s_initialized = 0u;
    s_address = 0u;
    s_configuration = 0u;
    s_hid_idle = 0u;
    s_hid_protocol = 1u;
    s_remote_wakeup = 0u;
    s_last_report_length = 0u;
    s_last_telemetry_length = 0u;
    s_webhid_ep2_blocked = 0u;
    s_webhid_transport_reset_complete = 0u;
    memset(s_last_report, 0, sizeof(s_last_report));
    memset(s_last_telemetry, 0, sizeof(s_last_telemetry));
    s_clock_last_cycles = SysTick->CNTL;
    s_clock_remainder = 0u;
    s_clock_millis = 0u;

    if(profile == USB_BOARD_PROFILE_XBOX_ONE)
    {
        usb_xbox_device_init();
    }

    R8_USBHS_PLL_CTRL = USBHS_PLL_EN;
    R16_PIN_CONFIG &= (uint16_t)~RB_PIN_USB2_EN;
    R8_USB2_CTRL = USBHS_UD_RST_LINK | USBHS_UD_PHY_SUSPENDM;
    R8_USB2_INT_EN = USBHS_UDIE_BUS_RST |
                     USBHS_UDIE_SUSPEND |
                     USBHS_UDIE_BUS_SLEEP |
                     USBHS_UDIE_TRANSFER |
                     USBHS_UDIE_LINK_RDY;
    endpoints_init();
    R8_USB2_BASE_MODE = USBHS_UD_SPEED_HIGH;
    R8_USB2_CTRL = USBHS_UD_DEV_EN |
                   USBHS_UD_DMA_EN |
                   USBHS_UD_LPM_EN |
                   USBHS_UD_PHY_SUSPENDM;
    PFIC_EnableIRQ(USB2_DEVICE_IRQn);
    s_initialized = 1u;
    return true;
}

void usb_device_hw_shutdown(void)
{
    usb_management_control_hw_disconnect();
    PFIC_DisableIRQ(USB2_DEVICE_IRQn);
    R8_USB2_CTRL |= USBHS_UD_RST_SIE;
    R8_USB2_CTRL &= (uint8_t)~USBHS_UD_RST_SIE;
    R16_PIN_CONFIG &= (uint16_t)~RB_PIN_USB2_EN;
    R8_USBHS_PLL_CTRL &= (uint8_t)~USBHS_PLL_EN;
    s_initialized = 0u;
    s_mounted = 0u;
    s_suspended = 0u;
    s_ep1_busy = 0u;
    s_ep7_busy = 0u;
    (void)data_path_reset(false);
}

void usb_device_hw_set_actions(uint32_t action_mask)
{
    if(s_profile == USB_BOARD_PROFILE_XBOX_ONE)
    {
        usb_xbox_device_set_actions(action_mask);
    }
}

bool usb_device_hw_send_report(const uint8_t *report, uint8_t length)
{
    if(s_profile == USB_BOARD_PROFILE_XBOX_ONE)
    {
        /*
         * The GIP state machine owns EP1 and gates input behind the actual
         * announce/auth/ACK sequence.  usb_profiles' 36-byte representation
         * is deliberately not sent a second time.
         */
        return (report != 0) &&
               (length == USB_XBOX_DEVICE_INPUT_BYTES);
    }
    if(!ep1_send(report, length))
    {
        return false;
    }
    memcpy(s_last_report, report, length);
    s_last_report_length = length;
    return true;
}

bool usb_device_hw_send_telemetry(const uint8_t *data, uint8_t length)
{
    if((data == 0) ||
       (length != USB_BOARD_TELEMETRY_FRAME_BYTES) ||
       (s_profile != USB_BOARD_PROFILE_XINPUT) ||
       (s_mounted == 0u) || (s_suspended != 0u) ||
       (s_ep7_busy != 0u))
    {
        return false;
    }
    memcpy(s_ep7_tx, data, length);
    memcpy(s_last_telemetry, data, length);
    s_last_telemetry_length = length;
    s_ep7_busy = 1u;
    R16_U2EP7_T_LEN = length;
    R8_U2EP7_TX_CTRL =
        (uint8_t)((R8_U2EP7_TX_CTRL &
                   (uint8_t)~USBHS_UEP_T_RES_MASK) |
                  USBHS_UEP_T_RES_ACK);
    return true;
}

bool usb_device_hw_send_webhid_report(const uint8_t *data,
                                      uint8_t length)
{
    if((data == 0) || (length != WEBHID_REPORT_BYTES) ||
       (s_profile != USB_BOARD_PROFILE_WEB_CONFIG) ||
       !ep1_send(data, length))
    {
        return false;
    }
    memcpy(s_last_report, data, length);
    s_last_report_length = length;
    return true;
}

void usb_device_hw_process(void)
{
    uint8_t reset_pending;
    uint8_t webhid_ep1_complete_pending;

    PFIC_DisableIRQ(USB2_DEVICE_IRQn);
    reset_pending = s_transport_reset_pending;
    s_transport_reset_pending = 0u;
    PFIC_EnableIRQ(USB2_DEVICE_IRQn);
    if(reset_pending != 0u)
    {
        usb_board_link_reset_channel(USB_BOARD_CHANNEL_WEBCONFIG);
        usb_device_transport_reset();
    }
    PFIC_DisableIRQ(USB2_DEVICE_IRQn);
    webhid_ep1_complete_pending =
        s_webhid_ep1_complete_pending;
    s_webhid_ep1_complete_pending = 0u;
    if((reset_pending != 0u) &&
       (s_profile == USB_BOARD_PROFILE_WEB_CONFIG) &&
       (s_connected != 0u))
    {
        s_webhid_transport_reset_complete = 1u;
    }
    webhid_try_reopen_out_endpoint();
    PFIC_EnableIRQ(USB2_DEVICE_IRQn);

    if((reset_pending == 0u) &&
       (webhid_ep1_complete_pending != 0u))
    {
        usb_device_webhid_report_complete();
    }

    if(s_profile == USB_BOARD_PROFILE_XBOX_ONE)
    {
        uint8_t length = 0u;
        usb_xbox_device_process(device_now_ms());
        if(s_xbox_out_ready != 0u)
        {
            uint8_t packet[USB_XBOX_DEVICE_PACKET_BYTES];
            uint8_t packet_length;

            PFIC_DisableIRQ(USB2_DEVICE_IRQn);
            packet_length = s_xbox_out_length;
            memcpy(packet, s_xbox_out, packet_length);
            s_xbox_out_ready = 0u;
            s_xbox_out_length = 0u;
            R8_U2EP2_RX_CTRL =
                (uint8_t)((R8_U2EP2_RX_CTRL &
                           (uint8_t)~USBHS_UEP_R_RES_MASK) |
                          USBHS_UEP_R_RES_ACK);
            PFIC_EnableIRQ(USB2_DEVICE_IRQn);
            (void)usb_xbox_device_out(packet, packet_length);
        }
        if((s_ep1_busy == 0u) &&
           usb_xbox_device_next_in(
               s_ep1_tx, USB_XBOX_DEVICE_PACKET_BYTES, &length))
        {
            s_ep1_busy = 1u;
            R16_U2EP1_T_LEN = length;
            R8_U2EP1_TX_CTRL =
                (uint8_t)((R8_U2EP1_TX_CTRL &
                           (uint8_t)~USBHS_UEP_T_RES_MASK) |
                          USBHS_UEP_T_RES_ACK);
        }
    }
    else if(s_profile == USB_BOARD_PROFILE_WEB_CONFIG)
    {
        PFIC_DisableIRQ(USB2_DEVICE_IRQn);
        if((s_webhid_out_count != 0u) &&
           usb_board_link_publish_bulk(
               USB_BOARD_CHANNEL_WEBCONFIG,
               s_webhid_out[s_webhid_out_head],
               WEBHID_REPORT_BYTES))
        {
            memset(s_webhid_out[s_webhid_out_head],
                   0,
                   WEBHID_REPORT_BYTES);
            s_webhid_out_head =
                (uint8_t)((s_webhid_out_head + 1u) %
                          USBDEV_WEBHID_OUT_QUEUE_DEPTH);
            --s_webhid_out_count;
            if(s_webhid_ep2_blocked == 0u)
            {
                R8_U2EP2_RX_CTRL =
                    (uint8_t)((R8_U2EP2_RX_CTRL &
                               (uint8_t)~USBHS_UEP_R_RES_MASK) |
                              USBHS_UEP_R_RES_ACK);
            }
        }
        PFIC_EnableIRQ(USB2_DEVICE_IRQn);
    }
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

bool usb_management_control_hw_connect(void)
{
    if((s_initialized == 0u) ||
       (s_profile == USB_BOARD_PROFILE_NONE))
    {
        return false;
    }

    PFIC_DisableIRQ(USB2_DEVICE_IRQn);
    R8_USB2_CTRL |= USBHS_UD_RST_SIE;
    R8_USB2_CTRL &= (uint8_t)~USBHS_UD_RST_SIE;
    R8_USB2_INT_FG = 0xFFu;
    s_address = 0u;
    s_configuration = 0u;
    s_mounted = 0u;
    s_suspended = 0u;
    R8_USB2_DEV_AD = 0u;
    endpoints_init();
    R16_PIN_CONFIG |= RB_PIN_USB2_EN;
    s_connected = 1u;
    PFIC_EnableIRQ(USB2_DEVICE_IRQn);
    return true;
}

void usb_management_control_hw_disconnect(void)
{
    const uint8_t was_initialized = s_initialized;

    if(was_initialized != 0u)
    {
        PFIC_DisableIRQ(USB2_DEVICE_IRQn);
    }
    R16_PIN_CONFIG &= (uint16_t)~RB_PIN_USB2_EN;
    s_connected = 0u;
    s_mounted = 0u;
    s_suspended = 0u;
    s_configuration = 0u;
    if(s_profile == USB_BOARD_PROFILE_XBOX_ONE)
    {
        usb_xbox_device_set_mounted(false, device_now_ms());
    }
    (void)data_path_reset(false);
    if(was_initialized != 0u)
    {
        PFIC_EnableIRQ(USB2_DEVICE_IRQn);
    }
}

bool usb_management_control_hw_clear_fault(void)
{
    bool reset_ok;

    if(s_initialized == 0u)
    {
        return false;
    }
    PFIC_DisableIRQ(USB2_DEVICE_IRQn);
    reset_ok = data_path_reset(true);
    if(reset_ok)
    {
        if(s_profile != USB_BOARD_PROFILE_WEB_CONFIG)
        {
            endpoint_controls_reset();
        }
        /*
         * CLEAR_FAULT is the synchronized WebConfig transport-generation
         * reset. Commit the channel/device reset only after the endpoint
         * quiesce succeeded; a failed attempt leaves the live generation
         * untouched and is reported to STM32 for a later retry.
         */
        s_transport_reset_pending = 0u;
        usb_board_link_reset_channel(USB_BOARD_CHANNEL_WEBCONFIG);
        usb_device_transport_reset();
        if((s_profile == USB_BOARD_PROFILE_WEB_CONFIG) &&
           (s_connected != 0u))
        {
            s_webhid_transport_reset_complete = 1u;
        }
        webhid_try_reopen_out_endpoint();
    }
    PFIC_EnableIRQ(USB2_DEVICE_IRQn);
    return reset_ok;
}

bool usb_management_control_hw_link_up(void)
{
    return (s_mounted != 0u) && (s_suspended == 0u);
}

usb_board_usb_speed_t usb_management_control_hw_speed(void)
{
    if((s_connected == 0u) || (s_mounted == 0u))
    {
        return USB_BOARD_USB_SPEED_NONE;
    }
    return ((R8_USB2_MIS_ST & USBHS_UDMS_HS_MOD) != 0u)
        ? USB_BOARD_USB_SPEED_HIGH
        : USB_BOARD_USB_SPEED_FULL;
}

static void complete_in_endpoint(uint8_t endpoint)
{
    switch(endpoint)
    {
    case 1u:
        if((s_profile == USB_BOARD_PROFILE_WEB_CONFIG) &&
           (s_ep1_busy != 0u))
        {
            /* Defer board-link bookkeeping out of the USB interrupt. */
            s_webhid_ep1_complete_pending = 1u;
        }
        R16_U2EP1_T_LEN = 0u;
        R8_U2EP1_TX_CTRL ^= USBHS_UEP_T_TOG_DATA1;
        R8_U2EP1_TX_CTRL =
            (uint8_t)((R8_U2EP1_TX_CTRL &
                       (uint8_t)~USBHS_UEP_T_RES_MASK) |
                      USBHS_UEP_T_RES_NAK);
        R8_U2EP1_TX_CTRL &= (uint8_t)~USBHS_UEP_T_DONE;
        s_ep1_busy = 0u;
        break;
    case 3u:
        R16_U2EP3_T_LEN = 0u;
        R8_U2EP3_TX_CTRL &= (uint8_t)~USBHS_UEP_T_DONE;
        break;
    case 5u:
        R16_U2EP5_T_LEN = 0u;
        R8_U2EP5_TX_CTRL &= (uint8_t)~USBHS_UEP_T_DONE;
        break;
    case 6u:
        R16_U2EP6_T_LEN = 0u;
        R8_U2EP6_TX_CTRL &= (uint8_t)~USBHS_UEP_T_DONE;
        break;
    case 7u:
        R16_U2EP7_T_LEN = 0u;
        R8_U2EP7_TX_CTRL ^= USBHS_UEP_T_TOG_DATA1;
        R8_U2EP7_TX_CTRL =
            (uint8_t)((R8_U2EP7_TX_CTRL &
                       (uint8_t)~USBHS_UEP_T_RES_MASK) |
                      USBHS_UEP_T_RES_NAK);
        R8_U2EP7_TX_CTRL &= (uint8_t)~USBHS_UEP_T_DONE;
        s_ep7_busy = 0u;
        break;
    default:
        break;
    }
}

static void complete_out_endpoint(uint8_t endpoint)
{
    if(endpoint == 2u)
    {
        if((R8_U2EP2_RX_CTRL &
            (USBHS_UEP_R_DONE | USBHS_UEP_R_TOG_MATCH)) ==
           (USBHS_UEP_R_DONE | USBHS_UEP_R_TOG_MATCH))
        {
            const uint16_t length = R16_U2EP2_RX_LEN;
            if(s_profile == USB_BOARD_PROFILE_WEB_CONFIG)
            {
                /*
                 * A valid endpoint OUT transaction is authoritative resume
                 * evidence. Some Windows controllers deliver this packet
                 * before the CH585 suspend-status interrupt reports the
                 * cleared MIS_ST bit. Publish the active state immediately
                 * so the board link restores WebHID credits and the response
                 * can use EP1 in this transport generation.
                 */
                s_suspended = 0u;
                (void)webhid_out_enqueue(s_ep2_rx, length);
            }
            else if((s_profile == USB_BOARD_PROFILE_XBOX_ONE) &&
                    (length != 0u) &&
                    (length <= sizeof(s_xbox_out)) &&
                    (s_xbox_out_ready == 0u))
            {
                memcpy(s_xbox_out, s_ep2_rx, length);
                s_xbox_out_length = (uint8_t)length;
                s_xbox_out_ready = 1u;
            }
            R8_U2EP2_RX_CTRL ^= USBHS_UEP_R_TOG_DATA1;
        }

        R8_U2EP2_RX_CTRL =
            (uint8_t)((R8_U2EP2_RX_CTRL &
                       (uint8_t)~USBHS_UEP_R_RES_MASK) |
                      (((s_profile == USB_BOARD_PROFILE_XBOX_ONE) &&
                        (s_xbox_out_ready != 0u)) ||
                       ((s_profile == USB_BOARD_PROFILE_WEB_CONFIG) &&
                        ((s_webhid_ep2_blocked != 0u) ||
                         (s_webhid_out_count >=
                          USBDEV_WEBHID_OUT_QUEUE_DEPTH)))
                           ? USBHS_UEP_R_RES_NAK
                           : USBHS_UEP_R_RES_ACK));
        R8_U2EP2_RX_CTRL &= (uint8_t)~USBHS_UEP_R_DONE;
    }
    else if(endpoint == 3u)
    {
        if((R8_U2EP3_RX_CTRL & USBHS_UEP_R_TOG_MATCH) != 0u)
        {
            R8_U2EP3_RX_CTRL ^= USBHS_UEP_R_TOG_DATA1;
        }
        R8_U2EP3_RX_CTRL =
            (uint8_t)((R8_U2EP3_RX_CTRL &
                       (uint8_t)~USBHS_UEP_R_RES_MASK) |
                      USBHS_UEP_R_RES_ACK);
        R8_U2EP3_RX_CTRL &= (uint8_t)~USBHS_UEP_R_DONE;
    }
    else if(endpoint == 4u)
    {
        if((R8_U2EP4_RX_CTRL & USBHS_UEP_R_TOG_MATCH) != 0u)
        {
            R8_U2EP4_RX_CTRL ^= USBHS_UEP_R_TOG_DATA1;
        }
        R8_U2EP4_RX_CTRL =
            (uint8_t)((R8_U2EP4_RX_CTRL &
                       (uint8_t)~USBHS_UEP_R_RES_MASK) |
                      USBHS_UEP_R_RES_ACK);
        R8_U2EP4_RX_CTRL &= (uint8_t)~USBHS_UEP_R_DONE;
    }
    else if(endpoint == 6u)
    {
        if((R8_U2EP6_RX_CTRL & USBHS_UEP_R_TOG_MATCH) != 0u)
        {
            R8_U2EP6_RX_CTRL ^= USBHS_UEP_R_TOG_DATA1;
        }
        R8_U2EP6_RX_CTRL =
            (uint8_t)((R8_U2EP6_RX_CTRL &
                       (uint8_t)~USBHS_UEP_R_RES_MASK) |
                      USBHS_UEP_R_RES_ACK);
        R8_U2EP6_RX_CTRL &= (uint8_t)~USBHS_UEP_R_DONE;
    }
}

__INTERRUPT
__HIGH_CODE
void USB2_DEVICE_IRQHandler(void)
{
    const uint8_t flags = R8_USB2_INT_FG;
    const uint8_t status = R8_USB2_INT_ST;

    if((flags & USBHS_UDIF_TRANSFER) != 0u)
    {
        const uint8_t endpoint = status & USBHS_UDIS_EP_ID_MASK;
        if((status & USBHS_UDIS_EP_DIR) == 0u)
        {
            if(endpoint == 0u)
            {
                if((R8_U2EP0_RX_CTRL &
                    USBHS_UEP_R_SETUP_IS) != 0u)
                {
                    handle_setup();
                }
                else
                {
                    handle_ep0_out_data();
                }
                R8_U2EP0_RX_CTRL &= (uint8_t)~USBHS_UEP_R_DONE;
            }
            else
            {
                complete_out_endpoint(endpoint);
            }
        }
        else if(endpoint == 0u)
        {
            handle_ep0_in_complete();
            R8_U2EP0_TX_CTRL &= (uint8_t)~USBHS_UEP_T_DONE;
        }
        else
        {
            complete_in_endpoint(endpoint);
        }
    }
    else if((flags & USBHS_UDIF_BUS_RST) != 0u)
    {
        s_mounted = 0u;
        s_suspended = 0u;
        s_address = 0u;
        s_configuration = 0u;
        s_remote_wakeup = 0u;
        s_hid_idle = 0u;
        s_hid_protocol = 1u;
        s_last_report_length = 0u;
        s_last_telemetry_length = 0u;
        R8_USB2_DEV_AD = 0u;
        if(s_profile == USB_BOARD_PROFILE_XBOX_ONE)
        {
            usb_xbox_device_set_mounted(false, device_now_ms());
        }
        /*
         * endpoints_init() invokes data_path_reset(false), so bus reset discards
         * both queued and in-flight reports before process context can forward
         * another WebHID frame.
         */
        endpoints_init();
        R8_USB2_INT_FG = USBHS_UDIF_BUS_RST;
    }
    else if((flags & USBHS_UDIF_LINK_RDY) != 0u)
    {
        R8_USB2_INT_FG = USBHS_UDIF_LINK_RDY;
    }
    else if((flags & USBHS_UDIF_SUSPEND) != 0u)
    {
        const uint8_t suspended =
            ((R8_USB2_MIS_ST & USBHS_UDMS_SUSPEND) != 0u)
                ? 1u
                : 0u;
        /*
         * USB suspend is an idle pause, not a transport-generation boundary.
         * Preserve EP1 ownership, endpoint toggles, browser OUT reports and
         * both software queues. BUS_RST and explicit CLEAR_FAULT remain the
         * only paths that discard the current WebHID generation.
         */
        s_suspended = suspended;
        R8_USB2_INT_FG = USBHS_UDIF_SUSPEND;
    }
    else
    {
        R8_USB2_INT_FG = flags;
    }
}
