#include "usb_device.h"

#include <stddef.h>
#include <string.h>

#include "CH58x_common.h"
#include "usb_auth.h"
#include "usb_board_link.h"
#include "usb_legacy_descriptors.h"
#include "usb_management_control.h"
#include "usb_ncm.h"
#include "usb_ps4_features.h"
#include "usb_xbox_device.h"

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
#define USBDEV_NCM_NTH_BYTES               12u
#define USBDEV_NCM_MAX_FRAMES                6u
#define USBDEV_HID_REPORT_INPUT              1u
#define USBDEV_HID_REPORT_OUTPUT             2u
#define USBDEV_HID_REPORT_FEATURE            3u
#define USBDEV_XBOX_OS_VENDOR_CODE         0x20u
#define USBDEV_XBOX_COMPAT_ID_INDEX      0x0004u

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
__attribute__((aligned(4))) static uint8_t s_ep2_tx[USBDEV_ENDPOINT_BYTES];
__attribute__((aligned(4))) static uint8_t s_ep3_rx[USBDEV_INTERRUPT_BYTES];
__attribute__((aligned(4))) static uint8_t s_ep4_rx[USBDEV_INTERRUPT_BYTES];
__attribute__((aligned(4))) static uint8_t s_ep6_rx[USBDEV_INTERRUPT_BYTES];
__attribute__((aligned(4))) static uint8_t s_ep7_tx[USBDEV_INTERRUPT_BYTES];
__attribute__((aligned(4))) static uint8_t s_unused_tx[USBDEV_INTERRUPT_BYTES];

__attribute__((aligned(4))) static uint8_t
    s_ncm_rx_ntb[USB_NCM_NTB_MAX_BYTES];
__attribute__((aligned(4))) static uint8_t
    s_ncm_tx_ntb[USB_NCM_NTB_MAX_BYTES];

static uint8_t s_control_response[USBDEV_CONTROL_BUFFER_BYTES];
static uint8_t s_control_out[USBDEV_CONTROL_BUFFER_BYTES];
static uint8_t s_other_speed[USB_NCM_CONFIGURATION_BYTES];
static uint8_t s_xinput_string[256];
static uint8_t s_last_report[USBDEV_INTERRUPT_BYTES];
static uint8_t s_last_telemetry[USB_BOARD_TELEMETRY_FRAME_BYTES];
static uint8_t s_xbox_out[USB_XBOX_DEVICE_PACKET_BYTES];

static volatile uint8_t s_mounted;
static volatile uint8_t s_suspended;
static volatile uint8_t s_connected;
static volatile uint8_t s_initialized;
static volatile uint8_t s_ep1_busy;
static volatile uint8_t s_ep2_busy;
static volatile uint8_t s_ep7_busy;
static volatile uint8_t s_xbox_out_ready;
static volatile uint8_t s_xbox_out_length;
static volatile uint8_t s_address;
static volatile uint8_t s_configuration;
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

static volatile uint16_t s_ncm_rx_length;
static volatile uint16_t s_ncm_rx_expected;
static volatile uint8_t s_ncm_rx_ready;
static uint8_t s_ncm_rx_parsed;
static uint8_t s_ncm_frame_count;
static uint8_t s_ncm_frame_index;
static uint16_t s_ncm_frame_offsets[USBDEV_NCM_MAX_FRAMES];
static uint16_t s_ncm_frame_lengths[USBDEV_NCM_MAX_FRAMES];

static uint16_t s_ncm_tx_length;
static uint16_t s_ncm_tx_offset;
static uint16_t s_ncm_tx_sequence;
static uint8_t s_ncm_tx_active;
static uint8_t s_ncm_tx_need_zlp;

static uint32_t s_clock_last_cycles;
static uint32_t s_clock_remainder;
static uint32_t s_clock_millis;

USB_BOARD_STATIC_ASSERT(sizeof(xinput_device_descriptor) == 18u);
USB_BOARD_STATIC_ASSERT(sizeof(xinput_configuration_descriptor) == 0xB2u);
USB_BOARD_STATIC_ASSERT(sizeof(xinput_telemetry_hid_report_descriptor) == 21u);
USB_BOARD_STATIC_ASSERT(USB_XBOX_DEVICE_PACKET_BYTES <= USBDEV_INTERRUPT_BYTES);

static uint16_t load_u16_le(const uint8_t *source)
{
    return (uint16_t)source[0] |
           (uint16_t)((uint16_t)source[1] << 8);
}

static uint8_t profile_interface_count(void)
{
    if(s_profile == USB_BOARD_PROFILE_XINPUT)
    {
        return 5u;
    }
    if(s_profile == USB_BOARD_PROFILE_WEB_CONFIG)
    {
        return 2u;
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

static usb_ncm_speed_t current_ncm_speed(void)
{
    return ((R8_USB2_MIS_ST & USBHS_UDMS_HS_MOD) != 0u)
        ? USB_NCM_SPEED_HIGH
        : USB_NCM_SPEED_FULL;
}

static uint16_t current_bulk_packet_bytes(void)
{
    return (usb_ncm_speed() == USB_NCM_SPEED_HIGH)
        ? USB_NCM_ENDPOINT_HS_BYTES
        : USB_NCM_ENDPOINT_FS_BYTES;
}

static void sync_ncm_speed(void)
{
    const usb_ncm_speed_t speed = current_ncm_speed();
    if(usb_ncm_speed() != speed)
    {
        const bool link_up = usb_management_control_is_connected();
        usb_ncm_init(speed);
        usb_ncm_set_link_state(link_up);
    }
    /*
     * U2EPn_MAX_LEN is also the DMA receive bound.  Keep it aligned with the
     * descriptor selected for the negotiated bus speed instead of leaving a
     * full-speed 64-byte endpoint armed for 512-byte writes.
     */
    R32_U2EP2_MAX_LEN = (speed == USB_NCM_SPEED_HIGH)
        ? USB_NCM_ENDPOINT_HS_BYTES
        : USB_NCM_ENDPOINT_FS_BYTES;
}

static void ncm_receive_reset(void)
{
    s_ncm_rx_length = 0u;
    s_ncm_rx_expected = 0u;
    s_ncm_rx_ready = 0u;
    s_ncm_rx_parsed = 0u;
    s_ncm_frame_count = 0u;
    s_ncm_frame_index = 0u;
}

static void data_path_reset(void)
{
    s_xbox_out_ready = 0u;
    s_xbox_out_length = 0u;
    ncm_receive_reset();
    s_ncm_tx_length = 0u;
    s_ncm_tx_offset = 0u;
    s_ncm_tx_active = 0u;
    s_ncm_tx_need_zlp = 0u;
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
        sync_ncm_speed();
        switch(type)
        {
        case USB_DESCR_TYP_DEVICE:
            descriptor = usb_ncm_device_descriptor(&descriptor_length);
            break;
        case USB_DESCR_TYP_CONFIG:
            descriptor = usb_ncm_configuration_descriptor(
                usb_ncm_speed(), &descriptor_length);
            break;
        case USB_DESCR_TYP_SPEED:
        {
            const usb_ncm_speed_t other =
                (usb_ncm_speed() == USB_NCM_SPEED_HIGH)
                    ? USB_NCM_SPEED_FULL
                    : USB_NCM_SPEED_HIGH;
            descriptor = usb_ncm_configuration_descriptor(
                other, &descriptor_length);
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
            break;
        }
        case USB_DESCR_TYP_STRING:
            descriptor =
                usb_ncm_string_descriptor(number, &descriptor_length);
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
    else if((s_profile == USB_BOARD_PROFILE_WEB_CONFIG) &&
            (usb_ncm_data_alt_setting() == 1u))
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
        ep2_max = current_bulk_packet_bytes();
        tx_enable |= RB_EP1_EN | RB_EP2_EN;
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
    R32_U2EP2_TX_DMA = (uint32_t)s_ep2_tx;
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
    s_ep2_busy = 0u;
    s_ep7_busy = 0u;
    data_path_reset();
}

static bool ep1_send(const uint8_t *data, uint8_t length)
{
    if((data == 0) || (length == 0u) ||
       (length > sizeof(s_ep1_tx)) ||
       (s_mounted == 0u) || (s_suspended != 0u) ||
       (s_ep1_busy != 0u))
    {
        return false;
    }
    memcpy(s_ep1_tx, data, length);
    s_ep1_busy = 1u;
    R16_U2EP1_T_LEN = length;
    R8_U2EP1_TX_CTRL =
        (uint8_t)((R8_U2EP1_TX_CTRL &
                   (uint8_t)~USBHS_UEP_T_RES_MASK) |
                  USBHS_UEP_T_RES_ACK);
    return true;
}

static void ncm_tx_kick(void)
{
    uint16_t packet_size;
    uint16_t remaining;
    uint16_t packet;

    if((s_ncm_tx_active == 0u) || (s_ep2_busy != 0u) ||
       (s_mounted == 0u) || (s_suspended != 0u) ||
       (usb_ncm_data_alt_setting() != 1u) ||
       !usb_ncm_link_is_up())
    {
        return;
    }

    packet_size = current_bulk_packet_bytes();
    if(s_ncm_tx_offset < s_ncm_tx_length)
    {
        remaining =
            (uint16_t)(s_ncm_tx_length - s_ncm_tx_offset);
        packet = (remaining > packet_size) ? packet_size : remaining;
        memcpy(s_ep2_tx, &s_ncm_tx_ntb[s_ncm_tx_offset], packet);
        s_ncm_tx_offset = (uint16_t)(s_ncm_tx_offset + packet);
    }
    else if(s_ncm_tx_need_zlp != 0u)
    {
        packet = 0u;
        s_ncm_tx_need_zlp = 0u;
    }
    else
    {
        s_ncm_tx_active = 0u;
        return;
    }

    s_ep2_busy = 1u;
    R16_U2EP2_T_LEN = packet;
    R8_U2EP2_TX_CTRL =
        (uint8_t)((R8_U2EP2_TX_CTRL &
                   (uint8_t)~USBHS_UEP_T_RES_MASK) |
                  USBHS_UEP_T_RES_ACK);
}

static bool ncm_record_frame(const uint8_t *frame,
                             uint16_t length,
                             void *context)
{
    const ptrdiff_t offset = frame - s_ncm_rx_ntb;
    (void)context;

    if((s_ncm_frame_count >= USBDEV_NCM_MAX_FRAMES) ||
       (offset < 0) ||
       ((uint32_t)offset + length > s_ncm_rx_expected))
    {
        return false;
    }
    s_ncm_frame_offsets[s_ncm_frame_count] = (uint16_t)offset;
    s_ncm_frame_lengths[s_ncm_frame_count] = length;
    ++s_ncm_frame_count;
    return true;
}

static void ncm_rx_rearm(void)
{
    ncm_receive_reset();
    R8_U2EP2_RX_CTRL =
        (uint8_t)((R8_U2EP2_RX_CTRL &
                   (uint8_t)~USBHS_UEP_R_RES_MASK) |
                  ((usb_ncm_data_alt_setting() == 1u)
                       ? USBHS_UEP_R_RES_ACK
                       : USBHS_UEP_R_RES_NAK));
}

static void ncm_receive_packet(uint16_t length)
{
    uint16_t expected;

    if(length == 0u)
    {
        return;
    }
    if(((uint32_t)s_ncm_rx_length + length >
        sizeof(s_ncm_rx_ntb)) ||
       (s_ncm_rx_ready != 0u))
    {
        ncm_rx_rearm();
        return;
    }

    memcpy(&s_ncm_rx_ntb[s_ncm_rx_length], s_ep2_rx, length);
    s_ncm_rx_length = (uint16_t)(s_ncm_rx_length + length);
    if((s_ncm_rx_expected == 0u) &&
       (s_ncm_rx_length >= USBDEV_NCM_NTH_BYTES))
    {
        expected = load_u16_le(&s_ncm_rx_ntb[8]);
        if((expected < 28u) ||
           (expected > sizeof(s_ncm_rx_ntb)))
        {
            ncm_rx_rearm();
            return;
        }
        s_ncm_rx_expected = expected;
    }
    if((s_ncm_rx_expected != 0u) &&
       (s_ncm_rx_length >= s_ncm_rx_expected))
    {
        if(s_ncm_rx_length == s_ncm_rx_expected)
        {
            s_ncm_rx_ready = 1u;
            R8_U2EP2_RX_CTRL =
                (uint8_t)((R8_U2EP2_RX_CTRL &
                           (uint8_t)~USBHS_UEP_R_RES_MASK) |
                          USBHS_UEP_R_RES_NAK);
        }
        else
        {
            ncm_rx_rearm();
        }
    }
    else if(length < current_bulk_packet_bytes())
    {
        /* A short bulk packet terminates the USB transfer: partial NTBs fail
         * closed instead of holding EP2 in an ambiguous receive state. */
        ncm_rx_rearm();
    }
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

static bool handle_web_control(PUSB_SETUP_REQ setup)
{
    uint16_t response_length = 0u;
    const usb_ncm_control_result_t result =
        usb_ncm_handle_setup(
            (const usb_ncm_setup_packet_t *)setup,
            s_control_response,
            sizeof(s_control_response),
            &response_length);

    if(result == USB_NCM_CONTROL_DATA)
    {
        ep0_tx(s_control_response, response_length, setup->wLength);
        return true;
    }
    if(result == USB_NCM_CONTROL_STATUS)
    {
        if((setup->bRequest == USB_SET_INTERFACE) &&
           (setup->wIndex == 1u))
        {
            /*
             * SET_INTERFACE creates a fresh endpoint state even when the host
             * selects the same alternate setting again.  Cancel any pending
             * NTB and reset both data toggles to DATA0 before enabling alt 1.
             */
            ncm_receive_reset();
            s_ncm_tx_length = 0u;
            s_ncm_tx_offset = 0u;
            s_ncm_tx_active = 0u;
            s_ncm_tx_need_zlp = 0u;
            s_ep2_busy = 0u;
            R16_U2EP2_T_LEN = 0u;
            R8_U2EP2_TX_CTRL = USBHS_UEP_T_RES_NAK;
            R8_U2EP2_RX_CTRL =
                (usb_ncm_data_alt_setting() == 1u)
                    ? USBHS_UEP_R_RES_ACK
                    : USBHS_UEP_R_RES_NAK;
        }
        ep0_status_in();
        return true;
    }
    return false;
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

    if((s_profile == USB_BOARD_PROFILE_WEB_CONFIG) &&
       handle_web_control(setup))
    {
        return;
    }

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
            if(s_profile == USB_BOARD_PROFILE_WEB_CONFIG)
            {
                usb_ncm_reset();
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
    memset(s_last_report, 0, sizeof(s_last_report));
    memset(s_last_telemetry, 0, sizeof(s_last_telemetry));
    s_clock_last_cycles = SysTick->CNTL;
    s_clock_remainder = 0u;
    s_clock_millis = 0u;

    if(profile == USB_BOARD_PROFILE_WEB_CONFIG)
    {
        usb_ncm_init(USB_NCM_SPEED_HIGH);
        usb_ncm_set_link_state(false);
    }
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
    s_ep2_busy = 0u;
    s_ep7_busy = 0u;
    data_path_reset();
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

bool usb_device_hw_send_network_frame(const uint8_t *data,
                                      uint16_t length)
{
    if((data == 0) || (length == 0u) ||
       (length > USB_NCM_ETHERNET_FRAME_MAX_BYTES) ||
       (s_profile != USB_BOARD_PROFILE_WEB_CONFIG) ||
       (s_mounted == 0u) ||
       (usb_ncm_data_alt_setting() != 1u) ||
       !usb_ncm_link_is_up() ||
       (s_ncm_tx_active != 0u))
    {
        return false;
    }
    if(!usb_ncm_pack_frame(data,
                           length,
                           s_ncm_tx_sequence++,
                           s_ncm_tx_ntb,
                           sizeof(s_ncm_tx_ntb),
                           &s_ncm_tx_length))
    {
        return false;
    }
    s_ncm_tx_offset = 0u;
    s_ncm_tx_need_zlp =
        usb_ncm_transfer_needs_zlp(
            s_ncm_tx_length, usb_ncm_speed()) ? 1u : 0u;
    s_ncm_tx_active = 1u;
    ncm_tx_kick();
    return true;
}

void usb_device_hw_process(void)
{
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
        if((s_ep1_busy == 0u) && (s_mounted != 0u) &&
           usb_ncm_notification_pending())
        {
            uint8_t notification_length = 0u;
            if(usb_ncm_next_notification(
                    s_ep1_tx,
                    USB_NCM_NOTIFICATION_MAX_BYTES,
                    &notification_length))
            {
                s_ep1_busy = 1u;
                R16_U2EP1_T_LEN = notification_length;
                R8_U2EP1_TX_CTRL =
                    (uint8_t)((R8_U2EP1_TX_CTRL &
                               (uint8_t)~USBHS_UEP_T_RES_MASK) |
                              USBHS_UEP_T_RES_ACK);
            }
        }

        if(s_ncm_rx_ready != 0u)
        {
            if(s_ncm_rx_parsed == 0u)
            {
                uint8_t parsed_count = 0u;
                s_ncm_frame_count = 0u;
                s_ncm_frame_index = 0u;
                if(usb_ncm_unpack_ntb(
                       s_ncm_rx_ntb,
                       s_ncm_rx_expected,
                       ncm_record_frame,
                       0,
                       &parsed_count) != USB_NCM_PARSE_OK ||
                   (parsed_count != s_ncm_frame_count))
                {
                    ncm_rx_rearm();
                }
                else
                {
                    s_ncm_rx_parsed = 1u;
                }
            }
            if((s_ncm_rx_parsed != 0u) &&
               (s_ncm_frame_index < s_ncm_frame_count))
            {
                const uint8_t index = s_ncm_frame_index;
                if(usb_board_link_publish_bulk(
                       USB_BOARD_CHANNEL_NETWORK,
                       &s_ncm_rx_ntb[s_ncm_frame_offsets[index]],
                       s_ncm_frame_lengths[index]))
                {
                    ++s_ncm_frame_index;
                }
            }
            if((s_ncm_rx_parsed != 0u) &&
               (s_ncm_frame_index >= s_ncm_frame_count))
            {
                ncm_rx_rearm();
            }
        }
        ncm_tx_kick();
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
    if(s_profile == USB_BOARD_PROFILE_WEB_CONFIG)
    {
        usb_ncm_reset();
        usb_ncm_set_link_state(false);
    }
    data_path_reset();
    if(was_initialized != 0u)
    {
        PFIC_EnableIRQ(USB2_DEVICE_IRQn);
    }
}

void usb_management_control_hw_clear_fault(void)
{
    if(s_initialized == 0u)
    {
        return;
    }
    PFIC_DisableIRQ(USB2_DEVICE_IRQn);
    data_path_reset();
    endpoint_controls_reset();
    PFIC_EnableIRQ(USB2_DEVICE_IRQn);
}

static void complete_in_endpoint(uint8_t endpoint)
{
    switch(endpoint)
    {
    case 1u:
        R16_U2EP1_T_LEN = 0u;
        R8_U2EP1_TX_CTRL ^= USBHS_UEP_T_TOG_DATA1;
        R8_U2EP1_TX_CTRL =
            (uint8_t)((R8_U2EP1_TX_CTRL &
                       (uint8_t)~USBHS_UEP_T_RES_MASK) |
                      USBHS_UEP_T_RES_NAK);
        R8_U2EP1_TX_CTRL &= (uint8_t)~USBHS_UEP_T_DONE;
        s_ep1_busy = 0u;
        break;
    case 2u:
        R16_U2EP2_T_LEN = 0u;
        R8_U2EP2_TX_CTRL ^= USBHS_UEP_T_TOG_DATA1;
        R8_U2EP2_TX_CTRL =
            (uint8_t)((R8_U2EP2_TX_CTRL &
                       (uint8_t)~USBHS_UEP_T_RES_MASK) |
                      USBHS_UEP_T_RES_NAK);
        R8_U2EP2_TX_CTRL &= (uint8_t)~USBHS_UEP_T_DONE;
        s_ep2_busy = 0u;
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
        const uint16_t length = R16_U2EP2_RX_LEN;
        if(s_profile == USB_BOARD_PROFILE_WEB_CONFIG)
        {
            ncm_receive_packet(length);
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
        R8_U2EP2_RX_CTRL =
            (uint8_t)((R8_U2EP2_RX_CTRL &
                       (uint8_t)~USBHS_UEP_R_RES_MASK) |
                      (((s_profile == USB_BOARD_PROFILE_XBOX_ONE) &&
                        (s_xbox_out_ready != 0u)) ||
                       ((s_profile == USB_BOARD_PROFILE_WEB_CONFIG) &&
                        (s_ncm_rx_ready != 0u))
                           ? USBHS_UEP_R_RES_NAK
                           : USBHS_UEP_R_RES_ACK));
        R8_U2EP2_RX_CTRL &= (uint8_t)~USBHS_UEP_R_DONE;
    }
    else if(endpoint == 3u)
    {
        R8_U2EP3_RX_CTRL ^= USBHS_UEP_R_TOG_DATA1;
        R8_U2EP3_RX_CTRL =
            (uint8_t)((R8_U2EP3_RX_CTRL &
                       (uint8_t)~USBHS_UEP_R_RES_MASK) |
                      USBHS_UEP_R_RES_ACK);
        R8_U2EP3_RX_CTRL &= (uint8_t)~USBHS_UEP_R_DONE;
    }
    else if(endpoint == 4u)
    {
        R8_U2EP4_RX_CTRL ^= USBHS_UEP_R_TOG_DATA1;
        R8_U2EP4_RX_CTRL =
            (uint8_t)((R8_U2EP4_RX_CTRL &
                       (uint8_t)~USBHS_UEP_R_RES_MASK) |
                      USBHS_UEP_R_RES_ACK);
        R8_U2EP4_RX_CTRL &= (uint8_t)~USBHS_UEP_R_DONE;
    }
    else if(endpoint == 6u)
    {
        R8_U2EP6_RX_CTRL ^= USBHS_UEP_R_TOG_DATA1;
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
        if(s_profile == USB_BOARD_PROFILE_WEB_CONFIG)
        {
            usb_ncm_reset();
        }
        if(s_profile == USB_BOARD_PROFILE_XBOX_ONE)
        {
            usb_xbox_device_set_mounted(false, device_now_ms());
        }
        endpoints_init();
        R8_USB2_INT_FG = USBHS_UDIF_BUS_RST;
    }
    else if((flags & USBHS_UDIF_LINK_RDY) != 0u)
    {
        if(s_profile == USB_BOARD_PROFILE_WEB_CONFIG)
        {
            sync_ncm_speed();
        }
        R8_USB2_INT_FG = USBHS_UDIF_LINK_RDY;
    }
    else if((flags & USBHS_UDIF_SUSPEND) != 0u)
    {
        s_suspended =
            ((R8_USB2_MIS_ST & USBHS_UDMS_SUSPEND) != 0u)
                ? 1u
                : 0u;
        R8_USB2_INT_FG = USBHS_UDIF_SUSPEND;
    }
    else
    {
        R8_USB2_INT_FG = flags;
    }
}
