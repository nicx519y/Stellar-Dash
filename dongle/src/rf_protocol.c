#include "rf_protocol.h"

static uint8_t checksum8(const uint8_t *data, size_t len)
{
    uint8_t s = 0u;
    size_t i;

    for (i = 0; i < len; ++i) {
        s = (uint8_t)(s + data[i]);
    }
    return s;
}

size_t rf_protocol_encode(const rf_proto_frame_t *frame, uint8_t *out, size_t out_cap)
{
    size_t total_len;
    size_t i;

    if ((frame == 0) || (out == 0) || (frame->payload_len > RF_PROTO_MAX_PAYLOAD)) {
        return 0u;
    }

    total_len = RF_PROTO_HEADER_SIZE + frame->payload_len + RF_PROTO_TAIL_CHECK_SIZE;
    if (out_cap < total_len) {
        return 0u;
    }

    out[0] = (uint8_t)(((frame->hdr.version & 0x03u) << 6) | (frame->hdr.type & 0x3Fu));
    out[1] = frame->hdr.flags;
    out[2] = frame->hdr.seq;
    out[3] = frame->hdr.ack_seq;
    out[4] = (uint8_t)(frame->hdr.ack_bits & 0xFFu);
    out[5] = (uint8_t)((frame->hdr.ack_bits >> 8) & 0xFFu);
    out[6] = frame->hdr.hop_idx;
    out[7] = frame->hdr.epoch_lsb;

    for (i = 0u; i < frame->payload_len; ++i) {
        out[RF_PROTO_HEADER_SIZE + i] = frame->payload[i];
    }

    out[total_len - 1u] = checksum8(out, total_len - 1u);
    return total_len;
}

bool rf_protocol_decode(const uint8_t *raw, size_t raw_len, rf_proto_frame_t *out)
{
    size_t payload_len;
    size_t i;
    uint8_t expect_chk;

    if ((raw == 0) || (out == 0) || (raw_len < (RF_PROTO_HEADER_SIZE + RF_PROTO_TAIL_CHECK_SIZE))) {
        return false;
    }

    payload_len = raw_len - RF_PROTO_HEADER_SIZE - RF_PROTO_TAIL_CHECK_SIZE;
    if (payload_len > RF_PROTO_MAX_PAYLOAD) {
        return false;
    }

    expect_chk = checksum8(raw, raw_len - 1u);
    if (expect_chk != raw[raw_len - 1u]) {
        return false;
    }

    out->hdr.version = (uint8_t)((raw[0] >> 6) & 0x03u);
    out->hdr.type = (uint8_t)(raw[0] & 0x3Fu);
    out->hdr.flags = raw[1];
    out->hdr.seq = raw[2];
    out->hdr.ack_seq = raw[3];
    out->hdr.ack_bits = (uint16_t)((uint16_t)raw[4] | ((uint16_t)raw[5] << 8));
    out->hdr.hop_idx = raw[6];
    out->hdr.epoch_lsb = raw[7];
    out->payload_len = (uint8_t)payload_len;

    for (i = 0u; i < payload_len; ++i) {
        out->payload[i] = raw[RF_PROTO_HEADER_SIZE + i];
    }

    return (out->hdr.version == RF_PROTO_VERSION);
}
