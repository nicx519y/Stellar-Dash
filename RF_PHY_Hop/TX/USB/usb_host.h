#ifndef TX_USB_HOST_H
#define TX_USB_HOST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The WCH USBFS host transaction helper uses a 20 us NAK retry unit.  Keep
 * public interrupt transfers bounded to 5 ms even if a caller supplies a
 * larger value.
 */
#define USB_HOST_INTERRUPT_MAX_BYTES        64u
#define USB_HOST_CONTROL_MAX_BYTES         255u
#define USB_HOST_DESCRIPTOR_MAX_BYTES      512u
#define USB_HOST_MAX_NAK_RETRY_20US        250u
#define USB_HOST_MAX_INTERFACES               8u
#define USB_HOST_INTERFACE_ANY              0xFFu

typedef struct
{
    uint8_t number;
    uint8_t alternate_setting;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t protocol;
    uint8_t interrupt_in_endpoint;
    uint8_t interrupt_out_endpoint;
    uint16_t interrupt_in_max_packet;
    uint16_t interrupt_out_max_packet;
    uint16_t hid_report_descriptor_length;
} usb_host_interface_t;

typedef enum
{
    USB_HOST_AUTH_INTERFACE_PS4 = 0,
    USB_HOST_AUTH_INTERFACE_XINPUT,
    USB_HOST_AUTH_INTERFACE_XBOX_GIP
} usb_host_auth_interface_t;

typedef enum
{
    USB_HOST_STATE_OFF = 0,
    USB_HOST_STATE_WAIT_DEVICE,
    USB_HOST_STATE_ATTACH_DEBOUNCE,
    USB_HOST_STATE_ENUMERATING,
    USB_HOST_STATE_READY,
    USB_HOST_STATE_RECOVERY_WAIT,
    USB_HOST_STATE_FAILED
} usb_host_state_t;

typedef struct
{
    usb_host_state_t state;
    uint8_t controller_ready;
    uint8_t attached;
    uint8_t enumerated;
    uint8_t enumeration_attempts;
    uint8_t current_fault;
    uint8_t last_error;
    uint8_t device_address;
    uint8_t device_speed;
    uint8_t device_type;
    uint8_t interrupt_in_endpoint;
    uint8_t interrupt_out_endpoint;
    uint16_t vid;
    uint16_t pid;
    uint32_t generation;
    uint32_t fault_generation;
} usb_host_snapshot_t;

bool usb_host_init(void);
void usb_host_shutdown(void);
void usb_host_process(void);
bool usb_host_is_ready(void);
bool usb_host_is_attached(void);
bool usb_host_is_enumerated(void);
uint8_t usb_host_last_fault(void);
uint8_t usb_host_last_error(void);
usb_host_state_t usb_host_state(void);
uint32_t usb_host_generation(void);
uint32_t usb_host_fault_generation(void);
bool usb_host_get_snapshot(usb_host_snapshot_t *snapshot);
uint8_t usb_host_interface_count(void);
bool usb_host_get_interface(uint8_t index,
                            usb_host_interface_t *interface_info);
bool usb_host_find_interface(uint8_t class_code,
                             uint8_t subclass,
                             uint8_t protocol,
                             usb_host_interface_t *interface_info);
bool usb_host_find_auth_interface(usb_host_auth_interface_t auth_interface,
                                  usb_host_interface_t *interface_info);
uint16_t usb_host_configuration_length(void);
bool usb_host_copy_configuration(uint8_t *data,
                                 uint16_t capacity,
                                 uint16_t *length);

/*
 * Execute a root-device control transfer. setup must contain the standard
 * eight-byte USB setup packet in wire byte order. The request wLength must not
 * exceed data_capacity or USB_HOST_CONTROL_MAX_BYTES.
 *
 * Return value is the WCH ERR_* / USB transfer status. The operation is
 * synchronous but bounded by the SDK's per-stage 200 ms timeout.
 */
uint8_t usb_host_control_transfer(const uint8_t setup[8],
                                  uint8_t *data,
                                  uint8_t data_capacity,
                                  uint8_t *transferred);

/*
 * Descriptor-sized EP0 transfer. The WCH SDK's HostCtrlTransfer() reports its
 * byte count through uint8_t and therefore wraps for descriptors over 255
 * bytes. This wrapper keeps a uint16_t count and is intentionally capped at
 * USB_HOST_DESCRIPTOR_MAX_BYTES.
 */
uint8_t usb_host_control_transfer_descriptor(const uint8_t setup[8],
                                             uint8_t *data,
                                             uint16_t data_capacity,
                                             uint16_t *transferred);

/*
 * Root-device interrupt endpoint helpers. A zero NAK retry budget performs a
 * single polling transaction. Successful transfers advance the endpoint data
 * toggle locally on CH585; callers must not maintain a second toggle.
 */
uint8_t usb_host_interrupt_in(uint8_t endpoint_address,
                              uint8_t *data,
                              uint8_t capacity,
                              uint8_t *transferred,
                              uint16_t nak_retry_20us);
uint8_t usb_host_interrupt_out(uint8_t endpoint_address,
                               const uint8_t *data,
                               uint8_t length,
                               uint16_t nak_retry_20us);

#ifdef __cplusplus
}
#endif

#endif
