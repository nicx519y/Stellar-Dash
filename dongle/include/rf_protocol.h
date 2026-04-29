#ifndef RF_PROTOCOL_H
#define RF_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RF_PROTO_VERSION          (1u)
#define RF_PROTO_HEADER_SIZE      (8u)
#define RF_PROTO_TAIL_CHECK_SIZE  (1u)
#define RF_PROTO_MAX_PAYLOAD      (24u)
#define RF_PROTO_MAX_FRAME        (RF_PROTO_HEADER_SIZE + RF_PROTO_MAX_PAYLOAD + RF_PROTO_TAIL_CHECK_SIZE)

typedef enum {
    RF_PKT_ADV_REQ = 1,
    RF_PKT_ADV_RSP = 2,
    RF_PKT_PAIR_CONFIRM = 3,
    RF_PKT_CONN_REQ = 4,
    RF_PKT_CONN_ACK = 5,
    RF_PKT_INPUT_DATA = 6,
    RF_PKT_CTRL_CMD = 7,
    RF_PKT_HEARTBEAT = 8,
    RF_PKT_UNBIND = 9
} rf_packet_type_t;

typedef struct {
    uint8_t version;
    uint8_t type;
    uint8_t flags;
    uint8_t seq;
    uint8_t ack_seq;
    uint16_t ack_bits;
    uint8_t hop_idx;
    uint8_t epoch_lsb;
} rf_proto_header_t;

typedef struct {
    rf_proto_header_t hdr;
    uint8_t payload[RF_PROTO_MAX_PAYLOAD];
    uint8_t payload_len;
} rf_proto_frame_t;

size_t rf_protocol_encode(const rf_proto_frame_t *frame, uint8_t *out, size_t out_cap);
bool rf_protocol_decode(const uint8_t *raw, size_t raw_len, rf_proto_frame_t *out);

#endif /* RF_PROTOCOL_H */
