#pragma once
#include <stdbool.h>
#include "usb_gip_protocol.h"
#define AUTH_GIP_QUEUE_SLOTS 24u
typedef struct
{
    uint8_t data[USB_GIP_PACKET_MAX_BYTES];
    uint8_t length;
} auth_gip_frame_t;

typedef struct
{
    auth_gip_frame_t frames[AUTH_GIP_QUEUE_SLOTS];
    volatile uint8_t head;
    volatile uint8_t tail;
} auth_gip_queue_t;

typedef union
{
    struct
    {
        uint8_t nonce[256], signature[1064];
    } ps;
    struct
    {
        uint8_t initial[34], serial[29], request[46], reply[46];
    } x360;
    struct
    {
        auth_gip_queue_t device_rx, device_tx, host_tx;
        usb_gip_rx_t device_parser, host_parser;
        usb_gip_tx_t tx;
    } gip;
    uint8_t webhid_out[8][1024];
} usb_mode_memory_t;

extern usb_mode_memory_t g_usb_mode_memory;
/* Called with Device disconnected, before init. No SPI buffers are overlaid. */
void usb_mode_memory_reset(bool web);
bool usb_mode_memory_is_web(void);
