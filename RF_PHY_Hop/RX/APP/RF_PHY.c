/********************************** (C) COPYRIGHT *******************************
 * File Name          : RF_PHY.c
 * Description        : 8K test flow based on WCH RF_Basic style
 *******************************************************************************/

#include "CONFIG.h"
#include "RF_PHY.h"
#include "HAL.h"
#include "wchrf.h"
#include "ch585_usbhs_device.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef RF_HOP_MODE
#define RF_HOP_MODE 2
#endif

#ifndef RF_TEST_FREQUENCY
#define RF_TEST_FREQUENCY 16
#endif
#ifndef RF_TEST_PROTOCOL_PACKET
#define RF_TEST_PROTOCOL_PACKET 0
#endif
#ifndef RF_TEST_ENABLE_HOP
#define RF_TEST_ENABLE_HOP 0
#endif
#ifndef RF_TEST_ENABLE_RF_RX
#define RF_TEST_ENABLE_RF_RX 1
#endif
#if (RF_TEST_PROTOCOL_PACKET == 1)
#define RF_TEST_DATA_LEN 12
#else
#define RF_TEST_DATA_LEN 12
#endif
#ifndef RF_REPORT_PPS
#define RF_REPORT_PPS 8000
#endif
#define RF_STAT_PRINT_PERIOD_MS 5000

/* RX 反向请求 burst 参数：只有质量触发 pending 后，RX 才会在 TX 反向窗口前发送这些包。 */
#define RF_REV_REQ_REPEAT 220u            /* 反向握手最多发送多少个请求包；200ms TX 窗口下留少量余量。 */
#define RF_REV_REQ_INTERVAL_MS 1          /* burst 内两个反向包的间隔；5ms TX 窗口建议 1ms。 */
#define RF_REV_REQ_START_COUNTDOWN_MS 2u  /* 当 TX 倒计时接近 0 时开始发，目标是对齐 TX 反向 RX 开窗。 */
#define RF_REV_ACK_TIMEOUT_MS 8u          /* RX 发出反向请求后等待 TX 正向 ACK 的时间，超时后才重试发送。 */
#define RF_REV_COUNTDOWN_FAR 0xFFu        /* TX 尚未接近反向窗口时的 cd 值。 */

/* RX 质量触发参数：用于判断当前频道是否需要跳频。 */
#define RF_QUALITY_MIN_HZ 7980u           /* 5s 窗口内有效接收频率低于该值，认为质量差。 */
#define RF_QUALITY_MAX_GAP 2500u          /* 5s 窗口内 seq gap 高于该值，认为质量差。 */
#define RF_QUALITY_BAD_WINDOWS 2u         /* 连续多少个差窗口后挂起一次质量跳频请求。 */
#define RF_QUALITY_HOP_COOLDOWN_MS 30000u /* 质量触发跳频后的冷却时间，避免频繁横跳。 */
#define RF_ACK_SWITCH_LOCK_MS 200u        /* 收到 ACK 并切频道后锁定新频道的时间；锁定期 timeout/CRCERR 不扫描回旧频道。 */
#define RF_REV_SWITCH_FALLBACK_MS 10u     /* 收到 ACK 后如果旧频道立刻变差，最多等这么久就按 ACK 指定频道本地切换。 */
#define TMR0_FREE_RUN_END 0x03FFFFFFUL
#define RF_USE_LOW_LEVEL_BASIC 0
#define RF_LINK_DEBUG_LOG 1
#ifndef RF_RX_RESTART_IN_CALLBACK
#define RF_RX_RESTART_IN_CALLBACK 1
#endif

#define RF_PKT_MAGIC 0xA7u
#define RF_PKT_TYPE_DATA 0x01u
#define RF_PKT_SESSION_ID 0x21u
#define RF_PKT_HOP_EPOCH 0u
#define RF_PKT_CRC_LEN 2u
#define RF_HOP_DWELL_PACKETS 16u            /* 旧的匀速跳频 dwell 参数；当前智能跳频路径不依赖它。 */
#define RF_HOP_CHANNEL_COUNT 33u            /* 智能跳频候选频道数；4..36 连续频道，包含单数频道，TX/RX 必须一致。 */
#define RF_TX_SEND_TIME (20u * 2u)          /* RFIP 发送时序参数；改动会影响反向包空口占用。 */
#define RF_LINK_ACCESS_ADDRESS 0x71764129UL /* 固定 access address，TX/RX 必须一致。 */
#define RF_LINK_CRC_INIT 0x555555UL         /* 固定 CRC init，TX/RX 必须一致。 */
#define RF_BUTTON_BYTES 3u
#define RF_SEQ_OFFSET 0u
#define RF_HOP_ADV_OFFSET 1u
#define RF_HOP_ADV_MARK 0xA5u
#define RF_HOP_ADV_EPOCH_OFFSET 2u
#define RF_HOP_ADV_CHANNEL_OFFSET 3u
#define RF_HOP_ADV_SWITCH_OFFSET 4u
#define RF_REV_REQ_MARK 0xC3u /* RX 反向跳频请求包标记。 */
#define RF_REV_REQ_EPOCH_OFFSET 1u
#define RF_REV_REQ_CHANNEL_OFFSET 2u
#define RF_REV_REQ_REASON_OFFSET 3u
#define RF_REV_REQ_TIME_LO_OFFSET 4u
#define RF_REV_REQ_TIME_HI_OFFSET 5u
#define RF_REV_ACK_MARK 0xC4u
#define RF_REV_ACK_TIME_LO_OFFSET 2u
#define RF_REV_ACK_TIME_HI_OFFSET 3u
#define RF_REV_ACK_CHANNEL_OFFSET 4u
#define RF_REV_ACK_REASON_OFFSET 5u
#define RF_REV_ACK_EPOCH_OFFSET 6u
#define RF_REV_ACK_SWITCH_OFFSET 7u
#define RF_REV_TRIGGER_PERIODIC 0u
#define RF_REV_TRIGGER_QUALITY 1u
#define RF_REV_TRIGGER_MANUAL 2u
#define RF_RX_REACQUIRE_MISS 6u      /* 连续丢包超过该值后进入/推进重捕获扫描。 */
#define RF_RX_FAST_REACQUIRE_MISS 8u /* 保留的快速重捕获阈值，当前主路径较少使用。 */
#define RF_RX_ENABLE_REACQUIRE_SCAN 0u /* 调试智能跳频主流程时关闭兜底扫描，避免 timeout/CRCERR 把 RX 带到其它频道。 */
#if (RF_TEST_ENABLE_HOP == 1)
#define RF_RX_TIMEOUT_HALF_US 5000u /* 匀速跳频模式下 RX timeout，单位沿用 WCH RFIP 命名。 */
#else
#define RF_RX_TIMEOUT_HALF_US 10000u /* 当前固定/智能跳频模式下 RX timeout。 */
#endif

#define SBP_RF_RF_RX_EVT 4
#define SBP_RF_STAT_EVT (1 << 5)
#define SBP_RF_REV_REQ_EVT (1 << 6)

#if (RF_LINK_DEBUG_LOG == 1)
#define RF_LINK_LOG(...) PRINT(__VA_ARGS__)
#else
#define RF_LINK_LOG(...) ((void)0)
#endif

uint8_t taskID;

typedef struct
{
    volatile uint32_t tmr_tick_win;
    volatile uint32_t tmr_tick_total;
    volatile uint32_t sched_due_win;
    volatile uint32_t sched_due_total;
    volatile uint32_t sched_sent_win;
    volatile uint32_t sched_sent_total;
    volatile uint32_t sched_miss_win;
    volatile uint32_t sched_miss_total;
    volatile uint32_t tx_try;
    volatile uint32_t tx_ok;
    volatile uint32_t tx_fail;
    volatile uint32_t rx_total;
    volatile uint32_t rx_ok;
    volatile uint32_t rx_fail;
    volatile uint32_t rx_crcerr;
    volatile uint32_t rx_timeout;
    volatile uint32_t rx_bad_magic;
    volatile uint32_t rx_bad_session;
    volatile uint32_t rx_bad_crc;
    volatile uint32_t rx_seq_gap;
    volatile uint32_t rx_dup;
    volatile uint32_t rx_reacquire;
    volatile uint32_t hop_done;
    volatile uint32_t rev_req_sent;
    volatile uint32_t rev_req_ok;
    volatile uint32_t rev_req_fail;
    volatile uint32_t rev_req_burst;
    volatile uint32_t rev_sync_seen;
    volatile uint32_t rev_quality_trigger;
    volatile uint32_t rev_ack_rx;
    volatile uint32_t rev_ack_latency_us;
    volatile uint32_t rev_ack_switch;
    volatile uint32_t rx_scan_switch;
} rf_stat_t;

static rfRoleParam_t gParm;
static rfipTx_t gTxParam;
static rfipRx_t gRxParam;
static rf_stat_t gStat = {0};
static uint32_t g_tmr_prev_cnt = 0;
static uint32_t g_tmr_acc_tick = 0;
static uint32_t g_tick_per_evt = 1;
static volatile uint8_t g_ret_role_init = 0xFF;
static volatile uint8_t g_task_id_dbg = 0xFF;
static volatile uint8_t g_basic_started = 0;
static volatile uint8_t g_low_config_ret = 0xFFu;
static volatile uint8_t g_low_channel = RF_TEST_FREQUENCY;
static volatile uint8_t g_low_rx_ret = 0xFFu;
static volatile uint8_t g_rx_restart_pending = 0u;
static volatile uint32_t g_rx_restart_mark_tmr = 0;
static volatile uint32_t g_rx_restart_delay_sum = 0;
static volatile uint32_t g_rx_restart_delay_max = 0;
static volatile uint32_t g_rx_restart_delay_cnt = 0;
static uint32_t g_last_stat_clock = 0;
static uint8_t g_rx_expected_seq = 0u;
static uint8_t g_rx_has_seq = 0u;
static uint8_t g_rx_miss_count = 0u;
static uint8_t g_rx_scan_index = 0u;
static uint8_t g_rx_scan_active = 0u;
static uint8_t g_rx_pending_hop = 0u;
static uint8_t g_rx_pending_epoch = 0u;
static uint8_t g_rx_pending_channel = RF_TEST_FREQUENCY;
static uint8_t g_rx_pending_switch_seq = 0u;
static uint8_t g_rev_req_active = 0u;
static uint8_t g_rev_req_remaining = 0u;
static uint8_t g_rev_req_epoch = 0u;
static uint8_t g_rev_req_next_channel = RF_TEST_FREQUENCY;
static uint8_t g_rev_req_reason = RF_REV_TRIGGER_PERIODIC;
static uint8_t g_rev_last_trigger_reason = RF_REV_TRIGGER_PERIODIC;
static uint16_t g_rev_req_time = 0u;
static uint8_t g_rev_ack_last_channel = RF_TEST_FREQUENCY;
static uint8_t g_rev_req_pending = 0u;
static uint8_t g_rev_wait_ack = 0u;
static uint8_t g_rev_switch_pending = 0u;
static uint8_t g_rev_switch_channel = RF_TEST_FREQUENCY;
static uint8_t g_rev_switch_seq = 0u;
static uint8_t g_rev_switch_epoch = 0u;
static uint32_t g_rev_req_next_clock = 0u;
static uint32_t g_rev_ack_deadline_clock = 0u;
static uint32_t g_rev_switch_deadline_clock = 0u;
static uint8_t g_rx_rev_countdown = RF_REV_COUNTDOWN_FAR;
static uint8_t g_rx_rev_countdown_armed = 0u;
static uint8_t g_quality_bad_windows = 0u;
static uint8_t g_quality_request_pending = 0u;
static uint32_t g_quality_last_trigger_clock = 0u;
static uint32_t g_ack_switch_lock_until_clock = 0u;
static uint8_t g_rx_last_packet_was_ack = 0u;

static uint8_t g_hop_channels[RF_HOP_CHANNEL_COUNT] = {
    4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u,
    15u, 16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u, 24u,
    25u, 26u, 27u, 28u, 29u, 30u, 31u, 32u, 33u, 34u,
    35u, 36u};

void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id);
static void rf_rx_start(void);
static uint32_t rf_tmr0_delta(uint32_t now, uint32_t prev);

__attribute__((__aligned__(4))) static uint8_t TxBuf[64];
__attribute__((__aligned__(4))) static uint8_t RxBuf[264];

static uint16_t rf_crc16_ccitt(const uint8_t *data, uint8_t len)
{
    uint16_t crc = 0xFFFFu;
    uint8_t i;
    uint8_t bit;

    for (i = 0u; i < len; ++i)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0u; bit < 8u; ++bit)
        {
            if ((crc & 0x8000u) != 0u)
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
    return RF_TEST_FREQUENCY;
#endif
}

static uint8_t rf_hop_index_for_seq(uint8_t seq)
{
#if (RF_TEST_ENABLE_HOP == 1)
    return (uint8_t)((seq / RF_HOP_DWELL_PACKETS) % RF_HOP_CHANNEL_COUNT);
#else
    (void)seq;
    return 0u;
#endif
}

static uint8_t rf_hop_index_for_channel(uint8_t channel)
{
    uint8_t i;

    for (i = 0u; i < RF_HOP_CHANNEL_COUNT; ++i)
    {
        if (g_hop_channels[i] == channel)
        {
            return i;
        }
    }
    return 0u;
}

static uint8_t rf_hop_channel_valid(uint8_t channel)
{
    uint8_t i;

    for (i = 0u; i < RF_HOP_CHANNEL_COUNT; ++i)
    {
        if (g_hop_channels[i] == channel)
        {
            return 1u;
        }
    }
    return 0u;
}

static void rf_hop_demote_channel(uint8_t channel)
{
    uint8_t i;
    uint8_t pos = RF_HOP_CHANNEL_COUNT;

    for (i = 0u; i < RF_HOP_CHANNEL_COUNT; ++i)
    {
        if (g_hop_channels[i] == channel)
        {
            pos = i;
            break;
        }
    }
    if (pos >= (RF_HOP_CHANNEL_COUNT - 1u))
    {
        return;
    }

    for (i = pos; i < (RF_HOP_CHANNEL_COUNT - 1u); ++i)
    {
        g_hop_channels[i] = g_hop_channels[i + 1u];
    }
    g_hop_channels[RF_HOP_CHANNEL_COUNT - 1u] = channel;
}

static void rf_rx_set_channel(uint8_t channel)
{
    if (gRxParam.frequency == channel)
    {
        return;
    }
    gRxParam.frequency = channel;
    gRxParam.whiteChannel = channel;
    g_low_channel = channel;
    g_rx_scan_index = rf_hop_index_for_channel(channel);
    gStat.hop_done++;
    if (g_quality_request_pending != 0u)
    {
        g_quality_request_pending = 0u;
        g_quality_bad_windows = 0u;
        g_quality_last_trigger_clock = TMOS_GetSystemClock();
    }
}

static uint8_t rf_next_channel_after(uint8_t channel)
{
    uint8_t index = rf_hop_index_for_channel(channel);

    index++;
    if (index >= RF_HOP_CHANNEL_COUNT)
    {
        index = 0u;
    }
    return g_hop_channels[index];
}

static uint16_t rf_now_us16(void)
{
    return (uint16_t)((uint64_t)TMOS_GetSystemClock() * SYSTEM_TIME_MICROSEN);
}

static uint8_t rf_ack_switch_lock_active(void)
{
    return ((int32_t)(TMOS_GetSystemClock() - g_ack_switch_lock_until_clock) < 0) ? 1u : 0u;
}

static uint8_t rf_seq_reached(uint8_t current, uint8_t target)
{
    return ((uint8_t)(current - target) < 0x80u) ? 1u : 0u;
}

static void rf_rev_switch_commit(void)
{
    if (g_rev_switch_pending == 0u)
    {
        return;
    }

    if (g_low_channel != g_rev_switch_channel)
    {
        gStat.rev_ack_switch++;
    }
    rf_rx_set_channel(g_rev_switch_channel);
    g_rev_switch_pending = 0u;
    g_rev_switch_deadline_clock = 0u;
    g_ack_switch_lock_until_clock = TMOS_GetSystemClock() + MS1_TO_SYSTEM_TIME(RF_ACK_SWITCH_LOCK_MS);
}

static void rf_rev_switch_check_fallback(void)
{
    if (g_rev_switch_pending == 0u)
    {
        return;
    }
    if ((int32_t)(TMOS_GetSystemClock() - g_rev_switch_deadline_clock) < 0)
    {
        return;
    }
    rf_rev_switch_commit();
}

static void rf_rx_tune_for_expected_seq(void)
{
#if (RF_TEST_ENABLE_HOP == 1)
    rf_rx_set_channel(rf_hop_channel_for_seq(g_rx_expected_seq));
    g_rx_scan_index = rf_hop_index_for_seq(g_rx_expected_seq);
#else
    g_rx_scan_index = rf_hop_index_for_channel(g_low_channel);
#endif
}

static void rf_rx_advance_reacquire_channel(void)
{
#if (RF_RX_ENABLE_REACQUIRE_SCAN == 0)
    g_rx_miss_count++;
    g_rx_scan_active = 0u;
    return;
#endif
#if (RF_TEST_ENABLE_HOP == 1)
    g_rx_miss_count++;
    if ((g_rx_has_seq != 0u) && (g_rx_miss_count <= RF_RX_FAST_REACQUIRE_MISS))
    {
        g_rx_expected_seq++;
        rf_rx_tune_for_expected_seq();
        return;
    }
    if (g_rx_miss_count > RF_RX_FAST_REACQUIRE_MISS)
    {
        gStat.rx_reacquire++;
    }
    g_rx_scan_index++;
    if (g_rx_scan_index >= RF_HOP_CHANNEL_COUNT)
    {
        g_rx_scan_index = 0u;
    }
    gStat.rx_scan_switch++;
    rf_rx_set_channel(g_hop_channels[g_rx_scan_index]);
#else
    g_rx_miss_count++;
    g_rx_scan_active = 1u;
    if (g_rx_pending_hop != 0u)
    {
        uint8_t dist = (uint8_t)(g_rx_pending_switch_seq - g_rx_expected_seq);
        if (dist <= g_rx_miss_count)
        {
            rf_rx_set_channel(g_rx_pending_channel);
            g_rx_pending_hop = 0u;
            g_rx_scan_active = 0u;
            return;
        }
    }

    if (g_rx_miss_count <= RF_RX_REACQUIRE_MISS)
    {
        return;
    }

    g_rx_scan_active = 1u;
    gStat.rx_reacquire++;
    g_rx_scan_index++;
    if (g_rx_scan_index >= RF_HOP_CHANNEL_COUNT)
    {
        g_rx_scan_index = 0u;
    }
    gStat.rx_scan_switch++;
    rf_rx_set_channel(g_hop_channels[g_rx_scan_index]);
#endif
}

static void rf_rx_request_restart(void)
{
    if (g_rev_req_active != 0u)
    {
        return;
    }
#if (RF_RX_RESTART_IN_CALLBACK == 1)
    uint32_t delay_ticks;
    uint32_t mark_tmr = TMR0_GetCurrentTimer();

    rf_rx_start();
    delay_ticks = rf_tmr0_delta(TMR0_GetCurrentTimer(), mark_tmr);
    g_rx_restart_delay_sum += delay_ticks;
    if (delay_ticks > g_rx_restart_delay_max)
    {
        g_rx_restart_delay_max = delay_ticks;
    }
    g_rx_restart_delay_cnt++;
#else
    if (g_rx_restart_pending == 0u)
    {
        g_rx_restart_mark_tmr = TMR0_GetCurrentTimer();
    }
    g_rx_restart_pending = 1u;
#endif
}

static void rf_rev_req_begin(uint8_t reason)
{
    if ((g_rev_req_active != 0u) || (g_rev_req_remaining != 0u))
    {
        return;
    }

    g_rev_req_epoch++;
    g_rev_req_next_channel = rf_next_channel_after(g_low_channel);
    g_rev_req_reason = reason;
    g_rev_last_trigger_reason = reason;
    g_rev_req_time = rf_now_us16();
    g_rev_req_remaining = RF_REV_REQ_REPEAT;
    g_rev_req_pending = 1u;
    g_rev_req_next_clock = TMOS_GetSystemClock();
    gStat.rev_req_burst++;
}

static void rf_rx_quality_window_update(uint32_t rx_hz, uint32_t gap_delta)
{
    uint32_t now_clock = TMOS_GetSystemClock();

    if ((rx_hz < RF_QUALITY_MIN_HZ) || (gap_delta > RF_QUALITY_MAX_GAP))
    {
        if (g_quality_bad_windows < RF_QUALITY_BAD_WINDOWS)
        {
            g_quality_bad_windows++;
        }
    }
    else
    {
        g_quality_bad_windows = 0u;
    }

    if (g_quality_bad_windows < RF_QUALITY_BAD_WINDOWS)
    {
        return;
    }
    if (g_quality_request_pending != 0u)
    {
        return;
    }
    if ((uint32_t)(now_clock - g_quality_last_trigger_clock) <
        MS1_TO_SYSTEM_TIME(RF_QUALITY_HOP_COOLDOWN_MS))
    {
        return;
    }

    g_quality_request_pending = 1u;
    g_quality_bad_windows = 0u;
    gStat.rev_quality_trigger++;
}

static void rf_rx_update_countdown(uint8_t countdown)
{
    g_rx_rev_countdown = countdown;

    if (countdown == RF_REV_COUNTDOWN_FAR)
    {
        g_rx_rev_countdown_armed = 1u;
        return;
    }

    if ((g_rx_rev_countdown_armed != 0u) &&
        (countdown <= RF_REV_REQ_START_COUNTDOWN_MS))
    {
        g_rx_rev_countdown_armed = 0u;
        if (g_quality_request_pending != 0u)
        {
            gStat.rev_sync_seen++;
            rf_rev_req_begin(RF_REV_TRIGGER_QUALITY);
        }
    }
}

static uint8_t rf_rx_validate_packet(void)
{
#if (RF_TEST_PROTOCOL_PACKET == 1)
    const uint8_t *packet = &RxBuf[2];
    uint16_t expected_crc;
    uint16_t packet_crc;
    uint8_t seq;
    uint8_t delta;

    if (RxBuf[1] != RF_TEST_DATA_LEN)
    {
        return 0u;
    }
    if ((packet[0] != RF_PKT_MAGIC) || (packet[1] != RF_PKT_TYPE_DATA))
    {
        gStat.rx_bad_magic++;
        return 0u;
    }
    if ((packet[2] != RF_PKT_SESSION_ID) || (packet[4] != RF_PKT_HOP_EPOCH))
    {
        gStat.rx_bad_session++;
        return 0u;
    }

    packet_crc = (uint16_t)packet[RF_TEST_DATA_LEN - 2u] |
                 ((uint16_t)packet[RF_TEST_DATA_LEN - 1u] << 8);
    expected_crc = rf_crc16_ccitt(packet, (uint8_t)(RF_TEST_DATA_LEN - RF_PKT_CRC_LEN));
    if (packet_crc != expected_crc)
    {
        gStat.rx_bad_crc++;
        return 0u;
    }

    seq = packet[3];
    if (g_rx_has_seq == 0u)
    {
        g_rx_has_seq = 1u;
    }
    else
    {
        delta = (uint8_t)(seq - g_rx_expected_seq);
        if (delta == 0u)
        {
        }
        else if (delta == 0xFFu)
        {
            gStat.rx_dup++;
        }
        else
        {
            gStat.rx_seq_gap += delta;
        }
    }

    g_rx_expected_seq = (uint8_t)(seq + 1u);
    g_rx_miss_count = 0u;
    rf_rx_tune_for_expected_seq();
    return 1u;
#else
    uint8_t seq;
    uint8_t delta;
    const uint8_t *packet = &RxBuf[2];

    g_rx_last_packet_was_ack = 0u;
    if (RxBuf[1] != RF_TEST_DATA_LEN)
    {
        return 0u;
    }

    seq = packet[RF_SEQ_OFFSET];
    if (g_rx_has_seq == 0u)
    {
        g_rx_has_seq = 1u;
    }
    else
    {
        delta = (uint8_t)(seq - g_rx_expected_seq);
        if (delta == 0u)
        {
        }
        else if (delta == 0xFFu)
        {
            gStat.rx_dup++;
        }
        else
        {
            gStat.rx_seq_gap += delta;
        }
    }

    g_rx_expected_seq = (uint8_t)(seq + 1u);
    g_rx_miss_count = 0u;
    g_rx_scan_active = 0u;
    rf_rx_tune_for_expected_seq();

    if ((packet[RF_HOP_ADV_OFFSET] == RF_REV_ACK_MARK) && (g_rev_wait_ack != 0u))
    {
        uint16_t ack_time = (uint16_t)packet[RF_REV_ACK_TIME_LO_OFFSET] |
                            ((uint16_t)packet[RF_REV_ACK_TIME_HI_OFFSET] << 8);
        uint8_t ack_channel = packet[RF_REV_ACK_CHANNEL_OFFSET];
        uint8_t ack_epoch = packet[RF_REV_ACK_EPOCH_OFFSET];
        uint8_t ack_switch_seq = packet[RF_REV_ACK_SWITCH_OFFSET];
        if ((ack_epoch != g_rev_req_epoch) || (rf_hop_channel_valid(ack_channel) == 0u))
        {
            return 1u;
        }
        gStat.rev_ack_rx++;
        gStat.rev_ack_latency_us = (uint16_t)(rf_now_us16() - ack_time);
        g_rev_ack_last_channel = ack_channel;
        g_rev_req_pending = 0u;
        g_rev_req_active = 0u;
        g_rev_wait_ack = 0u;
        g_rev_req_remaining = 0u;
        g_rev_switch_pending = 1u;
        g_rev_switch_channel = ack_channel;
        g_rev_switch_seq = ack_switch_seq;
        g_rev_switch_epoch = ack_epoch;
        g_rev_switch_deadline_clock = TMOS_GetSystemClock() + MS1_TO_SYSTEM_TIME(RF_REV_SWITCH_FALLBACK_MS);
        if (rf_seq_reached(g_rx_expected_seq, g_rev_switch_seq) != 0u)
        {
            rf_rev_switch_commit();
        }
        g_rx_last_packet_was_ack = 1u;
        (void)packet[RF_REV_ACK_REASON_OFFSET];
        return 1u;
    }

    if (packet[RF_HOP_ADV_OFFSET] == RF_HOP_ADV_MARK)
    {
        uint8_t next_channel = packet[RF_HOP_ADV_CHANNEL_OFFSET];
        if (rf_hop_channel_valid(next_channel) != 0u)
        {
            g_rx_pending_hop = 1u;
            g_rx_pending_epoch = packet[RF_HOP_ADV_EPOCH_OFFSET];
            g_rx_pending_channel = next_channel;
            g_rx_pending_switch_seq = packet[RF_HOP_ADV_SWITCH_OFFSET];
        }
    }
    else
    {
        rf_rx_update_countdown(packet[RF_HOP_ADV_OFFSET]);
    }

    if ((g_rx_pending_hop != 0u) && (g_rx_expected_seq == g_rx_pending_switch_seq))
    {
        rf_rx_set_channel(g_rx_pending_channel);
        g_rx_pending_hop = 0u;
    }
    if ((g_rev_switch_pending != 0u) && (rf_seq_reached(g_rx_expected_seq, g_rev_switch_seq) != 0u))
    {
        rf_rev_switch_commit();
    }
    return 1u;
#endif
}

static void rx_debug_log(const char *fmt, ...)
{
    char msg[128];
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    if (len <= 0)
    {
        return;
    }
    if (len >= (int)sizeof(msg))
    {
        len = (int)sizeof(msg) - 1;
    }

    PRINT("%s", msg);

    if (USBHS_DevEnumStatus == 0)
    {
        return;
    }
    if ((USBHS_Endp_Busy[DEF_UEP5] & DEF_UEP_BUSY) != 0)
    {
        return;
    }
    (void)USBHS_Endp_DataUp(DEF_UEP5, (uint8_t *)msg, (uint16_t)len, DEF_UEP_CPY_LOAD);
}

__HIGH_CODE
static void rf_tx_start(void)
{
    RFIP_SetTxStart();
    gTxParam.txDMA = (uint32_t)TxBuf;
    RFIP_SetTxParm(&gTxParam);
}

__HIGH_CODE
static void rf_rx_start(void)
{
    if (g_rev_req_active != 0u)
    {
        return;
    }
#if (RF_USE_LOW_LEVEL_BASIC == 1)
    RF_Shut();
    g_low_rx_ret = RF_Rx(NULL, 0, 0xFF, 0xFF);
#else
    gRxParam.timeOut = (g_rx_scan_active != 0u) ? 2000u : RF_RX_TIMEOUT_HALF_US;
    g_low_rx_ret = RFIP_SetRx(&gRxParam);
#endif
}

__HIGH_CODE
static void rf_fill_payload(void)
{
    uint8_t i;

    g_rev_req_time = rf_now_us16();
    TxBuf[0] = 0x55;
    TxBuf[1] = RF_TEST_DATA_LEN;
    TxBuf[2] = RF_REV_REQ_MARK;
    TxBuf[2u + RF_REV_REQ_EPOCH_OFFSET] = g_rev_req_epoch;
    TxBuf[2u + RF_REV_REQ_CHANNEL_OFFSET] = g_rev_req_next_channel;
    TxBuf[2u + RF_REV_REQ_REASON_OFFSET] = g_rev_req_reason;
    TxBuf[2u + RF_REV_REQ_TIME_LO_OFFSET] = (uint8_t)(g_rev_req_time & 0xFFu);
    TxBuf[2u + RF_REV_REQ_TIME_HI_OFFSET] = (uint8_t)(g_rev_req_time >> 8);
    TxBuf[8] = g_low_channel;
    TxBuf[9] = g_rx_expected_seq;
    for (i = 10u; i < (2u + RF_TEST_DATA_LEN); ++i)
    {
        TxBuf[i] = 0u;
    }
}

static void rf_rev_req_tx_once(void)
{
    if ((g_basic_started == 0u) || (g_rev_req_remaining == 0u) || (g_rev_wait_ack != 0u))
    {
        g_rev_req_active = 0u;
        rf_rx_start();
        return;
    }

    g_rev_req_active = 1u;
    rf_fill_payload();
    gTxParam.frequency = g_low_channel;
    gTxParam.whiteChannel = g_low_channel;
    gTxParam.txDMA = (uint32_t)TxBuf;
    gStat.rev_req_sent++;
    rf_tx_start();
}

static void rf_rev_req_schedule_retry(void)
{
    g_rev_wait_ack = 0u;
    if (g_rev_req_remaining != 0u)
    {
        g_rev_req_pending = 1u;
        g_rev_req_next_clock = TMOS_GetSystemClock() + MS1_TO_SYSTEM_TIME(RF_REV_REQ_INTERVAL_MS);
    }
    else
    {
        rf_hop_demote_channel(g_rev_req_next_channel);
        rf_rx_start();
    }
}

static void rf_rev_req_check_ack_timeout(void)
{
    if (g_rev_wait_ack == 0u)
    {
        return;
    }
    if ((int32_t)(TMOS_GetSystemClock() - g_rev_ack_deadline_clock) < 0)
    {
        return;
    }
    rf_rev_req_schedule_retry();
}

__HIGH_CODE
static uint32_t rf_tmr0_delta(uint32_t now, uint32_t prev)
{
    if (now >= prev)
    {
        return now - prev;
    }

    return (TMR0_FREE_RUN_END + 1U - prev) + now;
}

#if (RF_USE_LOW_LEVEL_BASIC == 1)
static void rf_low_status_cb(uint8_t sta, uint8_t crc, uint8_t *rxBuf)
{
    if (sta == RX_MODE_RX_DATA)
    {
        gStat.rx_total++;
        if ((crc == 0u) && (rxBuf != NULL))
        {
            gStat.rx_ok++;
        }
        else
        {
            gStat.rx_fail++;
        }
        tmos_set_event(taskID, SBP_RF_RF_RX_EVT);
        return;
    }

    if (sta == RX_MODE_TX_FINISH)
    {
        tmos_set_event(taskID, SBP_RF_RF_RX_EVT);
        return;
    }

    if ((sta == RX_MODE_TX_FAIL) || (sta == TX_MODE_TX_FAIL) || (sta == TX_MODE_RX_TIMEOUT))
    {
        gStat.rx_fail++;
        tmos_set_event(taskID, SBP_RF_RF_RX_EVT);
        return;
    }
}

static void rf_low_level_basic_start_rx(void)
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
    rx_debug_log("[RX][BASIC] cfg:%u ch:%u len:%u\r\n",
                 (unsigned int)g_low_config_ret,
                 (unsigned int)g_low_channel,
                 RF_TEST_DATA_LEN);
    if (g_basic_started != 0u)
    {
        rf_rx_start();
    }
}
#endif

__HIGH_CODE
void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id)
{
    (void)id;

    if (sta & RF_STATE_RX)
    {
        gStat.rx_total++;
        if (rf_rx_validate_packet() != 0u)
        {
            gStat.rx_ok++;
            if (g_rx_last_packet_was_ack == 0u)
            {
                g_ack_switch_lock_until_clock = 0u;
            }
        }
        else
        {
            gStat.rx_fail++;
            if (rf_ack_switch_lock_active() != 0u)
            {
                rf_rx_request_restart();
                return;
            }
            rf_rx_advance_reacquire_channel();
        }
        rf_rx_request_restart();
    }
    if (sta & RF_STATE_RX_CRCERR)
    {
        gStat.rx_total++;
        gStat.rx_fail++;
        gStat.rx_crcerr++;
        if (rf_ack_switch_lock_active() != 0u)
        {
            rf_rx_request_restart();
            return;
        }
        rf_rx_advance_reacquire_channel();
        rf_rx_request_restart();
    }
    if (sta & RF_STATE_TIMEOUT)
    {
        if (g_rev_wait_ack != 0u)
        {
            rf_rev_req_schedule_retry();
            return;
        }
        if (g_rev_req_active != 0u)
        {
            gStat.rev_req_fail++;
            if (g_rev_req_remaining != 0u)
            {
                g_rev_req_remaining--;
            }
            g_rev_req_active = 0u;
            rf_rx_start();
            if (g_rev_req_remaining != 0u)
            {
                g_rev_req_pending = 1u;
                g_rev_req_next_clock = TMOS_GetSystemClock() + MS1_TO_SYSTEM_TIME(RF_REV_REQ_INTERVAL_MS);
            }
            return;
        }
        gStat.rx_fail++;
        gStat.rx_timeout++;
        if (rf_ack_switch_lock_active() != 0u)
        {
            rf_rx_request_restart();
            return;
        }
        rf_rx_advance_reacquire_channel();
        rf_rx_request_restart();
    }
    if (sta & RF_STATE_TX_FINISH)
    {
        if (g_rev_req_active != 0u)
        {
            gStat.rev_req_ok++;
            if (g_rev_req_remaining != 0u)
            {
                g_rev_req_remaining--;
            }
            g_rev_req_active = 0u;
            g_rev_wait_ack = 1u;
            g_rev_ack_deadline_clock = TMOS_GetSystemClock() + MS1_TO_SYSTEM_TIME(RF_REV_ACK_TIMEOUT_MS);
            rf_rx_start();
        }
    }
}

void RF_Service(void)
{
    uint32_t delay_ticks;
    uint32_t now_clock = TMOS_GetSystemClock();

    rf_rev_req_check_ack_timeout();
    rf_rev_switch_check_fallback();

    if ((g_rev_req_pending != 0u) &&
        (g_rev_req_active == 0u) &&
        (g_rev_wait_ack == 0u) &&
        (g_rev_req_remaining != 0u) &&
        ((int32_t)(now_clock - g_rev_req_next_clock) >= 0))
    {
        g_rev_req_pending = 0u;
        rf_rev_req_tx_once();
    }

    if (g_rx_restart_pending == 0u)
    {
        return;
    }

    delay_ticks = rf_tmr0_delta(TMR0_GetCurrentTimer(), g_rx_restart_mark_tmr);
    g_rx_restart_delay_sum += delay_ticks;
    if (delay_ticks > g_rx_restart_delay_max)
    {
        g_rx_restart_delay_max = delay_ticks;
    }
    g_rx_restart_delay_cnt++;

    g_rx_restart_pending = 0u;
    rf_rx_start();
}

uint16_t RF_ProcessEvent(uint8_t task_id, uint16_t events)
{
    if (events & SYS_EVENT_MSG)
    {
        uint8_t *pMsg;

        if ((pMsg = tmos_msg_receive(task_id)) != NULL)
        {
            tmos_msg_deallocate(pMsg);
        }
        return (events ^ SYS_EVENT_MSG);
    }

    if (events & SBP_RF_STAT_EVT)
    {
        /* 旧的内部窗口统计会清零 gStat，和 RF_GetStatsLine() 的 [R5] 统计相互干扰。 */
        return events ^ SBP_RF_STAT_EVT;
    }

    if (events & SBP_RF_RF_RX_EVT)
    {
        rf_rx_start();
        return events ^ SBP_RF_RF_RX_EVT;
    }

    return 0;
}

uint16_t RF_GetStatsLine(char *buf, uint16_t len)
{
    uint32_t dt_ms;
    uint32_t ok_win;
    uint32_t gap_win;
    uint32_t crcerr_win;
    uint32_t timeout_win;
    uint32_t ack_win;
    uint32_t ack_switch_win;
    uint32_t rx_hz;
    uint32_t rev_req_burst;
    int n;

    if ((buf == NULL) || (len == 0u))
    {
        return 0;
    }

    dt_ms = RF_STAT_PRINT_PERIOD_MS;
    ok_win = gStat.rx_ok;
    gap_win = gStat.rx_seq_gap;
    crcerr_win = gStat.rx_crcerr;
    timeout_win = gStat.rx_timeout;
    ack_win = gStat.rev_ack_rx;
    ack_switch_win = gStat.rev_ack_switch;
    rev_req_burst = gStat.rev_req_burst;

    rx_hz = (uint32_t)(((uint64_t)ok_win * 1000u) / dt_ms);
    rf_rx_quality_window_update(rx_hz, gap_win);
    g_rx_restart_delay_sum = 0u;
    g_rx_restart_delay_max = 0u;
    g_rx_restart_delay_cnt = 0u;
    n = snprintf(buf, len,
                 "[R5]h%lu o%lu g%lu c%lu t%lu ch%u sw%lu tr%u rq%lu ak%lu/%luu\r\n",
                 rx_hz,
                 ok_win,
                 gap_win,
                 crcerr_win,
                 timeout_win,
                 (unsigned int)g_low_channel,
                 ack_switch_win,
                 (unsigned int)g_rev_last_trigger_reason,
                 rev_req_burst,
                 ack_win,
                 gStat.rev_ack_latency_us);

    gStat.rx_total = 0;
    gStat.rx_ok = 0;
    gStat.rx_fail = 0;
    gStat.rx_crcerr = 0;
    gStat.rx_timeout = 0;
    gStat.rx_bad_magic = 0;
    gStat.rx_bad_session = 0;
    gStat.rx_bad_crc = 0;
    gStat.rx_seq_gap = 0;
    gStat.rx_dup = 0;
    gStat.rx_reacquire = 0;
    gStat.hop_done = 0;
    gStat.rev_req_sent = 0;
    gStat.rev_req_ok = 0;
    gStat.rev_req_fail = 0;
    gStat.rev_req_burst = 0;
    gStat.rev_sync_seen = 0;
    gStat.rev_quality_trigger = 0;
    gStat.rev_ack_rx = 0;
    gStat.rev_ack_switch = 0;

    if (n <= 0)
    {
        return 0;
    }
    if (n >= (int)len)
    {
        return (uint16_t)(len - 1u);
    }
    return (uint16_t)n;
}

void RF_Init(void)
{
    g_ret_role_init = RF_RoleInit();
    taskID = TMOS_ProcessEventRegister(RF_ProcessEvent);
    g_task_id_dbg = taskID;
    TMR0_TimerInit(TMR0_FREE_RUN_END);

    PFIC_EnableIRQ(BLEB_IRQn);
    PFIC_EnableIRQ(BLEL_IRQn);

    g_last_stat_clock = TMOS_GetSystemClock();
    g_rx_scan_active = 0u;
    g_rx_pending_hop = 0u;
    g_rx_pending_epoch = 0u;
    g_rx_pending_channel = RF_TEST_FREQUENCY;
    g_rx_pending_switch_seq = 0u;
    g_rev_req_active = 0u;
    g_rev_req_remaining = 0u;
    g_rev_req_epoch = 0u;
    g_rev_req_next_channel = RF_TEST_FREQUENCY;
    g_rev_req_reason = RF_REV_TRIGGER_PERIODIC;
    g_rev_last_trigger_reason = RF_REV_TRIGGER_PERIODIC;
    g_rev_req_pending = 0u;
    g_rev_wait_ack = 0u;
    g_rev_switch_pending = 0u;
    g_rev_switch_channel = RF_TEST_FREQUENCY;
    g_rev_switch_seq = 0u;
    g_rev_switch_epoch = 0u;
    g_rev_req_next_clock = 0u;
    g_rev_ack_deadline_clock = 0u;
    g_rev_switch_deadline_clock = 0u;
    g_rx_rev_countdown = RF_REV_COUNTDOWN_FAR;
    g_rx_rev_countdown_armed = 1u;
    g_ack_switch_lock_until_clock = 0u;
    g_rx_last_packet_was_ack = 0u;
    g_quality_bad_windows = 0u;
    g_quality_request_pending = 0u;
    g_quality_last_trigger_clock = TMOS_GetSystemClock();

#if (RF_USE_LOW_LEVEL_BASIC == 1)
    rf_low_level_basic_start_rx();
#else
    rfRoleConfig_t conf = {0};
    bStatus_t ret;

    conf.TxPower = BLE_TX_POWER;
    conf.rfProcessCB = RF_ProcessCallBack;
    conf.processMask = RF_STATE_RX | RF_STATE_RX_CRCERR | RF_STATE_TX_FINISH | RF_STATE_TIMEOUT;
    ret = RFRole_BasicInit(&conf);
    g_low_config_ret = (uint8_t)ret;
    if (ret != SUCCESS)
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
    gTxParam.frequency = RF_TEST_FREQUENCY;
    gTxParam.whiteChannel = RF_TEST_FREQUENCY;
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
    gRxParam.timeOut = RF_RX_TIMEOUT_HALF_US;

    rf_rx_tune_for_expected_seq();
#if (RF_TEST_ENABLE_RF_RX == 1)
    rf_rx_start();
    g_basic_started = 1u;
#else
    g_low_rx_ret = 0xFEu;
    g_basic_started = 0u;
#endif
    rx_debug_log("[RX][RFIP] cfg:%u ch:%u rxret:%u rxen:%u\r\n",
                 (unsigned int)g_low_config_ret,
                 (unsigned int)g_low_channel,
                 (unsigned int)g_low_rx_ret,
                 RF_TEST_ENABLE_RF_RX);
#endif
}

/******************************** endfile @ RF_PHY ******************************/
