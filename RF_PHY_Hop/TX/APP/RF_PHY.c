/********************************** (C) COPYRIGHT *******************************
 * File Name          : RF_PHY.c
 * Description        : 8K test flow based on WCH RF_Basic style
 *******************************************************************************/

#include "CONFIG.h"
#include "RF_PHY.h"
#include "HAL.h"
#include "wchrf.h"
#include "rfm_config.h"
#include "rfm_input_stream.h"
#include "rfm_spi_port_internal.h"

#include <string.h>

#ifndef RF_HOP_MODE
#define RF_HOP_MODE 1
#endif

#ifndef RF_TEST_FREQUENCY
#define RF_TEST_FREQUENCY           16
#endif
#ifndef RF_TEST_PROTOCOL_PACKET
#define RF_TEST_PROTOCOL_PACKET     0
#endif
#ifndef RF_TEST_ENABLE_HOP
#define RF_TEST_ENABLE_HOP          0
#endif
#if (RF_TEST_PROTOCOL_PACKET == 1)
#define RF_TEST_DATA_LEN            12
#else
#define RF_TEST_DATA_LEN            12
#endif
#ifndef RF_REPORT_PPS
#define RF_REPORT_PPS               8000
#endif
#define RF_STAT_PRINT_PERIOD_MS     5000

/* 反向链路窗口参数：TX 每 RF_REV_PERIOD_MS 打开一次 RX 窗口，用来接收 RX 发来的跳频请求。 */
#define RF_REV_LISTEN_WINDOW_MS     200     /* 真正停止正向 TX 并打开 RX 的时间；200ms 用于完整反向握手。 */
#define RF_REV_LISTEN_TIMEOUT_US    50000u  /* RFIP 单次 RX 超时，受 uint16 限制；200ms 逻辑窗口内会自动重启 RX。 */
#define RF_REV_PERIOD_MS            20000u  /* TX 打开反向监听窗口的周期；越大固定损耗越低，但质量触发响应越慢。 */
#define RF_REV_PERIOD_PACKETS       (RF_REPORT_PPS * RF_REV_PERIOD_MS / 1000u) /* 20s 对应的 8K tick 数。 */
#define RF_REV_LISTEN_PACKETS       (RF_REPORT_PPS * RF_REV_LISTEN_WINDOW_MS / 1000u) /* 5ms 对应的 8K tick 数。 */
#define RF_REV_COUNTDOWN_LEAD_MS    200u    /* 提前多久在 air[1] 广播倒计时，RX 用它对齐反向发送 burst。 */
#define RF_REV_COUNTDOWN_FAR        0xFFu   /* 不在倒计时窗口内时发送的控制值。 */
#define TMR0_FREE_RUN_END           0x03FFFFFFUL
#define RF_USE_LOW_LEVEL_BASIC      0
#define RF_TX_USE_TMR0_IRQ          1
#define RF_LINK_DEBUG_LOG           1

#define RF_PKT_MAGIC                0xA7u
#define RF_PKT_TYPE_DATA            0x01u
#define RF_PKT_SESSION_ID           0x21u
#define RF_PKT_HOP_EPOCH            0u
#define RF_PKT_FLAGS_NONE           0u
#define RF_PKT_HEADER_LEN           6u
#define RF_PKT_INPUT_PAYLOAD_LEN    4u
#define RF_PKT_CRC_LEN              2u
#define RF_HOP_DWELL_PACKETS        16u     /* 旧的匀速跳频 dwell 参数；当前智能跳频路径不依赖它。 */
#define RF_HOP_CHANNEL_COUNT        9u      /* 固定跳频表长度，TX/RX 必须一致。 */
#define RF_TX_SEND_TIME             (20u * 2u) /* RFIP 发送时序参数；改动会影响空口占用和稳定性。 */
#define RF_LINK_ACCESS_ADDRESS      0x71764129UL /* 固定 access address，TX/RX 必须一致。 */
#define RF_LINK_CRC_INIT            0x555555UL   /* 固定 CRC init，TX/RX 必须一致。 */
#define RF_BUTTON_BYTES             3u
#define RF_SEQ_OFFSET               0u
#define RF_HOP_ADV_OFFSET           1u
#define RF_DATA_OFFSET              2u
#define RF_DATA_BYTES               (RF_TEST_DATA_LEN - RF_DATA_OFFSET)
#define RF_HOP_ADV_IDLE             0u
#define RF_HOP_ADV_MARK             0xA5u
#define RF_HOP_ADV_EPOCH_OFFSET     2u
#define RF_HOP_ADV_CHANNEL_OFFSET   3u
#define RF_HOP_ADV_SWITCH_OFFSET    4u
#define RF_HOP_ADV_REPEAT_PACKETS   32u     /* 旧 HOP_ADV 连发次数；当前 RX 反向请求跳频路径基本不使用。 */
#define RF_HOP_SWITCH_DELAY_PACKETS 200u    /* TX 收到跳频请求后延迟多少个 8K 包再切频道，200 包约 25ms。 */
#define RF_REV_REQ_MARK             0xC3u   /* RX 反向跳频请求包标记。 */
#define RF_REV_REQ_EPOCH_OFFSET     1u
#define RF_REV_REQ_CHANNEL_OFFSET   2u
#define RF_REV_REQ_REASON_OFFSET    3u
#define RF_REV_REQ_TIME_LO_OFFSET   4u
#define RF_REV_REQ_TIME_HI_OFFSET   5u
#define RF_REV_ACK_MARK             0xC4u   /* TX 正向 ACK 标记，ACK 会回带 RX 反向包时间戳。 */
#define RF_REV_ACK_TIME_LO_OFFSET   2u
#define RF_REV_ACK_TIME_HI_OFFSET   3u
#define RF_REV_ACK_CHANNEL_OFFSET   4u
#define RF_REV_ACK_REASON_OFFSET    5u
#define RF_REV_ACK_EPOCH_OFFSET     6u
#define RF_REV_ACK_SWITCH_OFFSET    7u
#define RF_REV_ACK_REPEAT_PACKETS   32u     /* 在旧频道连续广播 HOP_ACK 的包数；32 包约 4ms。 */
#define RF_REV_SWITCH_DELAY_PACKETS 64u     /* 预约切换延迟；64 包约 8ms，保证 RX 有时间收到多个 ACK。 */

#define SBP_RF_STAT_EVT              (1 << 5)

#if (RF_LINK_DEBUG_LOG == 1)
#define RF_LINK_LOG(...)            PRINT(__VA_ARGS__)
#else
#define RF_LINK_LOG(...)            ((void)0)
#endif

uint8_t taskID;

typedef struct
{
    volatile uint32_t tmr_tick_win;
    volatile uint32_t tmr_tick_total;
    volatile uint32_t tmr_irq_win;
    volatile uint32_t tmr_irq_total;
    volatile uint32_t sched_due_win;
    volatile uint32_t sched_due_total;
    volatile uint32_t sched_sent_win;
    volatile uint32_t sched_sent_total;
    volatile uint32_t sched_miss_win;
    volatile uint32_t sched_miss_total;
    volatile uint32_t tx_try;
    volatile uint32_t tx_ok;
    volatile uint32_t tx_fail;
    volatile uint32_t tx_idle;
    volatile uint32_t tx_start_fail;
    volatile uint32_t tx_parm_fail;
    volatile uint32_t tx_seq_rollback;
    volatile uint32_t tx_cb_other;
    volatile uint32_t payload_update;
    volatile uint32_t rx_total;
    volatile uint32_t rx_ok;
    volatile uint32_t rx_fail;
    volatile uint32_t spi_rx_total;
    volatile uint32_t spi_rx_win;
    volatile uint32_t rev_listen_win;
    volatile uint32_t rev_listen_timeout;
    volatile uint32_t rev_rx;
    volatile uint32_t rev_accept;
    volatile uint32_t rev_bad_len;
    volatile uint32_t rev_bad_mark;
    volatile uint32_t rev_crcerr;
    volatile uint32_t rev_start_fail;
    volatile uint32_t rev_ack_tx;
    volatile uint32_t hop_done;
} rf_stat_t;

static rfRoleParam_t gParm;
static rfipTx_t gTxParam;
static rfipRx_t gRxParam;
static rf_stat_t gStat = {0};
#if (RF_TX_USE_TMR0_IRQ == 0)
static uint32_t g_tmr_prev_cnt = 0;
static uint32_t g_tmr_acc_tick = 0;
#endif
static uint32_t g_tick_per_evt = 1;
static volatile uint8_t g_basic_started = 0;
static uint8_t g_spi_last_payload[RFM_RF_INPUT_PAYLOAD_LEN] = {0};
static uint8_t g_spi_has_payload = 0;
static uint32_t g_last_stat_clock = 0;
static uint32_t g_last_peek_ok = 0;
static uint32_t g_last_peek_miss = 0;
static volatile uint8_t g_low_config_ret = 0xFFu;
static volatile uint8_t g_low_channel = RF_TEST_FREQUENCY;
static volatile uint8_t g_low_tx_ret = 0xFFu;
static volatile uint8_t g_low_tx_inflight = 0;
static volatile uint8_t g_tx_channel = RF_TEST_FREQUENCY;
static volatile uint8_t g_tx_hop_active = 0u;
static volatile uint8_t g_tx_hop_silent = 0u;
static volatile uint8_t g_tx_hop_next_channel = RF_TEST_FREQUENCY;
static volatile uint8_t g_tx_hop_switch_seq = 0u;
static volatile uint8_t g_tx_hop_epoch = 0u;
static volatile uint8_t g_rev_listen_active = 0u;
static volatile uint8_t g_rev_listen_paused = 0u;
static volatile uint8_t g_rev_listen_pending = 0u;
static volatile uint8_t g_rev_last_len = 0u;
static volatile uint8_t g_rev_last_mark = 0u;
static volatile uint8_t g_rev_last_rxret = 0xFFu;
static volatile uint8_t g_rev_ack_pending = 0u;
static volatile uint8_t g_rev_ack_loaded = 0u;
static volatile uint8_t g_rev_ack_armed = 0u;
static volatile uint8_t g_rev_ack_tx_active = 0u;
static volatile uint8_t g_rev_ack_channel = RF_TEST_FREQUENCY;
static volatile uint8_t g_rev_ack_reason = 0u;
static volatile uint8_t g_rev_ack_epoch = 0u;
static volatile uint16_t g_rev_ack_time = 0u;
static volatile uint8_t g_rev_ack_switch_seq = 0u;
static volatile uint8_t g_rev_ack_repeat_remaining = 0u;
static volatile uint8_t g_rev_hop_pending = 0u;
static volatile uint16_t g_rev_listen_ticks_remaining = 0u;
static uint32_t g_tx_rev_packets_to_window = RF_REV_PERIOD_PACKETS;
static uint8_t g_tx_hop_index = 3u;
static uint8_t g_tx_seq = 0u;
static uint8_t g_tx_last_seq = 0u;

static const uint8_t g_hop_channels[RF_HOP_CHANNEL_COUNT] = {
    4u, 8u, 12u, 16u, 20u, 24u, 28u, 32u, 36u
};

void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id);
static void rf_fill_payload(void);

__attribute__((__aligned__(4))) static uint8_t TxBuf[64];
__attribute__((__aligned__(4))) static uint8_t RxBuf[264];

static uint16_t rf_crc16_ccitt(const uint8_t *data, uint8_t len)
{
    uint16_t crc = 0xFFFFu;
    uint8_t i;
    uint8_t bit;

    for(i = 0u; i < len; ++i)
    {
        crc ^= (uint16_t)data[i] << 8;
        for(bit = 0u; bit < 8u; ++bit)
        {
            if((crc & 0x8000u) != 0u)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            }
            else
            {
                crc <<= 1;
            }
        }
    }

    return crc;
}

static uint8_t rf_hop_channel_for_seq(uint8_t seq)
{
#if (RF_TEST_ENABLE_HOP == 1)
    uint8_t index = (uint8_t)((seq / RF_HOP_DWELL_PACKETS) % RF_HOP_CHANNEL_COUNT);
    return g_hop_channels[index];
#else
    (void)seq;
    return g_tx_channel;
#endif
}

static uint8_t rf_hop_index_for_channel(uint8_t channel)
{
    uint8_t i;

    for(i = 0u; i < RF_HOP_CHANNEL_COUNT; ++i)
    {
        if(g_hop_channels[i] == channel)
        {
            return i;
        }
    }
    return 0u;
}

static uint8_t rf_hop_channel_valid(uint8_t channel)
{
    uint8_t i;

    for(i = 0u; i < RF_HOP_CHANNEL_COUNT; ++i)
    {
        if(g_hop_channels[i] == channel)
        {
            return 1u;
        }
    }
    return 0u;
}

static void rf_tx_prepare_channel_for_seq(uint8_t seq)
{
    gTxParam.frequency = rf_hop_channel_for_seq(seq);
    gTxParam.whiteChannel = gTxParam.frequency;
    g_low_channel = (uint8_t)gTxParam.frequency;
}

static void rf_tx_begin_requested_hop(uint8_t next_channel)
{
    if(g_tx_hop_active != 0u)
    {
        return;
    }
    if((rf_hop_channel_valid(next_channel) == 0u) || (next_channel == g_tx_channel))
    {
        return;
    }

    g_tx_hop_next_channel = next_channel;
    g_tx_hop_switch_seq = (uint8_t)(g_tx_seq + RF_HOP_SWITCH_DELAY_PACKETS);
    g_tx_hop_epoch++;
    g_tx_hop_silent = 0u;
    g_tx_hop_active = 1u;
    gStat.rev_accept++;
}

static void rf_tx_resume_8k(void)
{
    if(g_rev_listen_paused == 0u)
    {
        return;
    }
    g_rev_listen_paused = 0u;
}

static void rf_tx_force_stop_reverse_rx(void)
{
    if(g_rev_listen_active == 0u)
    {
        return;
    }

    (void)RFRole_Stop();
    g_rev_listen_active = 0u;
    g_rev_listen_ticks_remaining = 0u;
    gStat.rev_listen_timeout++;
    rf_tx_resume_8k();
}

static void rf_tx_reverse_listen_tick(void)
{
    if(g_rev_listen_active == 0u)
    {
        return;
    }

    if(g_rev_listen_ticks_remaining != 0u)
    {
        g_rev_listen_ticks_remaining--;
    }
    if(g_rev_listen_ticks_remaining == 0u)
    {
        rf_tx_force_stop_reverse_rx();
    }
}

static void rf_tx_start_reverse_listen(void)
{
    if((g_basic_started == 0u) || (g_tx_hop_active != 0u))
    {
        return;
    }

    g_rev_listen_active = 1u;
    g_rev_listen_paused = 1u;
    g_rev_listen_pending = 0u;
    g_rev_listen_ticks_remaining = RF_REV_LISTEN_PACKETS;
    gStat.rev_listen_win++;

    gRxParam.frequency = g_tx_channel;
    gRxParam.whiteChannel = g_tx_channel;
    gRxParam.rxDMA = (uint32_t)RxBuf;
    gRxParam.rxMaxLen = RF_TEST_DATA_LEN;
    gRxParam.timeOut = RF_REV_LISTEN_TIMEOUT_US;
    (void)RFRole_Stop();
    g_rev_last_rxret = (uint8_t)RFIP_SetRx(&gRxParam);
    if(g_rev_last_rxret != SUCCESS)
    {
        gStat.rev_start_fail++;
        g_rev_listen_active = 0u;
        g_rev_listen_ticks_remaining = 0u;
        rf_tx_resume_8k();
    }
}

static void rf_tx_restart_reverse_listen(void)
{
    if(g_rev_listen_active == 0u)
    {
        return;
    }

    gRxParam.frequency = g_tx_channel;
    gRxParam.whiteChannel = g_tx_channel;
    gRxParam.rxDMA = (uint32_t)RxBuf;
    gRxParam.rxMaxLen = RF_TEST_DATA_LEN;
    gRxParam.timeOut = RF_REV_LISTEN_TIMEOUT_US;
    (void)RFRole_Stop();
    g_rev_last_rxret = (uint8_t)RFIP_SetRx(&gRxParam);
    if(g_rev_last_rxret != SUCCESS)
    {
        gStat.rev_start_fail++;
    }
}

static void rf_tx_finish_reverse_listen(uint8_t schedule_next)
{
    (void)RFRole_Stop();
    g_rev_listen_active = 0u;
    g_rev_listen_ticks_remaining = 0u;
    rf_tx_resume_8k();
    (void)schedule_next;
}

static uint8_t rf_tx_handle_reverse_packet(void)
{
    const uint8_t *packet = &RxBuf[2];
    uint8_t next_channel;
    uint16_t rx_time;

    g_rev_last_len = RxBuf[1];
    g_rev_last_mark = packet[0];
    if(RxBuf[1] != RF_TEST_DATA_LEN)
    {
        gStat.rev_bad_len++;
        return 0u;
    }
    if(packet[0] != RF_REV_REQ_MARK)
    {
        gStat.rev_bad_mark++;
        return 0u;
    }

    gStat.rev_rx++;
    next_channel = packet[RF_REV_REQ_CHANNEL_OFFSET];
    if(rf_hop_channel_valid(next_channel) == 0u)
    {
        gStat.rev_bad_mark++;
        return 0u;
    }

    rx_time = (uint16_t)packet[RF_REV_REQ_TIME_LO_OFFSET] |
              ((uint16_t)packet[RF_REV_REQ_TIME_HI_OFFSET] << 8);
    g_rev_ack_time = rx_time;
    g_rev_ack_channel = next_channel;
    g_rev_ack_reason = packet[RF_REV_REQ_REASON_OFFSET];
    g_rev_ack_epoch = packet[RF_REV_REQ_EPOCH_OFFSET];
    g_rev_ack_switch_seq = (uint8_t)(g_tx_seq + RF_REV_SWITCH_DELAY_PACKETS);
    g_rev_ack_repeat_remaining = RF_REV_ACK_REPEAT_PACKETS;
    g_rev_hop_pending = 1u;
    g_rev_ack_pending = 1u;
    gStat.rev_accept++;
    return 1u;
}

__HIGH_CODE
static void rf_tx_start(void)
{
    bStatus_t ret_start;
    bStatus_t ret_parm;
    uint8_t retry_seq = g_tx_last_seq;
    ret_start = RFIP_SetTxStart();
    if(ret_start != SUCCESS)
    {
        if(g_rev_ack_loaded != 0u)
        {
            g_rev_ack_pending = 1u;
            g_rev_ack_loaded = 0u;
        }
        g_tx_seq = retry_seq;
        gStat.tx_start_fail++;
        gStat.tx_seq_rollback++;
        return;
    }

    rf_tx_prepare_channel_for_seq(g_tx_seq);
    gTxParam.txDMA = (uint32_t)TxBuf;
    ret_parm = RFIP_SetTxParm(&gTxParam);
    if(ret_parm != SUCCESS)
    {
        if(g_rev_ack_loaded != 0u)
        {
            g_rev_ack_pending = 1u;
            g_rev_ack_loaded = 0u;
        }
        g_tx_seq = retry_seq;
        gStat.tx_parm_fail++;
        gStat.tx_seq_rollback++;
        return;
    }
    if(g_rev_ack_loaded != 0u)
    {
        g_rev_ack_loaded = 0u;
        g_rev_ack_armed = 1u;
        gStat.rev_ack_tx++;
    }
}

static uint8_t rf_tx_reverse_countdown_field(void)
{
    uint32_t lead_packets = RF_REPORT_PPS * RF_REV_COUNTDOWN_LEAD_MS / 1000u;
    uint32_t countdown_ms;

    if(g_tx_rev_packets_to_window > lead_packets)
    {
        return RF_REV_COUNTDOWN_FAR;
    }

    countdown_ms = (g_tx_rev_packets_to_window + ((RF_REPORT_PPS / 1000u) - 1u)) / (RF_REPORT_PPS / 1000u);
    if(countdown_ms > 254u)
    {
        countdown_ms = 254u;
    }
    /* countdown 和控制标记共用 air[1]，避开保留标记值，防止 RX 把普通倒计时包误判成控制包。 */
    if((countdown_ms == RF_HOP_ADV_MARK) || (countdown_ms == RF_REV_ACK_MARK))
    {
        countdown_ms--;
    }
    return (uint8_t)countdown_ms;
}

static void rf_tx_fill_ack_payload(void)
{
    TxBuf[2u + RF_HOP_ADV_OFFSET] = RF_REV_ACK_MARK;
    TxBuf[2u + RF_REV_ACK_TIME_LO_OFFSET] = (uint8_t)(g_rev_ack_time & 0xFFu);
    TxBuf[2u + RF_REV_ACK_TIME_HI_OFFSET] = (uint8_t)(g_rev_ack_time >> 8);
    TxBuf[2u + RF_REV_ACK_CHANNEL_OFFSET] = g_rev_ack_channel;
    TxBuf[2u + RF_REV_ACK_REASON_OFFSET] = g_rev_ack_reason;
    TxBuf[2u + RF_REV_ACK_EPOCH_OFFSET] = g_rev_ack_epoch;
    TxBuf[2u + RF_REV_ACK_SWITCH_OFFSET] = g_rev_ack_switch_seq;
    if(g_rev_ack_repeat_remaining != 0u)
    {
        g_rev_ack_repeat_remaining--;
    }
    if(g_rev_ack_repeat_remaining != 0u)
    {
        g_rev_ack_pending = 1u;
    }
    else
    {
        g_rev_ack_pending = 0u;
    }
    g_rev_ack_loaded = 1u;
}

static void rf_tx_advance_reverse_time(void)
{
    if(g_tx_rev_packets_to_window != 0u)
    {
        g_tx_rev_packets_to_window--;
    }
    if(g_tx_rev_packets_to_window == 0u)
    {
        g_tx_rev_packets_to_window = RF_REV_PERIOD_PACKETS;
        g_rev_listen_pending = 1u;
    }
}

#if (RF_TX_USE_TMR0_IRQ == 1)
__INTERRUPT
__HIGH_CODE
void TMR0_IRQHandler(void)
{
    if(TMR0_GetITFlag(TMR0_3_IT_CYC_END))
    {
        TMR0_ClearITFlag(TMR0_3_IT_CYC_END);
        gStat.tmr_irq_win++;
        gStat.tmr_irq_total++;
        gStat.sched_due_win++;
        gStat.sched_due_total++;

        if(g_rev_listen_active != 0u)
        {
            rf_tx_reverse_listen_tick();
            return;
        }

        if(g_rev_listen_pending != 0u)
        {
            rf_tx_start_reverse_listen();
            return;
        }

        if(g_rev_listen_paused != 0u)
        {
            return;
        }
        rf_fill_payload();
        gStat.sched_sent_win++;
        gStat.sched_sent_total++;
        gStat.tx_try++;
        gStat.payload_update++;
        rf_tx_start();
    }
}
#endif

__HIGH_CODE
static void rf_fill_payload(void)
{
    uint8_t i;
    uint8_t has_payload;
    uint8_t seq = g_tx_seq;
    uint8_t hop_active = g_tx_hop_active;
    uint8_t hop_adv = 0u;
    uint8_t *packet = &TxBuf[2];
    uint8_t *rf_data = &TxBuf[2u + RF_DATA_OFFSET];
    uint16_t crc;

    TxBuf[0] = 0x55;
    TxBuf[1] = RF_TEST_DATA_LEN;

    has_payload = rfm_spi_port_peek_latest_input(rf_data, RFM_RF_INPUT_PAYLOAD_LEN) ? 1u : 0u;
    if(has_payload == 0u)
    {
        has_payload = rfm_input_stream_take_latest(rf_data, RFM_RF_INPUT_PAYLOAD_LEN) ? 1u : 0u;
    }

    if(has_payload != 0u)
    {
        memcpy(g_spi_last_payload, rf_data, RFM_RF_INPUT_PAYLOAD_LEN);
        g_spi_has_payload = 1;
    }

#if (RF_TEST_PROTOCOL_PACKET == 1)
    packet[0] = RF_PKT_MAGIC;
    packet[1] = RF_PKT_TYPE_DATA;
    packet[2] = RF_PKT_SESSION_ID;
    packet[3] = g_tx_seq;
    packet[4] = RF_PKT_HOP_EPOCH;
    packet[5] = RF_PKT_FLAGS_NONE;
    g_tx_last_seq = g_tx_seq;
    g_tx_seq++;

    if(g_spi_has_payload != 0u)
    {
        for(i = 0; i < RF_PKT_INPUT_PAYLOAD_LEN; ++i)
        {
            packet[RF_PKT_HEADER_LEN + i] = g_spi_last_payload[i];
        }
    }
    else
    {
        for(i = 0; i < RF_PKT_INPUT_PAYLOAD_LEN; ++i)
        {
            packet[RF_PKT_HEADER_LEN + i] = (uint8_t)(i + 1u);
        }
    }

    crc = rf_crc16_ccitt(packet, (uint8_t)(RF_TEST_DATA_LEN - RF_PKT_CRC_LEN));
    packet[RF_TEST_DATA_LEN - 2u] = (uint8_t)(crc & 0xFFu);
    packet[RF_TEST_DATA_LEN - 1u] = (uint8_t)(crc >> 8);
#else
    (void)packet;
    (void)crc;
    if(g_spi_has_payload != 0u)
    {
        TxBuf[2u + RF_SEQ_OFFSET] = seq;
        TxBuf[2u + RF_HOP_ADV_OFFSET] = rf_tx_reverse_countdown_field();
        if(has_payload == 0u)
        {
            memcpy(rf_data, g_spi_last_payload, RFM_RF_INPUT_PAYLOAD_LEN);
        }
        TxBuf[2u + RF_DATA_OFFSET + 2u] &= 0x1Fu;
        if(g_rev_ack_pending != 0u)
        {
            rf_tx_fill_ack_payload();
        }
        if(hop_active != 0u)
        {
            if(seq == g_tx_hop_switch_seq)
            {
                g_tx_channel = g_tx_hop_next_channel;
                g_tx_hop_index = rf_hop_index_for_channel(g_tx_channel);
                g_tx_hop_active = 0u;
                gStat.hop_done++;
            }
            else if(g_tx_hop_silent == 0u)
            {
                hop_adv = 1u;
            }
        }
        if((g_rev_hop_pending != 0u) && (seq == g_rev_ack_switch_seq))
        {
            g_tx_channel = g_rev_ack_channel;
            g_tx_hop_index = rf_hop_index_for_channel(g_tx_channel);
            g_low_channel = g_tx_channel;
            g_tx_hop_active = 0u;
            g_rev_hop_pending = 0u;
            gStat.hop_done++;
        }
        if((hop_adv != 0u) && (g_rev_ack_tx_active == 0u))
        {
            TxBuf[2u + RF_HOP_ADV_OFFSET] = RF_HOP_ADV_MARK;
            TxBuf[2u + RF_HOP_ADV_EPOCH_OFFSET] = g_tx_hop_epoch;
            TxBuf[2u + RF_HOP_ADV_CHANNEL_OFFSET] = g_tx_hop_next_channel;
            TxBuf[2u + RF_HOP_ADV_SWITCH_OFFSET] = g_tx_hop_switch_seq;
        }
        g_tx_last_seq = seq;
        g_tx_seq = (uint8_t)(seq + 1u);
        rf_tx_advance_reverse_time();
        return;
    }

    TxBuf[2u + RF_SEQ_OFFSET] = seq;
    TxBuf[2u + RF_HOP_ADV_OFFSET] = rf_tx_reverse_countdown_field();
    for(i = 0; i < RF_DATA_BYTES; ++i)
    {
        TxBuf[2u + RF_DATA_OFFSET + i] = 0u;
    }
    if(g_rev_ack_pending != 0u)
    {
        rf_tx_fill_ack_payload();
    }
    if(hop_active != 0u)
    {
        if(seq == g_tx_hop_switch_seq)
        {
            g_tx_channel = g_tx_hop_next_channel;
            g_tx_hop_index = rf_hop_index_for_channel(g_tx_channel);
            g_tx_hop_active = 0u;
            gStat.hop_done++;
        }
        else if((g_tx_hop_silent == 0u) && (g_rev_ack_tx_active == 0u))
        {
            TxBuf[2u + RF_HOP_ADV_OFFSET] = RF_HOP_ADV_MARK;
            TxBuf[2u + RF_HOP_ADV_EPOCH_OFFSET] = g_tx_hop_epoch;
            TxBuf[2u + RF_HOP_ADV_CHANNEL_OFFSET] = g_tx_hop_next_channel;
            TxBuf[2u + RF_HOP_ADV_SWITCH_OFFSET] = g_tx_hop_switch_seq;
        }
    }
    if((g_rev_hop_pending != 0u) && (seq == g_rev_ack_switch_seq))
    {
        g_tx_channel = g_rev_ack_channel;
        g_tx_hop_index = rf_hop_index_for_channel(g_tx_channel);
        g_low_channel = g_tx_channel;
        g_tx_hop_active = 0u;
        g_rev_hop_pending = 0u;
        gStat.hop_done++;
    }
    g_tx_last_seq = seq;
    g_tx_seq = (uint8_t)(seq + 1u);
    rf_tx_advance_reverse_time();
#endif
}

bool RF_SPI_FastWriteInput(const uint8_t *payload, uint8_t len)
{
    if((payload == NULL) || (len != RFM_RF_INPUT_PAYLOAD_LEN))
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

__HIGH_CODE
static uint32_t rf_tmr0_delta(uint32_t now, uint32_t prev)
{
    if(now >= prev)
    {
        return now - prev;
    }

    return (TMR0_FREE_RUN_END + 1U - prev) + now;
}

#if (RF_USE_LOW_LEVEL_BASIC == 1)
static void rf_low_status_cb(uint8_t sta, uint8_t crc, uint8_t *rxBuf)
{
    (void)crc;
    (void)rxBuf;

    if((sta == TX_MODE_TX_FINISH) || (sta == TX_MODE_RX_DATA))
    {
        gStat.tx_ok++;
        g_low_tx_inflight = 0;
        return;
    }

    if((sta == TX_MODE_TX_FAIL) || (sta == TX_MODE_RX_TIMEOUT) || (sta == RX_MODE_TX_FAIL))
    {
        gStat.tx_fail++;
        g_low_tx_inflight = 0;
        return;
    }

    gStat.tx_cb_other++;
    g_low_tx_inflight = 0;
}

static void rf_low_level_basic_start_tx(void)
{
    rfConfig_t cfg;

    tmos_memset(&cfg, 0, sizeof(cfg));
    cfg.LLEMode = LLE_MODE_BASIC;
    cfg.Channel = RF_TEST_FREQUENCY;
    cfg.accessAddress = RF_LINK_ACCESS_ADDRESS;
    cfg.CRCInit = RF_LINK_CRC_INIT;
    cfg.rfStatusCB = rf_low_status_cb;
    cfg.ChannelMap = 0xFFFFFFFFUL;
    cfg.RxMaxlen = 251;
    cfg.TxMaxlen = RF_TEST_DATA_LEN;
#if (CLK_OSC32K != 0)
    cfg.HeartPeriod = 4;
#endif

    g_low_config_ret = RF_Config(&cfg);
    RF_SetChannel(RF_TEST_FREQUENCY);
    g_basic_started = (g_low_config_ret == SUCCESS) ? 1u : 0u;
    RF_LINK_LOG("[TX][BASIC] cfg:%u ch:%u pps:%u len:%u\r\n",
                (unsigned int)g_low_config_ret,
                (unsigned int)g_low_channel,
                RF_REPORT_PPS,
                RF_TEST_DATA_LEN);
}

static void rf_low_level_tx_once(void)
{
    if((g_basic_started == 0u) || (g_low_tx_inflight != 0u))
    {
        return;
    }

    rf_fill_payload();
    gStat.tx_try++;
    gStat.payload_update++;
    gStat.sched_sent_win++;
    gStat.sched_sent_total++;

    RF_Shut();
    g_low_tx_inflight = 1;
    g_low_tx_ret = RF_Tx(&TxBuf[2], RF_TEST_DATA_LEN, 0xFF, 0xFF);
    if(g_low_tx_ret != SUCCESS)
    {
        gStat.tx_start_fail++;
        g_low_tx_inflight = 0;
    }
}
#endif

static void rf_basic_start_tx(void)
{
    rfRoleConfig_t conf = {0};
    bStatus_t ret;

    conf.TxPower = BLE_TX_POWER;
    conf.rfProcessCB = RF_ProcessCallBack;
    conf.processMask = RF_STATE_RX | RF_STATE_RX_CRCERR | RF_STATE_TX_FINISH | RF_STATE_TIMEOUT | RF_STATE_TX_IDLE;
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
    rf_tx_prepare_channel_for_seq(g_tx_seq);
    gTxParam.sendTime = (uint8_t)gParm.sendTime;
    gTxParam.sendCount = 1;
    gTxParam.txDMA = (uint32_t)TxBuf;

    gRxParam.accessAddress = gParm.accessAddress;
    gRxParam.crcInit = gParm.crcInit;
    gRxParam.properties = gParm.properties;
    gRxParam.frequency = RF_TEST_FREQUENCY;
    gRxParam.whiteChannel = RF_TEST_FREQUENCY;
    gRxParam.rxDMA = (uint32_t)RxBuf;
    gRxParam.rxMaxLen = RF_TEST_DATA_LEN;
    gRxParam.timeOut = RF_REV_LISTEN_TIMEOUT_US;

    g_basic_started = 1;
    if(RFIP_SetTxParm(&gTxParam) != SUCCESS)
    {
        gStat.tx_parm_fail++;
    }
    RF_LINK_LOG("[TX][RFIP] cfg:%u ch:%u pps:%u len:%u\r\n",
                (unsigned int)g_low_config_ret,
                (unsigned int)g_low_channel,
                RF_REPORT_PPS,
                RF_TEST_DATA_LEN);
}

__HIGH_CODE
void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id)
{
    (void)id;

    if(sta & RF_STATE_TX_FINISH)
    {
        gStat.tx_ok++;
        if(g_rev_ack_tx_active != 0u)
        {
            g_tx_channel = g_rev_ack_channel;
            g_tx_hop_index = rf_hop_index_for_channel(g_tx_channel);
            g_low_channel = g_tx_channel;
            g_tx_hop_active = 0u;
            g_rev_ack_tx_active = 0u;
            gStat.hop_done++;
        }
    }
    if(sta & RF_STATE_RX)
    {
        if(g_rev_listen_active != 0u)
        {
            if(rf_tx_handle_reverse_packet() != 0u)
            {
                rf_tx_finish_reverse_listen(1u);
            }
            else
            {
                rf_tx_restart_reverse_listen();
            }
        }
        return;
    }
    if(sta & RF_STATE_RX_CRCERR)
    {
        if(g_rev_listen_active != 0u)
        {
            gStat.rev_crcerr++;
            rf_tx_restart_reverse_listen();
        }
        return;
    }
    if(sta & RF_STATE_TIMEOUT)
    {
        if(g_rev_listen_active != 0u)
        {
            gStat.rev_listen_timeout++;
            if(g_rev_listen_ticks_remaining != 0u)
            {
                rf_tx_restart_reverse_listen();
            }
            else
            {
                rf_tx_finish_reverse_listen(1u);
            }
            return;
        }
        gStat.tx_fail++;
    }
    if(sta & RF_STATE_TX_IDLE)
    {
        gStat.tx_idle++;
    }
    if((sta & (RF_STATE_TX_FINISH | RF_STATE_TIMEOUT | RF_STATE_TX_IDLE)) == 0u)
    {
        gStat.tx_cb_other++;
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
        uint32_t tmr_irq_win = gStat.tmr_irq_win;
        uint32_t tx_start_fail = gStat.tx_start_fail;
        uint32_t tx_parm_fail = gStat.tx_parm_fail;
        uint32_t tx_seq_rollback = gStat.tx_seq_rollback;
        uint32_t payload_update = gStat.payload_update;
        uint32_t peek_ok_total = rfm_spi_port_rx_peek_ok_count();
        uint32_t peek_miss_total = rfm_spi_port_rx_peek_miss_count();
        uint32_t peek_ok = peek_ok_total - g_last_peek_ok;
        uint32_t peek_miss = peek_miss_total - g_last_peek_miss;
        uint32_t now_clock = TMOS_GetSystemClock();
        uint32_t dt_ticks = now_clock - g_last_stat_clock;
        uint32_t dt_ms = (uint32_t)(((uint64_t)dt_ticks * SYSTEM_TIME_MICROSEN) / 1000u);
        uint32_t rev_listen_win = gStat.rev_listen_win;
        uint32_t rev_listen_timeout = gStat.rev_listen_timeout;
        uint32_t rev_rx = gStat.rev_rx;
        uint32_t rev_accept = gStat.rev_accept;
        uint32_t rev_bad_len = gStat.rev_bad_len;
        uint32_t rev_bad_mark = gStat.rev_bad_mark;
        uint32_t rev_crcerr = gStat.rev_crcerr;
        uint32_t rev_start_fail = gStat.rev_start_fail;
        uint32_t rev_ack_tx = gStat.rev_ack_tx;
        uint32_t hop_done = gStat.hop_done;

        if(dt_ms == 0u)
        {
            dt_ms = 1u;
        }
        RF_LINK_LOG("[TX][win] l:%u dt:%lums irq:%lu hz:%lu pk:%lu/%lu ch:%u cd:%u rq:%lu/%lu ack:%lu bad:%lu/%lu crc:%lu lm:%u/%u rr:%u hp:%lu ls:%lu/%lu e:%lu/%lu/%lu/%lu\n",
                    RF_TEST_DATA_LEN,
                    dt_ms,
                    tmr_irq_win,
                    (uint32_t)(((uint64_t)payload_update * 1000u) / dt_ms),
                    peek_ok,
                    peek_miss,
                    (unsigned int)g_low_channel,
                    (unsigned int)rf_tx_reverse_countdown_field(),
                    rev_accept,
                    rev_rx,
                    rev_ack_tx,
                    rev_bad_len,
                    rev_bad_mark,
                    rev_crcerr,
                    (unsigned int)g_rev_last_len,
                    (unsigned int)g_rev_last_mark,
                    (unsigned int)g_rev_last_rxret,
                    hop_done,
                    rev_listen_win,
                    rev_listen_timeout,
                    tx_start_fail,
                    tx_parm_fail,
                    tx_seq_rollback,
                    rev_start_fail);

        gStat.tmr_tick_win = 0;
        gStat.tmr_irq_win = 0;
        gStat.sched_due_win = 0;
        gStat.sched_sent_win = 0;
        gStat.sched_miss_win = 0;
        gStat.tx_try = 0;
        gStat.tx_ok = 0;
        gStat.tx_fail = 0;
        gStat.tx_idle = 0;
        gStat.tx_start_fail = 0;
        gStat.tx_parm_fail = 0;
        gStat.tx_seq_rollback = 0;
        gStat.tx_cb_other = 0;
        gStat.payload_update = 0;
        gStat.rx_total = 0;
        gStat.rx_ok = 0;
        gStat.rx_fail = 0;
        gStat.spi_rx_win = 0;
        gStat.rev_listen_win = 0;
        gStat.rev_listen_timeout = 0;
        gStat.rev_rx = 0;
        gStat.rev_accept = 0;
        gStat.rev_bad_len = 0;
        gStat.rev_bad_mark = 0;
        gStat.rev_crcerr = 0;
        gStat.rev_start_fail = 0;
        gStat.rev_ack_tx = 0;
        gStat.hop_done = 0;
        g_last_peek_ok = peek_ok_total;
        g_last_peek_miss = peek_miss_total;
        g_last_stat_clock = now_clock;

        tmos_start_task(taskID, SBP_RF_STAT_EVT, MS1_TO_SYSTEM_TIME(RF_STAT_PRINT_PERIOD_MS));
        return events ^ SBP_RF_STAT_EVT;
    }

    return 0;
}

__HIGH_CODE
void RF_TxMainLoopProcess(void)
{
#if (RF_USE_LOW_LEVEL_BASIC == 1)
    uint32_t now;
    uint32_t delta;
    uint32_t due;

    if(g_basic_started == 0u)
    {
        return;
    }

    now = TMR0_GetCurrentTimer();
    delta = rf_tmr0_delta(now, g_tmr_prev_cnt);
    g_tmr_prev_cnt = now;
    if(delta == 0)
    {
        return;
    }

    gStat.tmr_tick_win += delta;
    gStat.tmr_tick_total += delta;
    g_tmr_acc_tick += delta;

    due = g_tmr_acc_tick / g_tick_per_evt;
    if(due == 0)
    {
        return;
    }
    g_tmr_acc_tick -= due * g_tick_per_evt;
    gStat.sched_due_win += due;
    gStat.sched_due_total += due;

    while(due-- != 0u)
    {
        rf_low_level_tx_once();
    }
    return;
#else
#if (RF_TX_USE_TMR0_IRQ == 1)
    return;
#else
    uint32_t now;
    uint32_t delta;
    uint32_t due;

    if(g_basic_started == 0)
    {
        return;
    }

    now = TMR0_GetCurrentTimer();
    delta = rf_tmr0_delta(now, g_tmr_prev_cnt);
    g_tmr_prev_cnt = now;
    if(delta == 0)
    {
        return;
    }

    gStat.tmr_tick_win += delta;
    gStat.tmr_tick_total += delta;
    g_tmr_acc_tick += delta;

    due = g_tmr_acc_tick / g_tick_per_evt;
    if(due == 0)
    {
        return;
    }
    g_tmr_acc_tick -= due * g_tick_per_evt;

    gStat.sched_due_win += due;
    gStat.sched_due_total += due;

    while(due--)
    {
        rf_fill_payload();
        gStat.tx_try++;
        gStat.sched_sent_win++;
        gStat.sched_sent_total++;
        rf_tx_start();
    }
#endif
#endif
}

void RF_Init(void)
{
    taskID = TMOS_ProcessEventRegister(RF_ProcessEvent);
    RF_LINK_LOG("[TX][BASIC] boot task:%u\r\n",
                (unsigned int)taskID);

    PFIC_EnableIRQ(BLEB_IRQn);
    PFIC_EnableIRQ(BLEL_IRQn);

    tmos_start_task(taskID, SBP_RF_STAT_EVT, MS1_TO_SYSTEM_TIME(RF_STAT_PRINT_PERIOD_MS));

    rfm_input_stream_init();
    g_tx_channel = RF_TEST_FREQUENCY;
    g_tx_hop_index = rf_hop_index_for_channel(RF_TEST_FREQUENCY);
    g_tx_hop_active = 0u;
    g_tx_hop_silent = 0u;
    g_tx_hop_next_channel = RF_TEST_FREQUENCY;
    g_tx_hop_switch_seq = 0u;
    g_tx_hop_epoch = 0u;
    g_rev_listen_active = 0u;
    g_rev_listen_paused = 0u;
    g_rev_listen_pending = 0u;
    g_rev_listen_ticks_remaining = 0u;
    g_rev_ack_pending = 0u;
    g_rev_ack_loaded = 0u;
    g_rev_ack_armed = 0u;
    g_rev_ack_tx_active = 0u;
    g_rev_ack_repeat_remaining = 0u;
    g_rev_hop_pending = 0u;
    g_tx_rev_packets_to_window = RF_REV_PERIOD_PACKETS;
    memset(g_spi_last_payload, 0, sizeof(g_spi_last_payload));
    g_spi_has_payload = 0u;
    g_last_peek_ok = 0u;
    g_last_peek_miss = 0u;
    g_last_stat_clock = TMOS_GetSystemClock();
    g_tick_per_evt = GetSysClock() / RF_REPORT_PPS;
    if(g_tick_per_evt == 0)
    {
        g_tick_per_evt = 1;
    }
#if (RF_USE_LOW_LEVEL_BASIC == 1)
    rf_low_level_basic_start_tx();
#else
    rf_basic_start_tx();
#endif

#if (RF_TX_USE_TMR0_IRQ == 1)
    TMR0_TimerInit(g_tick_per_evt);
    TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    PFIC_SetPriority(TMR0_IRQn, 0x80);
    PFIC_EnableIRQ(TMR0_IRQn);
#else
    TMR0_TimerInit(TMR0_FREE_RUN_END);
    g_tmr_prev_cnt = TMR0_GetCurrentTimer();
    g_tmr_acc_tick = 0;
#endif
}

/******************************** endfile @ RF_PHY ******************************/
