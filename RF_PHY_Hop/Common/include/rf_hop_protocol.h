#ifndef RF_HOP_PROTOCOL_H
#define RF_HOP_PROTOCOL_H

#include <stdint.h>

#define RFH_AIR_PACKET_LEN             12u
#define RFH_AIR_DATA_LEN               10u
#define RFH_WCH_PREAMBLE               0x55u

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
#define RFH_DEFAULT_CHANNEL_B          24u
#define RFH_DISCOVERY_CHANNEL_A        RFH_DEFAULT_CHANNEL_A
#define RFH_DISCOVERY_CHANNEL_B        RFH_DEFAULT_CHANNEL_B
#define RFH_MIN_CHANNEL                4u
#define RFH_MAX_CHANNEL                36u

#define RFH_DEFAULT_RATE_HZ            8000u
#define RFH_DEFAULT_ACK_WINDOW_MS      1u
#define RFH_DEFAULT_ACK_WINDOW_US      500u
#define RFH_ACK_RX_PRE_GUARD_US_DEFAULT 250u
#define RFH_ACK_RX_POST_GUARD_US_DEFAULT 250u
#define RFH_DUAL_PERIOD_MS            2u
#define RFH_CONNECT_SESSION_ID         0x484F5031UL
#define RFH_PROTOCOL_VERSION           1u

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
#define RFH_CMD_RECONNECT             0x7Fu

#define RFH_ACK_STATUS_SEEK           0u
#define RFH_ACK_STATUS_CONNECTED      1u

typedef enum {
    RFH_PKT_CONNECT = 0u,
    RFH_PKT_DATA = 1u,
    RFH_PKT_ACK = 2u
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
    RFH_HOP_CMD_ID = RFH_CMD_SLOT_ID,
    RFH_HOP_CMD_CHANNEL = RFH_CMD_SLOT_ARG0,
    RFH_HOP_CMD_DELAY_LO_MS = RFH_CMD_SLOT_ARG1,
    RFH_HOP_CMD_DELAY_HI_MS = RFH_CMD_SLOT_ARG2,
    RFH_HOP_CMD_SEQ = RFH_CMD_SLOT_ARG3,
    RFH_HOP_CONFIRM_OLD_CHANNEL = RFH_CMD_SLOT_DATA0
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
