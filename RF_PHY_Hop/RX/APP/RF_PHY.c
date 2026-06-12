/********************************** (C) COPYRIGHT *******************************
 * File Name          : RF_PHY.c
 * Description        : RX side for the fixed-channel 8K 7+1 ACK protocol.
 *******************************************************************************/

#include "CONFIG.h"
#include "RF_PHY.h"
#include "HAL.h"
#include "wchrf.h"
#include "rf_hop_protocol.h"
#include "dongle_config.h"
#include "ch585_usbhs_device.h"
#include "usbd_compatibility_hid.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifndef RF_AUTO_ACK_DEMO_ENABLE
#define RF_AUTO_ACK_DEMO_ENABLE        1u
#endif

#if (RF_AUTO_ACK_DEMO_ENABLE == 1u)

#define RF_AUTO_DEMO_PACKET_LEN        RFH_AIR_PACKET_LEN
#define RF_AUTO_DEMO_DATA_TYPE         0xFFu
#define RF_AUTO_DEMO_ACK_TYPE          0xFFu
#define RF_AUTO_DEMO_CHANNEL           39u
#define RF_AUTO_DEMO_FREQUENCY_KHZ     2480000UL
#define RF_AUTO_DEMO_LLE_MODE          LLE_MODE_AUTO
#define RF_LINK_CRC_INIT               0x555555UL
#define TMR0_FREE_RUN_WRAP             0x04000000UL

typedef struct
{
    volatile uint32_t rx_arm;
    volatile uint32_t rx_arm_fail;
    volatile uint32_t data_ok;
    volatile uint32_t data_crc_err;
    volatile uint32_t data_type_err;
    volatile uint32_t ack_finish;
    volatile uint32_t ack_fail;
} rf_auto_demo_stat_t;

uint8_t taskID;

__attribute__((__aligned__(4))) static uint8_t TxBuf[RF_AUTO_DEMO_PACKET_LEN];

static rf_auto_demo_stat_t g_demo_stat;
static volatile uint8_t g_demo_config_ret = 0xFFu;
static volatile uint8_t g_demo_rearm_pending = 0u;
static volatile uint8_t g_demo_rx_active = 0u;
static uint8_t g_demo_ack_seq = 0u;

static void demo_fill_ack_packet(void)
{
    uint8_t i;

    memset(TxBuf, 0, sizeof(TxBuf));
    TxBuf[0] = rfh_make_header0(RFH_PKT_ACK, RFH_RATE_8K, RFH_FLAG_LINK_OK);
    TxBuf[1] = g_demo_ack_seq;
    TxBuf[2] = (uint8_t)(g_demo_stat.data_ok & 0xFFu);
    TxBuf[3] = (uint8_t)((g_demo_stat.data_ok >> 8) & 0xFFu);
    for(i = 4u; i < RF_AUTO_DEMO_PACKET_LEN; i++)
    {
        TxBuf[i] = (uint8_t)(0xC0u + i);
    }
}

static void demo_arm_rx(void)
{
    bStatus_t ret;

    if(g_demo_config_ret != SUCCESS)
    {
        return;
    }
    if(g_demo_rx_active != 0u)
    {
        return;
    }

    demo_fill_ack_packet();
    ret = RF_Rx(TxBuf,
                RF_AUTO_DEMO_PACKET_LEN,
                RF_AUTO_DEMO_DATA_TYPE,
                RF_AUTO_DEMO_ACK_TYPE);
    g_demo_stat.rx_arm++;
    if(ret == SUCCESS)
    {
        g_demo_rx_active = 1u;
        g_demo_ack_seq++;
    }
    else
    {
        g_demo_stat.rx_arm_fail++;
        g_demo_rearm_pending = 1u;
    }
}

static void RF_AutoDemoStatusCallBack(uint8_t sta, uint8_t rsr, uint8_t *rxBuf)
{
    switch(sta)
    {
        case RX_MODE_RX_DATA:
            g_demo_rx_active = 0u;
            if(rsr == 0u)
            {
                g_demo_stat.data_ok++;
            }
            else
            {
                if((rsr & (1u << 0)) != 0u)
                {
                    g_demo_stat.data_crc_err++;
                }
                if((rsr & (1u << 1)) != 0u)
                {
                    g_demo_stat.data_type_err++;
                }
            }
            (void)rxBuf;
            break;

        case RX_MODE_TX_FINISH:
            g_demo_stat.ack_finish++;
            g_demo_rearm_pending = 1u;
            break;

        case RX_MODE_TX_FAIL:
            g_demo_stat.ack_fail++;
            g_demo_rearm_pending = 1u;
            break;

        default:
            break;
    }
}

void RF_Service(void)
{
    if(g_demo_rearm_pending != 0u)
    {
        g_demo_rearm_pending = 0u;
        demo_arm_rx();
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
    return 0u;
}

uint8_t RF_StartPairing(void)
{
    return 1u;
}

uint8_t RF_StopPairing(void)
{
    return 1u;
}

uint8_t RF_IsPairingActive(void)
{
    return 0u;
}

rf_indicator_mode_t RF_GetIndicatorMode(void)
{
    if(g_demo_config_ret != SUCCESS)
    {
        return RF_INDICATOR_OFF;
    }
    return (g_demo_stat.data_ok != 0u) ? RF_INDICATOR_BLINK_500MS : RF_INDICATOR_BLINK_2000MS;
}

void RF_StartPacketLossScan(void)
{
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
    return 1u;
}

uint16_t RF_GetStatsLine(char *buf, uint16_t len)
{
    int written;

    if((buf == NULL) || (len == 0u))
    {
        return 0u;
    }

    written = snprintf(buf,
                       len,
                       "RA c%u m%02X r%lu/%lu d%lu a%lu/%lu e%lu/%lu v%u\r\n",
                       (unsigned int)g_demo_config_ret,
                       (unsigned int)RF_AUTO_DEMO_LLE_MODE,
                       (unsigned long)g_demo_stat.rx_arm,
                       (unsigned long)g_demo_stat.rx_arm_fail,
                       (unsigned long)g_demo_stat.data_ok,
                       (unsigned long)g_demo_stat.ack_finish,
                       (unsigned long)g_demo_stat.ack_fail,
                       (unsigned long)g_demo_stat.data_crc_err,
                       (unsigned long)g_demo_stat.data_type_err,
                       (unsigned int)g_demo_rx_active);
    if(written < 0)
    {
        return 0u;
    }

    g_demo_stat.rx_arm = 0u;
    g_demo_stat.rx_arm_fail = 0u;
    g_demo_stat.data_ok = 0u;
    g_demo_stat.data_crc_err = 0u;
    g_demo_stat.data_type_err = 0u;
    g_demo_stat.ack_finish = 0u;
    g_demo_stat.ack_fail = 0u;

    return (uint16_t)((written >= (int)len) ? (len - 1u) : (uint16_t)written);
}

uint8_t RF_TrySendTelemetryReport(void)
{
    return 0u;
}

void RF_Init(void)
{
    rfConfig_t rf_config;

    taskID = TMOS_ProcessEventRegister(RF_ProcessEvent);
    PFIC_EnableIRQ(BLEB_IRQn);
    PFIC_EnableIRQ(BLEL_IRQn);

    TMR0_TimerInit(TMR0_FREE_RUN_WRAP - 1u);

    memset(&rf_config, 0, sizeof(rf_config));
    rf_config.accessAddress = RFH_LINK_ACCESS_ADDRESS_DEFAULT;
    rf_config.CRCInit = RF_LINK_CRC_INIT;
    rf_config.Channel = RF_AUTO_DEMO_CHANNEL;
    rf_config.Frequency = RF_AUTO_DEMO_FREQUENCY_KHZ;
    rf_config.LLEMode = RF_AUTO_DEMO_LLE_MODE;
    rf_config.rfStatusCB = RF_AutoDemoStatusCallBack;
    rf_config.RxMaxlen = RF_AUTO_DEMO_PACKET_LEN;
    g_demo_config_ret = RF_Config(&rf_config);
    demo_arm_rx();
}

#else

#define RF_TX_SEND_TIME                RFH_TX_SEND_TIME_UNITS
#define RF_LINK_ACCESS_ADDRESS         RFH_LINK_ACCESS_ADDRESS_DEFAULT
#define RF_LINK_CRC_INIT               0x555555UL
#define RFIP_RX_NO_TIMEOUT             0u
#define TMR0_FREE_RUN_WRAP             0x04000000UL
#define RF_SWITCH_TEST_ENABLE          1u
#define RF_SWITCH_TEST_SEND_US         125u
#define RF_SWITCH_TEST_CYCLE_TICKS     8000u
#define RF_SWITCH_TEST_DEFAULT_ACK_TICKS 800u
#define RF_SWITCH_TEST_MARK_TX         0x54u
#define RF_SWITCH_TEST_MARK_RX         0x52u
#define RX_ACK_PHASE_SCAN_ENABLE       1u
#define RX_ACK_MIN_ARM_US              1
#define RX_HID_REPORT_MAGIC            0x48u
#define RX_HID_REPORT_VERSION          0x01u
#define RX_HID_TELEMETRY_MAGIC         0x314D4852UL

#define SBP_RF_RF_RX_EVT               4
#define SBP_RF_STAT_EVT                (1 << 5)

typedef struct
{
    volatile uint32_t rx_ok;
    volatile uint32_t rx_bad_len;
    volatile uint32_t rx_bad_type;
    volatile uint32_t rx_bad_slot;
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
} rf_stat_t;

uint8_t taskID;

static rfRoleParam_t gParm;
static rfipTx_t gTxParam;
static rfipRx_t gRxParam;
static rf_stat_t gStat = {0};

__attribute__((__aligned__(4))) static uint8_t TxBuf[64];
__attribute__((__aligned__(4))) static uint8_t RxBuf[264];

static volatile uint8_t g_basic_started = 0u;
static volatile uint8_t g_ack_tx_active = 0u;
static volatile uint8_t g_low_config_ret = 0xFFu;
static volatile uint8_t g_low_rx_ret = 0xFFu;
static volatile uint8_t g_low_tx_ret = 0xFFu;

static uint8_t g_group_valid = 0u;
static uint8_t g_current_group = 0u;
static uint8_t g_received_bitmap = 0u;
static uint8_t g_ack_pending = 0u;
static uint8_t g_ack_sent_for_group = 0u;
static uint8_t g_pending_ack_bitmap = RFH_ACK_MISSING_MASK;

static uint32_t g_total_groups = 0u;
static uint32_t g_total_rx_data = 0u;
static uint32_t g_total_expected_data = 0u;
static uint32_t g_total_ack_tx = 0u;
static uint32_t g_total_missing_packets = 0u;
static uint32_t g_total_bad_seq = 0u;
static uint32_t g_total_errors = 0u;

static uint32_t g_log_groups = 0u;
static uint32_t g_log_rx_data = 0u;
static uint32_t g_log_expected_data = 0u;
static uint32_t g_log_ack_tx = 0u;
static uint32_t g_log_missing_packets = 0u;
static uint32_t g_log_bad_seq = 0u;
static uint32_t g_log_errors = 0u;
static uint32_t g_log_first_slot_hist[RFH_DATA_SLOTS] = {0};
static uint8_t g_log_first_slot_mask = 0u;

#if (RX_ACK_PHASE_SCAN_ENABLE == 1)
static const int16_t g_ack_phase_scan_offsets_us[] = {
    -200, -175, -150, -125, -100, -75, -50, -25, 0,
    25, 50, 75, 100, 125, 150, 175, 200, 225, 250, 275, 300
};
static uint8_t g_ack_phase_scan_index = 0u;
#endif

#if (RF_SWITCH_TEST_ENABLE == 1)
static uint16_t g_sw_tick = 0u;
static uint8_t g_sw_role_tx = 0u;
static uint8_t g_sw_rx_active = 0u;
static uint8_t g_sw_seq = 0u;
static uint32_t g_sw_log_tx = 0u;
static uint32_t g_sw_log_rx = 0u;
static uint32_t g_sw_log_bad = 0u;
static uint32_t g_sw_log_switch = 0u;
static uint32_t g_sw_log_sync = 0u;
static uint32_t g_sw_total_rx = 0u;
static uint16_t g_sw_ack_ticks = RF_SWITCH_TEST_DEFAULT_ACK_TICKS;
#endif

static uint32_t g_hid_telemetry_seq = 0u;
static xinput_report_t g_current_xinput_report;
static xinput_report_t g_pending_xinput_report;
static uint8_t g_current_hid_report[HID_ENDPOINT_SIZE];
static uint8_t g_pending_hid_report[HID_ENDPOINT_SIZE];
static uint8_t g_input_payload_buffer[2][RF_INPUT_PAYLOAD_LEN];
static volatile uint8_t g_input_payload_active_index = 0u;
static volatile uint32_t g_input_payload_generation = 0u;
static uint32_t g_input_payload_served_generation = 0u;
static uint8_t g_xinput_pending = 0u;
static uint8_t g_hid_pending = 0u;
static uint8_t g_input_seen_valid = 0u;
static uint8_t g_last_input_seq = 0u;
static uint32_t g_last_input_key_mask = 0u;
static uint32_t g_last_valid_input_clock = 0u;

void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id);

static uint8_t rx_popcount7(uint8_t value)
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

static uint16_t rx_loss_permille(uint32_t missing, uint32_t expected)
{
    if(expected == 0u)
    {
        return 0u;
    }
    return (uint16_t)((missing * 1000u) / expected);
}

static void rx_note_error(void)
{
    g_total_errors++;
    g_log_errors++;
}

#if (RF_SWITCH_TEST_ENABLE != 1)
static uint8_t rx_log_dominant_first_slot(void)
{
    uint8_t i;
    uint8_t best_slot = 0u;
    uint32_t best_count = 0u;

    for(i = 0u; i < RFH_DATA_SLOTS; i++)
    {
        if(g_log_first_slot_hist[i] > best_count)
        {
            best_count = g_log_first_slot_hist[i];
            best_slot = i;
        }
    }

    return best_slot;
}
#endif

static uint32_t rx_us_to_tmr_cycles(uint32_t us)
{
    uint32_t cycles_per_us = GetSysClock() / 1000000u;
    uint32_t cycles;
    if(cycles_per_us == 0u)
    {
        cycles_per_us = 1u;
    }
    cycles = cycles_per_us * us;
    return (cycles == 0u) ? 1u : cycles;
}

static void rx_ack_timer_cancel(void)
{
    TMR1_ITCfg(DISABLE, TMR0_3_IT_CYC_END);
    TMR1_ClearITFlag(TMR0_3_IT_CYC_END);
    TMR1_Disable();
}

static void rx_ack_timer_arm_us(uint32_t due_us)
{
    uint32_t cycles = rx_us_to_tmr_cycles(due_us);

    rx_ack_timer_cancel();
    TMR1_TimerInit(cycles);
    TMR1_ClearITFlag(TMR0_3_IT_CYC_END);
    TMR1_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
}

static int16_t rx_ack_phase_offset_us(void)
{
#if (RX_ACK_PHASE_SCAN_ENABLE == 1)
    return g_ack_phase_scan_offsets_us[g_ack_phase_scan_index];
#else
    return 0;
#endif
}

static void rx_ack_phase_scan_advance(void)
{
#if (RX_ACK_PHASE_SCAN_ENABLE == 1)
    g_ack_phase_scan_index++;
    if(g_ack_phase_scan_index >=
       (uint8_t)(sizeof(g_ack_phase_scan_offsets_us) /
                 sizeof(g_ack_phase_scan_offsets_us[0])))
    {
        g_ack_phase_scan_index = 0u;
    }
#endif
}

static uint32_t rx_input_stale_ticks(void)
{
    uint32_t ticks = (INPUT_STALE_TIMEOUT_US + SYSTEM_TIME_MICROSEN - 1u) /
                     SYSTEM_TIME_MICROSEN;
    return (ticks == 0u) ? 1u : ticks;
}

static uint8_t rx_input_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0u;
    uint8_t i;
    uint8_t bit;

    for(i = 0u; i < len; i++)
    {
        crc ^= data[i];
        for(bit = 0u; bit < 8u; bit++)
        {
            if((crc & 0x80u) != 0u)
            {
                crc = (uint8_t)((crc << 1) ^ 0x07u);
            }
            else
            {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

static void rx_make_clear_xinput_report(xinput_report_t *report)
{
    memset(report, 0, sizeof(*report));
    report->report_id = 0u;
    report->report_size = XINPUT_ENDPOINT_SIZE;
}

static void rx_make_clear_hid_report(uint8_t *report)
{
    memset(report, 0, HID_ENDPOINT_SIZE);
    report[0] = RX_HID_REPORT_MAGIC;
    report[1] = RX_HID_REPORT_VERSION;
}

static uint8_t rx_try_submit_xinput_report(const xinput_report_t *report)
{
    if(USBHS_DevEnumStatus == 0u)
    {
        return 0u;
    }
    if((USBHS_Endp_Busy[DEF_UEP2] & DEF_UEP_BUSY) != 0u)
    {
        return 0u;
    }
    return (USBHS_Endp_DataUp(DEF_UEP2,
                              (uint8_t *)report,
                              (uint16_t)sizeof(*report),
                              DEF_UEP_CPY_LOAD) == 0u) ? 1u : 0u;
}

static uint8_t rx_try_submit_hid_report(const uint8_t *report)
{
    if(USBHS_DevEnumStatus == 0u)
    {
        return 0u;
    }
    if((USBHS_Endp_Busy[DEF_UEP6] & DEF_UEP_BUSY) != 0u)
    {
        return 0u;
    }
    return (USBHS_Endp_DataUp(DEF_UEP6,
                              (uint8_t *)report,
                              HID_ENDPOINT_SIZE,
                              DEF_UEP_CPY_LOAD) == 0u) ? 1u : 0u;
}

static void rx_queue_xinput_report(const xinput_report_t *report)
{
    if(memcmp(&g_current_xinput_report, report, sizeof(*report)) == 0)
    {
        return;
    }
    memcpy(&g_current_xinput_report, report, sizeof(*report));
    if(rx_try_submit_xinput_report(report) != 0u)
    {
        g_xinput_pending = 0u;
        return;
    }
    memcpy(&g_pending_xinput_report, report, sizeof(*report));
    g_xinput_pending = 1u;
}

static void rx_queue_hid_report(const uint8_t *report)
{
    if(memcmp(g_current_hid_report, report, HID_ENDPOINT_SIZE) == 0)
    {
        return;
    }
    memcpy(g_current_hid_report, report, HID_ENDPOINT_SIZE);
    memcpy(HID_Report_Buffer, report, HID_ENDPOINT_SIZE);
    if(rx_try_submit_hid_report(report) != 0u)
    {
        g_hid_pending = 0u;
        return;
    }
    memcpy(g_pending_hid_report, report, HID_ENDPOINT_SIZE);
    g_hid_pending = 1u;
}

static void rx_flush_xinput_pending(void)
{
    if((g_xinput_pending != 0u) &&
       (rx_try_submit_xinput_report(&g_pending_xinput_report) != 0u))
    {
        g_xinput_pending = 0u;
    }
}

static void rx_flush_hid_pending(void)
{
    if((g_hid_pending != 0u) &&
       (rx_try_submit_hid_report(g_pending_hid_report) != 0u))
    {
        g_hid_pending = 0u;
    }
}

static void rx_make_hid_report(uint8_t seq,
                               uint8_t flags,
                               uint32_t key_mask,
                               const uint8_t *raw_payload,
                               uint8_t *report)
{
    rx_make_clear_hid_report(report);
    report[2] = seq;
    report[3] = flags;
    report[4] = (uint8_t)(key_mask & 0xFFu);
    report[5] = (uint8_t)((key_mask >> 8) & 0xFFu);
    report[6] = (uint8_t)((key_mask >> 16) & 0xFFu);
    report[7] = (uint8_t)((key_mask >> 24) & 0xFFu);
    if(raw_payload != NULL)
    {
        memcpy(&report[8], raw_payload, RF_INPUT_PAYLOAD_LEN);
    }
    report[18] = (uint8_t)(RFH_FIXED_RATE_HZ & 0xFFu);
    report[19] = (uint8_t)(RFH_FIXED_RATE_HZ >> 8);
}

static uint8_t rx_parse_hitbox_input(const uint8_t *data,
                                     uint8_t *seq,
                                     uint32_t *key_mask)
{
    uint8_t flags;
    uint8_t version;

    if((data == NULL) || (seq == NULL) || (key_mask == NULL))
    {
        return 0u;
    }

    flags = data[1];
    version = (uint8_t)((flags & RF_INPUT_FORMAT_VERSION_MASK) >>
                        RF_INPUT_FORMAT_VERSION_SHIFT);
    if((version != RF_INPUT_FORMAT_VERSION) ||
       ((flags & RF_INPUT_FLAG_PROCESSED) == 0u))
    {
        return 0u;
    }
    if(rx_input_crc8(data, 9u) != data[9])
    {
        return 0u;
    }

    *seq = data[0];
    *key_mask = ((uint32_t)data[2]) |
                ((uint32_t)data[3] << 8) |
                ((uint32_t)data[4] << 16) |
                ((uint32_t)data[5] << 24);
    *key_mask &= RF_INPUT_KEY_MASK_VALID;
    return 1u;
}

static void rx_key_mask_to_xinput_report(uint32_t key_mask,
                                         xinput_report_t *report)
{
    rx_make_clear_xinput_report(report);

    report->buttons1 |= (key_mask & HBOX_KEY_UP) ? XBOX_MASK_UP : 0u;
    report->buttons1 |= (key_mask & HBOX_KEY_DOWN) ? XBOX_MASK_DOWN : 0u;
    report->buttons1 |= (key_mask & HBOX_KEY_LEFT) ? XBOX_MASK_LEFT : 0u;
    report->buttons1 |= (key_mask & HBOX_KEY_RIGHT) ? XBOX_MASK_RIGHT : 0u;
    report->buttons1 |= (key_mask & HBOX_KEY_S2) ? XBOX_MASK_START : 0u;
    report->buttons1 |= (key_mask & HBOX_KEY_S1) ? XBOX_MASK_BACK : 0u;
    report->buttons1 |= (key_mask & HBOX_KEY_L3) ? XBOX_MASK_LS : 0u;
    report->buttons1 |= (key_mask & HBOX_KEY_R3) ? XBOX_MASK_RS : 0u;

    report->buttons2 |= (key_mask & HBOX_KEY_L1) ? XBOX_MASK_LB : 0u;
    report->buttons2 |= (key_mask & HBOX_KEY_R1) ? XBOX_MASK_RB : 0u;
    report->buttons2 |= (key_mask & HBOX_KEY_A1) ? XBOX_MASK_HOME : 0u;
    report->buttons2 |= (key_mask & HBOX_KEY_B1) ? XBOX_MASK_A : 0u;
    report->buttons2 |= (key_mask & HBOX_KEY_B2) ? XBOX_MASK_B : 0u;
    report->buttons2 |= (key_mask & HBOX_KEY_B3) ? XBOX_MASK_X : 0u;
    report->buttons2 |= (key_mask & HBOX_KEY_B4) ? XBOX_MASK_Y : 0u;

    report->lt = (key_mask & HBOX_KEY_L2) ? 0xFFu : 0u;
    report->rt = (key_mask & HBOX_KEY_R2) ? 0xFFu : 0u;
}

static void rx_handle_hitbox_input(const uint8_t *data)
{
    uint8_t seq;
    uint32_t key_mask;
    xinput_report_t report;
    uint8_t hid_report[HID_ENDPOINT_SIZE];

    if(rx_parse_hitbox_input(data, &seq, &key_mask) == 0u)
    {
        rx_note_error();
        return;
    }

    g_input_seen_valid = 1u;
    g_last_valid_input_clock = TMOS_GetSystemClock();
    if((seq == g_last_input_seq) && (key_mask == g_last_input_key_mask))
    {
        return;
    }

    g_last_input_seq = seq;
    g_last_input_key_mask = key_mask;
    rx_key_mask_to_xinput_report(key_mask, &report);
    rx_make_hid_report(seq, data[1], key_mask, data, hid_report);
    rx_queue_xinput_report(&report);
    rx_queue_hid_report(hid_report);
}

static void rx_capture_hitbox_input(const uint8_t *data)
{
    uint8_t next_index;
    if(data == NULL)
    {
        return;
    }
    next_index = (uint8_t)(g_input_payload_active_index ^ 1u);
    memcpy(g_input_payload_buffer[next_index], data, RF_INPUT_PAYLOAD_LEN);
    g_input_payload_active_index = next_index;
    g_input_payload_generation++;
}

static void rx_service_hitbox_input(void)
{
    uint8_t local_payload[RF_INPUT_PAYLOAD_LEN];
    uint32_t generation = g_input_payload_generation;
    uint8_t active_index;

    if(generation == g_input_payload_served_generation)
    {
        return;
    }
    active_index = g_input_payload_active_index;
    memcpy(local_payload, g_input_payload_buffer[active_index], RF_INPUT_PAYLOAD_LEN);
    g_input_payload_served_generation = generation;
    rx_handle_hitbox_input(local_payload);
}

static void rx_clear_xinput_on_stale(uint32_t now_clock)
{
    xinput_report_t report;
    uint8_t hid_report[HID_ENDPOINT_SIZE];

    if(g_input_seen_valid == 0u)
    {
        return;
    }
    if(((int32_t)(now_clock - (g_last_valid_input_clock + rx_input_stale_ticks()))) < 0)
    {
        return;
    }

    g_input_seen_valid = 0u;
    g_last_input_key_mask = 0u;
    rx_make_clear_xinput_report(&report);
    rx_make_clear_hid_report(hid_report);
    rx_queue_xinput_report(&report);
    rx_queue_hid_report(hid_report);
}

static void rx_xinput_init(void)
{
    rx_make_clear_xinput_report(&g_current_xinput_report);
    rx_make_clear_xinput_report(&g_pending_xinput_report);
    rx_make_clear_hid_report(g_current_hid_report);
    rx_make_clear_hid_report(g_pending_hid_report);
    memcpy(HID_Report_Buffer, g_current_hid_report, HID_ENDPOINT_SIZE);
    memset(g_input_payload_buffer, 0, sizeof(g_input_payload_buffer));
    g_input_payload_active_index = 0u;
    g_input_payload_generation = 0u;
    g_input_payload_served_generation = 0u;
    g_xinput_pending = 0u;
    g_hid_pending = 0u;
    g_input_seen_valid = 0u;
    g_last_input_seq = 0u;
    g_last_input_key_mask = 0u;
    g_last_valid_input_clock = TMOS_GetSystemClock();
}

static void rx_start_rx(void)
{
    if((g_basic_started == 0u) || (g_ack_tx_active != 0u))
    {
        return;
    }

    gRxParam.frequency = RFH_FIXED_CHANNEL;
    gRxParam.whiteChannel = RFH_FIXED_CHANNEL;
    gRxParam.rxDMA = (uint32_t)RxBuf;
    gRxParam.rxMaxLen = RFH_AIR_PACKET_LEN;
    gRxParam.timeOut = RFIP_RX_NO_TIMEOUT;
    gStat.rx_arm_try++;
    g_low_rx_ret = (uint8_t)RFIP_SetRx(&gRxParam);
    if(g_low_rx_ret == SUCCESS)
    {
        gStat.rx_arm_ok++;
    }
    else
    {
        gStat.rx_arm_fail++;
        rx_note_error();
    }
}

#if (RF_SWITCH_TEST_ENABLE == 1)
static uint16_t rx_switch_tx_ticks(void)
{
    if(g_sw_ack_ticks >= RF_SWITCH_TEST_CYCLE_TICKS)
    {
        return (uint16_t)(RF_SWITCH_TEST_CYCLE_TICKS - 1u);
    }
    return (uint16_t)(RF_SWITCH_TEST_CYCLE_TICKS - g_sw_ack_ticks);
}

static uint16_t rx_switch_ack_window_us(void)
{
    return (uint16_t)(g_sw_ack_ticks * RFH_SLOT_US);
}

static void rx_switch_set_ack_ticks(uint16_t ack_ticks)
{
    if((ack_ticks == 0u) || (ack_ticks >= RF_SWITCH_TEST_CYCLE_TICKS))
    {
        return;
    }
    g_sw_ack_ticks = ack_ticks;
}

static void rx_switch_fill_packet(void)
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
    data[0] = RF_SWITCH_TEST_MARK_RX;
    data[1] = seq;
    data[2] = (uint8_t)(g_sw_tick & 0xFFu);
    data[3] = (uint8_t)(g_sw_tick >> 8);
    data[4] = (uint8_t)(g_sw_log_tx & 0xFFu);
    data[5] = (uint8_t)((g_sw_log_tx >> 8) & 0xFFu);
}

static void rx_switch_sync_from_tx_tick(uint16_t tx_tick, uint16_t ack_ticks)
{
    uint16_t next_tick;
    uint8_t role_tx;

    if(tx_tick >= RF_SWITCH_TEST_CYCLE_TICKS)
    {
        return;
    }

    rx_switch_set_ack_ticks(ack_ticks);

    next_tick = (uint16_t)(tx_tick + 1u);
    if(next_tick >= RF_SWITCH_TEST_CYCLE_TICKS)
    {
        next_tick = 0u;
    }

    role_tx = (next_tick >= rx_switch_tx_ticks()) ? 1u : 0u;
    if(role_tx != g_sw_role_tx)
    {
        (void)RFRole_Stop();
        g_sw_rx_active = 0u;
        g_sw_role_tx = role_tx;
        g_sw_log_switch++;
    }

    g_sw_tick = next_tick;
    g_sw_log_sync++;
}

static void rx_switch_start_rx(void)
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
        rx_note_error();
    }
}

static void rx_switch_start_tx(void)
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

    rx_switch_fill_packet();
    gTxParam.frequency = RFH_FIXED_CHANNEL;
    gTxParam.whiteChannel = RFH_FIXED_CHANNEL;
    gTxParam.txDMA = (uint32_t)TxBuf;
    gStat.tx_ack_try++;
    ret_start = RFIP_SetTxStart();
    g_low_tx_ret = (uint8_t)ret_start;
    if(ret_start != SUCCESS)
    {
        gStat.tx_ack_start_fail++;
        gStat.tx_ack_fail++;
        g_sw_log_bad++;
        rx_note_error();
        return;
    }
    ret_parm = RFIP_SetTxParm(&gTxParam);
    if(ret_parm != SUCCESS)
    {
        gStat.tx_ack_parm_fail++;
        gStat.tx_ack_fail++;
        g_sw_log_bad++;
        rx_note_error();
        return;
    }
    g_sw_log_tx++;
}

static void rx_switch_handle_rx(void)
{
    const uint8_t *air = &RxBuf[2];
    const uint8_t *data = &air[RFH_DATA_OFFSET];

    g_sw_rx_active = 0u;
    if((RxBuf[1] == RFH_AIR_PACKET_LEN) &&
       (rfh_packet_type(air[RFH_HDR0_OFFSET]) == RFH_PKT_DATA) &&
       (rfh_rate_code(air[RFH_HDR0_OFFSET]) == RFH_RATE_8K) &&
       (data[0] == RF_SWITCH_TEST_MARK_TX))
    {
        uint16_t tx_tick = (uint16_t)data[2] |
                           ((uint16_t)data[3] << 8);
        uint16_t ack_ticks = (uint16_t)data[4] |
                             ((uint16_t)data[5] << 8);

        rx_switch_sync_from_tx_tick(tx_tick, ack_ticks);
        g_sw_log_rx++;
        g_sw_total_rx++;
        return;
    }
    g_sw_log_bad++;
    rx_note_error();
}

static void rx_switch_tick(void)
{
    uint8_t role_tx = (g_sw_tick >= rx_switch_tx_ticks()) ? 1u : 0u;

    if(role_tx != g_sw_role_tx)
    {
        g_sw_role_tx = role_tx;
        g_sw_log_switch++;
        (void)RFRole_Stop();
        g_sw_rx_active = 0u;
    }

    if(g_sw_role_tx != 0u)
    {
        rx_switch_start_tx();
    }
    else
    {
        rx_switch_start_rx();
    }

    g_sw_tick++;
    if(g_sw_tick >= RF_SWITCH_TEST_CYCLE_TICKS)
    {
        g_sw_tick = 0u;
    }
}
#endif

static void rx_schedule_ack_for_slot(uint8_t slot)
{
    uint32_t slots_until_ack;
    int32_t due_us;

    if((slot >= RFH_DATA_SLOTS) ||
       (g_ack_sent_for_group != 0u) ||
       (g_ack_tx_active != 0u))
    {
        return;
    }

    slots_until_ack = (uint32_t)(RFH_ACK_SLOT - slot);
    due_us = (int32_t)(slots_until_ack * RFH_SLOT_US) +
             (int32_t)RFH_ACK_TX_OFFSET_US -
             (int32_t)RFH_RX_REPORT_DONE_US -
             (int32_t)RFH_TX_SETUP_US +
             (int32_t)rx_ack_phase_offset_us();
    if(due_us < RX_ACK_MIN_ARM_US)
    {
        due_us = RX_ACK_MIN_ARM_US;
    }
    if(due_us > (int32_t)(RFH_GROUP_SLOTS * RFH_SLOT_US))
    {
        due_us = (int32_t)(RFH_GROUP_SLOTS * RFH_SLOT_US);
    }
    g_ack_pending = 1u;
    rx_ack_timer_arm_us((uint32_t)due_us);
}

static void rx_fill_ack_packet(void)
{
    uint8_t *air = &TxBuf[2];
    uint8_t *data = &air[RFH_DATA_OFFSET];
    uint8_t rx_count;

    g_pending_ack_bitmap = (uint8_t)((~g_received_bitmap) & RFH_ACK_MISSING_MASK);
    rx_count = rx_popcount7(g_received_bitmap);

    TxBuf[0] = RFH_WCH_PREAMBLE;
    TxBuf[1] = RFH_AIR_PACKET_LEN;
    air[RFH_HDR0_OFFSET] = rfh_make_header0(RFH_PKT_ACK,
                                            RFH_RATE_8K,
                                            RFH_FLAG_LINK_OK);
    air[RFH_HDR1_OFFSET] = rfh_make_slot_header1(g_current_group, RFH_ACK_SLOT);
    memset(data, 0, RFH_AIR_DATA_LEN);
    data[0] = g_pending_ack_bitmap;
    data[1] = g_current_group;
    data[2] = rx_count;
    data[3] = RFH_FLAG_LINK_OK;
}

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
        rx_note_error();
        return 0u;
    }

    ret_parm = RFIP_SetTxParm(&gTxParam);
    if(ret_parm != SUCCESS)
    {
        gStat.tx_ack_parm_fail++;
        gStat.tx_ack_fail++;
        rx_note_error();
        return 0u;
    }
    g_ack_tx_active = 1u;
    return 1u;
}

static void rx_send_ack(void)
{
    if((g_basic_started == 0u) ||
       (g_ack_tx_active != 0u) ||
       (g_group_valid == 0u) ||
       (g_ack_sent_for_group != 0u))
    {
        return;
    }

    rx_fill_ack_packet();
    (void)RFRole_Stop();
    gTxParam.frequency = RFH_FIXED_CHANNEL;
    gTxParam.whiteChannel = RFH_FIXED_CHANNEL;
    gTxParam.txDMA = (uint32_t)TxBuf;
    g_ack_pending = 0u;
    if(rx_start_ack_tx_loaded() == 0u)
    {
        rx_start_rx();
    }
}

static void rx_begin_group(uint8_t group, uint8_t first_slot)
{
    if(g_group_valid != 0u)
    {
        uint8_t diff = rfh_group_diff(group, g_current_group);
        if(diff > 1u)
        {
            g_total_bad_seq += (uint32_t)(diff - 1u);
            g_log_bad_seq += (uint32_t)(diff - 1u);
        }
    }

    g_group_valid = 1u;
    g_current_group = group;
    g_received_bitmap = 0u;
    g_ack_pending = 0u;
    g_ack_sent_for_group = 0u;
    rx_ack_timer_cancel();

    if(first_slot < RFH_DATA_SLOTS)
    {
        g_log_first_slot_hist[first_slot]++;
        g_log_first_slot_mask |= (uint8_t)(1u << first_slot);
    }
}

static uint8_t rx_process_data_packet(const uint8_t *air)
{
    const uint8_t *data = &air[RFH_DATA_OFFSET];
    uint8_t group = rfh_header_group_id(air[RFH_HDR1_OFFSET]);
    uint8_t slot = rfh_header_slot_id(air[RFH_HDR1_OFFSET]);
    uint8_t bit;

    if((rfh_packet_type(air[RFH_HDR0_OFFSET]) != RFH_PKT_DATA) ||
       (rfh_rate_code(air[RFH_HDR0_OFFSET]) != RFH_RATE_8K) ||
       (slot >= RFH_DATA_SLOTS))
    {
        gStat.rx_bad_slot++;
        rx_note_error();
        return 0u;
    }

    if((g_group_valid == 0u) || (group != g_current_group))
    {
        rx_begin_group(group, slot);
    }

    bit = (uint8_t)(1u << slot);
    if((g_received_bitmap & bit) == 0u)
    {
        g_received_bitmap |= bit;
    }

    rx_capture_hitbox_input(data);
    rx_schedule_ack_for_slot(slot);
    g_total_rx_data++;
    g_log_rx_data++;
    return 1u;
}

static uint8_t rx_process_air_packet(const uint8_t *rx_buf)
{
    const uint8_t *air = &rx_buf[2];

    if(rx_buf[1] != RFH_AIR_PACKET_LEN)
    {
        gStat.rx_bad_len++;
        rx_note_error();
        return 0u;
    }
    if(rfh_packet_type(air[RFH_HDR0_OFFSET]) != RFH_PKT_DATA)
    {
        gStat.rx_bad_type++;
        rx_note_error();
        return 0u;
    }
    return rx_process_data_packet(air);
}

static void rx_service_timers(void)
{
    uint32_t now_clock = TMOS_GetSystemClock();

    rx_service_hitbox_input();
    rx_flush_xinput_pending();
    rx_flush_hid_pending();
    rx_clear_xinput_on_stale(now_clock);
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
    gRxParam.timeOut = RFIP_RX_NO_TIMEOUT;

    g_basic_started = 1u;
#if (RF_SWITCH_TEST_ENABLE == 1)
    g_sw_role_tx = 0u;
    g_sw_rx_active = 0u;
    g_sw_tick = 0u;
    rx_switch_start_rx();
#else
    rx_start_rx();
#endif
    (void)g_low_config_ret;
}

#if (RF_SWITCH_TEST_ENABLE == 1)
__INTERRUPT
__HIGH_CODE
void TMR1_IRQHandler(void)
{
    if(TMR1_GetITFlag(TMR0_3_IT_CYC_END) == 0u)
    {
        return;
    }

    TMR1_ClearITFlag(TMR0_3_IT_CYC_END);
    if(g_basic_started == 0u)
    {
        return;
    }

    rx_switch_tick();
}
#else
__INTERRUPT
__HIGH_CODE
void TMR1_IRQHandler(void)
{
    if(TMR1_GetITFlag(TMR0_3_IT_CYC_END) == 0u)
    {
        return;
    }

    TMR1_ClearITFlag(TMR0_3_IT_CYC_END);
    TMR1_ITCfg(DISABLE, TMR0_3_IT_CYC_END);
    TMR1_Disable();

    if((g_basic_started == 0u) ||
       (g_ack_pending == 0u) ||
       (g_ack_tx_active != 0u) ||
       (g_ack_sent_for_group != 0u))
    {
        return;
    }

    rx_send_ack();
}
#endif

__HIGH_CODE
void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id)
{
    uint8_t rx_snapshot[RFH_AIR_PACKET_LEN + 2u];
    uint8_t i;

    (void)id;

#if (RF_SWITCH_TEST_ENABLE == 1)
    if(sta & RF_STATE_RX)
    {
        rx_switch_handle_rx();
        if(g_sw_role_tx == 0u)
        {
            rx_switch_start_rx();
        }
    }
    if(sta & RF_STATE_RX_CRCERR)
    {
        gStat.rx_crcerr++;
        g_sw_rx_active = 0u;
        g_sw_log_bad++;
        rx_note_error();
        if(g_sw_role_tx == 0u)
        {
            rx_switch_start_rx();
        }
    }
    if(sta & RF_STATE_TIMEOUT)
    {
        gStat.rx_timeout++;
        g_sw_rx_active = 0u;
        if(g_sw_role_tx == 0u)
        {
            rx_switch_start_rx();
        }
    }
    if(sta & RF_STATE_TX_FINISH)
    {
        gStat.tx_ack_ok++;
    }
    return;
#endif

    if(sta & RF_STATE_RX)
    {
        for(i = 0u; i < (uint8_t)sizeof(rx_snapshot); i++)
        {
            rx_snapshot[i] = RxBuf[i];
        }
        if(rx_process_air_packet(rx_snapshot) != 0u)
        {
            gStat.rx_ok++;
        }
        rx_service_timers();
        if(g_ack_tx_active == 0u)
        {
            rx_start_rx();
        }
    }
    if(sta & RF_STATE_RX_CRCERR)
    {
        gStat.rx_crcerr++;
        rx_note_error();
        rx_start_rx();
    }
    if(sta & RF_STATE_TIMEOUT)
    {
        gStat.rx_timeout++;
        rx_start_rx();
    }
    if(sta & RF_STATE_TX_FINISH)
    {
        if(g_ack_tx_active != 0u)
        {
            uint8_t missing = rx_popcount7(g_pending_ack_bitmap);

            g_ack_tx_active = 0u;
            g_ack_sent_for_group = 1u;
            gStat.tx_ack_ok++;
            g_total_ack_tx++;
            g_total_groups++;
            g_total_expected_data += RFH_DATA_SLOTS;
            g_total_missing_packets += missing;
            g_log_ack_tx++;
            g_log_groups++;
            g_log_expected_data += RFH_DATA_SLOTS;
            g_log_missing_packets += missing;
            rx_start_rx();
        }
    }
}

void RF_Service(void)
{
    if(g_basic_started == 0u)
    {
        return;
    }
#if (RF_SWITCH_TEST_ENABLE == 1)
    return;
#endif
    rx_service_timers();
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

uint8_t RF_StartPairing(void)
{
    return 1u;
}

uint8_t RF_StopPairing(void)
{
    return 1u;
}

uint8_t RF_IsPairingActive(void)
{
    return 0u;
}

rf_indicator_mode_t RF_GetIndicatorMode(void)
{
    if(g_basic_started == 0u)
    {
        return RF_INDICATOR_OFF;
    }
    return RF_INDICATOR_BLINK_2000MS;
}

void RF_StartPacketLossScan(void)
{
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
    return 1u;
}

uint16_t RF_GetStatsLine(char *buf, uint16_t len)
{
#if (RF_SWITCH_TEST_ENABLE == 1)
    int written;

    if((buf == NULL) || (len == 0u))
    {
        return 0u;
    }

    written = snprintf(buf,
                       len,
                       "RS W=%uus T=%lu R=%lu Y=%lu S=%lu B=%lu E=%lu\r\n",
                       (unsigned int)rx_switch_ack_window_us(),
                       (unsigned long)g_sw_log_tx,
                       (unsigned long)g_sw_log_rx,
                       (unsigned long)g_sw_log_sync,
                       (unsigned long)g_sw_log_switch,
                       (unsigned long)g_sw_log_bad,
                       (unsigned long)g_log_errors);
    if(written < 0)
    {
        return 0u;
    }

    gStat.rx_ok = 0u;
    gStat.rx_bad_len = 0u;
    gStat.rx_bad_type = 0u;
    gStat.rx_bad_slot = 0u;
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
    g_sw_log_tx = 0u;
    g_sw_log_rx = 0u;
    g_sw_log_sync = 0u;
    g_sw_log_switch = 0u;
    g_sw_log_bad = 0u;
    g_log_errors = 0u;

    return (uint16_t)((written >= (int)len) ? (len - 1u) : (uint16_t)written);
#else
    uint16_t loss;
    uint8_t first_slot;
    int written;

    if((buf == NULL) || (len == 0u))
    {
        return 0u;
    }

    loss = rx_loss_permille(g_log_missing_packets, g_log_expected_data);
    first_slot = rx_log_dominant_first_slot();
    written = snprintf(buf,
                       len,
                       "R5 O=%d G=%lu P=%lu A=%lu L=%03u F=%u/%02X E=%lu\r\n",
                       (int)rx_ack_phase_offset_us(),
                       (unsigned long)g_log_groups,
                       (unsigned long)g_log_rx_data,
                       (unsigned long)g_log_ack_tx,
                       (unsigned int)loss,
                       (unsigned int)first_slot,
                       (unsigned int)g_log_first_slot_mask,
                       (unsigned long)g_log_errors);
    if(written < 0)
    {
        return 0u;
    }

    gStat.rx_ok = 0u;
    gStat.rx_bad_len = 0u;
    gStat.rx_bad_type = 0u;
    gStat.rx_bad_slot = 0u;
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
    g_log_groups = 0u;
    g_log_rx_data = 0u;
    g_log_expected_data = 0u;
    g_log_ack_tx = 0u;
    g_log_missing_packets = 0u;
    g_log_bad_seq = 0u;
    g_log_errors = 0u;
    memset(g_log_first_slot_hist, 0, sizeof(g_log_first_slot_hist));
    g_log_first_slot_mask = 0u;
    rx_ack_phase_scan_advance();

    return (uint16_t)((written >= (int)len) ? (len - 1u) : (uint16_t)written);
#endif
}

static void rx_put_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)(value >> 8);
}

static void rx_put_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)(value >> 24);
}

uint8_t RF_TrySendTelemetryReport(void)
{
    uint8_t report[HID_ENDPOINT_SIZE];
    uint16_t loss = rx_loss_permille(g_total_missing_packets, g_total_expected_data);

    memset(report, 0, sizeof(report));
    g_hid_telemetry_seq++;
    rx_put_u32(&report[0], RX_HID_TELEMETRY_MAGIC);
    rx_put_u32(&report[4], g_hid_telemetry_seq);
    rx_put_u16(&report[8], 100u);
    rx_put_u16(&report[10], RFH_FIXED_RATE_HZ);
    rx_put_u32(&report[12], g_total_rx_data);
    rx_put_u32(&report[16], g_total_expected_data);
    rx_put_u16(&report[20], loss);
    report[22] = 1u;
    report[23] = RFH_FIXED_CHANNEL;
    report[24] = g_current_group;
    report[25] = g_received_bitmap;
    report[26] = RFH_RATE_8K;
    report[27] = (uint8_t)(g_total_bad_seq & 0xFFu);
    report[28] = (uint8_t)(g_total_errors & 0xFFu);

    return rx_try_submit_hid_report(report);
}

void RF_Init(void)
{
#if (RF_SWITCH_TEST_ENABLE == 1)
    uint32_t tick_per_evt;
#endif

    taskID = TMOS_ProcessEventRegister(RF_ProcessEvent);

    PFIC_EnableIRQ(BLEB_IRQn);
    PFIC_EnableIRQ(BLEL_IRQn);

    TMR0_TimerInit(TMR0_FREE_RUN_WRAP - 1u);

    rx_xinput_init();
    g_group_valid = 0u;
    g_current_group = 0u;
    g_received_bitmap = 0u;
    g_ack_pending = 0u;
    g_ack_sent_for_group = 0u;
    g_pending_ack_bitmap = RFH_ACK_MISSING_MASK;
#if (RF_SWITCH_TEST_ENABLE == 1)
    g_sw_tick = 0u;
    g_sw_role_tx = 0u;
    g_sw_rx_active = 0u;
    g_sw_seq = 0u;
    g_sw_log_tx = 0u;
    g_sw_log_rx = 0u;
    g_sw_log_bad = 0u;
    g_sw_log_switch = 0u;
    g_sw_log_sync = 0u;
    g_sw_total_rx = 0u;
    g_sw_ack_ticks = RF_SWITCH_TEST_DEFAULT_ACK_TICKS;
#endif
#if (RX_ACK_PHASE_SCAN_ENABLE == 1)
    g_ack_phase_scan_index = 0u;
#endif

    rx_basic_start();

#if (RF_SWITCH_TEST_ENABLE == 1)
    tick_per_evt = rx_us_to_tmr_cycles(RF_SWITCH_TEST_SEND_US);
    TMR1_TimerInit(tick_per_evt);
    TMR1_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    PFIC_SetPriority(TMR1_IRQn, 0x80);
    PFIC_EnableIRQ(TMR1_IRQn);
#else
    TMR1_TimerInit(1u);
    rx_ack_timer_cancel();
    PFIC_SetPriority(TMR1_IRQn, 0x80);
    PFIC_EnableIRQ(TMR1_IRQn);
#endif
}

#endif
