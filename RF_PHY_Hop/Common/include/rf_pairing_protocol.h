#ifndef RF_PAIRING_PROTOCOL_H
#define RF_PAIRING_PROTOCOL_H

#include "rf_hop_protocol.h"

#include <stdint.h>

typedef struct {
    uint8_t cmd;
    uint8_t rate_code;
    uint8_t write_bond;
    uint32_t session;
    uint32_t arg;
    uint8_t meta;
} rf_pair_packet_t;

static inline uint8_t rf_pair_make_meta(uint8_t rate_code, uint8_t write_bond)
{
    uint8_t meta = (uint8_t)(((uint8_t)RFH_PAIR_PROTO_VERSION << RFH_PAIR_META_VERSION_SHIFT) |
                             (rate_code & RFH_PAIR_META_RATE_MASK));

    if(write_bond != 0u)
    {
        meta |= RFH_PAIR_META_WRITE_BOND;
    }
    return meta;
}

static inline uint8_t rf_pair_meta_version_ok(uint8_t meta)
{
    return (((meta & RFH_PAIR_META_VERSION_MASK) >> RFH_PAIR_META_VERSION_SHIFT) ==
            RFH_PAIR_PROTO_VERSION) ? 1u : 0u;
}

static inline uint8_t rf_pair_meta_rate(uint8_t meta)
{
    return (uint8_t)(meta & RFH_PAIR_META_RATE_MASK);
}

static inline uint8_t rf_pair_meta_write_bond(uint8_t meta)
{
    return ((meta & RFH_PAIR_META_WRITE_BOND) != 0u) ? 1u : 0u;
}

static inline uint8_t rf_pair_encode_air(uint8_t *air,
                                         uint8_t rate_code,
                                         uint8_t header1,
                                         uint8_t cmd,
                                         uint32_t session,
                                         uint32_t arg,
                                         uint8_t write_bond)
{
    uint8_t *data;

    if(air == 0)
    {
        return 0u;
    }

    air[RFH_HDR0_OFFSET] = rfh_make_header0(RFH_PKT_PAIR, rate_code, 0u);
    air[RFH_HDR1_OFFSET] = header1;
    data = &air[RFH_DATA_OFFSET];
    data[RFH_PAIR_CMD_ID] = cmd;
    rfh_put_u32(&data[RFH_PAIR_SESSION0], session);
    rfh_put_u32(&data[RFH_PAIR_ARG0], arg);
    data[RFH_PAIR_META] = rf_pair_make_meta(rate_code, write_bond);
    return 1u;
}

static inline uint8_t rf_pair_decode_air(const uint8_t *air,
                                         uint8_t len,
                                         rf_pair_packet_t *packet)
{
    const uint8_t *data;
    uint8_t meta;

    if((air == 0) || (packet == 0) || (len != RFH_AIR_PACKET_LEN))
    {
        return 0u;
    }
    if(rfh_packet_type(air[RFH_HDR0_OFFSET]) != RFH_PKT_PAIR)
    {
        return 0u;
    }

    data = &air[RFH_DATA_OFFSET];
    meta = data[RFH_PAIR_META];
    if(rf_pair_meta_version_ok(meta) == 0u)
    {
        return 0u;
    }

    packet->cmd = data[RFH_PAIR_CMD_ID];
    packet->rate_code = rf_pair_meta_rate(meta);
    packet->write_bond = rf_pair_meta_write_bond(meta);
    packet->session = rfh_get_u32(&data[RFH_PAIR_SESSION0]);
    packet->arg = rfh_get_u32(&data[RFH_PAIR_ARG0]);
    packet->meta = meta;
    return 1u;
}

#endif
