#ifndef TX_USB_AUTH_H
#define TX_USB_AUTH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_AUTH_CAP_BLOB_STAGING    (1ul << 0)
#define USB_AUTH_CAP_PS4             (1ul << 1)
#define USB_AUTH_CAP_XINPUT          (1ul << 2)
#define USB_AUTH_CAP_XBOX_GIP        (1ul << 3)
#define USB_AUTH_BLOB_MAX_BYTES      1024u

typedef enum
{
    USB_AUTH_SCHEME_NONE = 0,
    USB_AUTH_SCHEME_PS4,
    USB_AUTH_SCHEME_XINPUT,
    USB_AUTH_SCHEME_XBOX_GIP
} usb_auth_scheme_t;

typedef enum
{
    USB_AUTH_STATE_EMPTY = 0,
    USB_AUTH_STATE_RECEIVING,
    USB_AUTH_STATE_BLOB_READY,
    USB_AUTH_STATE_UNSUPPORTED,
    USB_AUTH_STATE_FAILED,
    USB_AUTH_STATE_AUTHENTICATED,
    USB_AUTH_STATE_WAIT_DEVICE,
    USB_AUTH_STATE_RUNNING
} usb_auth_state_t;

typedef enum
{
    USB_AUTH_ERROR_NONE = 0,
    USB_AUTH_ERROR_INVALID_ARGUMENT,
    USB_AUTH_ERROR_OUT_OF_ORDER,
    USB_AUTH_ERROR_OVERFLOW,
    USB_AUTH_ERROR_UNSUPPORTED,
    USB_AUTH_ERROR_NO_COMPLETE_BLOB,
    USB_AUTH_ERROR_NOT_READY,
    USB_AUTH_ERROR_WRONG_DEVICE,
    USB_AUTH_ERROR_HOST_TRANSFER,
    USB_AUTH_ERROR_TIMEOUT,
    USB_AUTH_ERROR_PROTOCOL,
    USB_AUTH_ERROR_QUEUE_FULL
} usb_auth_error_t;

typedef struct
{
    usb_auth_state_t state;
    usb_auth_scheme_t scheme;
    uint8_t transaction;
    uint8_t complete;
    uint16_t blob_length;
    usb_auth_error_t last_error;
    uint32_t capabilities;
    uint32_t generation;
} usb_auth_snapshot_t;

void usb_auth_init(void);
void usb_auth_clear(void);
bool usb_auth_write_blob(uint8_t transaction,
                         uint16_t offset,
                         const uint8_t *data,
                         uint8_t length,
                         bool final_fragment);
bool usb_auth_read_blob(uint8_t transaction,
                        uint16_t offset,
                        uint8_t *data,
                        uint8_t capacity,
                        uint8_t *length);
uint32_t usb_auth_capabilities(void);
bool usb_auth_scheme_supported(usb_auth_scheme_t scheme);
bool usb_auth_begin(usb_auth_scheme_t scheme, uint8_t transaction);
void usb_auth_process(void);
bool usb_auth_is_authenticated(usb_auth_scheme_t scheme);
bool usb_auth_host_device_ready(void);
bool usb_auth_device_hid_set_feature(uint8_t report_id,
                                     const uint8_t *data,
                                     uint16_t length);
bool usb_auth_device_hid_get_feature(uint8_t report_id,
                                     uint8_t *data,
                                     uint16_t capacity,
                                     uint16_t *length);
bool usb_auth_device_vendor_out(uint8_t request,
                                uint16_t value,
                                uint16_t index,
                                const uint8_t *data,
                                uint16_t length);
bool usb_auth_device_vendor_in(uint8_t request,
                               uint16_t value,
                               uint16_t index,
                               uint8_t *data,
                               uint16_t capacity,
                               uint16_t *length);
bool usb_auth_gip_device_out(const uint8_t *report, uint8_t length);
bool usb_auth_gip_device_in(uint8_t *report,
                            uint8_t capacity,
                            uint8_t *length);
bool usb_auth_gip_device_in_pending(void);
usb_auth_state_t usb_auth_state(void);
usb_auth_error_t usb_auth_last_error(void);
uint32_t usb_auth_generation(void);
bool usb_auth_get_snapshot(usb_auth_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif
