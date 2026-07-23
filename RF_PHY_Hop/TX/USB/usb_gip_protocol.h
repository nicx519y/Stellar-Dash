#ifndef TX_USB_GIP_PROTOCOL_H
#define TX_USB_GIP_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_GIP_PACKET_MAX_BYTES 64u
#define USB_GIP_DATA_MAX_BYTES 1024u
#define USB_GIP_CHUNK_DATA_BYTES 0x3Au

#define USB_GIP_FLAG_NEEDS_ACK   0x10u
#define USB_GIP_FLAG_INTERNAL    0x20u
#define USB_GIP_FLAG_CHUNK_START 0x40u
#define USB_GIP_FLAG_CHUNKED     0x80u

typedef struct
{
    uint8_t command;
    uint8_t flags;
    uint8_t sequence;
    uint8_t valid;
    uint8_t complete;
    uint8_t chunked;
    uint8_t chunk_ended;
    uint16_t encoded_chunk_length;
    uint16_t encoded_chunk_received;
    uint16_t expected_data_length;
    uint16_t data_length;
    uint8_t data[USB_GIP_DATA_MAX_BYTES];
} usb_gip_rx_t;

typedef struct
{
    uint8_t command;
    uint8_t flags;
    uint8_t sequence;
    uint8_t chunked;
    uint8_t chunk_ended;
    uint16_t encoded_chunk_length;
    uint16_t encoded_chunk_sent;
    uint16_t data_length;
    uint16_t data_sent;
    uint16_t chunks_sent;
    uint8_t data[USB_GIP_DATA_MAX_BYTES];
} usb_gip_tx_t;

void usb_gip_rx_reset(usb_gip_rx_t *context);
bool usb_gip_rx_consume(usb_gip_rx_t *context,
                        const uint8_t *packet,
                        uint8_t length);
bool usb_gip_rx_ack_required(const usb_gip_rx_t *context);
bool usb_gip_rx_make_ack(const usb_gip_rx_t *context,
                         uint8_t *packet,
                         uint8_t *length);

void usb_gip_tx_reset(usb_gip_tx_t *context);
bool usb_gip_tx_begin(usb_gip_tx_t *context,
                      uint8_t command,
                      uint8_t sequence,
                      bool internal,
                      bool needs_ack,
                      const uint8_t *data,
                      uint16_t length);
bool usb_gip_tx_next(usb_gip_tx_t *context,
                     uint8_t *packet,
                     uint8_t *length);
bool usb_gip_tx_complete(const usb_gip_tx_t *context);

#ifdef __cplusplus
}
#endif

#endif
