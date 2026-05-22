/********************************** (C) COPYRIGHT *******************************
 * File Name          : RF_PHY.c
 * Description        : RX side for the 12-byte RF hop protocol.
 *******************************************************************************/

#include "CONFIG.h"
#include "RF_PHY.h"
#include "HAL.h"
#include "wchrf.h"
#include "rf_hop_protocol.h"

#include <stdio.h>
#include <string.h>

#ifndef RF_HOP_MODE
#define RF_HOP_MODE                    2
#endif

#ifndef RF_REPORT_PPS
#define RF_REPORT_PPS                  RFH_DEFAULT_RATE_HZ
#endif

#ifndef RF_RX_PACKET_TIMEOUT_MS
#define RF_RX_PACKET_TIMEOUT_MS        RFH_RX_PACKET_TIMEOUT_MS_DEFAULT
#endif

#ifndef RF_RX_LINK_IDLE_TIMEOUT_MS
#define RF_RX_LINK_IDLE_TIMEOUT_MS     3000u
#endif

#ifndef RF_ACK_WINDOW_US
#define RF_ACK_WINDOW_US               RFH_DEFAULT_ACK_WINDOW_US
#endif

#ifndef RF_CONNECT_PREFER_CHANNEL_A
#define RF_CONNECT_PREFER_CHANNEL_A    1u
#endif

#ifndef RF_HOP_CONFIRM_ACK_TIMEOUT_MS
#define RF_HOP_CONFIRM_ACK_TIMEOUT_MS  RFH_HOP_CONFIRM_ACK_TIMEOUT_MS_DEFAULT
#endif

#ifndef RF_RX_CONFIRM_ACK_FAIL_LIMIT
#define RF_RX_CONFIRM_ACK_FAIL_LIMIT   3u
#endif

#ifndef RF_TEST_FREQUENCY
#define RF_TEST_FREQUENCY              RFH_DEFAULT_CHANNEL_A
#endif

#define RF_STAT_PRINT_PERIOD_MS        5000u
#define RF_TX_SEND_TIME                (20u * 2u)
#define RF_LINK_ACCESS_ADDRESS         0x71764129UL
#define RF_LINK_CRC_INIT               0x555555UL
/* WCH rfipRx_t.timeOut: 0 means no timeout; connected RX must stay armed. */
#define RFIP_RX_NO_TIMEOUT             0u
#define RF_CONNECTED_RX_TIMEOUT_US     RFIP_RX_NO_TIMEOUT
#define RF_DUAL_RX_DWELL_US            2000u
#define RF_ACK_SCHEDULE_TICKS          48u
#define RF_ACK_TX_LEAD_US              80u
#define RF_ACK_TX_POST_GUARD_US        RFH_ACK_RX_POST_GUARD_US_DEFAULT
#define RF_ACK_TX_SAFETY_MAX           8u
#define RF_ACK_RX_PRE_GUARD_US         RFH_ACK_RX_PRE_GUARD_US_DEFAULT
#define RF_ACK_RX_POST_GUARD_US        RFH_ACK_RX_POST_GUARD_US_DEFAULT
#define TMR0_FREE_RUN_END              0x03FFFFFFUL
#define TMR0_FREE_RUN_WRAP             0x04000000UL

#define SBP_RF_RF_RX_EVT               4
#define SBP_RF_STAT_EVT                (1 << 5)

typedef enum {
    RX_UNCONNECTED = 0u,
    RX_CONNECT_ACK_PENDING,
    RX_COMM,
    RX_HOP_RESERVED,
    RX_HOP_CONFIRM_ACK_PENDING
} rx_state_t;

typedef struct
{
    volatile uint32_t rx_ok;
    volatile uint32_t rx_bad_len;
    volatile uint32_t rx_bad_type;
    volatile uint32_t rx_bad_connect;
    volatile uint32_t rx_ignored_data;
    volatile uint32_t rx_crcerr;
    volatile uint32_t rx_timeout;
    volatile uint32_t rx_arm_try;
    volatile uint32_t rx_arm_ok;
    volatile uint32_t rx_arm_fail;
    volatile uint32_t tx_ack_try;
    volatile uint32_t tx_ack_start_fail;
    volatile uint32_t tx_ack_parm_fail;
    volatile uint32_t tx_ack_ok;
    volatile uint32_t tx_ack_fail;
    volatile uint32_t connect_rx;
    volatile uint32_t data_rx;
    volatile uint32_t scan_switch;
    volatile uint32_t hop_cmd_rx;
    volatile uint32_t hop_done;
} rf_stat_t;

uint8_t taskID;

static rfRoleParam_t gParm;
static rfipTx_t gTxParam;
static rfipRx_t gRxParam;
static rf_stat_t gStat = {0};

__attribute__((__aligned__(4))) static uint8_t TxBuf[64];
__attribute__((__aligned__(4))) static uint8_t RxBuf[264];

static volatile uint8_t g_ret_role_init = 0xFFu;
static volatile uint8_t g_low_config_ret = 0xFFu;
static volatile uint8_t g_low_rx_ret = 0xFFu;
static volatile uint8_t g_low_tx_ret = 0xFFu;
static volatile uint8_t g_basic_started = 0u;
static volatile uint8_t g_ack_tx_active = 0u;

static volatile rx_state_t g_state = RX_UNCONNECTED;
static rx_state_t g_ack_success_next_state = RX_COMM;
static uint8_t g_rx_channel = RF_TEST_FREQUENCY;
static uint8_t g_scan_channel_a = RFH_DEFAULT_CHANNEL_A;
static uint8_t g_scan_channel_b = RFH_DEFAULT_CHANNEL_B;
static uint8_t g_ack_channel = RFH_DEFAULT_CHANNEL_A;
static uint8_t g_ack_tx_channel = RFH_DEFAULT_CHANNEL_A;
static uint8_t g_old_channel = RFH_DEFAULT_CHANNEL_A;
static uint8_t g_target_channel = RFH_DEFAULT_CHANNEL_A;
static uint8_t g_hop_seq = 0u;
static uint8_t g_pending_ack_cmd = RFH_CMD_NONE;
static uint8_t g_confirm_ack_fail_count = 0u;

static uint16_t g_report_hz = RF_REPORT_PPS;
static uint8_t g_rate_code = RFH_RATE_8K;
static uint8_t g_ack_window_ms = RFH_DEFAULT_ACK_WINDOW_MS;
static uint8_t g_ack_window_packets = 8u;
static uint32_t g_tick_per_evt = 1u;
static uint8_t g_ack_pending = 0u;
static uint8_t g_ack_repeat_remaining = 0u;
static uint32_t g_ack_due_tmr = 0u;
static uint32_t g_ack_until_tmr = 0u;
static uint32_t g_last_rx_packet_clock = 0u;
static uint32_t g_log_last_clock = 0u;
static uint32_t g_hop_due_clock = 0u;
static uint32_t g_hop_confirm_deadline_clock = 0u;
static uint16_t g_rx_since_ack = 0u;
static uint16_t g_last_ack_loss_permille = 1000u;
static uint16_t g_last_ack_expected = 0u;
static uint16_t g_last_ack_rx_count = 0u;
static uint8_t g_last_ack_cmd = RFH_CMD_NONE;

static uint32_t g_log_rx_ok = 0u;
static uint32_t g_log_ack_ok = 0u;
static uint8_t g_log_hop_events = 0u;
static uint8_t g_log_unconnected_events = 0u;
static uint8_t g_log_errors = 0u;
static uint8_t g_log_app_timeout_events = 0u;
static uint8_t g_log_data_resync_events = 0u;

static uint8_t g_diag_pending = 0u;
static uint32_t g_diag_rx_arm_try = 0u;
static uint32_t g_diag_rx_arm_ok = 0u;
static uint32_t g_diag_rx_arm_fail = 0u;
static uint32_t g_diag_rx_ok = 0u;
static uint32_t g_diag_rx_crcerr = 0u;
static uint32_t g_diag_rx_timeout = 0u;
static uint32_t g_diag_rx_bad_len = 0u;
static uint32_t g_diag_rx_bad_type = 0u;
static uint32_t g_diag_rx_bad_connect = 0u;
static uint32_t g_diag_rx_ignored_data = 0u;
static uint32_t g_diag_tx_ack_try = 0u;
static uint32_t g_diag_tx_ack_fail = 0u;
static uint8_t g_diag_app_timeout_events = 0u;
static uint8_t g_diag_data_resync_events = 0u;
static volatile uint32_t g_rx_progress_count = 0u;
static uint32_t g_rx_progress_seen_count = 0u;
static uint32_t g_rx_progress_clock = 0u;

void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id);

static uint8_t rx_time_reached(uint32_t now, uint32_t deadline)
{
    return ((int32_t)(now - deadline) >= 0) ? 1u : 0u;
}

static uint8_t rx_tmr_reached(uint32_t now, uint32_t deadline)
{
    if(now >= deadline)
    {
        return 1u;
    }
    return ((deadline - now) > (TMR0_FREE_RUN_WRAP / 2u)) ? 1u : 0u;
}

static uint8_t rx_tmr_before(uint32_t a, uint32_t b)
{
    if(a <= b)
    {
        return ((b - a) < (TMR0_FREE_RUN_WRAP / 2u)) ? 1u : 0u;
    }
    return ((a - b) > (TMR0_FREE_RUN_WRAP / 2u)) ? 1u : 0u;
}

static uint32_t rx_tmr_add(uint32_t base, uint32_t delta)
{
    base += delta;
    while(base >= TMR0_FREE_RUN_WRAP)
    {
        base -= TMR0_FREE_RUN_WRAP;
    }
    return base;
}

static uint32_t rx_us_to_tmr_cycles(uint16_t us)
{
    uint32_t cycles_per_us = GetSysClock() / 1000000u;

    if(cycles_per_us == 0u)
    {
        cycles_per_us = 1u;
    }
    return (uint32_t)us * cycles_per_us;
}

static uint8_t rx_is_seek_scanning_state(void)
{
    return ((g_state == RX_UNCONNECTED) ||
            ((g_state == RX_CONNECT_ACK_PENDING) &&
             (g_pending_ack_cmd == RFH_CMD_CONNECT_REQ))) ? 1u : 0u;
}

static uint8_t rx_is_packet_timeout_state(void)
{
    return ((g_state == RX_HOP_RESERVED) ||
            (g_state == RX_HOP_CONFIRM_ACK_PENDING)) ? 1u : 0u;
}

static const char *rx_state_code(void)
{
    switch(g_state)
    {
    case RX_UNCONNECTED:
        return "U";
    case RX_CONNECT_ACK_PENDING:
        return "PA";
    case RX_COMM:
        return "C";
    case RX_HOP_RESERVED:
        return "HR";
    case RX_HOP_CONFIRM_ACK_PENDING:
    default:
        return "CA";
    }
}

static void rx_log_note_error(void)
{
    if(g_log_errors < 99u)
    {
        g_log_errors++;
    }
}

static void rx_log_note_hop_event(void)
{
    if(g_log_hop_events < 99u)
    {
        g_log_hop_events++;
    }
}

static void rx_log_note_unconnected(void)
{
    if(g_log_unconnected_events < 99u)
    {
        g_log_unconnected_events++;
    }
}

#if ((RF_RX_PACKET_TIMEOUT_MS > 0u) || (RF_RX_LINK_IDLE_TIMEOUT_MS > 0u))
static void rx_log_note_app_timeout(void)
{
    if(g_log_app_timeout_events < 99u)
    {
        g_log_app_timeout_events++;
    }
}
#endif

static void rx_log_note_data_resync(void)
{
    if(g_log_data_resync_events < 99u)
    {
        g_log_data_resync_events++;
    }
}

static void rx_note_packet_progress(void)
{
    g_rx_progress_count++;
}

static uint16_t rx_expected_packets_per_ack(void)
{
    uint32_t silent_packets;

    silent_packets = (uint32_t)rfh_packets_for_us(g_report_hz,
                                                   RF_ACK_RX_PRE_GUARD_US) +
                     (uint32_t)g_ack_window_packets +
                     (uint32_t)rfh_packets_for_us(g_report_hz,
                                                  RF_ACK_RX_POST_GUARD_US);
    if(silent_packets >= g_report_hz)
    {
        return 1u;
    }
    return (uint16_t)(g_report_hz - silent_packets);
}

static uint32_t rx_expected_packets_for_elapsed_ms(uint32_t elapsed_ms)
{
    uint32_t expected;

    if(elapsed_ms == 0u)
    {
        elapsed_ms = 1u;
    }
    expected = (uint32_t)(((uint64_t)rx_expected_packets_per_ack() *
                           elapsed_ms) / 1000u);
    return (expected == 0u) ? 1u : expected;
}

static uint32_t rx_log_elapsed_ms(uint32_t now_clock)
{
    uint32_t elapsed_ticks;
    uint32_t elapsed_ms;

    if(g_log_last_clock == 0u)
    {
        return RF_STAT_PRINT_PERIOD_MS;
    }

    elapsed_ticks = now_clock - g_log_last_clock;
    if((int32_t)elapsed_ticks <= 0)
    {
        return RF_STAT_PRINT_PERIOD_MS;
    }

    elapsed_ms = (uint32_t)(((uint64_t)elapsed_ticks * SYSTEM_TIME_MICROSEN) /
                            1000u);
    if((elapsed_ms < 1000u) || (elapsed_ms > 10000u))
    {
        return RF_STAT_PRINT_PERIOD_MS;
    }

    return elapsed_ms;
}

static uint16_t rx_calc_loss_permille(uint32_t rx_count, uint32_t expected)
{
    uint32_t lost;

    if(expected == 0u)
    {
        return 0u;
    }
    if(rx_count >= expected)
    {
        return 0u;
    }
    lost = expected - rx_count;
    return (uint16_t)((lost * 1000u) / expected);
}

static void rx_apply_rate(uint8_t rate_code, uint8_t ack_window_ms)
{
    if(rate_code > RFH_RATE_8K)
    {
        rate_code = RFH_RATE_8K;
    }
    g_rate_code = rate_code;
    g_report_hz = rfh_rate_hz_from_code(rate_code);
    g_ack_window_ms = (ack_window_ms == 0u) ? RFH_DEFAULT_ACK_WINDOW_MS : ack_window_ms;
    g_ack_window_packets = rfh_ack_window_packets_us(g_report_hz,
                                                     RF_ACK_WINDOW_US);
    g_tick_per_evt = GetSysClock() / g_report_hz;
    if(g_tick_per_evt == 0u)
    {
        g_tick_per_evt = 1u;
    }
}

static void rx_set_channel(uint8_t channel)
{
    if(rfh_channel_valid(channel) == 0u)
    {
        return;
    }
    g_rx_channel = channel;
    gRxParam.frequency = channel;
    gRxParam.whiteChannel = channel;
}

static uint8_t rx_scan_next_channel(uint8_t channel)
{
    if(rfh_channel_valid(channel) == 0u)
    {
        return RFH_MIN_CHANNEL;
    }
    channel++;
    if(channel > RFH_MAX_CHANNEL)
    {
        channel = RFH_MIN_CHANNEL;
    }
    return channel;
}

static uint8_t rx_scan_random_channel(void)
{
    uint32_t rnd = tmos_rand();
    uint8_t span = (uint8_t)(RFH_MAX_CHANNEL - RFH_MIN_CHANNEL + 1u);

    rnd ^= TMOS_GetSystemClock();
    return (uint8_t)(RFH_MIN_CHANNEL + (uint8_t)(rnd % span));
}

static void rx_enter_state(rx_state_t next)
{
    if(g_state == next)
    {
        return;
    }

    g_state = next;
    switch(next)
    {
    case RX_UNCONNECTED:
        g_ack_pending = 0u;
        g_ack_repeat_remaining = 0u;
        g_ack_until_tmr = 0u;
        g_pending_ack_cmd = RFH_CMD_NONE;
        g_hop_seq = 0u;
        g_confirm_ack_fail_count = 0u;
        g_rx_since_ack = 0u;
        g_scan_channel_a = RFH_MIN_CHANNEL;
        g_scan_channel_b = RFH_MAX_CHANNEL;
        rx_set_channel(rx_scan_random_channel());
        rx_log_note_unconnected();
        break;
    case RX_COMM:
        g_ack_success_next_state = RX_COMM;
        g_pending_ack_cmd = RFH_CMD_NONE;
        g_confirm_ack_fail_count = 0u;
        g_last_rx_packet_clock = TMOS_GetSystemClock();
        break;
    case RX_CONNECT_ACK_PENDING:
        break;
    case RX_HOP_RESERVED:
        rx_log_note_hop_event();
        break;
    case RX_HOP_CONFIRM_ACK_PENDING:
    default:
        rx_log_note_hop_event();
        break;
    }
}

static void rx_toggle_seek_channel(void)
{
    if((g_state == RX_UNCONNECTED) ||
       ((g_state == RX_CONNECT_ACK_PENDING) &&
        (g_pending_ack_cmd == RFH_CMD_CONNECT_REQ)))
    {
        rx_set_channel(rx_scan_next_channel(g_rx_channel));
    }
    else
    {
        if(g_rx_channel == g_scan_channel_a)
        {
            rx_set_channel(g_scan_channel_b);
        }
        else
        {
            rx_set_channel(g_scan_channel_a);
        }
    }
    gStat.scan_switch++;
}

__HIGH_CODE
static void rx_start_rx(void)
{
    if((g_basic_started == 0u) || (g_ack_tx_active != 0u))
    {
        return;
    }

    gRxParam.frequency = g_rx_channel;
    gRxParam.whiteChannel = g_rx_channel;
    gRxParam.rxDMA = (uint32_t)RxBuf;
    gRxParam.rxMaxLen = RFH_AIR_PACKET_LEN;
    gRxParam.timeOut = (rx_is_seek_scanning_state() != 0u) ?
                       RF_DUAL_RX_DWELL_US :
                       RF_CONNECTED_RX_TIMEOUT_US;
    gStat.rx_arm_try++;
    g_low_rx_ret = (uint8_t)RFIP_SetRx(&gRxParam);
    if(g_low_rx_ret != SUCCESS)
    {
        gStat.rx_arm_fail++;
        rx_log_note_error();
    }
    else
    {
        gStat.rx_arm_ok++;
    }
}

static void rx_schedule_ack(uint8_t countdown_ticks)
{
    uint32_t now_tmr;
    uint32_t delay_cycles;
    uint32_t lead_cycles;
    uint32_t window_cycles;
    uint32_t post_guard_cycles;
    uint32_t due_tmr;
    uint32_t until_tmr;

    if(countdown_ticks == RFH_ACK_COUNTDOWN_FAR)
    {
        return;
    }
    if(countdown_ticks > RF_ACK_SCHEDULE_TICKS)
    {
        return;
    }

    delay_cycles = (uint32_t)countdown_ticks * g_tick_per_evt;
    window_cycles = (uint32_t)g_ack_window_packets * g_tick_per_evt;
    post_guard_cycles = rx_us_to_tmr_cycles(RF_ACK_TX_POST_GUARD_US);
    lead_cycles = rx_us_to_tmr_cycles(RF_ACK_TX_LEAD_US);
    now_tmr = TMR0_GetCurrentTimer();
    until_tmr = rx_tmr_add(now_tmr, delay_cycles + window_cycles + post_guard_cycles);
    if(delay_cycles > lead_cycles)
    {
        delay_cycles -= lead_cycles;
    }
    else
    {
        delay_cycles = 0u;
    }

    due_tmr = rx_tmr_add(now_tmr, delay_cycles);
    if((g_ack_pending == 0u) || (rx_tmr_before(due_tmr, g_ack_due_tmr) != 0u))
    {
        g_ack_due_tmr = due_tmr;
        g_ack_until_tmr = until_tmr;
        g_ack_channel = g_rx_channel;
        g_ack_tx_channel = g_rx_channel;
    }
    g_ack_pending = 1u;
}

static void rx_prepare_command_ack(uint8_t cmd, rx_state_t next_state, uint8_t countdown_ticks)
{
    g_pending_ack_cmd = cmd;
    g_ack_success_next_state = next_state;
    rx_schedule_ack(countdown_ticks);
    if((cmd == RFH_CMD_CONNECT_REQ) || (cmd == RFH_CMD_HOP_PREPARE))
    {
        rx_enter_state(RX_CONNECT_ACK_PENDING);
    }
    else if(cmd == RFH_CMD_HOP_CONFIRM)
    {
        rx_enter_state(RX_HOP_CONFIRM_ACK_PENDING);
    }
}

static void rx_fill_ack_packet(void)
{
    uint8_t *air = &TxBuf[2];
    uint8_t *data = &air[RFH_DATA_OFFSET];
    uint16_t expected = rx_expected_packets_per_ack();
    uint16_t loss_permille = rx_calc_loss_permille(g_rx_since_ack, expected);
    uint8_t flags = RFH_FLAG_LINK_OK;

    if(g_pending_ack_cmd != RFH_CMD_NONE)
    {
        flags |= RFH_FLAG_CMD_ACK;
    }

    TxBuf[0] = RFH_WCH_PREAMBLE;
    TxBuf[1] = RFH_AIR_PACKET_LEN;
    air[RFH_HDR0_OFFSET] = rfh_make_header0(RFH_PKT_ACK, g_rate_code, flags);
    air[RFH_HDR1_OFFSET] = (loss_permille > 255u) ? 255u : (uint8_t)loss_permille;
    rfh_put_u16(&data[RFH_ACK_LOSS_PERMILLE_LO], loss_permille);
    rfh_put_u16(&data[RFH_ACK_RX_COUNT_LO], g_rx_since_ack);
    rfh_put_u16(&data[RFH_ACK_EXPECTED_COUNT_LO], expected);
    data[RFH_ACK_CMD_ID] = g_pending_ack_cmd;
    data[RFH_ACK_FLAGS] = flags;
    data[RFH_ACK_CHANNEL] = g_ack_channel;
    data[RFH_ACK_STATUS] = (g_state == RX_UNCONNECTED) ?
                           RFH_ACK_STATUS_SEEK :
                           RFH_ACK_STATUS_CONNECTED;

    g_last_ack_loss_permille = loss_permille;
    g_last_ack_expected = expected;
    g_last_ack_rx_count = g_rx_since_ack;
    g_last_ack_cmd = g_pending_ack_cmd;
}

__HIGH_CODE
static uint8_t rx_start_ack_tx_loaded(void)
{
    bStatus_t ret_start;
    bStatus_t ret_parm;

    if((g_basic_started == 0u) || (g_ack_tx_active != 0u))
    {
        return 0u;
    }

    gStat.tx_ack_try++;
    ret_start = RFIP_SetTxStart();
    g_low_tx_ret = (uint8_t)ret_start;
    if(ret_start != SUCCESS)
    {
        gStat.tx_ack_start_fail++;
        gStat.tx_ack_fail++;
        rx_log_note_error();
        return 0u;
    }

    ret_parm = RFIP_SetTxParm(&gTxParam);
    if(ret_parm != SUCCESS)
    {
        gStat.tx_ack_parm_fail++;
        gStat.tx_ack_fail++;
        rx_log_note_error();
        return 0u;
    }
    g_ack_tx_active = 1u;
    return 1u;
}

static void rx_after_ack_done(uint8_t success)
{
    rx_state_t next_state = g_ack_success_next_state;
    uint8_t ack_cmd = g_pending_ack_cmd;

    g_ack_tx_active = 0u;
    g_ack_pending = 0u;
    g_ack_repeat_remaining = 0u;
    g_ack_until_tmr = 0u;
    g_rx_since_ack = 0u;
    g_pending_ack_cmd = RFH_CMD_NONE;

    if(success != 0u)
    {
        if(ack_cmd == RFH_CMD_HOP_CONFIRM)
        {
            g_confirm_ack_fail_count = 0u;
        }
        if(next_state == RX_HOP_RESERVED)
        {
            rx_set_channel(g_old_channel);
            rx_enter_state(RX_HOP_RESERVED);
        }
        else if(next_state == RX_COMM)
        {
            rx_set_channel(g_ack_channel);
            rx_enter_state(RX_COMM);
        }
    }
    else
    {
        rx_log_note_error();
        if((ack_cmd == RFH_CMD_HOP_CONFIRM) &&
           (g_state == RX_HOP_CONFIRM_ACK_PENDING))
        {
            if(g_confirm_ack_fail_count < 255u)
            {
                g_confirm_ack_fail_count++;
            }
            if(g_confirm_ack_fail_count < RF_RX_CONFIRM_ACK_FAIL_LIMIT)
            {
                rx_prepare_command_ack(RFH_CMD_HOP_CONFIRM, RX_COMM, 0u);
                rx_start_rx();
                return;
            }
        }
        if((g_state == RX_CONNECT_ACK_PENDING) || (g_state == RX_HOP_CONFIRM_ACK_PENDING))
        {
            rx_enter_state(RX_UNCONNECTED);
        }
    }

    rx_start_rx();
}

__HIGH_CODE
static void rx_send_ack(void)
{
    if((g_basic_started == 0u) || (g_ack_tx_active != 0u))
    {
        return;
    }

    rx_fill_ack_packet();
    (void)RFRole_Stop();
    gTxParam.frequency = g_ack_tx_channel;
    gTxParam.whiteChannel = g_ack_tx_channel;
    gTxParam.txDMA = (uint32_t)TxBuf;
    g_ack_pending = 0u;
    g_ack_repeat_remaining = (RF_ACK_TX_SAFETY_MAX > 0u) ?
                             (RF_ACK_TX_SAFETY_MAX - 1u) :
                             0u;

    if(rx_start_ack_tx_loaded() == 0u)
    {
        rx_after_ack_done(0u);
    }
}

static uint8_t rx_handle_connect(const uint8_t *air)
{
    const uint8_t *data = &air[RFH_DATA_OFFSET];

    if(rfh_get_u32(&data[RFH_CONNECT_SESSION0]) != RFH_CONNECT_SESSION_ID)
    {
        gStat.rx_bad_connect++;
        rx_log_note_error();
        return 0u;
    }
    if(data[RFH_CONNECT_VERSION] != RFH_PROTOCOL_VERSION)
    {
        gStat.rx_bad_connect++;
        rx_log_note_error();
        return 0u;
    }

    g_scan_channel_a = data[RFH_CONNECT_CH_A];
    g_scan_channel_b = data[RFH_CONNECT_CH_B];
    if(rfh_channel_valid(g_scan_channel_a) == 0u)
    {
        g_scan_channel_a = RFH_DEFAULT_CHANNEL_A;
    }
    if(rfh_channel_valid(g_scan_channel_b) == 0u)
    {
        g_scan_channel_b = RFH_DEFAULT_CHANNEL_B;
    }
    if(g_scan_channel_b == g_scan_channel_a)
    {
        g_scan_channel_b = (g_scan_channel_a == RFH_DEFAULT_CHANNEL_A) ?
                           RFH_DEFAULT_CHANNEL_B :
                           RFH_DEFAULT_CHANNEL_A;
    }
    rx_apply_rate(data[RFH_CONNECT_RATE], data[RFH_CONNECT_ACK_WINDOW_MS]);
    rx_note_packet_progress();
    g_rx_since_ack = 0u;
    g_last_rx_packet_clock = TMOS_GetSystemClock();
    g_hop_seq = 0u;
    g_confirm_ack_fail_count = 0u;
    gStat.connect_rx++;
    rx_prepare_command_ack(RFH_CMD_CONNECT_REQ, RX_COMM, air[RFH_HDR1_OFFSET]);
#if (RF_CONNECT_PREFER_CHANNEL_A != 0u)
    g_ack_channel = g_scan_channel_a;
#endif
    return 1u;
}

static void rx_handle_hop_prepare(const uint8_t *air)
{
    const uint8_t *data = &air[RFH_DATA_OFFSET];
    uint8_t channel = data[RFH_HOP_CMD_CHANNEL];
    uint8_t seq = data[RFH_HOP_CMD_SEQ];
    uint16_t delay_ms;

    if((rfh_channel_valid(channel) == 0u) ||
       (channel == g_rx_channel) ||
       (seq == 0u))
    {
        rx_log_note_error();
        return;
    }
    if((g_state == RX_HOP_RESERVED) ||
       ((g_state == RX_CONNECT_ACK_PENDING) &&
        (g_pending_ack_cmd == RFH_CMD_HOP_PREPARE)))
    {
        if((seq != g_hop_seq) || (channel != g_target_channel))
        {
            rx_log_note_error();
            return;
        }
    }
    else if(g_state == RX_COMM)
    {
        if(seq == g_hop_seq)
        {
            rx_log_note_error();
            return;
        }
    }
    else
    {
        rx_log_note_error();
        return;
    }

    delay_ms = rfh_get_u16(&data[RFH_HOP_CMD_DELAY_LO_MS]);
    g_old_channel = g_rx_channel;
    g_target_channel = channel;
    g_hop_seq = seq;
    g_hop_due_clock = TMOS_GetSystemClock() + MS1_TO_SYSTEM_TIME(delay_ms);
    g_hop_confirm_deadline_clock = g_hop_due_clock +
                                   MS1_TO_SYSTEM_TIME(RF_HOP_CONFIRM_ACK_TIMEOUT_MS);
    gStat.hop_cmd_rx++;
    rx_prepare_command_ack(RFH_CMD_HOP_PREPARE, RX_HOP_RESERVED, air[RFH_HDR1_OFFSET]);
}

static void rx_handle_hop_confirm(const uint8_t *air)
{
    const uint8_t *data = &air[RFH_DATA_OFFSET];
    uint8_t channel = data[RFH_HOP_CMD_CHANNEL];
    uint8_t seq = data[RFH_HOP_CMD_SEQ];
    uint8_t old_channel = data[RFH_HOP_CONFIRM_OLD_CHANNEL];

    if((g_state != RX_HOP_RESERVED) &&
       (g_state != RX_HOP_CONFIRM_ACK_PENDING) &&
       !((g_state == RX_COMM) && (seq == g_hop_seq)))
    {
        rx_log_note_error();
        return;
    }
    if((channel != g_rx_channel) ||
       (channel != g_target_channel) ||
       (old_channel != g_old_channel))
    {
        rx_log_note_error();
        return;
    }
    if(seq != g_hop_seq)
    {
        rx_log_note_error();
        return;
    }

    gStat.hop_cmd_rx++;
    g_confirm_ack_fail_count = 0u;
    rx_prepare_command_ack(RFH_CMD_HOP_CONFIRM, RX_COMM, air[RFH_HDR1_OFFSET]);
}

static void rx_handle_command(const uint8_t *air)
{
    const uint8_t *data = &air[RFH_DATA_OFFSET];
    uint8_t flags = rfh_flags(air[RFH_HDR0_OFFSET]);

    if((flags & RFH_FLAG_CMD_PRESENT) == 0u)
    {
        return;
    }

    switch(data[RFH_CMD_SLOT_ID])
    {
    case RFH_CMD_HOP_PREPARE:
        rx_handle_hop_prepare(air);
        break;
    case RFH_CMD_HOP_CONFIRM:
        rx_handle_hop_confirm(air);
        break;
    case RFH_CMD_HOP_CANCEL:
        rx_enter_state(RX_COMM);
        break;
    default:
        rx_log_note_error();
        break;
    }
}

static uint8_t rx_handle_data(const uint8_t *air)
{
    if(g_state == RX_UNCONNECTED)
    {
        if((rfh_flags(air[RFH_HDR0_OFFSET]) & RFH_FLAG_LINK_OK) == 0u)
        {
            gStat.rx_ignored_data++;
            return 0u;
        }

        rx_log_note_data_resync();
        rx_enter_state(RX_COMM);
    }

    rx_apply_rate(rfh_rate_code(air[RFH_HDR0_OFFSET]), g_ack_window_ms);
    rx_note_packet_progress();
    g_last_rx_packet_clock = TMOS_GetSystemClock();
    if(g_rx_since_ack < 0xFFFFu)
    {
        g_rx_since_ack++;
    }
    gStat.data_rx++;
    g_log_rx_ok++;
    rx_handle_command(air);
    rx_schedule_ack(air[RFH_HDR1_OFFSET]);
    return 1u;
}

static uint8_t rx_process_air_packet(const uint8_t *rx_buf)
{
    const uint8_t *air = &rx_buf[2];
    uint8_t type;

    if(rx_buf[1] != RFH_AIR_PACKET_LEN)
    {
        gStat.rx_bad_len++;
        rx_log_note_error();
        return 0u;
    }

    type = rfh_packet_type(air[RFH_HDR0_OFFSET]);
    switch(type)
    {
    case RFH_PKT_CONNECT:
        return rx_handle_connect(air);
    case RFH_PKT_DATA:
        return rx_handle_data(air);
    default:
        gStat.rx_bad_type++;
        rx_log_note_error();
        return 0u;
    }
}

static void rx_service_timers(uint8_t check_idle_timeout)
{
    uint32_t now_clock = TMOS_GetSystemClock();
#if (RF_RX_LINK_IDLE_TIMEOUT_MS > 0u)
    uint32_t progress_count;
#endif

    if((g_ack_pending != 0u) &&
       (g_ack_tx_active == 0u) &&
       (rx_tmr_reached(TMR0_GetCurrentTimer(), g_ack_due_tmr) != 0u))
    {
        rx_send_ack();
        return;
    }

#if (RF_RX_PACKET_TIMEOUT_MS > 0u)
    if((check_idle_timeout != 0u) &&
       (rx_is_packet_timeout_state() != 0u) &&
       (g_ack_pending == 0u) &&
       (g_ack_tx_active == 0u) &&
       (rx_time_reached(now_clock,
                        g_last_rx_packet_clock +
                        MS1_TO_SYSTEM_TIME(RF_RX_PACKET_TIMEOUT_MS)) != 0u))
    {
        rx_log_note_app_timeout();
        rx_enter_state(RX_UNCONNECTED);
        (void)RFRole_Stop();
        rx_start_rx();
        return;
    }
#endif

#if (RF_RX_LINK_IDLE_TIMEOUT_MS > 0u)
    if(check_idle_timeout != 0u)
    {
        progress_count = g_rx_progress_count;
        if((g_rx_progress_clock == 0u) ||
           (progress_count != g_rx_progress_seen_count))
        {
            g_rx_progress_seen_count = progress_count;
            g_rx_progress_clock = now_clock;
        }

        if((g_state != RX_UNCONNECTED) &&
           (g_ack_tx_active == 0u) &&
           (rx_time_reached(now_clock,
                            g_rx_progress_clock +
                            MS1_TO_SYSTEM_TIME(RF_RX_LINK_IDLE_TIMEOUT_MS)) != 0u))
        {
            rx_log_note_app_timeout();
            rx_enter_state(RX_UNCONNECTED);
            (void)RFRole_Stop();
            rx_start_rx();
            return;
        }
    }
#endif

    if((g_state == RX_HOP_RESERVED) &&
       (rx_time_reached(now_clock, g_hop_due_clock) != 0u) &&
       (g_rx_channel != g_target_channel))
    {
        rx_set_channel(g_target_channel);
        gStat.hop_done++;
        rx_log_note_hop_event();
        (void)RFRole_Stop();
        rx_start_rx();
        return;
    }

    if((g_state == RX_HOP_RESERVED) &&
       (g_rx_channel == g_target_channel) &&
       (rx_time_reached(now_clock, g_hop_confirm_deadline_clock) != 0u))
    {
        rx_enter_state(RX_UNCONNECTED);
        (void)RFRole_Stop();
        rx_start_rx();
        return;
    }
}

static void rx_basic_start(void)
{
    rfRoleConfig_t conf = {0};
    bStatus_t ret;

    conf.TxPower = BLE_TX_POWER;
    conf.rfProcessCB = RF_ProcessCallBack;
    conf.processMask = RF_STATE_RX | RF_STATE_RX_CRCERR |
                       RF_STATE_TX_FINISH | RF_STATE_TIMEOUT;
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
    gTxParam.frequency = g_rx_channel;
    gTxParam.whiteChannel = g_rx_channel;
    gTxParam.sendTime = (uint8_t)gParm.sendTime;
    gTxParam.sendCount = 1u;
    gTxParam.txDMA = (uint32_t)TxBuf;

    gRxParam.accessAddress = gParm.accessAddress;
    gRxParam.crcInit = gParm.crcInit;
    gRxParam.properties = gParm.properties;
    gRxParam.frequency = g_rx_channel;
    gRxParam.whiteChannel = g_rx_channel;
    gRxParam.rxDMA = (uint32_t)RxBuf;
    gRxParam.rxMaxLen = RFH_AIR_PACKET_LEN;
    gRxParam.timeOut = RF_DUAL_RX_DWELL_US;

    g_basic_started = 1u;
    rx_start_rx();
    PRINT("[RX][RFH] cfg:%u scan:%u-%u rate:%u ack:%uus len:%u\r\n",
          (unsigned int)g_low_config_ret,
          RFH_MIN_CHANNEL,
          RFH_MAX_CHANNEL,
          (unsigned int)g_report_hz,
          (unsigned int)RF_ACK_WINDOW_US,
          RFH_AIR_PACKET_LEN);
}

__HIGH_CODE
void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id)
{
    uint8_t rx_snapshot[RFH_AIR_PACKET_LEN + 2u];
    uint8_t i;

    (void)id;

    if(sta & RF_STATE_RX)
    {
        for(i = 0u; i < (uint8_t)sizeof(rx_snapshot); i++)
        {
            rx_snapshot[i] = RxBuf[i];
        }

        if(g_ack_tx_active == 0u)
        {
            rx_start_rx();
        }

        if(rx_process_air_packet(rx_snapshot) != 0u)
        {
            gStat.rx_ok++;
        }
        rx_service_timers(0u);
    }
    if(sta & RF_STATE_RX_CRCERR)
    {
        gStat.rx_crcerr++;
        rx_log_note_error();
        if(rx_is_seek_scanning_state() != 0u)
        {
            rx_toggle_seek_channel();
        }
        rx_start_rx();
    }
    if(sta & RF_STATE_TIMEOUT)
    {
        gStat.rx_timeout++;
        if(rx_is_seek_scanning_state() != 0u)
        {
            rx_toggle_seek_channel();
        }
        rx_start_rx();
    }
    if(sta & RF_STATE_TX_FINISH)
    {
        if(g_ack_tx_active != 0u)
        {
            gStat.tx_ack_ok++;
            g_log_ack_ok++;
            g_ack_tx_active = 0u;
            if((g_ack_repeat_remaining != 0u) &&
               (rx_tmr_reached(TMR0_GetCurrentTimer(), g_ack_until_tmr) == 0u))
            {
                g_ack_repeat_remaining--;
                if(rx_start_ack_tx_loaded() != 0u)
                {
                    return;
                }
            }
            rx_after_ack_done(1u);
        }
    }
}

void RF_Service(void)
{
    if(g_basic_started == 0u)
    {
        return;
    }
    rx_service_timers(1u);
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
        return (events ^ SBP_RF_STAT_EVT);
    }

    if(events & SBP_RF_RF_RX_EVT)
    {
        rx_start_rx();
        return (events ^ SBP_RF_RF_RX_EVT);
    }

    return 0u;
}

void RF_StartPacketLossScan(void)
{
    g_rx_since_ack = 0u;
}

void RF_StartQualityScoreScan(void)
{
}

uint8_t RF_IsQualityScoreScanActive(void)
{
    return 0u;
}

uint8_t RF_HasPendingStatsLine(void)
{
    return g_diag_pending;
}

uint16_t RF_GetStatsLine(char *buf, uint16_t len)
{
    int n;
    uint32_t now_clock;
    uint32_t elapsed_ms;
    uint32_t expected;
    uint16_t loss;
    uint32_t ack_ok = (g_log_ack_ok > 99u) ? 99u : g_log_ack_ok;

    if((buf == NULL) || (len == 0u))
    {
        return 0u;
    }
    if(g_diag_pending != 0u)
    {
        g_diag_pending = 0u;
        n = snprintf(buf, len,
                     "RD A=%lu/%lu P=%lu/%lu/%lu B=%lu/%lu/%lu/%lu K=%lu/%lu U=%u/%u\r\n",
                     (unsigned long)g_diag_rx_arm_try,
                     (unsigned long)g_diag_rx_arm_fail,
                     (unsigned long)g_diag_rx_ok,
                     (unsigned long)g_diag_rx_crcerr,
                     (unsigned long)g_diag_rx_timeout,
                     (unsigned long)g_diag_rx_bad_len,
                     (unsigned long)g_diag_rx_bad_type,
                     (unsigned long)g_diag_rx_bad_connect,
                     (unsigned long)g_diag_rx_ignored_data,
                     (unsigned long)g_diag_tx_ack_try,
                     (unsigned long)g_diag_tx_ack_fail,
                     (unsigned int)g_diag_app_timeout_events,
                     (unsigned int)g_diag_data_resync_events);
        if(n <= 0)
        {
            return 0u;
        }
        if(n >= (int)len)
        {
            return (uint16_t)(len - 1u);
        }
        return (uint16_t)n;
    }

    now_clock = TMOS_GetSystemClock();
    elapsed_ms = rx_log_elapsed_ms(now_clock);
    expected = rx_expected_packets_for_elapsed_ms(elapsed_ms);
    loss = rx_calc_loss_permille(g_log_rx_ok, expected);

    if(rx_is_seek_scanning_state() != 0u)
    {
        loss = 1000u;
    }

    if(rx_is_seek_scanning_state() != 0u)
    {
        n = snprintf(buf, len,
                     "R5 S=%s C=%u/%u L=%03u P=%lu/%lu A=%lu U=%u E=%u\r\n",
                     rx_state_code(),
                     (unsigned int)g_scan_channel_a,
                     (unsigned int)g_scan_channel_b,
                     (unsigned int)loss,
                     g_log_rx_ok,
                     expected,
                     ack_ok,
                     (unsigned int)g_log_unconnected_events,
                     (unsigned int)g_log_errors);
    }
    else if((g_state == RX_HOP_RESERVED) || (g_state == RX_HOP_CONFIRM_ACK_PENDING))
    {
        n = snprintf(buf, len,
                     "R5 S=%s C=%u>%u L=%03u P=%lu/%lu A=%lu U=%u E=%u\r\n",
                     rx_state_code(),
                     (unsigned int)g_old_channel,
                     (unsigned int)g_target_channel,
                     (unsigned int)loss,
                     g_log_rx_ok,
                     expected,
                     ack_ok,
                     (unsigned int)g_log_unconnected_events,
                     (unsigned int)g_log_errors);
    }
    else
    {
        n = snprintf(buf, len,
                     "R5 S=%s C=%u L=%03u P=%lu/%lu A=%lu U=%u E=%u\r\n",
                     rx_state_code(),
                     (unsigned int)g_rx_channel,
                     (unsigned int)loss,
                     g_log_rx_ok,
                     expected,
                     ack_ok,
                     (unsigned int)g_log_unconnected_events,
                     (unsigned int)g_log_errors);
    }

    g_diag_rx_arm_try = gStat.rx_arm_try;
    g_diag_rx_arm_ok = gStat.rx_arm_ok;
    g_diag_rx_arm_fail = gStat.rx_arm_fail;
    g_diag_rx_ok = gStat.rx_ok;
    g_diag_rx_crcerr = gStat.rx_crcerr;
    g_diag_rx_timeout = gStat.rx_timeout;
    g_diag_rx_bad_len = gStat.rx_bad_len;
    g_diag_rx_bad_type = gStat.rx_bad_type;
    g_diag_rx_bad_connect = gStat.rx_bad_connect;
    g_diag_rx_ignored_data = gStat.rx_ignored_data;
    g_diag_tx_ack_try = gStat.tx_ack_try;
    g_diag_tx_ack_fail = gStat.tx_ack_fail;
    g_diag_app_timeout_events = g_log_app_timeout_events;
    g_diag_data_resync_events = g_log_data_resync_events;
    g_diag_pending = 1u;

    gStat.rx_ok = 0u;
    gStat.rx_bad_len = 0u;
    gStat.rx_bad_type = 0u;
    gStat.rx_bad_connect = 0u;
    gStat.rx_ignored_data = 0u;
    gStat.rx_crcerr = 0u;
    gStat.rx_timeout = 0u;
    gStat.rx_arm_try = 0u;
    gStat.rx_arm_ok = 0u;
    gStat.rx_arm_fail = 0u;
    gStat.tx_ack_try = 0u;
    gStat.tx_ack_start_fail = 0u;
    gStat.tx_ack_parm_fail = 0u;
    gStat.tx_ack_ok = 0u;
    gStat.tx_ack_fail = 0u;
    gStat.connect_rx = 0u;
    gStat.data_rx = 0u;
    gStat.scan_switch = 0u;
    gStat.hop_cmd_rx = 0u;
    gStat.hop_done = 0u;
    g_log_rx_ok = 0u;
    g_log_ack_ok = 0u;
    g_log_hop_events = 0u;
    g_log_unconnected_events = 0u;
    g_log_errors = 0u;
    g_log_app_timeout_events = 0u;
    g_log_data_resync_events = 0u;
    g_log_last_clock = now_clock;

    if(n <= 0)
    {
        return 0u;
    }
    if(n >= (int)len)
    {
        return (uint16_t)(len - 1u);
    }
    return (uint16_t)n;
}

void RF_Init(void)
{
    g_ret_role_init = RF_RoleInit();
    taskID = TMOS_ProcessEventRegister(RF_ProcessEvent);
    TMR0_TimerInit(TMR0_FREE_RUN_END);

    PFIC_EnableIRQ(BLEB_IRQn);
    PFIC_EnableIRQ(BLEL_IRQn);

    (void)g_ret_role_init;
    memset(TxBuf, 0, sizeof(TxBuf));
    memset(RxBuf, 0, sizeof(RxBuf));
    rx_apply_rate(rfh_rate_code_from_hz(RF_REPORT_PPS), RFH_DEFAULT_ACK_WINDOW_MS);
    g_rx_channel = RFH_MIN_CHANNEL;
    g_scan_channel_a = RFH_MIN_CHANNEL;
    g_scan_channel_b = RFH_MAX_CHANNEL;
    g_ack_channel = RFH_MIN_CHANNEL;
    g_ack_tx_channel = RFH_MIN_CHANNEL;
    g_old_channel = RFH_MIN_CHANNEL;
    g_target_channel = RFH_MIN_CHANNEL;
    g_ack_tx_active = 0u;
    g_ack_pending = 0u;
    g_ack_repeat_remaining = 0u;
    g_ack_due_tmr = 0u;
    g_ack_until_tmr = 0u;
    g_rx_since_ack = 0u;
    g_pending_ack_cmd = RFH_CMD_NONE;
    g_rx_progress_count = 0u;
    g_rx_progress_seen_count = 0u;
    g_state = RX_COMM;
    rx_enter_state(RX_UNCONNECTED);
    g_last_rx_packet_clock = TMOS_GetSystemClock();
    g_rx_progress_clock = g_last_rx_packet_clock;
    g_log_last_clock = g_last_rx_packet_clock;
    rx_basic_start();
}

/******************************** endfile @ RF_PHY ******************************/
