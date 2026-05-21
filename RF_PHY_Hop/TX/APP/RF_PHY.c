/********************************** (C) COPYRIGHT *******************************
 * File Name          : RF_PHY.c
 * Description        : TX side for the 12-byte RF hop protocol.
 *******************************************************************************/

#include "CONFIG.h"
#include "RF_PHY.h"
#include "HAL.h"
#include "wchrf.h"
#include "rfm_config.h"
#include "rfm_input_stream.h"
#include "rfm_spi_port_internal.h"
#include "rf_hop_protocol.h"

#include <stdbool.h>
#include <string.h>

#ifndef RF_HOP_MODE
#define RF_HOP_MODE                    1
#endif

#ifndef RF_REPORT_PPS
#define RF_REPORT_PPS                  RFH_DEFAULT_RATE_HZ
#endif

#ifndef RF_ACK_WINDOW_MS
#define RF_ACK_WINDOW_MS               RFH_DEFAULT_ACK_WINDOW_MS
#endif

#ifndef RF_ACK_WINDOW_US
#define RF_ACK_WINDOW_US               RFH_DEFAULT_ACK_WINDOW_US
#endif

#ifndef RF_ACK_MISS_LIMIT
#define RF_ACK_MISS_LIMIT              RFH_ACK_MISS_LIMIT_DEFAULT
#endif

#ifndef RF_HOP_LOSS_THRESHOLD_PERMILLE
#define RF_HOP_LOSS_THRESHOLD_PERMILLE RFH_HOP_LOSS_THRESHOLD_PERMILLE_DEFAULT
#endif

#ifndef RF_AUTO_HOP_ENABLE
#define RF_AUTO_HOP_ENABLE             1u
#endif

#ifndef RF_STARTUP_RANDOM_CHANNEL_ENABLE
#define RF_STARTUP_RANDOM_CHANNEL_ENABLE 1u
#endif

#ifndef RF_HOP_COOLDOWN_MS
#define RF_HOP_COOLDOWN_MS             RFH_HOP_COOLDOWN_MS_DEFAULT
#endif

#ifndef RF_HOP_PREPARE_ADVANCE_MS
#define RF_HOP_PREPARE_ADVANCE_MS      RFH_HOP_PREPARE_ADVANCE_MS_DEFAULT
#endif

#ifndef RF_HOP_PREPARE_ACK_TIMEOUT_MS
#define RF_HOP_PREPARE_ACK_TIMEOUT_MS  RFH_HOP_PREPARE_ACK_TIMEOUT_MS_DEFAULT
#endif

#ifndef RF_HOP_CONFIRM_ACK_TIMEOUT_MS
#define RF_HOP_CONFIRM_ACK_TIMEOUT_MS  RFH_HOP_CONFIRM_ACK_TIMEOUT_MS_DEFAULT
#endif

#ifndef RF_RECOVERY_DUAL_TIMEOUT_MS
#define RF_RECOVERY_DUAL_TIMEOUT_MS    3000u
#endif

#ifndef RF_TEST_FREQUENCY
#define RF_TEST_FREQUENCY              RFH_DEFAULT_CHANNEL_A
#endif

#define RF_STAT_PRINT_PERIOD_MS        5000u
#define RF_TX_SEND_TIME                (20u * 2u)
#define RF_LINK_ACCESS_ADDRESS         0x71764129UL
#define RF_LINK_CRC_INIT               0x555555UL
/* WCH rfipRx_t.timeOut: 0 means no timeout; keep RX armed until a packet/error. */
#define RFIP_RX_NO_TIMEOUT             0u
#define RF_ACK_RX_TIMEOUT_US           RFIP_RX_NO_TIMEOUT
#define RF_ACK_RX_PRE_GUARD_US         RFH_ACK_RX_PRE_GUARD_US_DEFAULT
#define RF_ACK_RX_POST_GUARD_US        RFH_ACK_RX_POST_GUARD_US_DEFAULT
#define RF_LINK_DEBUG_LOG              1

#define SBP_RF_STAT_EVT                (1 << 5)

#if (RF_LINK_DEBUG_LOG == 1)
#define RF_LINK_LOG(...)               PRINT(__VA_ARGS__)
#else
#define RF_LINK_LOG(...)               ((void)0)
#endif

typedef enum {
    TX_UNCONNECTED = 0u,
    TX_COMM,
    TX_HOP_PREPARE_ACK_WAIT,
    TX_HOP_RESERVED,
    TX_HOP_CONFIRM_ACK_WAIT,
    TX_RECOVERY_DUAL
} tx_state_t;

typedef struct
{
    volatile uint32_t tx_try;
    volatile uint32_t tx_ok;
    volatile uint32_t tx_idle;
    volatile uint32_t tx_fail;
    volatile uint32_t tx_start_fail;
    volatile uint32_t tx_parm_fail;
    volatile uint32_t rx_arm_try;
    volatile uint32_t rx_arm_ok;
    volatile uint32_t rx_arm_fail;
    volatile uint32_t rx_ack;
    volatile uint32_t rx_bad_ack;
    volatile uint32_t spi_rx_total;
    volatile uint32_t spi_rx_win;
    volatile uint32_t payload_update;
} rf_stat_t;

uint8_t taskID;

static rfRoleParam_t gParm;
static rfipTx_t gTxParam;
static rfipRx_t gRxParam;
static rf_stat_t gStat = {0};

__attribute__((__aligned__(4))) static uint8_t TxBuf[64];
__attribute__((__aligned__(4))) static uint8_t RxBuf[264];

static volatile uint8_t g_basic_started = 0u;
static volatile tx_state_t g_state = TX_UNCONNECTED;
static volatile uint8_t g_ack_rx_active = 0u;
static volatile uint8_t g_low_config_ret = 0xFFu;
static volatile uint8_t g_low_tx_ret = 0xFFu;
static volatile uint8_t g_low_rx_ret = 0xFFu;

static uint8_t g_current_channel = RF_TEST_FREQUENCY;
static uint8_t g_radio_channel = RF_TEST_FREQUENCY;
static uint8_t g_ack_rx_channel = RF_TEST_FREQUENCY;
static uint8_t g_dual_channel_a = RFH_DEFAULT_CHANNEL_A;
static uint8_t g_dual_channel_b = RFH_DEFAULT_CHANNEL_B;
static uint8_t g_connect_channel_a = RFH_DEFAULT_CHANNEL_A;
static uint8_t g_connect_channel_b = RFH_DEFAULT_CHANNEL_B;
static uint8_t g_old_channel = RFH_DEFAULT_CHANNEL_A;
static uint8_t g_target_channel = RFH_DEFAULT_CHANNEL_A;
static uint8_t g_hop_seq = 0u;
static uint8_t g_expected_ack_cmd = RFH_CMD_NONE;
static uint8_t g_ack_miss_count = 0u;
static uint8_t g_ack_seen_this_period = 0u;
static uint8_t g_ack_period_close_pending = 0u;

static uint16_t g_report_hz = RF_REPORT_PPS;
static uint8_t g_rate_code = RFH_RATE_8K;
static uint8_t g_ack_window_ms = RF_ACK_WINDOW_MS;
static uint8_t g_ack_window_packets = 8u;
static uint16_t g_second_pos = 0u;
static uint16_t g_dual_pos = 0u;
static uint32_t g_tick_per_evt = 1u;
static uint32_t g_hop_cooldown_until = 0u;
static uint32_t g_wait_ack_deadline_clock = 0u;
static uint32_t g_hop_due_clock = 0u;
static uint32_t g_recovery_deadline_clock = 0u;

static uint8_t g_last_payload[RFH_AIR_DATA_LEN] = {0};
static uint8_t g_has_payload = 0u;
static uint16_t g_last_ack_loss_permille = 1000u;
static uint16_t g_last_ack_rx_count = 0u;
static uint16_t g_last_ack_expected = 0u;
static uint8_t g_last_ack_status = 0u;
static uint8_t g_last_ack_cmd = RFH_CMD_NONE;

static uint8_t g_log_ack_ok = 0u;
static uint8_t g_log_ack_expected = 0u;
static uint8_t g_log_hop_events = 0u;
static uint8_t g_log_unconnected_events = 0u;
static uint8_t g_log_errors = 0u;

void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id);

static uint8_t tx_time_reached(uint32_t now, uint32_t deadline)
{
    return ((int32_t)(now - deadline) >= 0) ? 1u : 0u;
}

static uint16_t tx_ms_until(uint32_t deadline)
{
    uint32_t now = TMOS_GetSystemClock();
    uint32_t ticks;
    uint32_t ms;

    if(tx_time_reached(now, deadline) != 0u)
    {
        return 0u;
    }
    ticks = deadline - now;
    ms = (uint32_t)(((uint64_t)ticks * SYSTEM_TIME_MICROSEN + 999u) / 1000u);
    if(ms > 65535u)
    {
        ms = 65535u;
    }
    return (uint16_t)ms;
}

static const char *tx_state_code(void)
{
    switch(g_state)
    {
    case TX_UNCONNECTED:
        return "U";
    case TX_COMM:
        return "C";
    case TX_HOP_PREPARE_ACK_WAIT:
        return "PA";
    case TX_HOP_RESERVED:
        return "HR";
    case TX_HOP_CONFIRM_ACK_WAIT:
        return "CA";
    case TX_RECOVERY_DUAL:
    default:
        return "RD";
    }
}

static const char *tx_rate_code(void)
{
    switch(g_rate_code)
    {
    case RFH_RATE_1K:
        return "1K";
    case RFH_RATE_2K:
        return "2K";
    case RFH_RATE_4K:
        return "4K";
    case RFH_RATE_8K:
    default:
        return "8K";
    }
}

static void tx_log_note_error(void)
{
    if(g_log_errors < 99u)
    {
        g_log_errors++;
    }
}

static void tx_log_note_hop_event(void)
{
    if(g_log_hop_events < 99u)
    {
        g_log_hop_events++;
    }
}

static void tx_log_note_unconnected(void)
{
    if(g_log_unconnected_events < 99u)
    {
        g_log_unconnected_events++;
    }
}

static uint8_t tx_channel_add_wrap(uint8_t channel, uint8_t step)
{
    uint8_t span = (uint8_t)(RFH_MAX_CHANNEL - RFH_MIN_CHANNEL + 1u);
    uint8_t offset;

    if(rfh_channel_valid(channel) == 0u)
    {
        channel = RFH_DISCOVERY_CHANNEL_A;
    }
    offset = (uint8_t)(channel - RFH_MIN_CHANNEL);
    offset = (uint8_t)((offset + step) % span);
    return (uint8_t)(RFH_MIN_CHANNEL + offset);
}

static uint8_t tx_random_channel(void)
{
    uint32_t rnd = tmos_rand();
    uint8_t span = (uint8_t)(RFH_MAX_CHANNEL - RFH_MIN_CHANNEL + 1u);

    rnd ^= TMOS_GetSystemClock();
    rnd ^= ((uint32_t)g_second_pos << 8);
    return (uint8_t)(RFH_MIN_CHANNEL + (uint8_t)(rnd % span));
}

static void tx_prepare_random_connect_channels(void)
{
#if (RF_STARTUP_RANDOM_CHANNEL_ENABLE != 0u)
    g_connect_channel_a = tx_random_channel();
    g_connect_channel_b = tx_channel_add_wrap(g_connect_channel_a, 8u);
    if(g_connect_channel_b == g_connect_channel_a)
    {
        g_connect_channel_b = tx_channel_add_wrap(g_connect_channel_a, 1u);
    }
#else
    g_connect_channel_a = RFH_DEFAULT_CHANNEL_A;
    g_connect_channel_b = RFH_DEFAULT_CHANNEL_B;
#endif
}

static void tx_enter_state(tx_state_t next)
{
    uint32_t now = TMOS_GetSystemClock();

    if(g_state == next)
    {
        return;
    }

    g_state = next;
    switch(next)
    {
    case TX_UNCONNECTED:
        g_expected_ack_cmd = RFH_CMD_CONNECT_REQ;
        g_ack_miss_count = 0u;
        g_ack_seen_this_period = 0u;
        tx_prepare_random_connect_channels();
        g_dual_channel_a = g_connect_channel_a;
        g_dual_channel_b = g_connect_channel_b;
        g_current_channel = g_connect_channel_a;
        tx_log_note_unconnected();
        break;
    case TX_COMM:
        g_expected_ack_cmd = RFH_CMD_NONE;
        g_ack_miss_count = 0u;
        g_ack_seen_this_period = 0u;
        g_hop_cooldown_until = now + MS1_TO_SYSTEM_TIME(RF_HOP_COOLDOWN_MS);
        break;
    case TX_HOP_PREPARE_ACK_WAIT:
        g_expected_ack_cmd = RFH_CMD_HOP_PREPARE;
        g_wait_ack_deadline_clock = now + MS1_TO_SYSTEM_TIME(RF_HOP_PREPARE_ACK_TIMEOUT_MS);
        tx_log_note_hop_event();
        break;
    case TX_HOP_RESERVED:
        g_expected_ack_cmd = RFH_CMD_NONE;
        tx_log_note_hop_event();
        break;
    case TX_HOP_CONFIRM_ACK_WAIT:
        g_expected_ack_cmd = RFH_CMD_HOP_CONFIRM;
        g_wait_ack_deadline_clock = now + MS1_TO_SYSTEM_TIME(RF_HOP_CONFIRM_ACK_TIMEOUT_MS);
        tx_log_note_hop_event();
        break;
    case TX_RECOVERY_DUAL:
    default:
        g_expected_ack_cmd = RFH_CMD_CONNECT_REQ;
        g_dual_channel_a = g_old_channel;
        g_dual_channel_b = g_target_channel;
        g_connect_channel_a = g_old_channel;
        g_connect_channel_b = g_target_channel;
        g_recovery_deadline_clock = now + MS1_TO_SYSTEM_TIME(RF_RECOVERY_DUAL_TIMEOUT_MS);
        tx_log_note_unconnected();
        break;
    }
}

static uint16_t tx_ticks_per_ms(void)
{
    uint16_t ticks = (uint16_t)(g_report_hz / 1000u);
    return (ticks == 0u) ? 1u : ticks;
}

static uint8_t tx_in_ack_window(void)
{
    uint16_t ack_start;
    uint16_t pre_packets;
    uint16_t post_packets;
    uint16_t ext_start;

    if(g_ack_window_packets >= g_report_hz)
    {
        return 1u;
    }
    ack_start = (uint16_t)(g_report_hz - g_ack_window_packets);
    pre_packets = rfh_packets_for_us(g_report_hz, RF_ACK_RX_PRE_GUARD_US);
    post_packets = rfh_packets_for_us(g_report_hz, RF_ACK_RX_POST_GUARD_US);
    ext_start = (ack_start > pre_packets) ? (uint16_t)(ack_start - pre_packets) : 0u;
    if(g_second_pos >= ext_start)
    {
        return 1u;
    }
    return (g_second_pos < post_packets) ? 1u : 0u;
}

static uint8_t tx_dual_channel_for_tick(void)
{
    return (g_dual_pos < tx_ticks_per_ms()) ? g_dual_channel_a : g_dual_channel_b;
}

static uint8_t tx_ack_channel_for_tick(void)
{
    uint16_t ack_start;
    uint16_t pre_packets;
    uint16_t post_packets;
    uint16_t ext_start;
    uint16_t ack_offset;
    uint16_t chunk_packets;
    uint16_t total_packets;
    uint16_t chunk_index;

    if((g_state != TX_UNCONNECTED) && (g_state != TX_RECOVERY_DUAL))
    {
        return g_current_channel;
    }
    if(g_ack_window_packets >= g_report_hz)
    {
        return g_dual_channel_a;
    }

    ack_start = (uint16_t)(g_report_hz - g_ack_window_packets);
    pre_packets = rfh_packets_for_us(g_report_hz, RF_ACK_RX_PRE_GUARD_US);
    post_packets = rfh_packets_for_us(g_report_hz, RF_ACK_RX_POST_GUARD_US);
    ext_start = (ack_start > pre_packets) ? (uint16_t)(ack_start - pre_packets) : 0u;
    total_packets = (uint16_t)(pre_packets + g_ack_window_packets + post_packets);
    if(g_second_pos >= ext_start)
    {
        ack_offset = (uint16_t)(g_second_pos - ext_start);
    }
    else
    {
        ack_offset = (uint16_t)((g_report_hz - ext_start) + g_second_pos);
    }
    if(ack_offset >= total_packets)
    {
        ack_offset = 0u;
    }
    chunk_packets = (uint16_t)(g_ack_window_packets / 2u);
    if(chunk_packets == 0u)
    {
        chunk_packets = 1u;
    }
    chunk_index = (uint16_t)(ack_offset / chunk_packets);
    return ((chunk_index & 1u) == 0u) ? g_dual_channel_a : g_dual_channel_b;
}

static uint8_t tx_ack_countdown_field(void)
{
    return rfh_ack_countdown_ticks_by_packets(g_second_pos,
                                              g_report_hz,
                                              g_ack_window_packets);
}

static uint8_t tx_next_channel(uint8_t current)
{
    uint8_t next = (uint8_t)(current + 8u);

    while(next > RFH_MAX_CHANNEL)
    {
        next = (uint8_t)(RFH_MIN_CHANNEL + (next - RFH_MAX_CHANNEL - 1u));
    }
    if(next == current)
    {
        next = (current == RFH_MIN_CHANNEL) ? (uint8_t)(RFH_MIN_CHANNEL + 1u) : RFH_MIN_CHANNEL;
    }
    return next;
}

static void tx_start_hop_prepare(uint8_t target)
{
    uint32_t now;

    if(rfh_channel_valid(target) == 0u)
    {
        return;
    }
    if(target == g_current_channel)
    {
        return;
    }

    now = TMOS_GetSystemClock();
    g_old_channel = g_current_channel;
    g_target_channel = target;
    g_hop_seq++;
    if(g_hop_seq == 0u)
    {
        g_hop_seq = 1u;
    }
    g_hop_due_clock = now + MS1_TO_SYSTEM_TIME(RF_HOP_PREPARE_ADVANCE_MS);
    tx_enter_state(TX_HOP_PREPARE_ACK_WAIT);
}

static void tx_on_ack_period_close(void)
{
    if((g_state != TX_UNCONNECTED) && (g_state != TX_RECOVERY_DUAL))
    {
        if(g_log_ack_expected < 255u)
        {
            g_log_ack_expected++;
        }
        if(g_ack_seen_this_period == 0u)
        {
            if(g_ack_miss_count < 255u)
            {
                g_ack_miss_count++;
            }
            if(g_ack_miss_count >= RF_ACK_MISS_LIMIT)
            {
                tx_enter_state(TX_UNCONNECTED);
            }
        }
    }
    g_ack_seen_this_period = 0u;
}

static void tx_advance_tick(void)
{
    uint16_t dual_period = (uint16_t)(tx_ticks_per_ms() * RFH_DUAL_PERIOD_MS);
    uint16_t post_packets = rfh_packets_for_us(g_report_hz,
                                               RF_ACK_RX_POST_GUARD_US);

    g_second_pos++;
    if(g_second_pos >= g_report_hz)
    {
        g_second_pos = 0u;
        if(post_packets == 0u)
        {
            tx_on_ack_period_close();
        }
        else
        {
            g_ack_period_close_pending = 1u;
        }
    }
    else if((g_ack_period_close_pending != 0u) &&
            (g_second_pos >= post_packets))
    {
        g_ack_period_close_pending = 0u;
        tx_on_ack_period_close();
    }

    if(dual_period == 0u)
    {
        dual_period = 1u;
    }
    g_dual_pos++;
    if(g_dual_pos >= dual_period)
    {
        g_dual_pos = 0u;
    }
}

static uint8_t tx_load_latest_payload(uint8_t *dst)
{
    uint8_t has_payload;

    has_payload = rfm_spi_port_peek_latest_input(dst, RFH_AIR_DATA_LEN) ? 1u : 0u;
    if(has_payload == 0u)
    {
        has_payload = rfm_input_stream_take_latest(dst, RFH_AIR_DATA_LEN) ? 1u : 0u;
    }
    if(has_payload != 0u)
    {
        memcpy(g_last_payload, dst, RFH_AIR_DATA_LEN);
        g_has_payload = 1u;
        gStat.payload_update++;
        return 1u;
    }
    if(g_has_payload != 0u)
    {
        memcpy(dst, g_last_payload, RFH_AIR_DATA_LEN);
        return 1u;
    }
    memset(dst, 0, RFH_AIR_DATA_LEN);
    return 0u;
}

static void tx_fill_connect_packet(void)
{
    uint8_t *air = &TxBuf[2];
    uint8_t *data = &air[RFH_DATA_OFFSET];

    TxBuf[0] = RFH_WCH_PREAMBLE;
    TxBuf[1] = RFH_AIR_PACKET_LEN;
    air[RFH_HDR0_OFFSET] = rfh_make_header0(RFH_PKT_CONNECT,
                                            g_rate_code,
                                            RFH_FLAG_DUAL_REDUNDANT);
    air[RFH_HDR1_OFFSET] = tx_ack_countdown_field();
    rfh_put_u32(&data[RFH_CONNECT_SESSION0], RFH_CONNECT_SESSION_ID);
    data[RFH_CONNECT_RATE] = g_rate_code;
    data[RFH_CONNECT_CH_A] = g_connect_channel_a;
    data[RFH_CONNECT_CH_B] = g_connect_channel_b;
    data[RFH_CONNECT_ACK_WINDOW_MS] = g_ack_window_ms;
    data[RFH_CONNECT_OPTIONS] = RFH_FLAG_DUAL_REDUNDANT;
    data[RFH_CONNECT_VERSION] = RFH_PROTOCOL_VERSION;
}

static void tx_fill_hop_prepare_packet(uint8_t *air)
{
    uint8_t *data = &air[RFH_DATA_OFFSET];

    air[RFH_HDR0_OFFSET] = rfh_make_header0(RFH_PKT_DATA,
                                            g_rate_code,
                                            RFH_FLAG_LINK_OK | RFH_FLAG_CMD_PRESENT);
    air[RFH_HDR1_OFFSET] = tx_ack_countdown_field();
    memset(data, 0, RFH_AIR_DATA_LEN);
    data[RFH_CMD_SLOT_ID] = RFH_CMD_HOP_PREPARE;
    data[RFH_HOP_CMD_CHANNEL] = g_target_channel;
    rfh_put_u16(&data[RFH_HOP_CMD_DELAY_LO_MS], tx_ms_until(g_hop_due_clock));
    data[RFH_HOP_CMD_SEQ] = g_hop_seq;
}

static void tx_fill_hop_confirm_packet(uint8_t *air)
{
    uint8_t *data = &air[RFH_DATA_OFFSET];

    air[RFH_HDR0_OFFSET] = rfh_make_header0(RFH_PKT_DATA,
                                            g_rate_code,
                                            RFH_FLAG_LINK_OK | RFH_FLAG_CMD_PRESENT);
    air[RFH_HDR1_OFFSET] = tx_ack_countdown_field();
    memset(data, 0, RFH_AIR_DATA_LEN);
    data[RFH_CMD_SLOT_ID] = RFH_CMD_HOP_CONFIRM;
    data[RFH_HOP_CMD_CHANNEL] = g_target_channel;
    data[RFH_HOP_CMD_SEQ] = g_hop_seq;
    data[RFH_HOP_CONFIRM_OLD_CHANNEL] = g_old_channel;
}

static void tx_fill_data_packet(void)
{
    uint8_t *air = &TxBuf[2];
    uint8_t *data = &air[RFH_DATA_OFFSET];

    TxBuf[0] = RFH_WCH_PREAMBLE;
    TxBuf[1] = RFH_AIR_PACKET_LEN;

    if(g_state == TX_HOP_PREPARE_ACK_WAIT)
    {
        tx_fill_hop_prepare_packet(air);
        return;
    }
    if(g_state == TX_HOP_CONFIRM_ACK_WAIT)
    {
        tx_fill_hop_confirm_packet(air);
        return;
    }

    air[RFH_HDR0_OFFSET] = rfh_make_header0(RFH_PKT_DATA,
                                            g_rate_code,
                                            RFH_FLAG_LINK_OK);
    air[RFH_HDR1_OFFSET] = tx_ack_countdown_field();
    (void)tx_load_latest_payload(data);
}

__HIGH_CODE
static void tx_start_tx_packet(uint8_t channel)
{
    bStatus_t ret_start;
    bStatus_t ret_parm;

    if(g_basic_started == 0u)
    {
        return;
    }
    if(g_ack_rx_active != 0u)
    {
        (void)RFRole_Stop();
        g_ack_rx_active = 0u;
    }

    g_radio_channel = channel;
    gTxParam.frequency = channel;
    gTxParam.whiteChannel = channel;
    gTxParam.txDMA = (uint32_t)TxBuf;
    gStat.tx_try++;
    ret_start = RFIP_SetTxStart();
    g_low_tx_ret = (uint8_t)ret_start;
    if(ret_start != SUCCESS)
    {
        gStat.tx_start_fail++;
        gStat.tx_fail++;
        tx_log_note_error();
        return;
    }

    ret_parm = RFIP_SetTxParm(&gTxParam);
    if(ret_parm != SUCCESS)
    {
        gStat.tx_parm_fail++;
        gStat.tx_fail++;
        tx_log_note_error();
    }
}

__HIGH_CODE
static void tx_start_ack_rx(uint8_t channel)
{
    if(g_basic_started == 0u)
    {
        return;
    }
    if((g_ack_rx_active != 0u) && (g_ack_rx_channel == channel))
    {
        return;
    }

    (void)RFRole_Stop();
    gRxParam.frequency = channel;
    gRxParam.whiteChannel = channel;
    gRxParam.rxDMA = (uint32_t)RxBuf;
    gRxParam.rxMaxLen = RFH_AIR_PACKET_LEN;
    gRxParam.timeOut = RF_ACK_RX_TIMEOUT_US;
    gStat.rx_arm_try++;
    g_low_rx_ret = (uint8_t)RFIP_SetRx(&gRxParam);
    if(g_low_rx_ret == SUCCESS)
    {
        g_ack_rx_active = 1u;
        g_ack_rx_channel = channel;
        gStat.rx_arm_ok++;
    }
    else
    {
        gStat.rx_arm_fail++;
        tx_log_note_error();
    }
}

static void tx_rearm_ack_rx_if_window_open(void)
{
    uint8_t channel;

    if(tx_in_ack_window() == 0u)
    {
        g_ack_rx_active = 0u;
        return;
    }

    channel = tx_ack_channel_for_tick();
    g_ack_rx_active = 0u;
    tx_start_ack_rx(channel);
}

static void tx_stop_ack_rx(void)
{
    if(g_ack_rx_active == 0u)
    {
        return;
    }
    (void)RFRole_Stop();
    g_ack_rx_active = 0u;
    g_ack_rx_channel = g_current_channel;
}

static uint8_t tx_accept_ack(const uint8_t *air, const uint8_t *data)
{
    uint8_t cmd = data[RFH_ACK_CMD_ID];
    uint8_t status = data[RFH_ACK_STATUS];
    uint8_t channel = data[RFH_ACK_CHANNEL];

    if(rfh_packet_type(air[RFH_HDR0_OFFSET]) != RFH_PKT_ACK)
    {
        return 0u;
    }
    if(rfh_channel_valid(channel) == 0u)
    {
        return 0u;
    }

    g_last_ack_loss_permille = rfh_get_u16(&data[RFH_ACK_LOSS_PERMILLE_LO]);
    g_last_ack_rx_count = rfh_get_u16(&data[RFH_ACK_RX_COUNT_LO]);
    g_last_ack_expected = rfh_get_u16(&data[RFH_ACK_EXPECTED_COUNT_LO]);
    g_last_ack_cmd = cmd;
    g_last_ack_status = status;
    g_ack_miss_count = 0u;
    gStat.rx_ack++;
    if(g_ack_seen_this_period == 0u)
    {
        g_ack_seen_this_period = 1u;
        if(g_log_ack_ok < 255u)
        {
            g_log_ack_ok++;
        }
    }

    switch(g_state)
    {
    case TX_UNCONNECTED:
        if((cmd == RFH_CMD_CONNECT_REQ) && (status == RFH_ACK_STATUS_CONNECTED))
        {
            g_current_channel = channel;
            tx_enter_state(TX_COMM);
            return 1u;
        }
        break;
    case TX_COMM:
        if((RF_AUTO_HOP_ENABLE != 0u) &&
           (g_last_ack_loss_permille >= RF_HOP_LOSS_THRESHOLD_PERMILLE) &&
           (tx_time_reached(TMOS_GetSystemClock(), g_hop_cooldown_until) != 0u))
        {
            tx_start_hop_prepare(tx_next_channel(g_current_channel));
        }
        return 1u;
    case TX_HOP_PREPARE_ACK_WAIT:
        if(cmd == RFH_CMD_HOP_PREPARE)
        {
            tx_enter_state(TX_HOP_RESERVED);
            return 1u;
        }
        break;
    case TX_HOP_RESERVED:
        return 1u;
    case TX_HOP_CONFIRM_ACK_WAIT:
        if((cmd == RFH_CMD_HOP_CONFIRM) && (channel == g_target_channel))
        {
            g_current_channel = g_target_channel;
            tx_enter_state(TX_COMM);
            return 1u;
        }
        break;
    case TX_RECOVERY_DUAL:
        if((cmd == RFH_CMD_CONNECT_REQ) || (cmd == RFH_CMD_HOP_CONFIRM))
        {
            g_current_channel = channel;
            tx_enter_state(TX_COMM);
            return 1u;
        }
        break;
    default:
        break;
    }

    return 0u;
}

static void tx_handle_ack_packet(void)
{
    const uint8_t *air = &RxBuf[2];
    const uint8_t *data = &air[RFH_DATA_OFFSET];

    if(RxBuf[1] != RFH_AIR_PACKET_LEN)
    {
        gStat.rx_bad_ack++;
        tx_log_note_error();
        return;
    }

    if(tx_accept_ack(air, data) == 0u)
    {
        gStat.rx_bad_ack++;
        tx_log_note_error();
    }
}

static void tx_service_timers(void)
{
    uint32_t now = TMOS_GetSystemClock();

    if((g_state == TX_HOP_PREPARE_ACK_WAIT) &&
       (tx_time_reached(now, g_wait_ack_deadline_clock) != 0u))
    {
        tx_enter_state(TX_RECOVERY_DUAL);
        return;
    }
    if((g_state == TX_HOP_RESERVED) &&
       (tx_time_reached(now, g_hop_due_clock) != 0u))
    {
        g_current_channel = g_target_channel;
        tx_enter_state(TX_HOP_CONFIRM_ACK_WAIT);
        return;
    }
    if((g_state == TX_HOP_CONFIRM_ACK_WAIT) &&
       (tx_time_reached(now, g_wait_ack_deadline_clock) != 0u))
    {
        tx_enter_state(TX_RECOVERY_DUAL);
        return;
    }
    if((g_state == TX_RECOVERY_DUAL) &&
       (tx_time_reached(now, g_recovery_deadline_clock) != 0u))
    {
        tx_enter_state(TX_UNCONNECTED);
    }
}

static uint8_t tx_channel_for_state(void)
{
    switch(g_state)
    {
    case TX_UNCONNECTED:
    case TX_RECOVERY_DUAL:
        return tx_dual_channel_for_tick();
    case TX_HOP_CONFIRM_ACK_WAIT:
        return g_target_channel;
    case TX_COMM:
    case TX_HOP_PREPARE_ACK_WAIT:
    case TX_HOP_RESERVED:
    default:
        return g_current_channel;
    }
}

static void tx_fill_packet_for_state(void)
{
    switch(g_state)
    {
    case TX_UNCONNECTED:
    case TX_RECOVERY_DUAL:
        tx_fill_connect_packet();
        break;
    case TX_COMM:
    case TX_HOP_PREPARE_ACK_WAIT:
    case TX_HOP_RESERVED:
    case TX_HOP_CONFIRM_ACK_WAIT:
    default:
        tx_fill_data_packet();
        break;
    }
}

static void tx_log_5s_emit(void)
{
    uint16_t loss = g_last_ack_loss_permille;
    uint8_t ack_exp = g_log_ack_expected;
    uint8_t ack_ok = (g_log_ack_ok > 99u) ? 99u : g_log_ack_ok;

    if(loss > 1000u)
    {
        loss = 1000u;
    }
    if(ack_exp == 0u)
    {
        ack_exp = 5u;
    }

    if((g_state == TX_UNCONNECTED) || (g_state == TX_RECOVERY_DUAL))
    {
        RF_LINK_LOG("T5 S=%s C=%u/%u R=%s L=%03u A=%u/%u M=%u H=%u U=%u E=%u\r\n",
                    tx_state_code(),
                    (unsigned int)g_dual_channel_a,
                    (unsigned int)g_dual_channel_b,
                    tx_rate_code(),
                    (unsigned int)loss,
                    (unsigned int)ack_ok,
                    (unsigned int)ack_exp,
                    (unsigned int)g_ack_miss_count,
                    (unsigned int)g_log_hop_events,
                    (unsigned int)g_log_unconnected_events,
                    (unsigned int)g_log_errors);
    }
    else if((g_state == TX_HOP_PREPARE_ACK_WAIT) ||
            (g_state == TX_HOP_RESERVED) ||
            (g_state == TX_HOP_CONFIRM_ACK_WAIT))
    {
        RF_LINK_LOG("T5 S=%s C=%u>%u R=%s L=%03u A=%u/%u M=%u H=%u U=%u E=%u\r\n",
                    tx_state_code(),
                    (unsigned int)g_old_channel,
                    (unsigned int)g_target_channel,
                    tx_rate_code(),
                    (unsigned int)loss,
                    (unsigned int)ack_ok,
                    (unsigned int)ack_exp,
                    (unsigned int)g_ack_miss_count,
                    (unsigned int)g_log_hop_events,
                    (unsigned int)g_log_unconnected_events,
                    (unsigned int)g_log_errors);
    }
    else
    {
        RF_LINK_LOG("T5 S=%s C=%u R=%s L=%03u A=%u/%u M=%u H=%u U=%u E=%u\r\n",
                    tx_state_code(),
                    (unsigned int)g_current_channel,
                    tx_rate_code(),
                    (unsigned int)loss,
                    (unsigned int)ack_ok,
                    (unsigned int)ack_exp,
                    (unsigned int)g_ack_miss_count,
                    (unsigned int)g_log_hop_events,
                    (unsigned int)g_log_unconnected_events,
                    (unsigned int)g_log_errors);
    }

    RF_LINK_LOG("TD X=%lu/%lu/%lu/%lu RA=%lu/%lu A=%lu/%lu SPI=%lu/%lu\r\n",
                (unsigned long)gStat.tx_try,
                (unsigned long)gStat.tx_ok,
                (unsigned long)gStat.tx_idle,
                (unsigned long)gStat.tx_fail,
                (unsigned long)gStat.rx_arm_try,
                (unsigned long)gStat.rx_arm_fail,
                (unsigned long)gStat.rx_ack,
                (unsigned long)gStat.rx_bad_ack,
                (unsigned long)gStat.spi_rx_win,
                (unsigned long)gStat.payload_update);

    g_log_ack_ok = 0u;
    g_log_ack_expected = 0u;
    g_log_hop_events = 0u;
    g_log_unconnected_events = 0u;
    g_log_errors = 0u;
}

static void tx_basic_start(void)
{
    rfRoleConfig_t conf = {0};
    bStatus_t ret;

    conf.TxPower = BLE_TX_POWER;
    conf.rfProcessCB = RF_ProcessCallBack;
    conf.processMask = RF_STATE_RX | RF_STATE_RX_CRCERR |
                       RF_STATE_TX_FINISH | RF_STATE_TIMEOUT | RF_STATE_TX_IDLE;
    ret = RFRole_BasicInit(&conf);
    g_low_config_ret = (uint8_t)ret;
    if(ret != SUCCESS)
    {
        return;
    }

    gParm.accessAddress = RF_LINK_ACCESS_ADDRESS;
    gParm.crcInit = RF_LINK_CRC_INIT;
    gParm.properties = LLE_MODE_PHY_2M;
    gParm.sendTime = RF_TX_SEND_TIME;
    RFRole_SetParam(&gParm);

    gTxParam.accessAddress = gParm.accessAddress;
    gTxParam.crcInit = gParm.crcInit;
    gTxParam.properties = gParm.properties;
    gTxParam.frequency = g_current_channel;
    gTxParam.whiteChannel = g_current_channel;
    gTxParam.sendTime = (uint8_t)gParm.sendTime;
    gTxParam.sendCount = 1u;
    gTxParam.txDMA = (uint32_t)TxBuf;

    gRxParam.accessAddress = gParm.accessAddress;
    gRxParam.crcInit = gParm.crcInit;
    gRxParam.properties = gParm.properties;
    gRxParam.frequency = g_current_channel;
    gRxParam.whiteChannel = g_current_channel;
    gRxParam.rxDMA = (uint32_t)RxBuf;
    gRxParam.rxMaxLen = RFH_AIR_PACKET_LEN;
    gRxParam.timeOut = RF_ACK_RX_TIMEOUT_US;

    g_basic_started = 1u;
    RF_LINK_LOG("[TX][RFH] cfg:%u rate:%u r:%u/%u ack:%uus len:%u\r\n",
                (unsigned int)g_low_config_ret,
                (unsigned int)g_report_hz,
                (unsigned int)g_connect_channel_a,
                (unsigned int)g_connect_channel_b,
                (unsigned int)RF_ACK_WINDOW_US,
                RFH_AIR_PACKET_LEN);
}

__INTERRUPT
__HIGH_CODE
void TMR0_IRQHandler(void)
{
    uint8_t channel;

    if(TMR0_GetITFlag(TMR0_3_IT_CYC_END) == 0u)
    {
        return;
    }

    TMR0_ClearITFlag(TMR0_3_IT_CYC_END);

    if(g_basic_started == 0u)
    {
        tx_advance_tick();
        return;
    }

    tx_service_timers();
    if(tx_in_ack_window() != 0u)
    {
        tx_start_ack_rx(tx_ack_channel_for_tick());
        tx_advance_tick();
        return;
    }

    tx_stop_ack_rx();
    channel = tx_channel_for_state();
    tx_fill_packet_for_state();
    tx_start_tx_packet(channel);
    tx_advance_tick();
}

__HIGH_CODE
void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id)
{
    (void)id;

    if(sta & RF_STATE_TX_FINISH)
    {
        gStat.tx_ok++;
    }
    if(sta & RF_STATE_TX_IDLE)
    {
        gStat.tx_idle++;
    }
    if(sta & RF_STATE_RX)
    {
        if(g_ack_rx_active != 0u)
        {
            tx_handle_ack_packet();
            tx_rearm_ack_rx_if_window_open();
        }
    }
    if(sta & RF_STATE_RX_CRCERR)
    {
        if(g_ack_rx_active != 0u)
        {
            gStat.rx_bad_ack++;
            tx_log_note_error();
            tx_rearm_ack_rx_if_window_open();
        }
    }
    if(sta & RF_STATE_TIMEOUT)
    {
        if(g_ack_rx_active != 0u)
        {
            g_ack_rx_active = 0u;
            gStat.rx_bad_ack++;
            tx_log_note_error();
            tx_rearm_ack_rx_if_window_open();
        }
        else
        {
            gStat.tx_fail++;
            tx_log_note_error();
        }
    }
}

uint16_t RF_ProcessEvent(uint8_t task_id, uint16_t events)
{
    if(events & SYS_EVENT_MSG)
    {
        uint8_t *pMsg;

        if((pMsg = tmos_msg_receive(task_id)) != NULL)
        {
            tmos_msg_deallocate(pMsg);
        }
        return (events ^ SYS_EVENT_MSG);
    }

    if(events & SBP_RF_STAT_EVT)
    {
        tx_log_5s_emit();
        gStat.tx_try = 0u;
        gStat.tx_ok = 0u;
        gStat.tx_idle = 0u;
        gStat.tx_fail = 0u;
        gStat.tx_start_fail = 0u;
        gStat.tx_parm_fail = 0u;
        gStat.rx_arm_try = 0u;
        gStat.rx_arm_ok = 0u;
        gStat.rx_arm_fail = 0u;
        gStat.rx_ack = 0u;
        gStat.rx_bad_ack = 0u;
        gStat.spi_rx_win = 0u;
        gStat.payload_update = 0u;
        tmos_start_task(taskID, SBP_RF_STAT_EVT, MS1_TO_SYSTEM_TIME(RF_STAT_PRINT_PERIOD_MS));
        return (events ^ SBP_RF_STAT_EVT);
    }

    return 0u;
}

__HIGH_CODE
void RF_TxMainLoopProcess(void)
{
}

bool RF_SPI_FastWriteInput(const uint8_t *payload, uint8_t len)
{
    if((payload == NULL) || (len != RFH_AIR_DATA_LEN))
    {
        return false;
    }

    PFIC_DisableIRQ(TMR0_IRQn);
    if(!rfm_input_stream_push(payload, len))
    {
        PFIC_EnableIRQ(TMR0_IRQn);
        return false;
    }
    PFIC_EnableIRQ(TMR0_IRQn);

    gStat.spi_rx_total++;
    gStat.spi_rx_win++;
    return true;
}

void RF_Init(void)
{
    taskID = TMOS_ProcessEventRegister(RF_ProcessEvent);

    PFIC_EnableIRQ(BLEB_IRQn);
    PFIC_EnableIRQ(BLEL_IRQn);

    rfm_input_stream_init();
    memset(g_last_payload, 0, sizeof(g_last_payload));
    g_has_payload = 0u;
    g_report_hz = RF_REPORT_PPS;
    if((g_report_hz != 1000u) && (g_report_hz != 2000u) &&
       (g_report_hz != 4000u) && (g_report_hz != 8000u))
    {
        g_report_hz = RFH_DEFAULT_RATE_HZ;
    }
    g_rate_code = rfh_rate_code_from_hz(g_report_hz);
    g_ack_window_ms = RF_ACK_WINDOW_MS;
    if(g_ack_window_ms == 0u)
    {
        g_ack_window_ms = RFH_DEFAULT_ACK_WINDOW_MS;
    }
    g_ack_window_packets = rfh_ack_window_packets_us(g_report_hz,
                                                     RF_ACK_WINDOW_US);
    g_second_pos = 0u;
    g_dual_pos = 0u;
    g_current_channel = RFH_DISCOVERY_CHANNEL_A;
    g_radio_channel = RFH_DISCOVERY_CHANNEL_A;
    g_ack_rx_channel = RFH_DISCOVERY_CHANNEL_A;
    g_dual_channel_a = RFH_DISCOVERY_CHANNEL_A;
    g_dual_channel_b = RFH_DISCOVERY_CHANNEL_B;
    g_connect_channel_a = RFH_DEFAULT_CHANNEL_A;
    g_connect_channel_b = RFH_DEFAULT_CHANNEL_B;
    g_old_channel = RFH_DISCOVERY_CHANNEL_A;
    g_target_channel = RFH_DISCOVERY_CHANNEL_A;
    g_ack_rx_active = 0u;
    g_log_ack_ok = 0u;
    g_log_ack_expected = 0u;
    g_ack_period_close_pending = 0u;
    g_log_hop_events = 0u;
    g_log_unconnected_events = 0u;
    g_log_errors = 0u;
    g_state = TX_COMM;
    tx_enter_state(TX_UNCONNECTED);

    tmos_start_task(taskID, SBP_RF_STAT_EVT, MS1_TO_SYSTEM_TIME(RF_STAT_PRINT_PERIOD_MS));
    tx_basic_start();

    g_tick_per_evt = GetSysClock() / g_report_hz;
    if(g_tick_per_evt == 0u)
    {
        g_tick_per_evt = 1u;
    }
    TMR0_TimerInit(g_tick_per_evt);
    TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    PFIC_SetPriority(TMR0_IRQn, 0x80);
    PFIC_EnableIRQ(TMR0_IRQn);
}

/******************************** endfile @ RF_PHY ******************************/
