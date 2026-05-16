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

#define RF_TEST_FREQUENCY           16
#define RF_TEST_DATA_LEN            4
#define RF_REPORT_PPS               8000
#define RF_STAT_PRINT_PERIOD_MS     5000
#define TMR0_FREE_RUN_END           0x03FFFFFFUL
#define RF_USE_FAST_8K              0
#define RF_USE_LOW_LEVEL_BASIC      0
#define RF_FAST_AUTO_RESTART        1
#define RF_FAST_USE_EXPLICIT_INFO   1
#define RF_FAST_USE_FREQUENCY_LIST  0
#define RF_FAST_BOUND_TIMEOUT_MS    1000
#define RF_FAST_RESTART_DELAY_MS    200
#define RF_LINK_DEBUG_LOG           1

#define SBP_RF_START_DEVICE_EVT      1
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
} rf_stat_t;

static rfRoleParam_t gParm;
static rfipTx_t gTxParam;
static rfipRx_t gRxParam;
static rf_stat_t gStat = {0};
static uint32_t g_tmr_prev_cnt = 0;
static uint32_t g_tmr_acc_tick = 0;
static uint32_t g_tick_per_evt = 1;
static RF_DMADESCTypeDef gTxDesc;
static RF_DMADESCTypeDef gRxDesc;
static rfBoundHost_t gFastHostCfg;
static rfRoleList_t gHostBindList[1];
static rfRoleSpeed_t gHostSpeedList;
static uint32_t gBoundFrequencyList[1] = {RF_TEST_FREQUENCY};
static rfHostBoundFre_t gBoundFreList;
static const uint8_t gFastRxOwnInfo[6] = {0x48, 0x42, 0x4F, 0x58, 0x52, 0x58};
static const uint8_t gFastTxPeerInfo[6] = {0x48, 0x42, 0x4F, 0x58, 0x54, 0x58};
static volatile uint8_t g_bound_status = 0xFF;
static volatile uint8_t g_bound_role = 0xFF;
static volatile uint8_t g_bound_id = 0xFF;
static volatile uint8_t g_bound_type = 0xFF;
static volatile uint8_t g_bound_hop = 0xFF;
static volatile uint32_t g_bound_cb_cnt = 0;
static volatile uint8_t g_ret_switch_fast = 0xFF;
static volatile uint8_t g_ret_fast_init = 0xFF;
static volatile uint8_t g_ret_speed_list = 0xFF;
static volatile uint8_t g_ret_freq_list = 0xFF;
static volatile uint8_t g_ret_start_host = 0xFF;
static volatile uint8_t g_ret_role_init = 0xFF;
static volatile uint8_t g_evt_set_ret = 0xFF;
static volatile uint8_t g_task_id_dbg = 0xFF;
static volatile uint8_t g_basic_started = 0;
static volatile uint8_t g_host_bound_ok = 0;
static volatile uint32_t g_host_restart_count = 0;
static volatile uint8_t g_low_config_ret = 0xFFu;
static volatile uint8_t g_low_channel = RF_TEST_FREQUENCY;
static volatile uint8_t g_low_rx_ret = 0xFFu;
static uint32_t g_last_stat_clock = 0;

void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id);

__attribute__((__aligned__(4))) static uint8_t TxBuf[64];
__attribute__((__aligned__(4))) static uint8_t RxBuf[264];

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
    gRxParam.timeOut = 0;
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

__HIGH_CODE
static void rf_bound_callback(staBound_t *sta)
{
    if(sta != NULL)
    {
        g_bound_status = sta->status;
        g_bound_role = sta->role;
        g_bound_id = sta->devId;
        g_bound_type = sta->devType;
        g_bound_hop = sta->hop;
        g_bound_cb_cnt++;
        if(sta->status == BOUND_STA_SUCCESS)
        {
            g_host_bound_ok = 1;
        }
        else
        {
            g_host_bound_ok = 0;
#if (RF_FAST_AUTO_RESTART == 1)
            tmos_start_task(taskID, SBP_RF_START_DEVICE_EVT, MS1_TO_SYSTEM_TIME(RF_FAST_RESTART_DELAY_MS));
#endif
        }
        rx_debug_log("[RX][BOUND] sta:%u role:%u id:%u type:%u hop:%u cb:%lu\r\n",
                     sta->status, sta->role, sta->devId, sta->devType, sta->hop,
                     g_bound_cb_cnt);
    }
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

static void rf_dma_desc_init(void)
{
    gTxDesc.Status = STA_DMA_ENABLE | (sizeof(TxBuf) & STA_LEN_MASK);
    gTxDesc.BufferSize = sizeof(TxBuf);
    gTxDesc.BufferAddr = (uint32_t)TxBuf;
    gTxDesc.NextDescAddr = (uint32_t)&gTxDesc;

    gRxDesc.Status = STA_DMA_ENABLE | (sizeof(RxBuf) & STA_LEN_MASK);
    gRxDesc.BufferSize = sizeof(RxBuf);
    gRxDesc.BufferAddr = (uint32_t)RxBuf;
    gRxDesc.NextDescAddr = (uint32_t)&gRxDesc;
}

static void rf_fast_start_host(void)
{
    rfRoleConfig_t conf = {0};
    bStatus_t ret;

    rf_fill_payload();
    rf_dma_desc_init();
    RFBound_Stop();

    ret = RFRole_SwitchMode(RFIP_MODE_RF_FAST);
    g_ret_switch_fast = (uint8_t)ret;
    rx_debug_log("[RX][FAST] switch:%u\r\n", ret);

    conf.TxPower = BLE_TX_POWER;
    conf.pTx = &gTxDesc;
    conf.pRx = &gRxDesc;
    conf.rfProcessCB = RF_ProcessCallBack;
    conf.processMask = RF_STATE_RX | RF_STATE_RX_CRCERR | RF_STATE_TX_FINISH | RF_STATE_TIMEOUT;
    ret = RFRole_FastInit(&conf);
    g_ret_fast_init = (uint8_t)ret;
    rx_debug_log("[RX][FAST] init:%u\r\n", ret);

    tmos_memset(&gFastHostCfg, 0, sizeof(gFastHostCfg));
    tmos_memset(gHostBindList, 0, sizeof(gHostBindList));
    tmos_memset(&gHostSpeedList, 0, sizeof(gHostSpeedList));

    gHostBindList[0].deviceId = RF_ROLE_ID_INVALD;
    gHostBindList[0].rssi = 0;
    gHostBindList[0].devType = 0;
#if (RF_FAST_USE_EXPLICIT_INFO == 1)
    memcpy(gHostBindList[0].peerInfo, gFastTxPeerInfo, sizeof(gHostBindList[0].peerInfo));
#endif
    gHostSpeedList.number = 1;
    gHostSpeedList.pList = gHostBindList;
    ret = RFBound_SetSpeedType(&gHostSpeedList);
    g_ret_speed_list = (uint8_t)ret;
    rx_debug_log("[RX][FAST] speed:%u\r\n", ret);

#if (RF_FAST_USE_FREQUENCY_LIST == 1)
    tmos_memset(&gBoundFreList, 0, sizeof(gBoundFreList));
    gBoundFreList.number = 1;
    gBoundFreList.pFrequency = gBoundFrequencyList;
    ret = RFBound_SetFrequencyList(&gBoundFreList);
    g_ret_freq_list = (uint8_t)ret;
    rx_debug_log("[RX][FAST] freq:%u ch:%lu\r\n", ret, gBoundFrequencyList[0]);
#else
    g_ret_freq_list = 0xFEu;
    rx_debug_log("[RX][FAST] freq:skip\r\n");
#endif

    gFastHostCfg.hop = RF_HOP_OFF;
    gFastHostCfg.periTime = 8;
    gFastHostCfg.devType = 0;
    gFastHostCfg.timeout = RF_FAST_BOUND_TIMEOUT_MS;
#if (RF_FAST_USE_EXPLICIT_INFO == 1)
    memcpy(gFastHostCfg.OwnInfo, gFastRxOwnInfo, sizeof(gFastHostCfg.OwnInfo));
    memcpy(gFastHostCfg.PeerInfo, gFastTxPeerInfo, sizeof(gFastHostCfg.PeerInfo));
#endif
    gFastHostCfg.rfBoundCB = rf_bound_callback;
    gFastHostCfg.ChannelMap = 0x1FFFFFFFUL;
    ret = RFBound_Start8kHost(&gFastHostCfg);
    g_ret_start_host = (uint8_t)ret;
    rx_debug_log("[RX][FAST] start_host:%u explicit:%u\r\n", ret, RF_FAST_USE_EXPLICIT_INFO);

    if(ret != SUCCESS)
    {
        rx_debug_log("[RX][FAST] fallback_basic\r\n");
        conf.TxPower = BLE_TX_POWER;
        conf.rfProcessCB = RF_ProcessCallBack;
        conf.processMask = RF_STATE_RX | RF_STATE_RX_CRCERR | RF_STATE_TX_FINISH | RF_STATE_TIMEOUT;
        if(RFRole_BasicInit(&conf) == SUCCESS)
        {
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
            rf_rx_start();
        }
    }
}

__HIGH_CODE
void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id)
{
    (void)id;

    if(sta & RF_STATE_RX)
    {
        gStat.rx_total++;
        gStat.rx_ok++;
        rf_rx_start();
    }
    if(sta & RF_STATE_RX_CRCERR)
    {
        gStat.rx_total++;
        gStat.rx_fail++;
        rf_rx_start();
    }
    if(sta & RF_STATE_TIMEOUT)
    {
        gStat.rx_fail++;
        rf_rx_start();
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
#if (RF_USE_LOW_LEVEL_BASIC == 1)
        return events ^ SBP_RF_STAT_EVT;
#else
        uint32_t rx_total = gStat.rx_total;
        uint32_t rx_ok = gStat.rx_ok;
        uint32_t rx_fail = gStat.rx_fail;
        uint32_t tx_ok = gStat.tx_ok;
        uint32_t now_clock = TMOS_GetSystemClock();
        uint32_t dt_ticks = now_clock - g_last_stat_clock;
        uint32_t dt_ms = (uint32_t)(((uint64_t)dt_ticks * SYSTEM_TIME_MICROSEN) / 1000u);

        if(dt_ms == 0u)
        {
            dt_ms = 1u;
        }

        rx_debug_log("[RX][5s] dt:%lums rx:%lu ok:%lu fail:%lu rx_hz:%lu txok:%lu bound:%lu ok:%u sta:%u role:%u id:%u type:%u hop:%u start:%u basic:%u cfg:%u ch:%u rxret:%u\r\n",
                     dt_ms,
                     rx_total,
                     rx_ok,
                     rx_fail,
                     (uint32_t)(((uint64_t)rx_ok * 1000u) / dt_ms),
                     tx_ok,
                     g_bound_cb_cnt,
                     (unsigned int)g_host_bound_ok,
                     (unsigned int)g_bound_status,
                     (unsigned int)g_bound_role,
                     (unsigned int)g_bound_id,
                     (unsigned int)g_bound_type,
                     (unsigned int)g_bound_hop,
                     (unsigned int)g_ret_start_host,
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
        g_last_stat_clock = now_clock;

        tmos_start_task(taskID, SBP_RF_STAT_EVT, MS1_TO_SYSTEM_TIME(RF_STAT_PRINT_PERIOD_MS));
        return events ^ SBP_RF_STAT_EVT;
#endif
    }

    if(events & SBP_RF_RF_RX_EVT)
    {
        rf_rx_start();
        return events ^ SBP_RF_RF_RX_EVT;
    }

    if(events & SBP_RF_START_DEVICE_EVT)
    {
        g_host_restart_count++;
        rf_fast_start_host();
        return events ^ SBP_RF_START_DEVICE_EVT;
    }

    return 0;
}

uint16_t RF_GetStatsLine(char *buf, uint16_t len)
{
    static uint32_t last_clock = 0;
    static uint32_t last_ok = 0;
    static uint32_t last_fail = 0;
    uint32_t now_clock;
    uint32_t dt_ticks;
    uint32_t dt_ms;
    uint32_t ok_total;
    uint32_t fail_total;
    uint32_t ok_delta;
    uint32_t fail_delta;
    int n;

    if((buf == NULL) || (len == 0u))
    {
        return 0;
    }

    now_clock = TMOS_GetSystemClock();
    ok_total = gStat.rx_ok;
    fail_total = gStat.rx_fail;

    if(last_clock == 0u)
    {
        last_clock = g_last_stat_clock;
        last_ok = 0u;
        last_fail = 0u;
    }

    dt_ticks = now_clock - last_clock;
    dt_ms = (uint32_t)(((uint64_t)dt_ticks * SYSTEM_TIME_MICROSEN) / 1000u);
    if(dt_ms == 0u)
    {
        dt_ms = 1u;
    }

    ok_delta = ok_total - last_ok;
    fail_delta = fail_total - last_fail;
    n = snprintf(buf, len,
                 "[RX][5s] dt:%lu ok:%lu fail:%lu hz:%lu cfg:%u r:%u\r\n",
                 dt_ms,
                 ok_delta,
                 fail_delta,
                 (uint32_t)(((uint64_t)ok_delta * 1000u) / dt_ms),
                 (unsigned int)g_low_config_ret,
                 (unsigned int)g_low_rx_ret);

    last_clock = now_clock;
    last_ok = ok_total;
    last_fail = fail_total;

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

    PFIC_EnableIRQ(BLEB_IRQn);
    PFIC_EnableIRQ(BLEL_IRQn);

#if (RF_USE_FAST_8K == 1)
    tmos_start_task(taskID, SBP_RF_STAT_EVT, MS1_TO_SYSTEM_TIME(RF_STAT_PRINT_PERIOD_MS));
#endif

    g_last_stat_clock = TMOS_GetSystemClock();

#if (RF_USE_LOW_LEVEL_BASIC == 1)
    rf_low_level_basic_start_rx();
#elif (RF_USE_FAST_8K == 1)
    g_evt_set_ret = tmos_set_event(taskID, SBP_RF_START_DEVICE_EVT);
    rx_debug_log("[RX][FAST] boot task:%u start_evt:%u explicit:%u\r\n",
                 (unsigned int)g_task_id_dbg,
                 (unsigned int)g_evt_set_ret,
                 RF_FAST_USE_EXPLICIT_INFO);
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

    rf_rx_start();
    g_basic_started = 1;
    rx_debug_log("[RX][RFIP] cfg:%u ch:%u rxret:%u\r\n",
                 (unsigned int)g_low_config_ret,
                 (unsigned int)g_low_channel,
                 (unsigned int)g_low_rx_ret);
#endif
}

/******************************** endfile @ RF_PHY ******************************/
