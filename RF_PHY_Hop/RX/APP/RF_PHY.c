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
#define RF_TEST_FREQUENCY           16
#endif
#ifndef RF_TEST_PROTOCOL_PACKET
#define RF_TEST_PROTOCOL_PACKET     0
#endif
#ifndef RF_TEST_ENABLE_HOP
#define RF_TEST_ENABLE_HOP          0
#endif
#ifndef RF_TEST_ENABLE_RF_RX
#define RF_TEST_ENABLE_RF_RX        1
#endif
#if (RF_TEST_PROTOCOL_PACKET == 1)
#define RF_TEST_DATA_LEN            12
#else
#define RF_TEST_DATA_LEN            4
#endif
#ifndef RF_REPORT_PPS
#define RF_REPORT_PPS               8000
#endif
#define RF_STAT_PRINT_PERIOD_MS     5000
#define TMR0_FREE_RUN_END           0x03FFFFFFUL
#define RF_USE_LOW_LEVEL_BASIC      0
#define RF_LINK_DEBUG_LOG           1
#ifndef RF_RX_RESTART_IN_CALLBACK
#define RF_RX_RESTART_IN_CALLBACK   1
#endif

#define RF_PKT_MAGIC                0xA7u
#define RF_PKT_TYPE_DATA            0x01u
#define RF_PKT_SESSION_ID           0x21u
#define RF_PKT_HOP_EPOCH            0u
#define RF_PKT_CRC_LEN              2u
#define RF_HOP_DWELL_PACKETS        16u
#define RF_HOP_CHANNEL_COUNT        9u
#define RF_TX_SEND_TIME             (20u * 2u)
#define RF_LINK_ACCESS_ADDRESS      0x71764129UL
#define RF_LINK_CRC_INIT            0x555555UL
#define RF_BUTTON_BYTES             3u
#define RF_RX_FAST_REACQUIRE_MISS   8u
#if (RF_TEST_ENABLE_HOP == 1)
#define RF_RX_TIMEOUT_HALF_US       5000u
#else
#define RF_RX_TIMEOUT_HALF_US       10000u
#endif

#define SBP_RF_RF_RX_EVT             4
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

static const uint8_t g_hop_channels[RF_HOP_CHANNEL_COUNT] = {
    4u, 8u, 12u, 16u, 20u, 24u, 28u, 32u, 36u
};

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

static void rf_rx_set_channel(uint8_t channel)
{
    if(gRxParam.frequency == channel)
    {
        return;
    }
    gRxParam.frequency = channel;
    gRxParam.whiteChannel = channel;
    g_low_channel = channel;
}

static void rf_rx_tune_for_expected_seq(void)
{
    rf_rx_set_channel(rf_hop_channel_for_seq(g_rx_expected_seq));
    g_rx_scan_index = rf_hop_index_for_seq(g_rx_expected_seq);
}

static void rf_rx_advance_reacquire_channel(void)
{
#if (RF_TEST_ENABLE_HOP == 1)
    g_rx_miss_count++;
    if((g_rx_has_seq != 0u) && (g_rx_miss_count <= RF_RX_FAST_REACQUIRE_MISS))
    {
        g_rx_expected_seq++;
        rf_rx_tune_for_expected_seq();
        return;
    }
    if(g_rx_miss_count > RF_RX_FAST_REACQUIRE_MISS)
    {
        gStat.rx_reacquire++;
    }
    g_rx_scan_index++;
    if(g_rx_scan_index >= RF_HOP_CHANNEL_COUNT)
    {
        g_rx_scan_index = 0u;
    }
    rf_rx_set_channel(g_hop_channels[g_rx_scan_index]);
#else
    rf_rx_set_channel(RF_TEST_FREQUENCY);
#endif
}

static void rf_rx_request_restart(void)
{
#if (RF_RX_RESTART_IN_CALLBACK == 1)
    uint32_t delay_ticks;
    uint32_t mark_tmr = TMR0_GetCurrentTimer();

    rf_rx_start();
    delay_ticks = rf_tmr0_delta(TMR0_GetCurrentTimer(), mark_tmr);
    g_rx_restart_delay_sum += delay_ticks;
    if(delay_ticks > g_rx_restart_delay_max)
    {
        g_rx_restart_delay_max = delay_ticks;
    }
    g_rx_restart_delay_cnt++;
#else
    if(g_rx_restart_pending == 0u)
    {
        g_rx_restart_mark_tmr = TMR0_GetCurrentTimer();
    }
    g_rx_restart_pending = 1u;
#endif
}

static uint8_t rf_rx_validate_packet(void)
{
#if (RF_TEST_PROTOCOL_PACKET == 1)
    const uint8_t *packet = &RxBuf[2];
    uint16_t expected_crc;
    uint16_t packet_crc;
    uint8_t seq;
    uint8_t delta;

    if(RxBuf[1] != RF_TEST_DATA_LEN)
    {
        return 0u;
    }
    if((packet[0] != RF_PKT_MAGIC) || (packet[1] != RF_PKT_TYPE_DATA))
    {
        gStat.rx_bad_magic++;
        return 0u;
    }
    if((packet[2] != RF_PKT_SESSION_ID) || (packet[4] != RF_PKT_HOP_EPOCH))
    {
        gStat.rx_bad_session++;
        return 0u;
    }

    packet_crc = (uint16_t)packet[RF_TEST_DATA_LEN - 2u] |
                 ((uint16_t)packet[RF_TEST_DATA_LEN - 1u] << 8);
    expected_crc = rf_crc16_ccitt(packet, (uint8_t)(RF_TEST_DATA_LEN - RF_PKT_CRC_LEN));
    if(packet_crc != expected_crc)
    {
        gStat.rx_bad_crc++;
        return 0u;
    }

    seq = packet[3];
    if(g_rx_has_seq == 0u)
    {
        g_rx_has_seq = 1u;
    }
    else
    {
        delta = (uint8_t)(seq - g_rx_expected_seq);
        if(delta == 0u)
        {
        }
        else if(delta == 0xFFu)
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

    if(RxBuf[1] != RF_TEST_DATA_LEN)
    {
        return 0u;
    }

    seq = RxBuf[2u + RF_BUTTON_BYTES];
    if(g_rx_has_seq == 0u)
    {
        g_rx_has_seq = 1u;
    }
    else
    {
        delta = (uint8_t)(seq - g_rx_expected_seq);
        if(delta == 0u)
        {
        }
        else if(delta == 0xFFu)
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

    if(len <= 0)
    {
        return;
    }
    if(len >= (int)sizeof(msg))
    {
        len = (int)sizeof(msg) - 1;
    }

    PRINT("%s", msg);

    if(USBHS_DevEnumStatus == 0)
    {
        return;
    }
    if((USBHS_Endp_Busy[DEF_UEP5] & DEF_UEP_BUSY) != 0)
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
#if (RF_USE_LOW_LEVEL_BASIC == 1)
    RF_Shut();
    g_low_rx_ret = RF_Rx(NULL, 0, 0xFF, 0xFF);
#else
    gRxParam.timeOut = RF_RX_TIMEOUT_HALF_US;
    g_low_rx_ret = RFIP_SetRx(&gRxParam);
#endif
}

__HIGH_CODE
static void rf_fill_payload(void)
{
    TxBuf[0]++;
    TxBuf[1] = RF_TEST_DATA_LEN;
    TxBuf[2] = 2;
    TxBuf[3] = 3;
    TxBuf[4] = 4;
    TxBuf[5] = 5;
    TxBuf[6] = 6;
    TxBuf[7] = 7;
    TxBuf[8] = 8;
    TxBuf[9] = 9;
    TxBuf[10] = 0;
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
    if(sta == RX_MODE_RX_DATA)
    {
        gStat.rx_total++;
        if((crc == 0u) && (rxBuf != NULL))
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

    if(sta == RX_MODE_TX_FINISH)
    {
        tmos_set_event(taskID, SBP_RF_RF_RX_EVT);
        return;
    }

    if((sta == RX_MODE_TX_FAIL) || (sta == TX_MODE_TX_FAIL) || (sta == TX_MODE_RX_TIMEOUT))
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
    if(g_basic_started != 0u)
    {
        rf_rx_start();
    }
}
#endif

__HIGH_CODE
void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id)
{
    (void)id;

    if(sta & RF_STATE_RX)
    {
        gStat.rx_total++;
        if(rf_rx_validate_packet() != 0u)
        {
            gStat.rx_ok++;
        }
        else
        {
            gStat.rx_fail++;
            rf_rx_advance_reacquire_channel();
        }
        rf_rx_request_restart();
    }
    if(sta & RF_STATE_RX_CRCERR)
    {
        gStat.rx_total++;
        gStat.rx_fail++;
        gStat.rx_crcerr++;
        rf_rx_advance_reacquire_channel();
        rf_rx_request_restart();
    }
    if(sta & RF_STATE_TIMEOUT)
    {
        gStat.rx_fail++;
        gStat.rx_timeout++;
        rf_rx_advance_reacquire_channel();
        rf_rx_request_restart();
    }
}

void RF_Service(void)
{
    uint32_t delay_ticks;

    if(g_rx_restart_pending == 0u)
    {
        return;
    }

    delay_ticks = rf_tmr0_delta(TMR0_GetCurrentTimer(), g_rx_restart_mark_tmr);
    g_rx_restart_delay_sum += delay_ticks;
    if(delay_ticks > g_rx_restart_delay_max)
    {
        g_rx_restart_delay_max = delay_ticks;
    }
    g_rx_restart_delay_cnt++;

    g_rx_restart_pending = 0u;
    rf_rx_start();
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
        uint32_t rx_total = gStat.rx_total;
        uint32_t rx_ok = gStat.rx_ok;
        uint32_t rx_fail = gStat.rx_fail;
        uint32_t rx_bad_crc = gStat.rx_bad_crc;
        uint32_t rx_seq_gap = gStat.rx_seq_gap;
        uint32_t rx_reacquire = gStat.rx_reacquire;
        uint32_t tx_ok = gStat.tx_ok;
        uint32_t now_clock = TMOS_GetSystemClock();
        uint32_t dt_ticks = now_clock - g_last_stat_clock;
        uint32_t dt_ms = (uint32_t)(((uint64_t)dt_ticks * SYSTEM_TIME_MICROSEN) / 1000u);

        if(dt_ms == 0u)
        {
            dt_ms = 1u;
        }

        rx_debug_log("[RX][5s] mode:%u hop:%u len:%u dt:%lums rx:%lu ok:%lu fail:%lu rx_hz:%lu crc:%lu gap:%lu reacq:%lu txok:%lu basic:%u cfg:%u ch:%u rxret:%u\r\n",
                     RF_TEST_PROTOCOL_PACKET,
                     RF_TEST_ENABLE_HOP,
                     RF_TEST_DATA_LEN,
                     dt_ms,
                     rx_total,
                     rx_ok,
                     rx_fail,
                     (uint32_t)(((uint64_t)rx_ok * 1000u) / dt_ms),
                     rx_bad_crc,
                     rx_seq_gap,
                     rx_reacquire,
                     tx_ok,
                     (unsigned int)g_basic_started,
                     (unsigned int)g_low_config_ret,
                     (unsigned int)g_low_channel,
                     (unsigned int)g_low_rx_ret);

        gStat.tmr_tick_win = 0;
        gStat.sched_due_win = 0;
        gStat.sched_sent_win = 0;
        gStat.sched_miss_win = 0;
        gStat.tx_try = 0;
        gStat.tx_ok = 0;
        gStat.tx_fail = 0;
        gStat.rx_total = 0;
        gStat.rx_ok = 0;
        gStat.rx_fail = 0;
        gStat.rx_bad_magic = 0;
        gStat.rx_bad_session = 0;
        gStat.rx_bad_crc = 0;
        gStat.rx_seq_gap = 0;
        gStat.rx_dup = 0;
        gStat.rx_reacquire = 0;
        g_last_stat_clock = now_clock;

        tmos_start_task(taskID, SBP_RF_STAT_EVT, MS1_TO_SYSTEM_TIME(RF_STAT_PRINT_PERIOD_MS));
        return events ^ SBP_RF_STAT_EVT;
    }

    if(events & SBP_RF_RF_RX_EVT)
    {
        rf_rx_start();
        return events ^ SBP_RF_RF_RX_EVT;
    }

    return 0;
}

uint16_t RF_GetStatsLine(char *buf, uint16_t len)
{
    static uint32_t last_clock = 0;
    static uint32_t last_ok = 0;
    static uint32_t last_fail = 0;
    static uint32_t last_gap = 0;
    static uint32_t last_crcerr = 0;
    static uint32_t last_timeout = 0;
    uint32_t now_clock;
    uint32_t dt_ticks;
    uint32_t dt_ms;
    uint32_t ok_total;
    uint32_t fail_total;
    uint32_t gap_total;
    uint32_t crcerr_total;
    uint32_t timeout_total;
    uint32_t ok_delta;
    uint32_t fail_delta;
    uint32_t gap_delta;
    uint32_t crcerr_delta;
    uint32_t timeout_delta;
    uint32_t delay_sum;
    uint32_t delay_max;
    uint32_t delay_cnt;
    uint32_t delay_avg_us;
    uint32_t delay_max_us;
    uint32_t sys_clock;
    int n;

    if((buf == NULL) || (len == 0u))
    {
        return 0;
    }

    now_clock = TMOS_GetSystemClock();
    ok_total = gStat.rx_ok;
    fail_total = gStat.rx_fail;
    gap_total = gStat.rx_seq_gap;
    crcerr_total = gStat.rx_crcerr;
    timeout_total = gStat.rx_timeout;

    if(last_clock == 0u)
    {
        last_clock = g_last_stat_clock;
        last_ok = 0u;
        last_fail = 0u;
        last_gap = 0u;
        last_crcerr = 0u;
        last_timeout = 0u;
    }

    dt_ticks = now_clock - last_clock;
    dt_ms = (uint32_t)(((uint64_t)dt_ticks * SYSTEM_TIME_MICROSEN) / 1000u);
    if(dt_ms == 0u)
    {
        dt_ms = 1u;
    }

    ok_delta = ok_total - last_ok;
    fail_delta = fail_total - last_fail;
    gap_delta = gap_total - last_gap;
    crcerr_delta = crcerr_total - last_crcerr;
    timeout_delta = timeout_total - last_timeout;
    delay_sum = g_rx_restart_delay_sum;
    delay_max = g_rx_restart_delay_max;
    delay_cnt = g_rx_restart_delay_cnt;
    g_rx_restart_delay_sum = 0u;
    g_rx_restart_delay_max = 0u;
    g_rx_restart_delay_cnt = 0u;
    sys_clock = GetSysClock();
    if((delay_cnt == 0u) || (sys_clock == 0u))
    {
        delay_avg_us = 0u;
        delay_max_us = 0u;
    }
    else
    {
        delay_avg_us = (uint32_t)((((uint64_t)delay_sum * 1000000u) / sys_clock) / delay_cnt);
        delay_max_us = (uint32_t)(((uint64_t)delay_max * 1000000u) / sys_clock);
    }
    n = snprintf(buf, len,
                 "[R5]d%lu o%lu f%lu h%lu g%lu c%lu t%lu a%lu m%lu\r\n",
                 dt_ms,
                 ok_delta,
                 fail_delta,
                 (uint32_t)(((uint64_t)ok_delta * 1000u) / dt_ms),
                 gap_delta,
                 crcerr_delta,
                 timeout_delta,
                 delay_avg_us,
                 delay_max_us);

    last_clock = now_clock;
    last_ok = ok_total;
    last_fail = fail_total;
    last_gap = gap_total;
    last_crcerr = crcerr_total;
    last_timeout = timeout_total;

    if(n <= 0)
    {
        return 0;
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
    g_task_id_dbg = taskID;
    TMR0_TimerInit(TMR0_FREE_RUN_END);

    PFIC_EnableIRQ(BLEB_IRQn);
    PFIC_EnableIRQ(BLEL_IRQn);

    tmos_start_task(taskID, SBP_RF_STAT_EVT, MS1_TO_SYSTEM_TIME(RF_STAT_PRINT_PERIOD_MS));

    g_last_stat_clock = TMOS_GetSystemClock();

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
