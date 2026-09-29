#ifndef TX_USB_PROFILES_H
#define TX_USB_PROFILES_H

#include <stdbool.h>
#include <stdint.h>

#include "usb_board_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_PROFILE_MAX_REPORT_BYTES 64u
#define USB_PROFILE_XINPUT_REPORT_BYTES 20u
#define USB_PROFILE_PS4_REPORT_BYTES 64u
#define USB_PROFILE_SWITCH_REPORT_BYTES 8u
#define USB_PROFILE_XBOX_ONE_REPORT_BYTES 36u

typedef struct
{
    uint8_t bytes[USB_PROFILE_MAX_REPORT_BYTES];
    uint8_t length;
} usb_profile_report_t;

bool usb_profiles_is_supported(usb_board_profile_t profile);
uint16_t usb_profiles_capability_flags(void);
bool usb_profiles_build_report(usb_board_profile_t profile,
                               const usb_board_input_v1_t *input,
                               usb_profile_report_t *report);
uint16_t usb_profiles_xinput_buttons(uint32_t actions);

#ifdef __cplusplus
}
#endif

#endif
