#ifndef USB_ENDPOINT_RESET_CONTROL_H
#define USB_ENDPOINT_RESET_CONTROL_H

#include <stdint.h>

/*
 * The CH585 USBHS endpoint DATA toggle is advanced by software after DONE.
 * A transport reset may deliberately discard the completed payload, but it
 * must still consume the DONE indication or the host and device will disagree
 * about the next DATA PID.
 *
 * required_mask is zero for IN endpoints. For OUT endpoints it is the
 * hardware TOG_MATCH bit, so a duplicate packet clears DONE without advancing
 * the expected DATA PID.
 *
 * Call this only after the endpoint has been disabled and the SIE is idle.
 */
static inline uint8_t usb_endpoint_reset_control(
    uint8_t control,
    uint8_t done_mask,
    uint8_t required_mask,
    uint8_t data_toggle_mask,
    uint8_t idle_response)
{
    if(((control & done_mask) != 0u) &&
       ((control & required_mask) == required_mask))
    {
        control ^= data_toggle_mask;
    }

    return (uint8_t)((control & data_toggle_mask) | idle_response);
}

#endif
