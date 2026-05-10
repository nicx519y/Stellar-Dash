/********************************** (C) COPYRIGHT *******************************
 * File Name          : RF_PHY.c
 * Description        : 8K test flow based on WCH RF_Basic style
 *******************************************************************************/

#include "CONFIG.h"
#include "RF_PHY.h"
#include "HAL.h"
#include "wchrf.h"

#ifndef RF_HOP_MODE
#define RF_HOP_MODE 1
#endif

#define RF_TEST_FREQUENCY           16
#define RF_TEST_DATA_LEN            10
#define RF_REPORT_PPS               8000
#define RF_STAT_PRINT_PERIOD_MS     5000
#define TMR0_FREE_RUN_END           0x03FFFFFFUL
#define RF_USE_FAST_8K              1

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
static volatile uint8_t g_fast_started = 0;
static volatile uint8_t g_basic_started = 0;

void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id);

__attribute__((__aligned__(4))) static uint8_t TxBuf[64];
__attribute__((__aligned__(4))) static uint8_t RxBuf[264];

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
    gRxParam.timeOut = 0;
    RFIP_SetRx(&gRxParam);
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
        PRINT("bound sta:%u role:%u id:%u type:%u hop:%u\n",
              sta->status, sta->role, sta->devId, sta->devType, sta->hop);
    }
}

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

static void rf_basic_start_tx(void)
{
    rfRoleConfig_t conf = {0};

    conf.TxPower = BLE_TX_POWER;
    conf.rfProcessCB = RF_ProcessCallBack;
    conf.processMask = RF_STATE_TX_FINISH | RF_STATE_TIMEOUT;
    if(RFRole_BasicInit(&conf) != SUCCESS)
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
    gRxParam.rxMaxLen = 251;
    gRxParam.timeOut = 0;

    g_basic_started = 1;
}

static void rf_fast_start_host(void)
{
    rfRoleConfig_t conf = {0};
    bStatus_t ret;

    rf_fill_payload();
    rf_dma_desc_init();

    ret = RFRole_SwitchMode(RFIP_MODE_RF_FAST);
    PRINT("RFRole_SwitchMode FAST ret:%u\n", ret);

    conf.TxPower = BLE_TX_POWER;
    conf.pTx = &gTxDesc;
    conf.pRx = &gRxDesc;
    conf.rfProcessCB = RF_ProcessCallBack;
    conf.processMask = RF_STATE_TX_FINISH | RF_STATE_TIMEOUT;
    ret = RFRole_FastInit(&conf);
    PRINT("RFRole_FastInit ret:%u\n", ret);

    tmos_memset(&gFastHostCfg, 0, sizeof(gFastHostCfg));
    gFastHostCfg.hop = RF_HOP_OFF;
    gFastHostCfg.periTime = 8;
    gFastHostCfg.devType = 0;
    gFastHostCfg.timeout = 1000;
    gFastHostCfg.rfBoundCB = rf_bound_callback;
    gFastHostCfg.ChannelMap = 0x1FFFFFFFUL;
    ret = RFBound_Start8kHost(&gFastHostCfg);
    PRINT("RFBound_Start8kHost ret:%u\n", ret);

    g_fast_started = (ret == SUCCESS) ? 1 : 0;
    if(g_fast_started == 0)
    {
        PRINT("FAST host start failed, fallback to BASIC fixed channel.\n");
        rf_basic_start_tx();
    }
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
        uint32_t tmr_tick_win = gStat.tmr_tick_win;
        uint32_t sched_due_win = gStat.sched_due_win;
        uint32_t sched_sent_win = gStat.sched_sent_win;
        uint32_t sched_miss_win = gStat.sched_miss_win;
        uint32_t tx_ok = gStat.tx_ok;
        uint32_t tx_try = gStat.tx_try;
        uint32_t sent_rate = (sched_due_win != 0) ? ((sched_sent_win * 100U) / sched_due_win) : 0;

        PRINT("[5s] tick:%lu due:%lu sent:%lu miss:%lu txok:%lu/%lu sent_rate:%lu%%\n",
              tmr_tick_win, sched_due_win, sched_sent_win, sched_miss_win, tx_ok, tx_try, sent_rate);

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

        tmos_start_task(taskID, SBP_RF_STAT_EVT, MS1_TO_SYSTEM_TIME(RF_STAT_PRINT_PERIOD_MS));
        return events ^ SBP_RF_STAT_EVT;
    }

    if(events & SBP_RF_START_DEVICE_EVT)
    {
        rf_fast_start_host();
        return events ^ SBP_RF_START_DEVICE_EVT;
    }

    return 0;
}

__HIGH_CODE
void RF_TxMainLoopProcess(void)
{
    uint32_t now;
    uint32_t delta;
    uint32_t due;

    if((g_fast_started == 0) && (g_basic_started == 0))
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
}

void RF_Init(void)
{
    taskID = TMOS_ProcessEventRegister(RF_ProcessEvent);

    PFIC_EnableIRQ(BLEB_IRQn);
    PFIC_EnableIRQ(BLEL_IRQn);

    tmos_start_task(taskID, SBP_RF_STAT_EVT, MS1_TO_SYSTEM_TIME(RF_STAT_PRINT_PERIOD_MS));

    PRINT("RF FAST 8K TX mode start.\n");
    TMR0_TimerInit(TMR0_FREE_RUN_END);
    g_tmr_prev_cnt = TMR0_GetCurrentTimer();
    g_tmr_acc_tick = 0;
    g_tick_per_evt = GetSysClock() / RF_REPORT_PPS;
    if(g_tick_per_evt == 0)
    {
        g_tick_per_evt = 1;
    }

#if (RF_USE_FAST_8K == 1)
    tmos_set_event(taskID, SBP_RF_START_DEVICE_EVT);
#else
    rf_basic_start_tx();
#endif
}

/******************************** endfile @ RF_PHY ******************************/
