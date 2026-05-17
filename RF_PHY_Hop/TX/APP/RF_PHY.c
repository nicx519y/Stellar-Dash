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

#include <string.h>

#ifndef RF_HOP_MODE
#define RF_HOP_MODE 1
#endif

#define RF_TEST_FREQUENCY           16
#ifndef RF_TEST_PROTOCOL_PACKET
#define RF_TEST_PROTOCOL_PACKET     0
#endif
#ifndef RF_TEST_ENABLE_HOP
#define RF_TEST_ENABLE_HOP          0
#endif
#if (RF_TEST_PROTOCOL_PACKET == 1)
#define RF_TEST_DATA_LEN            12
#else
#define RF_TEST_DATA_LEN            4
#endif
#define RF_REPORT_PPS               8000
#define RF_STAT_PRINT_PERIOD_MS     5000
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
#define RF_HOP_DWELL_PACKETS        16u
#define RF_HOP_CHANNEL_COUNT        9u

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
    volatile uint32_t tx_cb_other;
    volatile uint32_t payload_update;
    volatile uint32_t rx_total;
    volatile uint32_t rx_ok;
    volatile uint32_t rx_fail;
    volatile uint32_t spi_rx_total;
    volatile uint32_t spi_rx_win;
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
static volatile uint8_t g_low_config_ret = 0xFFu;
static volatile uint8_t g_low_channel = RF_TEST_FREQUENCY;
static volatile uint8_t g_low_tx_ret = 0xFFu;
static volatile uint8_t g_low_tx_inflight = 0;
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
    return RF_TEST_FREQUENCY;
#endif
}

__HIGH_CODE
static void rf_tx_start(void)
{
    bStatus_t ret_start;
    bStatus_t ret_parm;

    ret_start = RFIP_SetTxStart();
    if(ret_start != SUCCESS)
    {
        gStat.tx_start_fail++;
        return;
    }
    gTxParam.frequency = rf_hop_channel_for_seq(g_tx_last_seq);
    g_low_channel = (uint8_t)gTxParam.frequency;
    gTxParam.txDMA = (uint32_t)TxBuf;
    ret_parm = RFIP_SetTxParm(&gTxParam);
    if(ret_parm != SUCCESS)
    {
        gStat.tx_parm_fail++;
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
        rf_fill_payload();
        gStat.tmr_irq_win++;
        gStat.tmr_irq_total++;
        gStat.sched_due_win++;
        gStat.sched_due_total++;
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
    uint8_t payload[RFM_RF_INPUT_PAYLOAD_LEN];
    uint8_t has_payload;
    uint8_t *packet = &TxBuf[2];
    uint16_t crc;

    TxBuf[0] = 0x55;
    TxBuf[1] = RF_TEST_DATA_LEN;

    has_payload = rfm_input_stream_take_latest(payload, RFM_RF_INPUT_PAYLOAD_LEN) ? 1u : 0u;

    if(has_payload != 0u)
    {
        memcpy(g_spi_last_payload, payload, RFM_RF_INPUT_PAYLOAD_LEN);
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
        for(i = 0; i < RF_TEST_DATA_LEN; ++i)
        {
            TxBuf[2 + i] = g_spi_last_payload[i];
        }
        return;
    }

    for(i = 0; i < RF_TEST_DATA_LEN; ++i)
    {
        TxBuf[2 + i] = (uint8_t)(i + 1u);
    }
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
    cfg.accessAddress = 0x71764129;
    cfg.CRCInit = 0x555555;
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
    conf.processMask = RF_STATE_TX_FINISH | RF_STATE_TIMEOUT | RF_STATE_TX_IDLE;
    ret = RFRole_BasicInit(&conf);
    g_low_config_ret = (uint8_t)ret;
    if(ret != SUCCESS)
    {
        return;
    }

    gParm.accessAddress = 0x71764129;
    gParm.crcInit = 0x555555;
    gParm.properties = LLE_MODE_PHY_2M;
    gParm.sendTime = 20 * 2;
    RFRole_SetParam(&gParm);

    gTxParam.accessAddress = gParm.accessAddress;
    gTxParam.crcInit = gParm.crcInit;
    gTxParam.properties = gParm.properties;
    gTxParam.frequency = RF_TEST_FREQUENCY;
    gTxParam.sendCount = 1;
    gTxParam.txDMA = (uint32_t)TxBuf;

    gRxParam.accessAddress = gParm.accessAddress;
    gRxParam.crcInit = gParm.crcInit;
    gRxParam.properties = gParm.properties;
    gRxParam.frequency = RF_TEST_FREQUENCY;
    gRxParam.rxDMA = (uint32_t)RxBuf;
    gRxParam.rxMaxLen = RF_TEST_DATA_LEN;
    gRxParam.timeOut = 0;

    g_basic_started = 1;
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
    }
    if(sta & RF_STATE_TIMEOUT)
    {
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
        uint32_t sched_sent_win = gStat.sched_sent_win;
        uint32_t tx_ok = gStat.tx_ok;
        uint32_t tx_fail = gStat.tx_fail;
        uint32_t tx_idle = gStat.tx_idle;
        uint32_t tx_start_fail = gStat.tx_start_fail;
        uint32_t tx_parm_fail = gStat.tx_parm_fail;
        uint32_t payload_update = gStat.payload_update;
        uint32_t spi_rx_win = gStat.spi_rx_win;
        uint32_t spi_rx_total = gStat.spi_rx_total;
        uint32_t now_clock = TMOS_GetSystemClock();
        uint32_t dt_ticks = now_clock - g_last_stat_clock;
        uint32_t dt_ms = (uint32_t)(((uint64_t)dt_ticks * SYSTEM_TIME_MICROSEN) / 1000u);

        if(dt_ms == 0u)
        {
            dt_ms = 1u;
        }

        RF_LINK_LOG("[TX][5s] mode:%u hop:%u len:%u dt:%lums irq:%lu sent:%lu upd:%lu upd_hz:%lu txok:%lu fail:%lu idle:%lu sf:%lu pf:%lu spi:%lu/%lu basic:%u cfg:%u ch:%u txret:%u\n",
                    RF_TEST_PROTOCOL_PACKET,
                    RF_TEST_ENABLE_HOP,
                    RF_TEST_DATA_LEN,
                    dt_ms,
                    tmr_irq_win,
                    sched_sent_win,
                    payload_update,
                    (uint32_t)(((uint64_t)payload_update * 1000u) / dt_ms),
                    tx_ok,
                    tx_fail,
                    tx_idle,
                    tx_start_fail,
                    tx_parm_fail,
                    spi_rx_win,
                    spi_rx_total,
                    (unsigned int)g_basic_started,
                    (unsigned int)g_low_config_ret,
                    (unsigned int)g_low_channel,
                    (unsigned int)g_low_tx_ret);

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
        gStat.tx_cb_other = 0;
        gStat.payload_update = 0;
        gStat.rx_total = 0;
        gStat.rx_ok = 0;
        gStat.rx_fail = 0;
        gStat.spi_rx_win = 0;
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
    memset(g_spi_last_payload, 0, sizeof(g_spi_last_payload));
    g_spi_has_payload = 0u;
    g_last_stat_clock = TMOS_GetSystemClock();
    g_tick_per_evt = GetSysClock() / RF_REPORT_PPS;
    if(g_tick_per_evt == 0)
    {
        g_tick_per_evt = 1;
    }
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

#if (RF_USE_LOW_LEVEL_BASIC == 1)
    rf_low_level_basic_start_tx();
#else
    rf_basic_start_tx();
#endif
}

/******************************** endfile @ RF_PHY ******************************/
