#ifndef TX_USB_HOST_H
#define TX_USB_HOST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define USB_HOST_INTERRUPT_MAX_BYTES 64u
#define USB_HOST_CONTROL_MAX_BYTES 512u
#define USB_HOST_DESCRIPTOR_MAX_BYTES 512u
#define USB_HOST_MAX_NAK_RETRY_20US 250u
#define USB_HOST_MAX_INTERFACES 8u
#define USB_HOST_INTERFACE_ANY 0xFFu

    typedef struct
    {
        uint8_t device_address;
        uint8_t hid_instance;
        uint8_t ps_auth;
        uint32_t generation;
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
    bool usb_host_get_interface(uint8_t index, usb_host_interface_t *interface_info);
    bool usb_host_find_interface(uint8_t class_code, uint8_t subclass, uint8_t protocol,
                                 usb_host_interface_t *interface_info);
    bool usb_host_find_auth_interface(usb_host_auth_interface_t auth_interface,
                                      usb_host_interface_t *interface_info);
    uint16_t usb_host_configuration_length(void);
    bool usb_host_copy_configuration(uint8_t *data, uint16_t capacity, uint16_t *length);

/* Polling facade over asynchronous TinyUSB transfers. PENDING is not failure.
 * A repeated identical request consumes its completion; buffers are owned here.
 * Binding and cancellation are main-loop-only, never called from Device ISR. */
#define USB_HOST_PENDING 0xFEu
#define USB_HOST_TIMEOUT 0xFDu
#define USB_HOST_STALL 0xFCu
    bool usb_host_select(const usb_host_interface_t *interface_info);
    bool usb_host_interface_alive(const usb_host_interface_t *interface_info);
    void usb_host_cancel(void);
    uint32_t usb_host_session(void);
    uint8_t usb_host_control_transfer(const uint8_t setup[8], uint8_t *data, uint16_t capacity,
                                      uint16_t *actual);
    uint8_t usb_host_control_transfer_descriptor(const uint8_t setup[8], uint8_t *data,
                                                 uint16_t capacity, uint16_t *actual);
    uint8_t usb_host_interrupt_in(uint8_t endpoint, uint8_t *data, uint16_t capacity,
                                  uint16_t *actual, uint16_t unused);
    uint8_t usb_host_interrupt_out(uint8_t endpoint, const uint8_t *data, uint16_t length,
                                   uint16_t unused);
    uint16_t usb_host_diag(uint8_t page, uint8_t index, uint8_t *out, uint16_t capacity);
#ifdef __cplusplus
}
#endif
#endif
