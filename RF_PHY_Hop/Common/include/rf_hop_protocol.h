#ifndef RF_HOP_PROTOCOL_H
#define RF_HOP_PROTOCOL_H

#include <stdint.h>

#define RFH_AIR_PACKET_LEN             12u
#define RFH_AIR_DATA_LEN               10u
#define RFH_INPUT_AIR_DATA_LEN         5u
#define RFH_INPUT_AIR_PACKET_LEN       (RFH_DATA_OFFSET + RFH_INPUT_AIR_DATA_LEN)
#define RFH_WCH_PREAMBLE               0x55u

#define RFH_FIXED_CHANNEL              16u
#define RFH_FIXED_RATE_HZ              8000u
#define RFH_GROUP_SLOTS                8u
#define RFH_DATA_SLOTS                 7u
#define RFH_ACK_SLOT                   7u
#define RFH_SLOT_MASK                  0x07u
#define RFH_GROUP_SHIFT                3u
#define RFH_GROUP_MASK                 0xF8u
#define RFH_ACK_MISSING_MASK           0x7Fu
#define RFH_TX_SEND_TIME_UNITS         (20u * 2u)
#define RFH_TX_SETUP_US                44u
#define RFH_ACK_TX_OFFSET_US           16u
#define RFH_RX_REPORT_DONE_US          132u
#define RFH_SLOT_US                    125u

#define RFH_HDR0_OFFSET                0u
#define RFH_HDR1_OFFSET                1u
#define RFH_DATA_OFFSET                2u

#define RFH_TYPE_SHIFT                 6u
#define RFH_TYPE_MASK                  0xC0u
#define RFH_RATE_SHIFT                 4u
#define RFH_RATE_MASK                  0x30u
#define RFH_FLAG_MASK                  0x0Fu

#define RFH_FLAG_CMD_PRESENT           0x01u
#define RFH_FLAG_CMD_HOP               RFH_FLAG_CMD_PRESENT
#define RFH_FLAG_CMD_ACK               0x02u
#define RFH_FLAG_DUAL_REDUNDANT        0x04u
#define RFH_FLAG_LINK_OK               0x08u

#define RFH_ACK_COUNTDOWN_FAR          0xFFu

#define RFH_DEFAULT_CHANNEL_A          16u
#define RFH_DEFAULT_CHANNEL_B          39u
#define RFH_DISCOVERY_CHANNEL_A        16u
#define RFH_DISCOVERY_CHANNEL_B        39u
#define RFH_PAIR_CHANNEL_FIXED         12u
#define RFH_MIN_CHANNEL                0u
#define RFH_MAX_CHANNEL                39u
#define RFH_HOP_CHANNEL_COUNT          7u
#define RFH_HOP_CHANNELS              { 10u, 16u, 22u, 24u, 28u, 34u, 39u }

#define RFH_DEFAULT_RATE_HZ            8000u
#define RFH_DEFAULT_ACK_WINDOW_MS      1u
#define RFH_DEFAULT_ACK_WINDOW_US      500u
#define RFH_ACK_RX_PRE_GUARD_US_DEFAULT 250u
#define RFH_ACK_RX_POST_GUARD_US_DEFAULT 250u
#define RFH_DUAL_PERIOD_MS            2u
#define RFH_CONNECT_SESSION_ID         0x484F5031UL
#define RFH_PROTOCOL_VERSION           1u
#define RFH_LINK_ACCESS_ADDRESS_DEFAULT 0x71764129UL
#define RFH_CONNECT_STAGE_SYN          1u
#define RFH_CONNECT_STAGE_FINAL        3u
#define RFH_CONNECT_WINDOW_MS          50u
#define RFH_CONNECT_SUPERFRAME_MS      (RFH_CONNECT_WINDOW_MS * 2u)
#define RFH_CONNECT_DWELL_MS           10u
#define RFH_CONNECT_RESPONSE_INTERVAL_MS 5u
#define RFH_CONNECT_FINAL_TX_MS        500u
#define RFH_CONNECT_FINAL_WAIT_MS      1000u

#ifndef RFH_TEST_FIXED_BOND_ENABLE
#define RFH_TEST_FIXED_BOND_ENABLE     0u
#endif

/*
 * Development-only fixed working address. Product pairing must use
 * RFH_PAIR_ACCESS_ADDRESS for discovery, then store a generated link address.
 */
#define RFH_TEST_FIXED_ACCESS_ADDRESS  0x6D35B8C9UL

#define RFH_PAIR_ACCESS_ADDRESS        0x6D5A3C17UL
#define RFH_PAIR_ACCESS_ADDRESS_MAGIC  0x52485041UL
#define RFH_PAIR_CHANNEL_A             RFH_PAIR_CHANNEL_FIXED
#define RFH_PAIR_CHANNEL_B             RFH_PAIR_CHANNEL_FIXED
#define RFH_PAIR_WINDOW_MS             60000u
#define RFH_PAIR_CONFIRM_TIMEOUT_MS    3000u
#define RFH_PAIR_PROTO_VERSION         1u
#define RFH_PAIR_ACCEPT_TICK_US        1000u
#define RFH_PAIR_TX_WINDOW_MS          100u
#define RFH_PAIR_RESPONSE_BURST_MS     100u
#define RFH_PAIR_ACCEPT_GUARD_MS       4u
#define RFH_PAIR_ACCEPT_MIN_COUNTDOWN_MS (RFH_PAIR_ACCEPT_GUARD_MS + 4u)
#define RFH_PAIR_DISCOVERY_CYCLE_MS    (RFH_PAIR_TX_WINDOW_MS * 4u)
#define RFH_PAIR_CONFIRM_CYCLE_MS      (RFH_PAIR_TX_WINDOW_MS * 2u)

#define RFH_ACK_MISS_LIMIT_DEFAULT                 3u
#define RFH_RX_PACKET_TIMEOUT_MS_DEFAULT           100u
#define RFH_HOP_LOSS_THRESHOLD_PERMILLE_DEFAULT    30u
#define RFH_HOP_COOLDOWN_MS_DEFAULT                10000u
#define RFH_HOP_PREPARE_ADVANCE_MS_DEFAULT         1000u
#define RFH_HOP_PREPARE_ACK_TIMEOUT_MS_DEFAULT     1000u
#define RFH_HOP_CONFIRM_ACK_TIMEOUT_MS_DEFAULT     1000u
#define RFH_LOG_LINE_VISIBLE_MAX                   62u

#define RFH_CMD_NONE                  0x00u
#define RFH_CMD_CONNECT_REQ           0x01u
#define RFH_CMD_HOP_PREPARE           0x10u
#define RFH_CMD_HOP_CONFIRM           0x11u
#define RFH_CMD_HOP_CANCEL            0x12u
#define RFH_CMD_RATE_UPDATE           0x20u
#define RFH_CMD_MONITOR_CONFIG        0x21u
#define RFH_CMD_TIME_SYNC             0x22u
#define RFH_CMD_TIME_SYNC_ECHO        0x23u
#define RFH_CMD_LATENCY_INPUT         0x24u
#define RFH_CMD_SCORE_HINT            0x25u
#define RFH_CMD_BATTERY_STATUS        0x26u
#define RFH_CMD_PAIR_OFFER            0x30u
#define RFH_CMD_PAIR_ACCEPT           0x31u
#define RFH_CMD_PAIR_CONFIRM          0x32u
#define RFH_CMD_PAIR_DONE             0x33u
#define RFH_CMD_PAIR_REJECT           0x34u
#define RFH_CMD_RECONNECT             0x7Fu

#define RFH_PAIR_REJECT_BAD_VERSION   1u
#define RFH_PAIR_REJECT_BAD_STATE     2u
#define RFH_PAIR_REJECT_BAD_ADDRESS   3u
#define RFH_PAIR_REJECT_BOND_FAILED   4u

#define RFH_ACK_STATUS_SEEK           0u
#define RFH_ACK_STATUS_CONNECTED      1u
#define RFH_ACK_STATUS_FINAL_READY    2u

typedef enum {
    RFH_PKT_CONNECT = 0u,
    RFH_PKT_DATA = 1u,
    RFH_PKT_ACK = 2u,
    RFH_PKT_PAIR = 3u
} rfh_packet_type_t;

typedef enum {
    RFH_RATE_1K = 0u,
    RFH_RATE_2K = 1u,
    RFH_RATE_4K = 2u,
    RFH_RATE_8K = 3u
} rfh_rate_code_t;

enum {
    RFH_CONNECT_SESSION0 = 0u,
    RFH_CONNECT_SESSION1 = 1u,
    RFH_CONNECT_SESSION2 = 2u,
    RFH_CONNECT_SESSION3 = 3u,
    RFH_CONNECT_RATE = 4u,
    RFH_CONNECT_CH_A = 5u,
    RFH_CONNECT_CH_B = 6u,
    RFH_CONNECT_ACK_WINDOW_MS = 7u,
    RFH_CONNECT_OPTIONS = 8u,
    RFH_CONNECT_VERSION = 9u
};

enum {
    RFH_CMD_SLOT_ID = 0u,
    RFH_CMD_SLOT_ARG0 = 1u,
    RFH_CMD_SLOT_ARG1 = 2u,
    RFH_CMD_SLOT_ARG2 = 3u,
    RFH_CMD_SLOT_ARG3 = 4u,
    RFH_CMD_SLOT_DATA0 = 5u,
    RFH_CMD_SLOT_DATA1 = 6u,
    RFH_CMD_SLOT_DATA2 = 7u,
    RFH_CMD_SLOT_DATA3 = 8u,
    RFH_CMD_SLOT_DATA4 = 9u
};

enum {
    RFH_TIME_SYNC_ECHO_CMD_ID = 0u,
    RFH_TIME_SYNC_ECHO_SEQ = 1u,
    RFH_TIME_SYNC_ECHO_RX_TICK = 2u,
    RFH_TIME_SYNC_ECHO_TX_TICK = 6u
};

enum {
    RFH_LATENCY_CMD_ID = 0u,
    RFH_LATENCY_INPUT_SEQ = 1u,
    RFH_LATENCY_KEY_MASK = 2u,
    RFH_LATENCY_SAMPLE_TICK = 6u
};

enum {
    RFH_HOP_CMD_ID = RFH_CMD_SLOT_ID,
    RFH_HOP_CMD_CHANNEL = RFH_CMD_SLOT_ARG0,
    RFH_HOP_CMD_DELAY_LO_MS = RFH_CMD_SLOT_ARG1,
    RFH_HOP_CMD_DELAY_HI_MS = RFH_CMD_SLOT_ARG2,
    RFH_HOP_CMD_SEQ = RFH_CMD_SLOT_ARG3,
    RFH_HOP_CONFIRM_OLD_CHANNEL = RFH_CMD_SLOT_DATA0,
    RFH_HOP_CMD_SCORE_LO = RFH_CMD_SLOT_DATA1,
    RFH_HOP_CMD_SCORE_HI = RFH_CMD_SLOT_DATA2
};

enum {
    RFH_ACK_LOSS_PERMILLE_LO = 0u,
    RFH_ACK_LOSS_PERMILLE_HI = 1u,
    RFH_ACK_RX_COUNT_LO = 2u,
    RFH_ACK_RX_COUNT_HI = 3u,
    RFH_ACK_EXPECTED_COUNT_LO = 4u,
    RFH_ACK_EXPECTED_COUNT_HI = 5u,
    RFH_ACK_CMD_ID = 6u,
    RFH_ACK_FLAGS = 7u,
    RFH_ACK_CHANNEL = 8u,
    RFH_ACK_STATUS = 9u
};

#define RFH_ACK_AVG_IRQ_US_LO          RFH_ACK_RX_COUNT_LO
#define RFH_ACK_AVG_IRQ_US_HI          RFH_ACK_RX_COUNT_HI
#define RFH_ACK_MAX_IRQ_US_LO          RFH_ACK_EXPECTED_COUNT_LO
#define RFH_ACK_MAX_IRQ_US_HI          RFH_ACK_EXPECTED_COUNT_HI

enum {
    RFH_ACK_MON_FLAGS = RFH_ACK_FLAGS,
    RFH_ACK_MON_PERIOD_CODE = RFH_ACK_CHANNEL,
    RFH_ACK_MON_MANUAL_CHANNEL = RFH_ACK_CHANNEL,
    RFH_ACK_MON_SEQ = RFH_ACK_STATUS
};

enum {
    RFH_PAIR_CMD_ID = 0u,
    RFH_PAIR_SESSION0 = 1u,
    RFH_PAIR_SESSION1 = 2u,
    RFH_PAIR_SESSION2 = 3u,
    RFH_PAIR_SESSION3 = 4u,
    RFH_PAIR_ARG0 = 5u,
    RFH_PAIR_ARG1 = 6u,
    RFH_PAIR_ARG2 = 7u,
    RFH_PAIR_ARG3 = 8u,
    RFH_PAIR_META = 9u
};

#define RFH_PAIR_META_VERSION_SHIFT    4u
#define RFH_PAIR_META_VERSION_MASK     0xF0u
#define RFH_PAIR_META_RATE_MASK        0x03u
#define RFH_PAIR_META_WRITE_BOND       0x08u

static inline uint32_t rfh_fnv1a32_bytes(const uint8_t *bytes, uint32_t len)
{
    uint32_t hash = 2166136261UL;
    uint32_t i;

    for(i = 0u; i < len; ++i)
    {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
    return hash;
}

static inline uint32_t rfh_fnv1a32_mix_u32(uint32_t hash, uint32_t value)
{
    hash ^= (uint8_t)(value & 0xFFu);
    hash *= 16777619UL;
    hash ^= (uint8_t)((value >> 8) & 0xFFu);
    hash *= 16777619UL;
    hash ^= (uint8_t)((value >> 16) & 0xFFu);
    hash *= 16777619UL;
    hash ^= (uint8_t)(value >> 24);
    hash *= 16777619UL;
    return hash;
}

static inline uint8_t rfh_access_address_valid(uint32_t aa)
{
    uint8_t i;
    uint8_t transitions = 0u;
    uint8_t run_len = 1u;
    uint8_t prev = (uint8_t)(aa & 1u);
    uint8_t bit;

    if((aa == 0x00000000UL) ||
       (aa == 0xFFFFFFFFUL) ||
       (aa == 0x55555555UL) ||
       (aa == 0xAAAAAAAAUL) ||
       (aa == RFH_PAIR_ACCESS_ADDRESS) ||
       (aa == RFH_LINK_ACCESS_ADDRESS_DEFAULT))
    {
        return 0u;
    }

    for(i = 1u; i < 32u; ++i)
    {
        bit = (uint8_t)((aa >> i) & 1u);
        if(bit == prev)
        {
            run_len++;
            if(run_len > 6u)
            {
                return 0u;
            }
        }
        else
        {
            transitions++;
            run_len = 1u;
            prev = bit;
        }
    }

    return ((transitions >= 8u) && (transitions <= 24u)) ? 1u : 0u;
}

static inline uint32_t rfh_access_address_from_seed(uint32_t seed)
{
    uint16_t i;
    uint32_t aa = seed;

    for(i = 0u; i < 256u; ++i)
    {
        aa ^= aa << 13;
        aa ^= aa >> 17;
        aa ^= aa << 5;
        aa ^= 0xA5A55A5AUL + ((uint32_t)i * 0x9E3779B9UL);
        if(rfh_access_address_valid(aa) != 0u)
        {
            return aa;
        }
    }

    /*
     * Extremely unlikely fallback. Keep it seed-derived so production builds
     * never collapse back to a fleet-wide working access address.
     */
    aa = seed ^ (seed << 7) ^ (seed >> 9) ^ 0xD3C5A7B9UL;
    aa ^= (aa << 11) | (aa >> 21);
    if(rfh_access_address_valid(aa) == 0u)
    {
        aa ^= 0x5A5AA5A5UL;
    }
    return aa;
}

static inline uint32_t rfh_pair_confirm32(uint32_t session_nonce,
                                          uint32_t tx_id_hash,
                                          uint32_t rx_id_hash,
                                          uint32_t link_access_address)
{
    static const uint8_t tag[] = "HBOX_PAIR_DONE";
    uint32_t hash = rfh_fnv1a32_bytes(tag, (uint32_t)(sizeof(tag) - 1u));

    hash = rfh_fnv1a32_mix_u32(hash, session_nonce);
    hash = rfh_fnv1a32_mix_u32(hash, tx_id_hash);
    hash = rfh_fnv1a32_mix_u32(hash, rx_id_hash);
    hash = rfh_fnv1a32_mix_u32(hash, link_access_address);
    return hash;
}

static inline uint8_t rfh_make_header0(uint8_t type, uint8_t rate, uint8_t flags)
{
    return (uint8_t)(((type << RFH_TYPE_SHIFT) & RFH_TYPE_MASK) |
                     ((rate << RFH_RATE_SHIFT) & RFH_RATE_MASK) |
                     (flags & RFH_FLAG_MASK));
}

static inline uint8_t rfh_packet_type(uint8_t header0)
{
    return (uint8_t)((header0 & RFH_TYPE_MASK) >> RFH_TYPE_SHIFT);
}

static inline uint8_t rfh_rate_code(uint8_t header0)
{
    return (uint8_t)((header0 & RFH_RATE_MASK) >> RFH_RATE_SHIFT);
}

static inline uint8_t rfh_flags(uint8_t header0)
{
    return (uint8_t)(header0 & RFH_FLAG_MASK);
}

static inline uint8_t rfh_make_slot_header1(uint8_t group_id, uint8_t slot_id)
{
    return (uint8_t)(((group_id << RFH_GROUP_SHIFT) & RFH_GROUP_MASK) |
                     (slot_id & RFH_SLOT_MASK));
}

static inline uint8_t rfh_header_group_id(uint8_t header1)
{
    return (uint8_t)((header1 & RFH_GROUP_MASK) >> RFH_GROUP_SHIFT);
}

static inline uint8_t rfh_header_slot_id(uint8_t header1)
{
    return (uint8_t)(header1 & RFH_SLOT_MASK);
}

static inline uint8_t rfh_group_diff(uint8_t newer, uint8_t older)
{
    return (uint8_t)((newer - older) & 0x1Fu);
}

static inline uint16_t rfh_rate_hz_from_code(uint8_t code)
{
    switch(code)
    {
    case RFH_RATE_1K:
        return 1000u;
    case RFH_RATE_2K:
        return 2000u;
    case RFH_RATE_4K:
        return 4000u;
    case RFH_RATE_8K:
    default:
        return 8000u;
    }
}

static inline uint8_t rfh_rate_code_from_hz(uint16_t hz)
{
    if(hz <= 1000u)
    {
        return RFH_RATE_1K;
    }
    if(hz <= 2000u)
    {
        return RFH_RATE_2K;
    }
    if(hz <= 4000u)
    {
        return RFH_RATE_4K;
    }
    return RFH_RATE_8K;
}

static inline uint8_t rfh_channel_valid(uint8_t channel)
{
    return ((channel >= RFH_MIN_CHANNEL) && (channel <= RFH_MAX_CHANNEL)) ? 1u : 0u;
}

static inline uint8_t rfh_hop_channel_at(uint8_t index)
{
    static const uint8_t channels[RFH_HOP_CHANNEL_COUNT] = RFH_HOP_CHANNELS;

    return (index < RFH_HOP_CHANNEL_COUNT) ? channels[index] : channels[0];
}

static inline uint8_t rfh_hop_channel_valid(uint8_t channel)
{
    uint8_t i;

    for(i = 0u; i < RFH_HOP_CHANNEL_COUNT; i++)
    {
        if(rfh_hop_channel_at(i) == channel)
        {
            return 1u;
        }
    }
    return 0u;
}

static inline uint8_t rfh_ack_window_packets(uint16_t report_hz, uint8_t ack_window_ms)
{
    uint16_t packets = (uint16_t)((report_hz / 1000u) * ack_window_ms);
    if(packets == 0u)
    {
        packets = 1u;
    }
    if(packets > 255u)
    {
        packets = 255u;
    }
    return (uint8_t)packets;
}

static inline uint8_t rfh_packets_for_us(uint16_t report_hz, uint16_t usec)
{
    uint32_t packets;

    if(usec == 0u)
    {
        return 0u;
    }

    packets = ((uint32_t)report_hz * (uint32_t)usec + 999999u) / 1000000u;
    if(packets == 0u)
    {
        packets = 1u;
    }
    if(packets > 255u)
    {
        packets = 255u;
    }
    return (uint8_t)packets;
}

static inline uint8_t rfh_ack_window_packets_us(uint16_t report_hz, uint16_t ack_window_us)
{
    uint8_t packets = rfh_packets_for_us(report_hz, ack_window_us);
    return (packets == 0u) ? 1u : packets;
}

static inline uint8_t rfh_ack_countdown_ms(uint16_t packet_pos,
                                           uint16_t report_hz,
                                           uint8_t ack_window_ms)
{
    uint16_t ticks_per_ms = report_hz / 1000u;
    uint16_t ack_packets;
    uint16_t ack_start;
    uint16_t ticks_until;
    uint16_t ms_until;

    if(ticks_per_ms == 0u)
    {
        ticks_per_ms = 1u;
    }

    ack_packets = (uint16_t)(ticks_per_ms * ack_window_ms);
    if(ack_packets == 0u)
    {
        ack_packets = 1u;
    }
    if(ack_packets >= report_hz)
    {
        ack_start = 0u;
    }
    else
    {
        ack_start = (uint16_t)(report_hz - ack_packets);
    }

    if(packet_pos >= ack_start)
    {
        return 0u;
    }

    ticks_until = (uint16_t)(ack_start - packet_pos);
    ms_until = (uint16_t)((ticks_until + ticks_per_ms - 1u) / ticks_per_ms);
    if(ms_until > 254u)
    {
        return RFH_ACK_COUNTDOWN_FAR;
    }
    return (uint8_t)ms_until;
}

static inline uint8_t rfh_ack_countdown_ticks_by_packets(uint16_t packet_pos,
                                                         uint16_t report_hz,
                                                         uint8_t ack_packets)
{
    uint16_t ack_start;
    uint16_t ticks_until;

    if(ack_packets == 0u)
    {
        ack_packets = 1u;
    }
    if(ack_packets >= report_hz)
    {
        ack_start = 0u;
    }
    else
    {
        ack_start = (uint16_t)(report_hz - ack_packets);
    }

    if(packet_pos >= ack_start)
    {
        return 0u;
    }

    ticks_until = (uint16_t)(ack_start - packet_pos);
    if(ticks_until > 254u)
    {
        return RFH_ACK_COUNTDOWN_FAR;
    }
    return (uint8_t)ticks_until;
}

static inline uint8_t rfh_ack_countdown_ticks(uint16_t packet_pos,
                                              uint16_t report_hz,
                                              uint8_t ack_window_ms)
{
    uint16_t ticks_per_ms = report_hz / 1000u;
    uint16_t ack_packets;
    uint16_t ack_start;
    uint16_t ticks_until;

    if(ticks_per_ms == 0u)
    {
        ticks_per_ms = 1u;
    }

    ack_packets = (uint16_t)(ticks_per_ms * ack_window_ms);
    if(ack_packets == 0u)
    {
        ack_packets = 1u;
    }
    if(ack_packets >= report_hz)
    {
        ack_start = 0u;
    }
    else
    {
        ack_start = (uint16_t)(report_hz - ack_packets);
    }

    if(packet_pos >= ack_start)
    {
        return 0u;
    }

    ticks_until = (uint16_t)(ack_start - packet_pos);
    if(ticks_until > 254u)
    {
        return RFH_ACK_COUNTDOWN_FAR;
    }
    return (uint8_t)ticks_until;
}

static inline void rfh_put_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)(value >> 8);
}

static inline uint16_t rfh_get_u16(const uint8_t *src)
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static inline void rfh_put_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)(value >> 24);
}

static inline uint32_t rfh_get_u32(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

#endif
