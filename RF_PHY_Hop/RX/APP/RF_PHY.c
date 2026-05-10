/********************************** (C) COPYRIGHT *******************************
 * File Name          : RF_PHY.c
 * Description        : 8K test flow based on WCH RF_Basic style
 *******************************************************************************/

#include "CONFIG.h"
#include "RF_PHY.h"
#include "HAL.h"
#include "wchrf.h"
#include "ch585_usbhs_device.h"
#include <stdio.h>
#include <string.h>

#ifndef RF_HOP_MODE
#define RF_HOP_MODE 2
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
static rfBoundDevice_t gFastDevCfg;
static volatile uint8_t g_bound_status = 0xFF;
static volatile uint8_t g_bound_role = 0xFF;
static volatile uint8_t g_bound_hop = 0xFF;
static volatile uint32_t g_bound_cb_cnt = 0;
static volatile uint8_t g_ret_switch_fast = 0xFF;
static volatile uint8_t g_ret_fast_init = 0xFF;
static volatile uint8_t g_ret_start_dev = 0xFF;
static volatile uint8_t g_ret_role_init = 0xFF;
static volatile uint8_t g_evt_set_ret = 0xFF;
static volatile uint8_t g_task_id_dbg = 0xFF;
static volatile uint8_t g_basic_started = 0;

void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id);

__attribute__((__aligned__(4))) static uint8_t TxBuf[64];
__attribute__((__aligned__(4))) static uint8_t RxBuf[264];

static void rf_cdc_log_5s(uint32_t rx_ok_5s, uint32_t rx_total_5s, uint32_t rx_fail_5s)
{
    char msg[96];
    int len;

    if(USBHS_DevEnumStatus == 0)
    {
        return;
    }
    if((USBHS_Endp_Busy[DEF_UEP5] & DEF_UEP_BUSY) != 0)
    {
        return;
    }

    (void)rx_total_5s;
    (void)rx_fail_5s;

    len = sprintf(msg,
                  "[5s]o:%lu b:%u c:%lu s:%u f:%u r:%u i:%u e:%u\r\n",
                  rx_ok_5s, g_ret_start_dev, g_bound_cb_cnt,
                  g_ret_switch_fast, g_ret_fast_init,
                  g_ret_role_init, g_task_id_dbg, g_evt_set_ret);
    if(len <= 0)
    {
        return;
    }
    if(len > 64)
    {
        len = 64;
    }

    if(USBHS_Endp_DataUp(DEF_UEP5, (uint8_t *)msg, (uint16_t)len, DEF_UEP_CPY_LOAD) != 0)
    {
        /* Endpoint busy or invalid len, skip this period. */
        return;
    }
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
        g_bound_status = sta->status;
        g_bound_role = sta->role;
        g_bound_hop = sta->hop;
        g_bound_cb_cnt++;
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

static void rf_fast_start_device(void)
{
    rfRoleConfig_t conf = {0};
    bStatus_t ret;

    rf_fill_payload();
    rf_dma_desc_init();

    ret = RFRole_SwitchMode(RFIP_MODE_RF_FAST);
    g_ret_switch_fast = (uint8_t)ret;
    PRINT("RFRole_SwitchMode FAST ret:%u\n", ret);

    conf.TxPower = BLE_TX_POWER;
    conf.pTx = &gTxDesc;
    conf.pRx = &gRxDesc;
    conf.rfProcessCB = RF_ProcessCallBack;
    conf.processMask = RF_STATE_RX | RF_STATE_RX_CRCERR | RF_STATE_TX_FINISH | RF_STATE_TIMEOUT;
    ret = RFRole_FastInit(&conf);
    g_ret_fast_init = (uint8_t)ret;
    PRINT("RFRole_FastInit ret:%u\n", ret);

    tmos_memset(&gFastDevCfg, 0, sizeof(gFastDevCfg));
    gFastDevCfg.devType = 0;
    gFastDevCfg.deviceId = RF_ROLE_ID_INVALD;
    gFastDevCfg.speed = 8;
    gFastDevCfg.timeout = 1000;
    gFastDevCfg.rfBoundCB = rf_bound_callback;
    ret = RFBound_Start8kDevice(&gFastDevCfg);
    g_ret_start_dev = (uint8_t)ret;
    PRINT("RFBound_Start8kDevice ret:%u\n", ret);

    if(ret != SUCCESS)
    {
        PRINT("FAST device start failed, fallback to BASIC fixed channel.\n");
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
            gRxParam.rxMaxLen = 251;
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
        rf_cdc_log_5s(gStat.rx_ok, gStat.rx_total, gStat.rx_fail);

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
        rf_fast_start_device();
        return events ^ SBP_RF_START_DEVICE_EVT;
    }

    return 0;
}

__HIGH_CODE
void RF_TxMainLoopProcess(void)
{
    return;
}

void RF_SetRoleInitStatus(uint8_t status)
{
    g_ret_role_init = status;
}

void RF_Init(void)
{
    taskID = TMOS_ProcessEventRegister(RF_ProcessEvent);
    g_task_id_dbg = taskID;

    PFIC_EnableIRQ(BLEB_IRQn);
    PFIC_EnableIRQ(BLEL_IRQn);

    tmos_start_task(taskID, SBP_RF_STAT_EVT, MS1_TO_SYSTEM_TIME(RF_STAT_PRINT_PERIOD_MS));

    PRINT("RF FAST 8K RX mode start.\n");

#if (RF_USE_FAST_8K == 1)
    g_evt_set_ret = tmos_set_event(taskID, SBP_RF_START_DEVICE_EVT);
#else
    conf.TxPower = BLE_TX_POWER;
    conf.rfProcessCB = RF_ProcessCallBack;
    conf.processMask = RF_STATE_RX | RF_STATE_RX_CRCERR | RF_STATE_TX_FINISH | RF_STATE_TIMEOUT;
    RFRole_BasicInit(&conf);

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

    rf_rx_start();
#endif
}

/******************************** endfile @ RF_PHY ******************************/
