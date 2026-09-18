#include "usb_profiles.h"

#include <string.h>

typedef struct USB_BOARD_PACKED
{
    uint8_t command;
    uint8_t flags;
    uint8_t sequence;
    uint8_t length;
    uint8_t buttons0;
    uint8_t buttons1;
    uint16_t left_trigger;
    uint16_t right_trigger;
    int16_t left_stick_x;
    int16_t left_stick_y;
    int16_t right_stick_x;
    int16_t right_stick_y;
    uint8_t reserved[18];
} usb_xbox_one_input_report_t;

USB_BOARD_STATIC_ASSERT(USB_PROFILE_MAX_REPORT_BYTES >=
                        USB_PROFILE_PS4_REPORT_BYTES);
USB_BOARD_STATIC_ASSERT(sizeof(usb_xbox_one_input_report_t) ==
                        USB_PROFILE_XBOX_ONE_REPORT_BYTES);

static uint8_t dpad_hat(uint32_t actions, uint8_t neutral)
{
    const uint8_t up = (actions & (1ul << 0)) != 0u;
    const uint8_t down = (actions & (1ul << 1)) != 0u;
    const uint8_t left = (actions & (1ul << 2)) != 0u;
    const uint8_t right = (actions & (1ul << 3)) != 0u;

    if(up && right) return 1u;
    if(right && down) return 3u;
    if(down && left) return 5u;
    if(left && up) return 7u;
    if(up) return 0u;
    if(right) return 2u;
    if(down) return 4u;
    if(left) return 6u;
    return neutral;
}

uint16_t usb_profiles_xinput_buttons(uint32_t actions)
{
    uint16_t buttons = 0u;
    buttons |= (actions & (1ul << 0)) ? 0x0001u : 0u;
    buttons |= (actions & (1ul << 1)) ? 0x0002u : 0u;
    buttons |= (actions & (1ul << 2)) ? 0x0004u : 0u;
    buttons |= (actions & (1ul << 3)) ? 0x0008u : 0u;
    buttons |= (actions & (1ul << 12)) ? 0x0020u : 0u; /* back */
    buttons |= (actions & (1ul << 13)) ? 0x0010u : 0u; /* start */
    buttons |= (actions & (1ul << 14)) ? 0x0040u : 0u;
    buttons |= (actions & (1ul << 15)) ? 0x0080u : 0u;
    buttons |= (actions & (1ul << 8)) ? 0x0100u : 0u;
    buttons |= (actions & (1ul << 9)) ? 0x0200u : 0u;
    buttons |= (actions & (1ul << 16)) ? 0x0400u : 0u;
    buttons |= (actions & (1ul << 4)) ? 0x1000u : 0u;
    buttons |= (actions & (1ul << 5)) ? 0x2000u : 0u;
    buttons |= (actions & (1ul << 6)) ? 0x4000u : 0u;
    buttons |= (actions & (1ul << 7)) ? 0x8000u : 0u;
    return buttons;
}

static bool build_xinput(const usb_board_input_v1_t *input,
                         usb_profile_report_t *report)
{
    const uint32_t actions = input->action_mask_le;
    const uint16_t buttons = usb_profiles_xinput_buttons(actions);

    memset(report, 0, sizeof(*report));
    report->length = USB_PROFILE_XINPUT_REPORT_BYTES;
    report->bytes[0] = 0x00u;
    report->bytes[1] = 0x14u;
    report->bytes[2] = (uint8_t)buttons;
    report->bytes[3] = (uint8_t)(buttons >> 8);
    report->bytes[4] = (actions & (1ul << 10)) ? 0xFFu : 0u;
    report->bytes[5] = (actions & (1ul << 11)) ? 0xFFu : 0u;
    return true;
}

static bool build_ps4_compat(const usb_board_input_v1_t *input,
                             usb_profile_report_t *report)
{
    const uint32_t actions = input->action_mask_le;

    memset(report, 0, sizeof(*report));
    report->length = USB_PROFILE_PS4_REPORT_BYTES;
    report->bytes[0] = 0x01u;
    report->bytes[1] = 0x80u;
    report->bytes[2] = 0x80u;
    report->bytes[3] = 0x80u;
    report->bytes[4] = 0x80u;
    report->bytes[5] = dpad_hat(actions, 0x08u);
    report->bytes[5] |= (actions & (1ul << 4)) ? 0x20u : 0u;
    report->bytes[5] |= (actions & (1ul << 5)) ? 0x40u : 0u;
    report->bytes[5] |= (actions & (1ul << 6)) ? 0x10u : 0u;
    report->bytes[5] |= (actions & (1ul << 7)) ? 0x80u : 0u;
    report->bytes[6] |= (actions & (1ul << 8)) ? 0x01u : 0u;
    report->bytes[6] |= (actions & (1ul << 9)) ? 0x02u : 0u;
    report->bytes[6] |= (actions & (1ul << 10)) ? 0x04u : 0u;
    report->bytes[6] |= (actions & (1ul << 11)) ? 0x08u : 0u;
    report->bytes[6] |= (actions & (1ul << 12)) ? 0x10u : 0u;
    report->bytes[6] |= (actions & (1ul << 13)) ? 0x20u : 0u;
    report->bytes[6] |= (actions & (1ul << 14)) ? 0x40u : 0u;
    report->bytes[6] |= (actions & (1ul << 15)) ? 0x80u : 0u;
    report->bytes[7] |= (actions & (1ul << 16)) ? 0x01u : 0u;
    report->bytes[7] |= (actions & (1ul << 17)) ? 0x02u : 0u;
    report->bytes[8] = (actions & (1ul << 10)) ? 0xFFu : 0u;
    report->bytes[9] = (actions & (1ul << 11)) ? 0xFFu : 0u;

    /* Match PS4Driver's plugged-in power state and centered touch contacts. */
    report->bytes[30] = 0x1Bu;
    report->bytes[33] = (actions & (1ul << 17)) ? 0x01u : 0u;
    report->bytes[35] = (actions & (1ul << 17)) ? 0x00u : 0x80u;
    report->bytes[36] = 0xC0u;
    report->bytes[37] = 0x73u;
    report->bytes[38] = 0x1Du;
    report->bytes[39] = 0x80u;
    report->bytes[40] = 0xC0u;
    report->bytes[41] = 0x73u;
    report->bytes[42] = 0x1Du;
    return true;
}

static uint16_t switch_buttons(uint32_t actions)
{
    uint16_t buttons = 0u;
    buttons |= (actions & (1ul << 4)) ? (1u << 1) : 0u;  /* B */
    buttons |= (actions & (1ul << 5)) ? (1u << 2) : 0u;  /* A */
    buttons |= (actions & (1ul << 6)) ? (1u << 0) : 0u;  /* Y */
    buttons |= (actions & (1ul << 7)) ? (1u << 3) : 0u;  /* X */
    buttons |= (actions & (1ul << 8)) ? (1u << 4) : 0u;  /* L */
    buttons |= (actions & (1ul << 9)) ? (1u << 5) : 0u;  /* R */
    buttons |= (actions & (1ul << 10)) ? (1u << 6) : 0u; /* ZL */
    buttons |= (actions & (1ul << 11)) ? (1u << 7) : 0u; /* ZR */
    buttons |= (actions & (1ul << 12)) ? (1u << 8) : 0u;
    buttons |= (actions & (1ul << 13)) ? (1u << 9) : 0u;
    buttons |= (actions & (1ul << 14)) ? (1u << 10) : 0u;
    buttons |= (actions & (1ul << 15)) ? (1u << 11) : 0u;
    buttons |= (actions & (1ul << 16)) ? (1u << 12) : 0u;
    buttons |= (actions & (1ul << 17)) ? (1u << 13) : 0u;
    return buttons;
}

static bool build_switch(const usb_board_input_v1_t *input,
                         usb_profile_report_t *report)
{
    const uint32_t actions = input->action_mask_le;
    const uint16_t buttons = switch_buttons(actions);

    memset(report, 0, sizeof(*report));
    report->length = USB_PROFILE_SWITCH_REPORT_BYTES;
    report->bytes[0] = (uint8_t)buttons;
    report->bytes[1] = (uint8_t)(buttons >> 8);
    report->bytes[2] = dpad_hat(actions, 0x08u);
    report->bytes[3] = 0x80u;
    report->bytes[4] = 0x80u;
    report->bytes[5] = 0x80u;
    report->bytes[6] = 0x80u;
    return true;
}

static bool build_xbox_one(const usb_board_input_v1_t *input,
                           usb_profile_report_t *report)
{
    const uint32_t actions = input->action_mask_le;
    usb_xbox_one_input_report_t *gip;

    memset(report, 0, sizeof(*report));
    report->length = USB_PROFILE_XBOX_ONE_REPORT_BYTES;
    gip = (usb_xbox_one_input_report_t *)report->bytes;

    gip->command = 0x20u; /* GIP_INPUT_REPORT */
    gip->flags = 0u;
    gip->sequence = (input->seq == 0u) ? 1u : input->seq;
    gip->length = (uint8_t)(sizeof(*gip) - 4u);

    /*
     * The Guide button is emitted by the GIP virtual-key state machine, not
     * inside the regular 0x20 report.  This mirrors XBOneDriver::process().
     */
    gip->buttons0 |= (actions & (1ul << 13)) ? (1u << 2) : 0u; /* Menu */
    gip->buttons0 |= (actions & (1ul << 12)) ? (1u << 3) : 0u; /* View */
    gip->buttons0 |= (actions & (1ul << 4)) ? (1u << 4) : 0u;  /* A */
    gip->buttons0 |= (actions & (1ul << 5)) ? (1u << 5) : 0u;  /* B */
    gip->buttons0 |= (actions & (1ul << 6)) ? (1u << 6) : 0u;  /* X */
    gip->buttons0 |= (actions & (1ul << 7)) ? (1u << 7) : 0u;  /* Y */

    gip->buttons1 |= (actions & (1ul << 0)) ? (1u << 0) : 0u;
    gip->buttons1 |= (actions & (1ul << 1)) ? (1u << 1) : 0u;
    gip->buttons1 |= (actions & (1ul << 2)) ? (1u << 2) : 0u;
    gip->buttons1 |= (actions & (1ul << 3)) ? (1u << 3) : 0u;
    gip->buttons1 |= (actions & (1ul << 8)) ? (1u << 4) : 0u;
    gip->buttons1 |= (actions & (1ul << 9)) ? (1u << 5) : 0u;
    gip->buttons1 |= (actions & (1ul << 14)) ? (1u << 6) : 0u;
    gip->buttons1 |= (actions & (1ul << 15)) ? (1u << 7) : 0u;
    gip->left_trigger = (actions & (1ul << 10)) ? 0x03FFu : 0u;
    gip->right_trigger = (actions & (1ul << 11)) ? 0x03FFu : 0u;
    return true;
}

bool usb_profiles_is_supported(usb_board_profile_t profile)
{
    switch(profile)
    {
    case USB_BOARD_PROFILE_XINPUT:
    case USB_BOARD_PROFILE_PS4:
    case USB_BOARD_PROFILE_PS5_COMPAT:
    case USB_BOARD_PROFILE_SWITCH:
    case USB_BOARD_PROFILE_XBOX_ONE:
    case USB_BOARD_PROFILE_WEB_CONFIG:
        return true;
    default:
        return false;
    }
}

uint16_t usb_profiles_capability_flags(void)
{
    return USB_BOARD_CAP_PROFILE_XINPUT |
           USB_BOARD_CAP_PROFILE_PS4 |
           USB_BOARD_CAP_PROFILE_PS5_COMPAT |
           USB_BOARD_CAP_PROFILE_SWITCH |
           USB_BOARD_CAP_PROFILE_XBOX_ONE |
           USB_BOARD_CAP_PROFILE_WEB_CONFIG;
}

bool usb_profiles_build_report(usb_board_profile_t profile,
                               const usb_board_input_v1_t *input,
                               usb_profile_report_t *report)
{
    if((input == 0) || (report == 0))
    {
        return false;
    }

    switch(profile)
    {
    case USB_BOARD_PROFILE_XINPUT:
        return build_xinput(input, report);
    case USB_BOARD_PROFILE_PS4:
    case USB_BOARD_PROFILE_PS5_COMPAT:
        /* PS5 compatibility intentionally retains PS4 arcade-stick semantics. */
        return build_ps4_compat(input, report);
    case USB_BOARD_PROFILE_SWITCH:
        return build_switch(input, report);
    case USB_BOARD_PROFILE_XBOX_ONE:
        return build_xbox_one(input, report);
    case USB_BOARD_PROFILE_WEB_CONFIG:
        memset(report, 0, sizeof(*report));
        return true;
    default:
        return false;
    }
}
