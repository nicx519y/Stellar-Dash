/********************************** (C) COPYRIGHT *******************************
 * File Name          : RF_PHY.c
 * Description        : TX side for the fixed-channel 8K 7+1 ACK protocol.
 *******************************************************************************/

#include "CONFIG.h"
#include "RF_PHY.h"
#include "HAL.h"
#include "wchrf.h"
#include "rfm_config.h"
#include "rfm_input_stream.h"
#include "rfm_spi_bridge.h"
#include "rf_hop_protocol.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef RF_SERIAL_LOG
#define RF_SERIAL_LOG                  0
#endif

#ifndef RF_AUTO_ACK_DEMO_ENABLE
#define RF_AUTO_ACK_DEMO_ENABLE        1u
#endif

#if (RF_AUTO_ACK_DEMO_ENABLE == 1u)

#define RF_AUTO_DEMO_PACKET_LEN        RFH_AIR_PACKET_LEN
#define RF_AUTO_DEMO_DMA_LEN           (RF_AUTO_DEMO_PACKET_LEN + 2u)
#define RF_AUTO_DEMO_DATA_TYPE         0xFFu
#define RF_AUTO_DEMO_ACK_TYPE          0xFFu
#define RF_AUTO_DEMO_CHANNEL           39u
#define RF_AUTO_DEMO_FREQUENCY_KHZ     2480000UL
#define RF_AUTO_DEMO_PHY_PROPS         LLE_MODE_PHY_2M
#define RF_AUTO_DEMO_WAIT_ACK_ENABLE   0u
#define RF_AUTO_DEMO_ACK_BIT           (RF_AUTO_DEMO_WAIT_ACK_ENABLE ? PROP_WAIT_ACK : 0u)
#define RF_AUTO_DEMO_REPORT_HZ         8000u
#define RF_AUTO_DEMO_RATE_CODE         RFH_RATE_8K
#define RF_AUTO_DEMO_LOG_PERIOD_MS     5000u
#define RF_AUTO_DEMO_PENDING_MAX       4u
#define RF_AUTO_DEMO_TX_STUCK_MS       10u
#define RF_AUTO_DEMO_TX_IN_ISR         1u
#define RF_AUTO_DEMO_ACK_INTERVAL_TICKS (RF_AUTO_DEMO_REPORT_HZ / 2u)
#define RF_AUTO_DEMO_ACK_REQUEST_BURST 3u
#define RF_AUTO_DEMO_ACK_RX_TIMEOUT_US 1200u
#define RF_AUTO_DEMO_ACK_RX_TIMEOUT_UNITS (RF_AUTO_DEMO_ACK_RX_TIMEOUT_US * 2u)
#define RF_AUTO_DEMO_ACK_TOKEN_OFFSET  10u
#define RF_AUTO_DEMO_ACK_REMAIN_OFFSET 11u
#define RF_AUTO_DEMO_INITIAL_CHANNEL   39u
#define RF_AUTO_DEMO_HOP_SCORE_THRESHOLD 180u
#define RF_AUTO_DEMO_HOP_ACK_MISS_THRESHOLD 2u
#define RF_AUTO_DEMO_HOP_COOLDOWN_MS   10000u
#define RF_AUTO_DEMO_HOP_PREPARE_TIMEOUT_MS 1000u
#define RF_AUTO_DEMO_HOP_CONFIRM_TIMEOUT_MS 1000u
#define RF_AUTO_DEMO_HOP_RECOVERY_TIMEOUT_MS 3000u
#define RF_AUTO_DEMO_HOP_RECOVERY_DWELL_MS 500u
#define RF_LINK_CRC_INIT               0x555555UL

#if (RF_SERIAL_LOG == 1)
#define RF_AUTO_LOG(...)               PRINT(__VA_ARGS__)
#else
#define RF_AUTO_LOG(...)               ((void)0)
#endif

typedef struct
{
    volatile uint32_t tx_start;
    volatile uint32_t tx_busy;
    volatile uint32_t tx_fail;
    volatile uint32_t tx_finish;
    volatile uint32_t ack_ok;
    volatile uint32_t ack_timeout;
    volatile uint32_t ack_crc_err;
    volatile uint32_t ack_type_err;
    volatile uint32_t ack_req;
    volatile uint32_t report_due;
    volatile uint32_t report_drop;
    volatile uint32_t tx_stuck;
    volatile uint32_t hop_event;
} rf_auto_demo_stat_t;

typedef enum
{
    RF_AUTO_HOP_COMM = 0u,
    RF_AUTO_HOP_PREPARE_ACK_WAIT,
    RF_AUTO_HOP_CONFIRM_ACK_WAIT,
    RF_AUTO_HOP_RECOVERY_DUAL
} rf_auto_hop_state_t;

uint8_t taskID;

static rfRoleParam_t gParm;
static rfipTx_t gTxParam;
static rfipRx_t gRxParam;
__attribute__((__aligned__(4))) static uint8_t TxBuf[RF_AUTO_DEMO_DMA_LEN];
__attribute__((__aligned__(4))) static uint8_t RxBuf[264];

static rf_auto_demo_stat_t g_demo_stat;
static volatile uint8_t g_demo_config_ret = 0xFFu;
static volatile uint8_t g_demo_tx_start_ret = 0xFFu;
static volatile uint8_t g_demo_tx_parm_ret = 0xFFu;
static volatile uint8_t g_demo_rx_ret = 0xFFu;
static volatile uint8_t g_demo_tx_busy = 0u;
static volatile uint8_t g_demo_ack_rx_active = 0u;
static volatile uint8_t g_demo_wait_ack_after_tx = 0u;
static volatile uint8_t g_demo_pause_tx = 0u;
static volatile uint8_t g_demo_force_ack_burst = 0u;
static volatile uint8_t g_pending_event_state_code = 0u;
#if (RF_AUTO_DEMO_TX_IN_ISR == 0u)
static volatile uint32_t g_demo_pending_reports = 0u;
#endif
static uint8_t g_demo_seq = 0u;
static uint16_t g_demo_ack_tick = 0u;
static uint8_t g_demo_ack_burst_left = 0u;
static uint8_t g_demo_ack_token = 0u;
static uint8_t g_demo_active_ack_token = 0u;
static volatile uint8_t g_demo_current_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
static uint8_t g_demo_old_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
static uint8_t g_demo_target_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
static uint8_t g_demo_hop_seq = 0u;
static rf_auto_hop_state_t g_demo_hop_state = RF_AUTO_HOP_COMM;
static uint16_t g_demo_last_quality = 0u;
static uint8_t g_demo_ack_miss_count = 0u;
static uint32_t g_demo_hop_deadline_clock = 0u;
static uint32_t g_demo_hop_cooldown_until = 0u;
static uint32_t g_demo_recovery_switch_clock = 0u;
static uint8_t g_demo_recovery_side = 0u;
#if (RF_AUTO_DEMO_TX_IN_ISR == 0u)
static uint32_t g_demo_tx_start_clock = 0u;
#endif
static uint32_t g_demo_last_log_clock = 0u;

static const uint8_t g_demo_hop_channels[] = { 39u, 16u, 24u, 32u };

static uint16_t tx_saturate_u16(uint32_t value)
{
    return (value > 0xFFFFu) ? 0xFFFFu : (uint16_t)value;
}

static char demo_hop_state_char(void)
{
    switch(g_demo_hop_state)
    {
    case RF_AUTO_HOP_PREPARE_ACK_WAIT:
        return 'P';
    case RF_AUTO_HOP_CONFIRM_ACK_WAIT:
        return 'C';
    case RF_AUTO_HOP_RECOVERY_DUAL:
        return 'R';
    case RF_AUTO_HOP_COMM:
    default:
        return 'M';
    }
}

static uint8_t demo_next_channel(uint8_t current)
{
    uint8_t i;

    for(i = 0u; i < (uint8_t)(sizeof(g_demo_hop_channels) / sizeof(g_demo_hop_channels[0])); i++)
    {
        if(g_demo_hop_channels[i] == current)
        {
            i++;
            if(i >= (uint8_t)(sizeof(g_demo_hop_channels) / sizeof(g_demo_hop_channels[0])))
            {
                i = 0u;
            }
            return g_demo_hop_channels[i];
        }
    }
    return g_demo_hop_channels[0];
}

static void demo_apply_channel(uint8_t channel)
{
    g_demo_pause_tx = 1u;
    (void)RFRole_Stop();

    gParm.frequency = channel;
    RFRole_SetParam(&gParm);

    gTxParam.frequency = channel;
    gTxParam.whiteChannel = channel;
    gRxParam.frequency = channel;
    gRxParam.whiteChannel = channel;

    g_demo_current_channel = channel;
    g_demo_ack_rx_active = 0u;
    g_demo_wait_ack_after_tx = 0u;
    g_demo_pause_tx = 0u;
}

static uint8_t demo_active_hop_cmd(void)
{
    if(g_demo_hop_state == RF_AUTO_HOP_PREPARE_ACK_WAIT)
    {
        return RFH_CMD_HOP_PREPARE;
    }
    if(g_demo_hop_state == RF_AUTO_HOP_CONFIRM_ACK_WAIT)
    {
        return RFH_CMD_HOP_CONFIRM;
    }
    if(g_demo_hop_state == RF_AUTO_HOP_RECOVERY_DUAL)
    {
        return (g_demo_current_channel == g_demo_old_channel) ?
               RFH_CMD_HOP_PREPARE : RFH_CMD_HOP_CONFIRM;
    }
    return RFH_CMD_NONE;
}

static void demo_start_hop_prepare(uint32_t now)
{
    if(g_demo_hop_state != RF_AUTO_HOP_COMM)
    {
        return;
    }
    if((int32_t)(now - g_demo_hop_cooldown_until) < 0)
    {
        return;
    }

    g_demo_old_channel = g_demo_current_channel;
    g_demo_target_channel = demo_next_channel(g_demo_current_channel);
    g_demo_hop_seq++;
    if(g_demo_hop_seq == 0u)
    {
        g_demo_hop_seq = 1u;
    }
    g_demo_hop_state = RF_AUTO_HOP_PREPARE_ACK_WAIT;
    g_demo_hop_deadline_clock = now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_PREPARE_TIMEOUT_MS);
    g_demo_force_ack_burst = 1u;
    g_demo_stat.hop_event++;
}

static void demo_finish_hop(uint32_t now)
{
    g_demo_hop_state = RF_AUTO_HOP_COMM;
    g_demo_old_channel = g_demo_current_channel;
    g_demo_target_channel = g_demo_current_channel;
    g_demo_ack_miss_count = 0u;
    g_demo_force_ack_burst = 0u;
    g_demo_hop_cooldown_until = now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_COOLDOWN_MS);
    g_demo_stat.hop_event++;
}

static void demo_enter_recovery(uint32_t now)
{
    g_demo_hop_state = RF_AUTO_HOP_RECOVERY_DUAL;
    g_demo_hop_deadline_clock = now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_RECOVERY_TIMEOUT_MS);
    g_demo_recovery_switch_clock = now;
    g_demo_recovery_side = 0u;
    demo_apply_channel(g_demo_old_channel);
    g_demo_force_ack_burst = 1u;
    g_demo_stat.hop_event++;
}

static void demo_handle_command_ack(uint8_t cmd, uint8_t seq, uint8_t channel)
{
    uint32_t now = TMOS_GetSystemClock();

    if(seq != g_demo_hop_seq)
    {
        return;
    }

    if((g_demo_hop_state == RF_AUTO_HOP_PREPARE_ACK_WAIT) &&
       (cmd == RFH_CMD_HOP_PREPARE))
    {
        demo_apply_channel(g_demo_target_channel);
        g_demo_hop_state = RF_AUTO_HOP_CONFIRM_ACK_WAIT;
        g_demo_hop_deadline_clock = now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_CONFIRM_TIMEOUT_MS);
        g_demo_force_ack_burst = 1u;
        g_demo_stat.hop_event++;
        return;
    }

    if((g_demo_hop_state == RF_AUTO_HOP_CONFIRM_ACK_WAIT) &&
       (cmd == RFH_CMD_HOP_CONFIRM) &&
       (channel == g_demo_target_channel))
    {
        demo_finish_hop(now);
        return;
    }

    if(g_demo_hop_state == RF_AUTO_HOP_RECOVERY_DUAL)
    {
        if(cmd == RFH_CMD_HOP_PREPARE)
        {
            demo_apply_channel(g_demo_target_channel);
            g_demo_hop_state = RF_AUTO_HOP_CONFIRM_ACK_WAIT;
            g_demo_hop_deadline_clock = now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_CONFIRM_TIMEOUT_MS);
            g_demo_force_ack_burst = 1u;
            g_demo_stat.hop_event++;
        }
        else if((cmd == RFH_CMD_HOP_CONFIRM) &&
                (channel == g_demo_target_channel))
        {
            demo_finish_hop(now);
        }
    }
}

static void demo_handle_ack_packet(void)
{
    const uint8_t *air = &RxBuf[2];
    const uint8_t *data = &air[RFH_DATA_OFFSET];
    uint8_t cmd;
    uint8_t seq;
    uint8_t channel;

    if((RxBuf[1] != RF_AUTO_DEMO_PACKET_LEN) ||
       (rfh_packet_type(air[RFH_HDR0_OFFSET]) != RFH_PKT_ACK))
    {
        g_demo_stat.ack_type_err++;
        if(g_demo_hop_state != RF_AUTO_HOP_COMM)
        {
            g_demo_force_ack_burst = 1u;
        }
        return;
    }

    g_demo_last_quality = rfh_get_u16(&data[RFH_ACK_LOSS_PERMILLE_LO]);
    cmd = data[RFH_ACK_CMD_ID];
    channel = data[RFH_ACK_CHANNEL];
    seq = data[RFH_ACK_STATUS];

    g_demo_ack_miss_count = 0u;
    g_demo_stat.ack_ok++;

    if((cmd == RFH_CMD_HOP_PREPARE) || (cmd == RFH_CMD_HOP_CONFIRM))
    {
        demo_handle_command_ack(cmd, seq, channel);
        if(g_demo_hop_state != RF_AUTO_HOP_COMM)
        {
            g_demo_force_ack_burst = 1u;
        }
    }
    else if((g_demo_hop_state == RF_AUTO_HOP_COMM) &&
            (g_demo_last_quality >= RF_AUTO_DEMO_HOP_SCORE_THRESHOLD))
    {
        demo_start_hop_prepare(TMOS_GetSystemClock());
    }
    else if(g_demo_hop_state != RF_AUTO_HOP_COMM)
    {
        g_demo_force_ack_burst = 1u;
    }
}

static void demo_fill_tx_packet(uint8_t request_ack, uint8_t ack_token, uint8_t ack_burst_left)
{
    uint8_t i;
    uint8_t *air = &TxBuf[2];
    uint8_t *data = &air[RFH_DATA_OFFSET];
    uint8_t flags = RFH_FLAG_LINK_OK;
    uint8_t hop_cmd = demo_active_hop_cmd();

    if(request_ack != 0u)
    {
        flags |= RFH_FLAG_CMD_ACK;
    }
    if(hop_cmd != RFH_CMD_NONE)
    {
        flags |= RFH_FLAG_CMD_PRESENT;
    }

    memset(TxBuf, 0, sizeof(TxBuf));
    TxBuf[0] = RFH_WCH_PREAMBLE;
    TxBuf[1] = RF_AUTO_DEMO_PACKET_LEN;
    air[0] = rfh_make_header0(RFH_PKT_DATA, RF_AUTO_DEMO_RATE_CODE, flags);
    air[1] = g_demo_seq;
    if(hop_cmd != RFH_CMD_NONE)
    {
        data[RFH_HOP_CMD_ID] = hop_cmd;
        data[RFH_HOP_CMD_CHANNEL] = g_demo_target_channel;
        data[RFH_HOP_CMD_DELAY_LO_MS] = 0u;
        data[RFH_HOP_CMD_DELAY_HI_MS] = 0u;
        data[RFH_HOP_CMD_SEQ] = g_demo_hop_seq;
        data[RFH_HOP_CONFIRM_OLD_CHANNEL] = g_demo_old_channel;
    }
    else
    {
        data[0] = (uint8_t)(g_demo_stat.tx_start & 0xFFu);
        data[1] = (uint8_t)((g_demo_stat.tx_start >> 8) & 0xFFu);
    }
    for(i = 8u; i < RF_AUTO_DEMO_ACK_TOKEN_OFFSET; i++)
    {
        air[i] = (uint8_t)(0xA0u + i);
    }
    if(request_ack != 0u)
    {
        air[RF_AUTO_DEMO_ACK_TOKEN_OFFSET] = ack_token;
        air[RF_AUTO_DEMO_ACK_REMAIN_OFFSET] = ack_burst_left;
    }
}

static void demo_log_stats(uint32_t now)
{
    if((uint32_t)(now - g_demo_last_log_clock) < MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_LOG_PERIOD_MS))
    {
        return;
    }
    g_demo_last_log_clock = now;

    RF_AUTO_LOG("T8 c%u S%c h%u>%u q%u p%02X hz%u due%lu tx%lu fin%lu aq%lu ack%lu to%lu fail%lu e%lu/%lu dr%lu st%lu H%lu b%u r%u/%u/%u s%u\r\n",
                (unsigned int)g_demo_config_ret,
                demo_hop_state_char(),
                (unsigned int)g_demo_current_channel,
                (unsigned int)g_demo_target_channel,
                (unsigned int)g_demo_last_quality,
                (unsigned int)(RF_AUTO_DEMO_PHY_PROPS | RF_AUTO_DEMO_ACK_BIT),
                (unsigned int)RF_AUTO_DEMO_REPORT_HZ,
                (unsigned long)g_demo_stat.report_due,
                (unsigned long)g_demo_stat.tx_start,
                (unsigned long)g_demo_stat.tx_finish,
                (unsigned long)g_demo_stat.ack_req,
                (unsigned long)g_demo_stat.ack_ok,
                (unsigned long)g_demo_stat.ack_timeout,
                (unsigned long)g_demo_stat.tx_fail,
                (unsigned long)g_demo_stat.ack_crc_err,
                (unsigned long)g_demo_stat.ack_type_err,
                (unsigned long)g_demo_stat.report_drop,
                (unsigned long)g_demo_stat.tx_stuck,
                (unsigned long)g_demo_stat.hop_event,
                (unsigned int)g_demo_tx_busy,
                (unsigned int)g_demo_tx_start_ret,
                (unsigned int)g_demo_tx_parm_ret,
                (unsigned int)g_demo_rx_ret,
                (unsigned int)g_demo_seq);

    g_demo_stat.tx_start = 0u;
    g_demo_stat.tx_fail = 0u;
    g_demo_stat.tx_finish = 0u;
    g_demo_stat.ack_ok = 0u;
    g_demo_stat.ack_timeout = 0u;
    g_demo_stat.ack_crc_err = 0u;
    g_demo_stat.ack_type_err = 0u;
    g_demo_stat.ack_req = 0u;
    g_demo_stat.report_due = 0u;
    g_demo_stat.report_drop = 0u;
    g_demo_stat.tx_stuck = 0u;
    g_demo_stat.hop_event = 0u;
}

#if (RF_AUTO_DEMO_TX_IN_ISR == 0u)
static void demo_check_tx_stuck(uint32_t now)
{
    if(g_demo_tx_busy == 0u)
    {
        return;
    }
    if((uint32_t)(now - g_demo_tx_start_clock) < MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_TX_STUCK_MS))
    {
        return;
    }

    (void)RFRole_Stop();
    g_demo_tx_busy = 0u;
    g_demo_stat.tx_stuck++;
}
#endif

#if (RF_AUTO_DEMO_TX_IN_ISR == 0u)
static void demo_try_send(void)
{
    bStatus_t ret;

    if(g_demo_config_ret != SUCCESS)
    {
        return;
    }
    if(g_demo_tx_busy != 0u)
    {
        return;
    }
    if(g_demo_pending_reports == 0u)
    {
        return;
    }
    g_demo_pending_reports--;

    demo_fill_tx_packet(0u, 0u, 0u);
    g_demo_tx_busy = 1u;
    g_demo_tx_start_clock = TMOS_GetSystemClock();
    g_demo_stat.tx_start++;
    gTxParam.txDMA = (uint32_t)TxBuf;
    g_demo_tx_start_ret = (uint8_t)RFIP_SetTxStart();
    if(g_demo_tx_start_ret != SUCCESS)
    {
        g_demo_tx_busy = 0u;
        g_demo_stat.tx_fail++;
        return;
    }
    ret = RFIP_SetTxParm(&gTxParam);
    g_demo_tx_parm_ret = (uint8_t)ret;
    if(ret != SUCCESS)
    {
        g_demo_tx_busy = 0u;
        g_demo_stat.tx_fail++;
    }
    g_demo_seq++;
}
#endif

static void demo_arm_ack_rx(void)
{
    if(g_demo_config_ret != SUCCESS)
    {
        return;
    }

    gRxParam.rxDMA = (uint32_t)RxBuf;
    gRxParam.timeOut = RF_AUTO_DEMO_ACK_RX_TIMEOUT_UNITS;
    g_demo_rx_ret = (uint8_t)RFIP_SetRx(&gRxParam);
    if(g_demo_rx_ret == SUCCESS)
    {
        g_demo_ack_rx_active = 1u;
    }
    else
    {
        g_demo_stat.ack_timeout++;
    }
}

static void demo_start_ack_burst(void)
{
    g_demo_ack_token++;
    if(g_demo_ack_token == 0u)
    {
        g_demo_ack_token = 1u;
    }
    g_demo_active_ack_token = g_demo_ack_token;
    g_demo_ack_burst_left = RF_AUTO_DEMO_ACK_REQUEST_BURST;
    g_demo_force_ack_burst = 0u;
}

static void demo_note_ack_timeout(void)
{
    g_demo_ack_miss_count++;
    if(g_demo_hop_state == RF_AUTO_HOP_COMM)
    {
        if(g_demo_ack_miss_count >= RF_AUTO_DEMO_HOP_ACK_MISS_THRESHOLD)
        {
            demo_start_hop_prepare(TMOS_GetSystemClock());
        }
        return;
    }

    g_demo_force_ack_burst = 1u;
}

static void demo_service_hop(uint32_t now)
{
    if((g_demo_hop_state == RF_AUTO_HOP_PREPARE_ACK_WAIT) &&
       ((int32_t)(now - g_demo_hop_deadline_clock) >= 0))
    {
        g_demo_hop_state = RF_AUTO_HOP_COMM;
        g_demo_force_ack_burst = 0u;
        g_demo_hop_cooldown_until = now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_COOLDOWN_MS / 2u);
        g_demo_stat.hop_event++;
        return;
    }

    if((g_demo_hop_state == RF_AUTO_HOP_CONFIRM_ACK_WAIT) &&
       ((int32_t)(now - g_demo_hop_deadline_clock) >= 0))
    {
        demo_enter_recovery(now);
        return;
    }

    if(g_demo_hop_state == RF_AUTO_HOP_RECOVERY_DUAL)
    {
        if((int32_t)(now - g_demo_hop_deadline_clock) >= 0)
        {
            demo_apply_channel(g_demo_old_channel);
            g_demo_hop_state = RF_AUTO_HOP_COMM;
            g_demo_force_ack_burst = 0u;
            g_demo_hop_cooldown_until = now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_COOLDOWN_MS / 2u);
            g_demo_stat.hop_event++;
            return;
        }
        if((uint32_t)(now - g_demo_recovery_switch_clock) >=
           MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_RECOVERY_DWELL_MS))
        {
            g_demo_recovery_switch_clock = now;
            g_demo_recovery_side ^= 1u;
            demo_apply_channel((g_demo_recovery_side == 0u) ?
                               g_demo_old_channel : g_demo_target_channel);
            g_demo_force_ack_burst = 1u;
        }
    }
}

__INTERRUPT
__HIGH_CODE
void TMR0_IRQHandler(void)
{
    if(TMR0_GetITFlag(TMR0_3_IT_CYC_END) == 0u)
    {
        return;
    }

    TMR0_ClearITFlag(TMR0_3_IT_CYC_END);
    g_demo_stat.report_due++;
#if (RF_AUTO_DEMO_TX_IN_ISR != 0u)
    if(g_demo_config_ret == SUCCESS)
    {
        bStatus_t ret;
        uint8_t request_ack = 0u;
        uint8_t ack_token = 0u;
        uint8_t ack_burst_left = 0u;

        if((g_demo_pause_tx != 0u) ||
           (g_demo_ack_rx_active != 0u) ||
           (g_demo_wait_ack_after_tx != 0u))
        {
            g_demo_stat.report_drop++;
            return;
        }

        if(g_demo_ack_burst_left != 0u)
        {
            request_ack = 1u;
            ack_token = g_demo_active_ack_token;
            g_demo_ack_burst_left--;
            ack_burst_left = g_demo_ack_burst_left;
            if(g_demo_ack_burst_left == 0u)
            {
                g_demo_wait_ack_after_tx = 1u;
            }
        }
        else
        {
            if(g_demo_force_ack_burst != 0u)
            {
                demo_start_ack_burst();

                request_ack = 1u;
                ack_token = g_demo_active_ack_token;
                g_demo_ack_burst_left--;
                ack_burst_left = g_demo_ack_burst_left;
                if(g_demo_ack_burst_left == 0u)
                {
                    g_demo_wait_ack_after_tx = 1u;
                }
                g_demo_stat.ack_req++;
            }
            else
            {
                g_demo_ack_tick++;
                if(g_demo_ack_tick >= RF_AUTO_DEMO_ACK_INTERVAL_TICKS)
                {
                    g_demo_ack_tick = 0u;
                    demo_start_ack_burst();

                    request_ack = 1u;
                    ack_token = g_demo_active_ack_token;
                    g_demo_ack_burst_left--;
                    ack_burst_left = g_demo_ack_burst_left;
                    if(g_demo_ack_burst_left == 0u)
                    {
                        g_demo_wait_ack_after_tx = 1u;
                    }
                    g_demo_stat.ack_req++;
                }
            }
        }

        demo_fill_tx_packet(request_ack, ack_token, ack_burst_left);
        g_demo_stat.tx_start++;
        gTxParam.txDMA = (uint32_t)TxBuf;
        g_demo_tx_start_ret = (uint8_t)RFIP_SetTxStart();
        ret = RFIP_SetTxParm(&gTxParam);
        g_demo_tx_parm_ret = (uint8_t)ret;
        if((g_demo_tx_start_ret != SUCCESS) || (ret != SUCCESS))
        {
            g_demo_stat.tx_fail++;
        }
        g_demo_seq++;
    }
#else
    if(g_demo_pending_reports < RF_AUTO_DEMO_PENDING_MAX)
    {
        g_demo_pending_reports++;
    }
    else
    {
        g_demo_stat.report_drop++;
    }
#endif
}

__HIGH_CODE
void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id)
{
    (void)id;

    if(sta & RF_STATE_TX_FINISH)
    {
        g_demo_tx_busy = 0u;
        g_demo_stat.tx_finish++;
        if(g_demo_wait_ack_after_tx != 0u)
        {
            g_demo_wait_ack_after_tx = 0u;
            demo_arm_ack_rx();
        }
    }
    if(sta & RF_STATE_RX)
    {
        g_demo_tx_busy = 0u;
        g_demo_ack_rx_active = 0u;
        demo_handle_ack_packet();
    }
    if(sta & RF_STATE_RX_CRCERR)
    {
        g_demo_tx_busy = 0u;
        g_demo_ack_rx_active = 0u;
        g_demo_stat.ack_crc_err++;
        demo_note_ack_timeout();
    }
    if(sta & RF_STATE_TIMEOUT)
    {
        g_demo_tx_busy = 0u;
        g_demo_ack_rx_active = 0u;
        g_demo_stat.ack_timeout++;
        demo_note_ack_timeout();
    }
}

void RF_TxMainLoopProcess(void)
{
    uint32_t now = TMOS_GetSystemClock();

#if (RF_AUTO_DEMO_TX_IN_ISR == 0u)
    demo_check_tx_stuck(now);
    demo_try_send();
#endif
    demo_service_hop(now);
    demo_log_stats(now);
}

bool RF_SPI_FastWriteInput(const uint8_t *payload, uint8_t len)
{
    (void)payload;
    (void)len;
    return true;
}

bool RF_SetReportRateHz(uint16_t hz)
{
    return (hz == RF_AUTO_DEMO_REPORT_HZ);
}

uint16_t RF_GetReportRateHz(void)
{
    return RF_AUTO_DEMO_REPORT_HZ;
}

bool RF_StartPairing(void)
{
    g_pending_event_state_code = RF_LINK_STATE_CONNECTED;
    return true;
}

bool RF_StopPairing(void)
{
    g_pending_event_state_code = RF_LINK_STATE_CONNECTED;
    return true;
}

bool RF_Unbind(void)
{
    g_pending_event_state_code = RF_LINK_STATE_CONNECTED;
    return true;
}

uint8_t RF_GetLinkStateCode(void)
{
    return (g_demo_config_ret == SUCCESS) ? RF_LINK_STATE_CONNECTED : RF_LINK_STATE_IDLE;
}

uint8_t RF_ConsumePendingEventStateCode(void)
{
    uint8_t state = g_pending_event_state_code;
    g_pending_event_state_code = 0u;
    return state;
}

uint8_t RF_PeekPendingEventStateCode(void)
{
    return g_pending_event_state_code;
}

void RF_ClearPendingEventStateCode(uint8_t state_code)
{
    if((state_code == 0u) || (g_pending_event_state_code == state_code))
    {
        g_pending_event_state_code = 0u;
    }
}

uint8_t RF_IsConnected(void)
{
    return (g_demo_config_ret == SUCCESS) ? 1u : 0u;
}

uint8_t RF_HasBond(void)
{
    return 0u;
}

uint16_t RF_GetRxOkCount(void)
{
    return tx_saturate_u16(g_demo_stat.ack_ok);
}

uint16_t RF_GetRxFailCount(void)
{
    return tx_saturate_u16(g_demo_stat.ack_timeout + g_demo_stat.ack_crc_err + g_demo_stat.ack_type_err);
}

uint16_t RF_GetTxFailCount(void)
{
    return tx_saturate_u16(g_demo_stat.tx_fail);
}

uint32_t RF_GetRejectCount(void)
{
    return g_demo_stat.ack_type_err;
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
    return 0u;
}

void RF_Init(void)
{
    rfRoleConfig_t conf;
    uint32_t tick_per_evt;

    taskID = TMOS_ProcessEventRegister(RF_ProcessEvent);
    PFIC_EnableIRQ(BLEB_IRQn);
    PFIC_EnableIRQ(BLEL_IRQn);

    memset(&conf, 0, sizeof(conf));
    conf.TxPower = BLE_TX_POWER;
    conf.rfProcessCB = RF_ProcessCallBack;
    conf.processMask = RF_STATE_RX | RF_STATE_RX_CRCERR |
                       RF_STATE_TX_FINISH | RF_STATE_TIMEOUT | RF_STATE_TX_IDLE;
    g_demo_config_ret = (uint8_t)RFRole_BasicInit(&conf);

    memset(&gParm, 0, sizeof(gParm));
    gParm.accessAddress = RFH_LINK_ACCESS_ADDRESS_DEFAULT;
    gParm.crcInit = RF_LINK_CRC_INIT;
    gParm.frequency = RF_AUTO_DEMO_INITIAL_CHANNEL;
    gParm.properties = RF_AUTO_DEMO_PHY_PROPS | RF_AUTO_DEMO_ACK_BIT;
    gParm.rxMaxLen = RF_AUTO_DEMO_PACKET_LEN;
    gParm.sendTime = RFH_TX_SEND_TIME_UNITS;
    RFRole_SetParam(&gParm);

    memset(&gTxParam, 0, sizeof(gTxParam));
    gTxParam.accessAddress = gParm.accessAddress;
    gTxParam.crcInit = gParm.crcInit;
    gTxParam.frequency = RF_AUTO_DEMO_INITIAL_CHANNEL;
    gTxParam.properties = gParm.properties;
    gTxParam.whiteChannel = RF_AUTO_DEMO_INITIAL_CHANNEL;
    gTxParam.sendTime = (uint8_t)gParm.sendTime;
    gTxParam.sendCount = 1u;
    gTxParam.txDMA = (uint32_t)TxBuf;

    memset(&gRxParam, 0, sizeof(gRxParam));
    gRxParam.accessAddress = gParm.accessAddress;
    gRxParam.crcInit = gParm.crcInit;
    gRxParam.frequency = RF_AUTO_DEMO_INITIAL_CHANNEL;
    gRxParam.properties = RF_AUTO_DEMO_PHY_PROPS;
    gRxParam.rxDMA = (uint32_t)RxBuf;
    gRxParam.whiteChannel = RF_AUTO_DEMO_INITIAL_CHANNEL;
    gRxParam.rxMaxLen = RF_AUTO_DEMO_PACKET_LEN;
    gRxParam.timeOut = RF_AUTO_DEMO_ACK_RX_TIMEOUT_UNITS;

    g_pending_event_state_code = RF_LINK_STATE_CONNECTED;
#if (RF_AUTO_DEMO_TX_IN_ISR == 0u)
    g_demo_pending_reports = 0u;
#endif
    g_demo_last_log_clock = TMOS_GetSystemClock();

    tick_per_evt = GetSysClock() / RF_AUTO_DEMO_REPORT_HZ;
    if(tick_per_evt == 0u)
    {
        tick_per_evt = 1u;
    }
    TMR0_TimerInit(tick_per_evt);
    TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    PFIC_SetPriority(TMR0_IRQn, 0x80);
    PFIC_EnableIRQ(TMR0_IRQn);

    g_demo_current_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
    g_demo_old_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
    g_demo_target_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
    g_demo_hop_cooldown_until = TMOS_GetSystemClock() +
                                MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_COOLDOWN_MS);

    RF_AUTO_LOG("T8 init cfg=%u ch=%u hz=%u prop=%02X ackMs=%u len=%u hopThr=%u\r\n",
                (unsigned int)g_demo_config_ret,
                (unsigned int)RF_AUTO_DEMO_INITIAL_CHANNEL,
                (unsigned int)RF_AUTO_DEMO_REPORT_HZ,
                (unsigned int)gParm.properties,
                (unsigned int)(RF_AUTO_DEMO_ACK_INTERVAL_TICKS * 1000u / RF_AUTO_DEMO_REPORT_HZ),
                (unsigned int)RF_AUTO_DEMO_PACKET_LEN,
                (unsigned int)RF_AUTO_DEMO_HOP_SCORE_THRESHOLD);
}

#else

#define RF_STAT_PRINT_PERIOD_MS        5000u
#define RF_TX_SEND_TIME                RFH_TX_SEND_TIME_UNITS
#define RF_LINK_ACCESS_ADDRESS         RFH_LINK_ACCESS_ADDRESS_DEFAULT
#define RF_LINK_CRC_INIT               0x555555UL
#define RFIP_RX_NO_TIMEOUT             0u
#define RF_ACK_RX_TIMEOUT_US           RFIP_RX_NO_TIMEOUT
#define RF_ACK_RX_START_SLOT           3u
#define RF_DATA_TO_ACK_RX_DELAY_US     80u
#define RF_SWITCH_TEST_ENABLE          1u
#define RF_SWITCH_TEST_CYCLE_TICKS     8000u
#define RF_SWITCH_TEST_DEFAULT_ACK_TICKS 800u
#define RF_SWITCH_TEST_MARK_TX         0x54u
#define RF_SWITCH_TEST_MARK_RX         0x52u

#define SBP_RF_STAT_EVT                (1 << 5)

#if (RF_SERIAL_LOG == 1)
#define RF_LINK_LOG(...)               PRINT(__VA_ARGS__)
#else
#define RF_LINK_LOG(...)               ((void)0)
#endif

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
    volatile uint32_t rx_bad_len;
    volatile uint32_t rx_bad_type;
    volatile uint32_t rx_bad_seq;
    volatile uint32_t rx_crcerr;
    volatile uint32_t rx_timeout;
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
static volatile uint8_t g_ack_rx_active = 0u;
static volatile uint8_t g_low_config_ret = 0xFFu;
static volatile uint8_t g_low_tx_ret = 0xFFu;
static volatile uint8_t g_low_rx_ret = 0xFFu;
static volatile uint8_t g_pending_event_state_code = 0u;

static uint8_t g_slot_id = 0u;
static uint8_t g_group_id = 0u;
static uint8_t g_ack_wait_group = 0u;
static uint8_t g_waiting_ack = 0u;
static uint8_t g_ack_wait_prepared = 0u;
static uint8_t g_last_ack_bitmap = RFH_ACK_MISSING_MASK;
static uint32_t g_total_groups = 0u;
static uint32_t g_total_tx_data = 0u;
static uint32_t g_total_ack_ok = 0u;
static uint32_t g_total_ack_expected = 0u;
static uint32_t g_total_ack_miss = 0u;
static uint32_t g_total_missing_packets = 0u;
static uint32_t g_total_errors = 0u;

static uint32_t g_log_groups = 0u;
static uint32_t g_log_tx_data = 0u;
static uint32_t g_log_ack_ok = 0u;
static uint32_t g_log_ack_expected = 0u;
static uint32_t g_log_ack_miss = 0u;
static uint32_t g_log_missing_packets = 0u;
static uint32_t g_log_errors = 0u;
static uint8_t g_last_bad_len = 0u;
static uint8_t g_last_bad_h0 = 0u;
static uint8_t g_last_bad_h1 = 0u;
static uint8_t g_last_bad_d1 = 0u;

static uint8_t g_last_payload[RFH_AIR_DATA_LEN] = {0};
static uint8_t g_has_payload = 0u;

#if (RF_SWITCH_TEST_ENABLE == 1)
static uint16_t g_sw_tick = 0u;
static uint8_t g_sw_seq = 0u;
static uint8_t g_sw_rx_active = 0u;
static uint8_t g_sw_role_tx = 1u;
static uint32_t g_sw_log_tx = 0u;
static uint32_t g_sw_log_rx = 0u;
static uint32_t g_sw_log_bad = 0u;
static uint32_t g_sw_log_switch = 0u;
static uint32_t g_sw_total_rx = 0u;
static uint16_t g_sw_ack_ticks = RF_SWITCH_TEST_DEFAULT_ACK_TICKS;
static uint8_t g_sw_ack_scan_index = 0u;
static const uint16_t g_sw_ack_scan_ticks[] = {
    800u, 400u, 200u, 100u, 80u, 64u, 48u, 32u, 24u,
    16u, 12u, 8u, 6u, 4u, 3u, 2u, 1u
};
#endif

void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id);

static uint16_t tx_saturate_u16(uint32_t value)
{
    return (value > 0xFFFFu) ? 0xFFFFu : (uint16_t)value;
}

static uint8_t tx_popcount7(uint8_t value)
{
    uint8_t count = 0u;
    value &= RFH_ACK_MISSING_MASK;
    while(value != 0u)
    {
        count = (uint8_t)(count + (value & 1u));
        value >>= 1;
    }
    return count;
}

static void tx_note_error(void)
{
    g_total_errors++;
    g_log_errors++;
}

static void tx_capture_bad_ack(void)
{
    const uint8_t *air = &RxBuf[2];
    const uint8_t *data = &air[RFH_DATA_OFFSET];

    g_last_bad_len = RxBuf[1];
    g_last_bad_h0 = air[RFH_HDR0_OFFSET];
    g_last_bad_h1 = air[RFH_HDR1_OFFSET];
    g_last_bad_d1 = data[1];
}

#if (RF_SWITCH_TEST_ENABLE != 1)
static uint16_t tx_loss_permille(void)
{
    uint32_t expected = g_log_ack_ok * RFH_DATA_SLOTS;
    if(expected == 0u)
    {
        return 0u;
    }
    return (uint16_t)((g_log_missing_packets * 1000u) / expected);
}

static uint16_t tx_ack_loss_permille(void)
{
    if(g_log_ack_expected == 0u)
    {
        return 0u;
    }
    return (uint16_t)((g_log_ack_miss * 1000u) / g_log_ack_expected);
}
#endif

static uint8_t tx_load_latest_payload(uint8_t *dst)
{
    if(rfm_input_stream_take_latest(dst, RFH_AIR_DATA_LEN))
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

static void tx_fill_data_packet(void)
{
    uint8_t *air = &TxBuf[2];
    uint8_t *data = &air[RFH_DATA_OFFSET];

    TxBuf[0] = RFH_WCH_PREAMBLE;
    TxBuf[1] = RFH_AIR_PACKET_LEN;
    air[RFH_HDR0_OFFSET] = rfh_make_header0(RFH_PKT_DATA,
                                            RFH_RATE_8K,
                                            RFH_FLAG_LINK_OK);
    air[RFH_HDR1_OFFSET] = rfh_make_slot_header1(g_group_id, g_slot_id);
    (void)tx_load_latest_payload(data);
}

#if (RF_SWITCH_TEST_ENABLE == 1)
static uint16_t tx_switch_tx_ticks(void)
{
    if(g_sw_ack_ticks >= RF_SWITCH_TEST_CYCLE_TICKS)
    {
        return (uint16_t)(RF_SWITCH_TEST_CYCLE_TICKS - 1u);
    }
    return (uint16_t)(RF_SWITCH_TEST_CYCLE_TICKS - g_sw_ack_ticks);
}

static uint16_t tx_switch_ack_window_us(void)
{
    return (uint16_t)(g_sw_ack_ticks * RFH_SLOT_US);
}

static void tx_switch_ack_scan_advance(void)
{
    g_sw_ack_scan_index++;
    if(g_sw_ack_scan_index >=
       (uint8_t)(sizeof(g_sw_ack_scan_ticks) / sizeof(g_sw_ack_scan_ticks[0])))
    {
        g_sw_ack_scan_index = 0u;
    }
    g_sw_ack_ticks = g_sw_ack_scan_ticks[g_sw_ack_scan_index];
}

static void tx_switch_fill_packet(void)
{
    uint8_t *air = &TxBuf[2];
    uint8_t *data = &air[RFH_DATA_OFFSET];
    uint8_t seq = g_sw_seq++;

    TxBuf[0] = RFH_WCH_PREAMBLE;
    TxBuf[1] = RFH_AIR_PACKET_LEN;
    air[RFH_HDR0_OFFSET] = rfh_make_header0(RFH_PKT_DATA,
                                            RFH_RATE_8K,
                                            RFH_FLAG_LINK_OK);
    air[RFH_HDR1_OFFSET] = rfh_make_slot_header1((uint8_t)(seq >> 3),
                                                 (uint8_t)(seq & RFH_SLOT_MASK));
    memset(data, 0, RFH_AIR_DATA_LEN);
    data[0] = RF_SWITCH_TEST_MARK_TX;
    data[1] = seq;
    data[2] = (uint8_t)(g_sw_tick & 0xFFu);
    data[3] = (uint8_t)(g_sw_tick >> 8);
    data[4] = (uint8_t)(g_sw_ack_ticks & 0xFFu);
    data[5] = (uint8_t)(g_sw_ack_ticks >> 8);
}

static void tx_switch_start_rx(void)
{
    if((g_basic_started == 0u) || (g_sw_rx_active != 0u))
    {
        return;
    }

    (void)RFRole_Stop();
    gRxParam.frequency = RFH_FIXED_CHANNEL;
    gRxParam.whiteChannel = RFH_FIXED_CHANNEL;
    gRxParam.rxDMA = (uint32_t)RxBuf;
    gRxParam.rxMaxLen = RFH_AIR_PACKET_LEN;
    gRxParam.timeOut = RFIP_RX_NO_TIMEOUT;
    gStat.rx_arm_try++;
    g_low_rx_ret = (uint8_t)RFIP_SetRx(&gRxParam);
    if(g_low_rx_ret == SUCCESS)
    {
        g_sw_rx_active = 1u;
        gStat.rx_arm_ok++;
    }
    else
    {
        gStat.rx_arm_fail++;
        g_sw_log_bad++;
        tx_note_error();
    }
}

static void tx_switch_start_tx(void)
{
    bStatus_t ret_start;
    bStatus_t ret_parm;

    if(g_basic_started == 0u)
    {
        return;
    }
    if(g_sw_rx_active != 0u)
    {
        (void)RFRole_Stop();
        g_sw_rx_active = 0u;
    }

    tx_switch_fill_packet();
    gTxParam.frequency = RFH_FIXED_CHANNEL;
    gTxParam.whiteChannel = RFH_FIXED_CHANNEL;
    gTxParam.txDMA = (uint32_t)TxBuf;
    gStat.tx_try++;
    ret_start = RFIP_SetTxStart();
    g_low_tx_ret = (uint8_t)ret_start;
    if(ret_start != SUCCESS)
    {
        gStat.tx_start_fail++;
        gStat.tx_fail++;
        g_sw_log_bad++;
        tx_note_error();
        return;
    }
    ret_parm = RFIP_SetTxParm(&gTxParam);
    if(ret_parm != SUCCESS)
    {
        gStat.tx_parm_fail++;
        gStat.tx_fail++;
        g_sw_log_bad++;
        tx_note_error();
        return;
    }
    g_sw_log_tx++;
}

static void tx_switch_handle_rx(void)
{
    const uint8_t *air = &RxBuf[2];
    const uint8_t *data = &air[RFH_DATA_OFFSET];

    g_sw_rx_active = 0u;
    if((RxBuf[1] == RFH_AIR_PACKET_LEN) &&
       (rfh_packet_type(air[RFH_HDR0_OFFSET]) == RFH_PKT_DATA) &&
       (rfh_rate_code(air[RFH_HDR0_OFFSET]) == RFH_RATE_8K) &&
       (data[0] == RF_SWITCH_TEST_MARK_RX))
    {
        g_sw_log_rx++;
        g_sw_total_rx++;
        return;
    }
    g_sw_log_bad++;
    tx_note_error();
}

static void tx_switch_tick(void)
{
    uint8_t role_tx = (g_sw_tick < tx_switch_tx_ticks()) ? 1u : 0u;

    if(role_tx != g_sw_role_tx)
    {
        g_sw_role_tx = role_tx;
        g_sw_log_switch++;
        (void)RFRole_Stop();
        g_sw_rx_active = 0u;
    }

    if(role_tx != 0u)
    {
        tx_switch_start_tx();
    }
    else
    {
        tx_switch_start_rx();
    }

    g_sw_tick++;
    if(g_sw_tick >= RF_SWITCH_TEST_CYCLE_TICKS)
    {
        g_sw_tick = 0u;
    }
}
#endif

static void tx_start_tx_packet(void)
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
        if(g_waiting_ack != 0u)
        {
            g_total_ack_miss++;
            g_log_ack_miss++;
            g_waiting_ack = 0u;
        }
    }
    if(g_slot_id == 0u)
    {
        g_ack_wait_prepared = 0u;
    }

    gTxParam.frequency = RFH_FIXED_CHANNEL;
    gTxParam.whiteChannel = RFH_FIXED_CHANNEL;
    gTxParam.txDMA = (uint32_t)TxBuf;
    gStat.tx_try++;
    ret_start = RFIP_SetTxStart();
    g_low_tx_ret = (uint8_t)ret_start;
    if(ret_start != SUCCESS)
    {
        gStat.tx_start_fail++;
        gStat.tx_fail++;
        tx_note_error();
        return;
    }

    ret_parm = RFIP_SetTxParm(&gTxParam);
    if(ret_parm != SUCCESS)
    {
        gStat.tx_parm_fail++;
        gStat.tx_fail++;
        tx_note_error();
    }
}

static void tx_prepare_ack_wait(uint8_t group)
{
    if((g_ack_wait_prepared != 0u) &&
       (g_ack_wait_group == group))
    {
        return;
    }

    g_ack_wait_group = group;
    g_waiting_ack = 1u;
    g_ack_wait_prepared = 1u;
    g_total_groups++;
    g_total_ack_expected++;
    g_log_groups++;
    g_log_ack_expected++;
}

static void tx_start_ack_rx(void)
{
    if(g_basic_started == 0u)
    {
        return;
    }
    if(g_ack_rx_active != 0u)
    {
        return;
    }

    (void)RFRole_Stop();
    gRxParam.frequency = RFH_FIXED_CHANNEL;
    gRxParam.whiteChannel = RFH_FIXED_CHANNEL;
    gRxParam.rxDMA = (uint32_t)RxBuf;
    gRxParam.rxMaxLen = RFH_AIR_PACKET_LEN;
    gRxParam.timeOut = RF_ACK_RX_TIMEOUT_US;
    gStat.rx_arm_try++;
    g_low_rx_ret = (uint8_t)RFIP_SetRx(&gRxParam);
    if(g_low_rx_ret == SUCCESS)
    {
        g_ack_rx_active = 1u;
        gStat.rx_arm_ok++;
    }
    else
    {
        gStat.rx_arm_fail++;
        tx_note_error();
    }
}

static uint8_t tx_handle_ack_packet(void)
{
    const uint8_t *air = &RxBuf[2];
    const uint8_t *data = &air[RFH_DATA_OFFSET];
    uint8_t header_group;
    uint8_t slot;
    uint8_t bitmap;
    uint8_t missing;

    if(RxBuf[1] != RFH_AIR_PACKET_LEN)
    {
        gStat.rx_bad_ack++;
        gStat.rx_bad_len++;
        tx_capture_bad_ack();
        tx_note_error();
        return 0u;
    }
    if(rfh_packet_type(air[RFH_HDR0_OFFSET]) != RFH_PKT_ACK)
    {
        gStat.rx_bad_ack++;
        gStat.rx_bad_type++;
        tx_capture_bad_ack();
        tx_note_error();
        return 0u;
    }
    slot = rfh_header_slot_id(air[RFH_HDR1_OFFSET]);
    header_group = rfh_header_group_id(air[RFH_HDR1_OFFSET]);
    if((slot != RFH_ACK_SLOT) ||
       (header_group != g_ack_wait_group) ||
       (data[1] != g_ack_wait_group))
    {
        gStat.rx_bad_ack++;
        gStat.rx_bad_seq++;
        tx_capture_bad_ack();
        tx_note_error();
        return 0u;
    }

    bitmap = (uint8_t)(data[0] & RFH_ACK_MISSING_MASK);
    missing = tx_popcount7(bitmap);
    g_last_ack_bitmap = bitmap;
    g_waiting_ack = 0u;
    g_total_ack_ok++;
    g_total_missing_packets += missing;
    g_log_ack_ok++;
    g_log_missing_packets += missing;
    gStat.rx_ack++;
    return 1u;
}

static void tx_rearm_ack_rx_if_waiting(void)
{
    if(g_waiting_ack != 0u)
    {
        tx_start_ack_rx();
    }
}

static void tx_advance_slot(void)
{
    g_slot_id++;
    if(g_slot_id >= RFH_GROUP_SLOTS)
    {
        g_slot_id = 0u;
        g_group_id = (uint8_t)((g_group_id + 1u) & 0x1Fu);
    }
}

__INTERRUPT
__HIGH_CODE
void TMR0_IRQHandler(void)
{
    if(TMR0_GetITFlag(TMR0_3_IT_CYC_END) == 0u)
    {
        return;
    }
    TMR0_ClearITFlag(TMR0_3_IT_CYC_END);

    if(g_basic_started == 0u)
    {
        tx_advance_slot();
        return;
    }

#if (RF_SWITCH_TEST_ENABLE == 1)
    tx_switch_tick();
    return;
#endif

    if(g_slot_id < RFH_DATA_SLOTS)
    {
        if(g_slot_id == RF_ACK_RX_START_SLOT)
        {
            tx_prepare_ack_wait(g_group_id);
        }
        if(g_slot_id <= RF_ACK_RX_START_SLOT)
        {
            tx_fill_data_packet();
            tx_start_tx_packet();
            g_total_tx_data++;
            g_log_tx_data++;
        }
        if((g_slot_id == RF_ACK_RX_START_SLOT) &&
           (g_waiting_ack != 0u))
        {
            mDelayuS(RF_DATA_TO_ACK_RX_DELAY_US);
            tx_start_ack_rx();
        }
        else if((g_slot_id > RF_ACK_RX_START_SLOT) &&
                (g_waiting_ack != 0u))
        {
            tx_start_ack_rx();
        }
    }
    else
    {
        tx_prepare_ack_wait(g_group_id);
        tx_start_ack_rx();
    }

    tx_advance_slot();
}

__HIGH_CODE
void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id)
{
    (void)id;

#if (RF_SWITCH_TEST_ENABLE == 1)
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
        if(g_sw_rx_active != 0u)
        {
            tx_switch_handle_rx();
            if(g_sw_role_tx == 0u)
            {
                tx_switch_start_rx();
            }
        }
    }
    if(sta & RF_STATE_RX_CRCERR)
    {
        gStat.rx_bad_ack++;
        g_sw_rx_active = 0u;
        g_sw_log_bad++;
        tx_note_error();
        if(g_sw_role_tx == 0u)
        {
            tx_switch_start_rx();
        }
    }
    if(sta & RF_STATE_TIMEOUT)
    {
        gStat.rx_bad_ack++;
        g_sw_rx_active = 0u;
        g_sw_log_bad++;
        tx_note_error();
        if(g_sw_role_tx == 0u)
        {
            tx_switch_start_rx();
        }
    }
    return;
#endif

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
            g_ack_rx_active = 0u;
            if(tx_handle_ack_packet() == 0u)
            {
                tx_rearm_ack_rx_if_waiting();
            }
        }
    }
    if(sta & RF_STATE_RX_CRCERR)
    {
        if(g_ack_rx_active != 0u)
        {
            g_ack_rx_active = 0u;
            gStat.rx_bad_ack++;
            gStat.rx_crcerr++;
            tx_capture_bad_ack();
            tx_note_error();
            tx_rearm_ack_rx_if_waiting();
        }
    }
    if(sta & RF_STATE_TIMEOUT)
    {
        if(g_ack_rx_active != 0u)
        {
            g_ack_rx_active = 0u;
            gStat.rx_bad_ack++;
            gStat.rx_timeout++;
            tx_capture_bad_ack();
            tx_note_error();
            tx_rearm_ack_rx_if_waiting();
        }
    }
}

static void tx_log_5s_emit(void)
{
#if (RF_SWITCH_TEST_ENABLE == 1)
    RF_LINK_LOG("TS W=%uus TX=%lu RX=%lu SW=%lu B=%lu E=%lu\r\n",
                (unsigned int)tx_switch_ack_window_us(),
                (unsigned long)g_sw_log_tx,
                (unsigned long)g_sw_log_rx,
                (unsigned long)g_sw_log_switch,
                (unsigned long)g_sw_log_bad,
                (unsigned long)g_log_errors);

    RF_LINK_LOG("TD X=%lu/%lu/%lu/%lu RA=%lu/%lu R=%lu\r\n",
                (unsigned long)gStat.tx_try,
                (unsigned long)gStat.tx_ok,
                (unsigned long)gStat.tx_idle,
                (unsigned long)gStat.tx_fail,
                (unsigned long)gStat.rx_arm_try,
                (unsigned long)gStat.rx_arm_fail,
                (unsigned long)g_sw_total_rx);

    g_sw_log_tx = 0u;
    g_sw_log_rx = 0u;
    g_sw_log_switch = 0u;
    g_sw_log_bad = 0u;
    g_log_errors = 0u;
    tx_switch_ack_scan_advance();
    return;
#else
    uint16_t loss = tx_loss_permille();
    uint16_t ack_loss = tx_ack_loss_permille();

    RF_LINK_LOG("T5 C=%u R=8K G=%lu D=%lu A=%lu/%lu AckM=%lu AL=%03u Miss=%lu L=%03u LastBM=%02X E=%lu\r\n",
                (unsigned int)RFH_FIXED_CHANNEL,
                (unsigned long)g_log_groups,
                (unsigned long)g_log_tx_data,
                (unsigned long)g_log_ack_ok,
                (unsigned long)g_log_ack_expected,
                (unsigned long)g_log_ack_miss,
                (unsigned int)ack_loss,
                (unsigned long)g_log_missing_packets,
                (unsigned int)loss,
                (unsigned int)g_last_ack_bitmap,
                (unsigned long)g_log_errors);

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
    RF_LINK_LOG("AB L/T/S/C/O=%lu/%lu/%lu/%lu/%lu LB=%02X,%02X,%02X,%02X\r\n",
                (unsigned long)gStat.rx_bad_len,
                (unsigned long)gStat.rx_bad_type,
                (unsigned long)gStat.rx_bad_seq,
                (unsigned long)gStat.rx_crcerr,
                (unsigned long)gStat.rx_timeout,
                (unsigned int)g_last_bad_len,
                (unsigned int)g_last_bad_h0,
                (unsigned int)g_last_bad_h1,
                (unsigned int)g_last_bad_d1);

    g_log_groups = 0u;
    g_log_tx_data = 0u;
    g_log_ack_ok = 0u;
    g_log_ack_expected = 0u;
    g_log_ack_miss = 0u;
    g_log_missing_packets = 0u;
    g_log_errors = 0u;
#endif
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
    gTxParam.frequency = RFH_FIXED_CHANNEL;
    gTxParam.whiteChannel = RFH_FIXED_CHANNEL;
    gTxParam.sendTime = (uint8_t)gParm.sendTime;
    gTxParam.sendCount = 1u;
    gTxParam.txDMA = (uint32_t)TxBuf;

    gRxParam.accessAddress = gParm.accessAddress;
    gRxParam.crcInit = gParm.crcInit;
    gRxParam.properties = gParm.properties;
    gRxParam.frequency = RFH_FIXED_CHANNEL;
    gRxParam.whiteChannel = RFH_FIXED_CHANNEL;
    gRxParam.rxDMA = (uint32_t)RxBuf;
    gRxParam.rxMaxLen = RFH_AIR_PACKET_LEN;
    gRxParam.timeOut = RF_ACK_RX_TIMEOUT_US;

    g_basic_started = 1u;
    RF_LINK_LOG("[TX][RF7] cfg:%u ch:%u rate:%u len:%u\r\n",
                (unsigned int)g_low_config_ret,
                (unsigned int)RFH_FIXED_CHANNEL,
                (unsigned int)RFH_FIXED_RATE_HZ,
                RFH_AIR_PACKET_LEN);
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
        rfm_spi_bridge_diag_emit(RF_STAT_PRINT_PERIOD_MS);
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
        gStat.rx_bad_len = 0u;
        gStat.rx_bad_type = 0u;
        gStat.rx_bad_seq = 0u;
        gStat.rx_crcerr = 0u;
        gStat.rx_timeout = 0u;
        gStat.spi_rx_win = 0u;
        gStat.payload_update = 0u;
        tmos_start_task(taskID, SBP_RF_STAT_EVT, MS1_TO_SYSTEM_TIME(RF_STAT_PRINT_PERIOD_MS));
        return (events ^ SBP_RF_STAT_EVT);
    }

    return 0u;
}

void RF_TxMainLoopProcess(void)
{
}

bool RF_SPI_FastWriteInput(const uint8_t *payload, uint8_t len)
{
    if((payload == NULL) || (len != RFH_AIR_DATA_LEN))
    {
        return false;
    }
    if(!rfm_input_stream_push(payload, len))
    {
        return false;
    }
    gStat.spi_rx_total++;
    gStat.spi_rx_win++;
    return true;
}

bool RF_SetReportRateHz(uint16_t hz)
{
    return (hz == RFH_FIXED_RATE_HZ);
}

uint16_t RF_GetReportRateHz(void)
{
    return RFH_FIXED_RATE_HZ;
}

bool RF_StartPairing(void)
{
    g_pending_event_state_code = RF_LINK_STATE_CONNECTED;
    return true;
}

bool RF_StopPairing(void)
{
    g_pending_event_state_code = RF_LINK_STATE_CONNECTED;
    return true;
}

bool RF_Unbind(void)
{
    g_pending_event_state_code = RF_LINK_STATE_CONNECTED;
    return true;
}

uint8_t RF_GetLinkStateCode(void)
{
    return (g_basic_started == 0u) ? RF_LINK_STATE_IDLE : RF_LINK_STATE_CONNECTED;
}

uint8_t RF_ConsumePendingEventStateCode(void)
{
    uint8_t state = g_pending_event_state_code;
    g_pending_event_state_code = 0u;
    return state;
}

uint8_t RF_PeekPendingEventStateCode(void)
{
    return g_pending_event_state_code;
}

void RF_ClearPendingEventStateCode(uint8_t state_code)
{
    if((state_code == 0u) || (g_pending_event_state_code == state_code))
    {
        g_pending_event_state_code = 0u;
    }
}

uint8_t RF_IsConnected(void)
{
    return (g_basic_started != 0u) ? 1u : 0u;
}

uint8_t RF_HasBond(void)
{
    return 0u;
}

uint16_t RF_GetRxOkCount(void)
{
    return tx_saturate_u16(g_total_ack_ok);
}

uint16_t RF_GetRxFailCount(void)
{
    return tx_saturate_u16(g_total_ack_miss + gStat.rx_bad_ack);
}

uint16_t RF_GetTxFailCount(void)
{
    return tx_saturate_u16(gStat.tx_fail);
}

uint32_t RF_GetRejectCount(void)
{
    return g_total_missing_packets;
}

void RF_Init(void)
{
    uint32_t tick_per_evt;

    taskID = TMOS_ProcessEventRegister(RF_ProcessEvent);

    PFIC_EnableIRQ(BLEB_IRQn);
    PFIC_EnableIRQ(BLEL_IRQn);

    rfm_input_stream_init();
    memset(g_last_payload, 0, sizeof(g_last_payload));
    g_has_payload = 0u;
    g_slot_id = 0u;
    g_group_id = 0u;
    g_ack_wait_group = 0u;
    g_waiting_ack = 0u;
    g_ack_wait_prepared = 0u;
    g_last_ack_bitmap = RFH_ACK_MISSING_MASK;
#if (RF_SWITCH_TEST_ENABLE == 1)
    g_sw_tick = 0u;
    g_sw_seq = 0u;
    g_sw_rx_active = 0u;
    g_sw_role_tx = 1u;
    g_sw_log_tx = 0u;
    g_sw_log_rx = 0u;
    g_sw_log_bad = 0u;
    g_sw_log_switch = 0u;
    g_sw_total_rx = 0u;
    g_sw_ack_scan_index = 0u;
    g_sw_ack_ticks = g_sw_ack_scan_ticks[g_sw_ack_scan_index];
#endif

    tmos_start_task(taskID, SBP_RF_STAT_EVT, MS1_TO_SYSTEM_TIME(RF_STAT_PRINT_PERIOD_MS));
    tx_basic_start();

    tick_per_evt = GetSysClock() / RFH_FIXED_RATE_HZ;
    if(tick_per_evt == 0u)
    {
        tick_per_evt = 1u;
    }
    TMR0_TimerInit(tick_per_evt);
    TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    PFIC_SetPriority(TMR0_IRQn, 0x80);
    PFIC_EnableIRQ(TMR0_IRQn);
}

#endif
