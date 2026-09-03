#ifndef TX_USB_NCM_H
#define TX_USB_NCM_H

#include <stdbool.h>
#include <stdint.h>

#include "usb_board_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * These values match the former STM32 TinyUSB WebConfig profile:
 * application/Cpp_Core/Inc/tusb_config.h
 * application/Cpp_Core/Src/drivers/net/NetDriver.cpp
 * application/Libs/tinyusb/src/class/net/ncm_device.c
 */
#define USB_NCM_EP0_BYTES                 64u
#define USB_NCM_ENDPOINT_NOTIFICATION_IN  0x81u
#define USB_NCM_ENDPOINT_DATA_OUT         0x02u
#define USB_NCM_ENDPOINT_DATA_IN          0x82u
#define USB_NCM_ENDPOINT_FS_BYTES         64u
#define USB_NCM_ENDPOINT_HS_BYTES         512u
#define USB_NCM_ETHERNET_FRAME_MAX_BYTES  1514u
#define USB_NCM_TCP_MSS                   1460u
#define USB_NCM_NTB_MAX_BYTES             ((2u * USB_NCM_TCP_MSS) + 100u)
#define USB_NCM_MAX_DATAGRAMS_PER_NTB     6u
#define USB_NCM_DEVICE_DESCRIPTOR_BYTES   18u
#define USB_NCM_CONFIGURATION_BYTES       94u
#define USB_NCM_NTB_PARAMETERS_BYTES      28u
#define USB_NCM_NOTIFICATION_MAX_BYTES    16u

typedef enum
{
    USB_NCM_SPEED_FULL = USB_BOARD_USB_SPEED_FULL,
    USB_NCM_SPEED_HIGH = USB_BOARD_USB_SPEED_HIGH
} usb_ncm_speed_t;

typedef enum
{
    USB_NCM_CONTROL_STALL = 0,
    USB_NCM_CONTROL_STATUS,
    USB_NCM_CONTROL_DATA
} usb_ncm_control_result_t;

typedef enum
{
    USB_NCM_PARSE_OK = 0,
    USB_NCM_PARSE_INVALID_ARGUMENT,
    USB_NCM_PARSE_BAD_NTH,
    USB_NCM_PARSE_BAD_LENGTH,
    USB_NCM_PARSE_BAD_NDP,
    USB_NCM_PARSE_TOO_MANY_DATAGRAMS,
    USB_NCM_PARSE_BAD_DATAGRAM,
    USB_NCM_PARSE_SINK_REJECTED
} usb_ncm_parse_result_t;

#if defined(__GNUC__)
#define USB_NCM_PACKED __attribute__((packed))
#else
#define USB_NCM_PACKED
#endif

typedef struct USB_NCM_PACKED
{
    uint8_t bm_request_type;
    uint8_t b_request;
    uint16_t w_value_le;
    uint16_t w_index_le;
    uint16_t w_length_le;
} usb_ncm_setup_packet_t;

typedef struct USB_NCM_PACKED
{
    uint16_t length_le;
    uint16_t formats_supported_le;
    uint32_t ntb_in_max_size_le;
    uint16_t ndb_in_divisor_le;
    uint16_t ndb_in_payload_remainder_le;
    uint16_t ndb_in_alignment_le;
    uint16_t reserved_le;
    uint32_t ntb_out_max_size_le;
    uint16_t ndb_out_divisor_le;
    uint16_t ndb_out_payload_remainder_le;
    uint16_t ndb_out_alignment_le;
    uint16_t ntb_out_max_datagrams_le;
} usb_ncm_ntb_parameters_t;

typedef bool (*usb_ncm_frame_sink_t)(const uint8_t *frame,
                                     uint16_t length,
                                     void *context);

void usb_ncm_init(usb_ncm_speed_t speed);
void usb_ncm_reset(void);

const uint8_t *usb_ncm_device_descriptor(uint16_t *length);
const uint8_t *usb_ncm_configuration_descriptor(usb_ncm_speed_t speed,
                                                 uint16_t *length);
const uint8_t *usb_ncm_string_descriptor(uint8_t index, uint16_t *length);
const usb_ncm_ntb_parameters_t *usb_ncm_ntb_parameters(void);

bool usb_ncm_set_mac(const uint8_t mac[6]);
void usb_ncm_get_mac(uint8_t mac[6]);

usb_ncm_control_result_t usb_ncm_handle_setup(
    const usb_ncm_setup_packet_t *setup,
    uint8_t *response,
    uint16_t response_capacity,
    uint16_t *response_length);

void usb_ncm_set_link_state(bool link_up);
bool usb_ncm_link_is_up(void);
uint8_t usb_ncm_data_alt_setting(void);
usb_ncm_speed_t usb_ncm_speed(void);

bool usb_ncm_notification_pending(void);
bool usb_ncm_next_notification(uint8_t *buffer,
                               uint8_t capacity,
                               uint8_t *length);

bool usb_ncm_pack_frame(const uint8_t *frame,
                        uint16_t frame_length,
                        uint16_t sequence,
                        uint8_t *ntb,
                        uint16_t ntb_capacity,
                        uint16_t *ntb_length);

usb_ncm_parse_result_t usb_ncm_unpack_ntb(const uint8_t *ntb,
                                          uint16_t ntb_length,
                                          usb_ncm_frame_sink_t sink,
                                          void *context,
                                          uint8_t *frame_count);

bool usb_ncm_transfer_needs_zlp(uint16_t transfer_length,
                                usb_ncm_speed_t speed);

#ifdef __cplusplus
}
#endif

#endif
