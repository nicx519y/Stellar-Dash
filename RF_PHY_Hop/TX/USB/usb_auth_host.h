#ifndef TX_USB_AUTH_HOST_H
#define TX_USB_AUTH_HOST_H

#include <stdbool.h>
#include <stdint.h>

#include "usb_auth.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    USB_AUTH_ENGINE_IDLE = 0,
    USB_AUTH_ENGINE_WAIT_DEVICE,
    USB_AUTH_ENGINE_RUNNING,
    USB_AUTH_ENGINE_AUTHENTICATED,
    USB_AUTH_ENGINE_FAILED
} usb_auth_engine_state_t;

void usb_auth_host_engine_init(void);
void usb_auth_host_engine_clear(void);
bool usb_auth_host_engine_begin(usb_auth_scheme_t scheme);
void usb_auth_host_engine_process(void);
usb_auth_engine_state_t usb_auth_host_engine_state(void);
usb_auth_error_t usb_auth_host_engine_error(void);
bool usb_auth_host_engine_device_ready(void);
bool usb_auth_host_engine_hid_set_feature(uint8_t report_id,
                                          const uint8_t *data,
                                          uint16_t length);
bool usb_auth_host_engine_hid_get_feature(uint8_t report_id,
                                          uint8_t *data,
                                          uint16_t capacity,
                                          uint16_t *length);
bool usb_auth_host_engine_vendor_out(uint8_t request,
                                     uint16_t value,
                                     uint16_t index,
                                     const uint8_t *data,
                                     uint16_t length);
bool usb_auth_host_engine_vendor_in(uint8_t request,
                                    uint16_t value,
                                    uint16_t index,
                                    uint8_t *data,
                                    uint16_t capacity,
                                    uint16_t *length);
bool usb_auth_host_engine_gip_device_out(const uint8_t *report,
                                         uint8_t length);
bool usb_auth_host_engine_gip_device_in(uint8_t *report,
                                        uint8_t capacity,
                                        uint8_t *length);
bool usb_auth_host_engine_gip_device_in_pending(void);

#ifdef __cplusplus
}
#endif

#endif
