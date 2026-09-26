#ifndef TX_USB_XBOX_DEVICE_H
#define TX_USB_XBOX_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_XBOX_DEVICE_PACKET_BYTES 64u
#define USB_XBOX_DEVICE_INPUT_BYTES 36u

/*
 * Console-facing Xbox One/GIP state machine.
 *
 * All timing is supplied by the caller in monotonic milliseconds.  OUT and
 * next_in are intended to be called by the USB Device endpoint owner; process
 * may run from the normal CH585 USB service loop.
 */
void usb_xbox_device_init(void);
void usb_xbox_device_reset(void);
void usb_xbox_device_set_mounted(bool mounted, uint32_t now_ms);
void usb_xbox_device_set_actions(uint32_t action_mask);
void usb_xbox_device_process(uint32_t now_ms);
bool usb_xbox_device_out(const uint8_t *packet, uint8_t length);
bool usb_xbox_device_next_in(uint8_t *packet,
                             uint8_t capacity,
                             uint8_t *length);

#ifdef __cplusplus
}
#endif

#endif
