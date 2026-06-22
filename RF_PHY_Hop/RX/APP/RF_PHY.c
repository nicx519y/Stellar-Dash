/********************************** (C) COPYRIGHT *******************************
 * File Name          : RF_PHY.c
 * Description        : RX side for RF PHY DATA + 500ms ACK control protocol.
 *******************************************************************************/

#include "CONFIG.h"
#include "RF_PHY.h"
#include "HAL.h"
#include "wchrf.h"
#include "rf_hop_protocol.h"
#include "rf_hop_bond.h"
#include "rf_hop_score.h"
#include "rf_monitor_control.h"
#include "dongle_config.h"
#include "ch585_usbhs_device.h"
#include "usbd_compatibility_hid.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define RF_AUTO_DEMO_PACKET_LEN        RFH_AIR_PACKET_LEN
#define RF_AUTO_DEMO_DMA_LEN           (RF_AUTO_DEMO_PACKET_LEN + 2u)
#define RF_AUTO_DEMO_DATA_TYPE         0xFFu
#define RF_AUTO_DEMO_ACK_TYPE          0xFFu
#define RF_AUTO_DEMO_CHANNEL           39u
#define RF_AUTO_DEMO_FREQUENCY_KHZ     2480000UL
#ifndef RF_AUTO_DEMO_TX_POWER
#define RF_AUTO_DEMO_TX_POWER          BLE_TX_POWER
#endif
#ifndef RF_AUTO_DEMO_PHY_PROPS
#define RF_AUTO_DEMO_PHY_PROPS         LLE_MODE_PHY_2M
#endif
#define RF_AUTO_DEMO_SEND_ACK_ENABLE   1u
#define RF_AUTO_DEMO_ACK_BIT           0u
#define RF_AUTO_DEMO_REPORT_HZ         8000u
#define RF_AUTO_DEMO_RATE_CODE         RFH_RATE_8K
#ifndef RF_AUTO_DEMO_ACK_TX_DELAY_US
#define RF_AUTO_DEMO_ACK_TX_DELAY_US   30u
#endif
#define RF_AUTO_DEMO_ACK_TOKEN_OFFSET  10u
#define RF_AUTO_DEMO_ACK_REMAIN_OFFSET 11u
#define RF_AUTO_DEMO_DISCOVERY_CHANNEL_A RFH_DISCOVERY_CHANNEL_A
#define RF_AUTO_DEMO_DISCOVERY_CHANNEL_B RFH_DISCOVERY_CHANNEL_B
#define RF_AUTO_DEMO_INITIAL_CHANNEL   RF_AUTO_DEMO_DISCOVERY_CHANNEL_B
#define RF_AUTO_DEMO_DISCOVERY_SCAN_DWELL_MS 3u
#define RF_AUTO_DEMO_HOP_DUAL_DWELL_MS 2u
#define RF_AUTO_DEMO_HOP_DUAL_TIMEOUT_MS 3000u
#define RF_AUTO_DEMO_HOP_CONFIRM_ACK_KEEP_TOKENS 6u
#define RF_AUTO_DEMO_RECOVERY_DWELL_MS 20u
#define RF_AUTO_DEMO_PAIR_TX_REJECT_REASON_DEFAULT RFH_PAIR_REJECT_BAD_STATE
#define RF_AUTO_DEMO_PAIR_AFTER_ACCEPT 1u
#define RF_AUTO_DEMO_PAIR_AFTER_DONE   2u
#define RF_AUTO_DEMO_PAIR_AFTER_REJECT 3u
#define RF_AUTO_DEMO_PAIR_DONE_REPEAT_COUNT 6u
/*
 * 频道评分可调项：
 * 分数越低越好，越高越差，最终限制在 0..1000。
 * 指标值按 0..1000 归一化后，以“指标值 * WEIGHT / 100”累加到 SCORE_BASE。
 * 调大某项 WEIGHT 会放大该指标对坏分的影响。
 */
#define RF_AUTO_DEMO_SCORE_BASE        0u    /* 无异常时坏分为 0 */
#define RF_AUTO_DEMO_SCORE_LOSS_WEIGHT 200u  /* 丢包/坏包率权重 */
#define RF_AUTO_DEMO_SCORE_CRC_WEIGHT  50u  /* CRC 错误权重 */
#define RF_AUTO_DEMO_SCORE_TYPE_WEIGHT 50u  /* 包类型/格式错误权重 */
#define RF_AUTO_DEMO_SCORE_TIMEOUT_WEIGHT 20u /* 正向包超时权重 */
#define RF_AUTO_DEMO_SCORE_IRQ_WEIGHT  200u  /* RX IRQ 延迟权重 */
#define RF_AUTO_DEMO_SCORE_WINDOW_MS   10000u /* 活动频道评分时间窗口：10 秒内所有事件样本求平均后更新一次分数 */
#define RF_AUTO_DEMO_CHANNEL_SCORE_INIT RF_AUTO_DEMO_SCORE_BASE /* 初始频道坏分 */
#define RF_AUTO_DEMO_CHANNEL_SCORE_GOOD RF_AUTO_DEMO_SCORE_BASE /* 明确好样本坏分 */
#define RF_AUTO_DEMO_CHANNEL_SCORE_BAD 1000u /* 饱和坏分 */

#define RF_LINK_CRC_INIT               0x555555UL
#define RF_RX_DMA_SLOT_COUNT           2u
#define RF_RX_PENDING_DEPTH            16u
#define RF_RX_PENDING_DRAIN_MAX        16u
#define RF_RX_PENDING_REPORT_CHUNK     2u
#define RX_HID_TELEMETRY_MAGIC         0x314D4852UL
#define RX_HID_SCORE_MAGIC             0x31534852UL
#define RX_HID_INPUT_MAGIC             0x31494852UL
#define RX_HID_LATENCY_MAGIC           0x314C4852UL
#define RX_HID_LATENCY_V2_MAGIC        0x324C4852UL
#define RX_HID_HOP_EVENT_NONE          0u
#define RX_HID_HOP_EVENT_START         1u
#define RX_HID_HOP_EVENT_FINISH        2u
#define RX_HID_INPUT_KEEPALIVE_DIV     3u
#define RX_HID_SILENT_TICKS_SAT        0xFFFEu
#define TMR0_FREE_RUN_WRAP             0x04000000UL
#define RX_LATENCY_STAGE_FLAG_SPLIT    0x01u
#define RX_LATENCY_STAGE_FLAG_STM32_SAT 0x02u
#define RX_LATENCY_STAGE_FLAG_TX_SAT   0x04u
#define RX_LATENCY_STAGE_FLAG_RX_SAT   0x08u

typedef struct
{
    volatile uint32_t rx_arm;
    volatile uint32_t rx_arm_fail;
    volatile uint32_t data_ok;
    volatile uint32_t data_crc_err;
    volatile uint32_t data_type_err;
    volatile uint32_t ack_req;
    volatile uint32_t ack_finish;
    volatile uint32_t ack_fail;
    volatile uint32_t tx_parm_fail;
    volatile uint32_t hop_event;
    volatile uint32_t seq_gap;
    volatile uint32_t pending_drop;
} rf_auto_demo_stat_t;

typedef enum
{
    RF_AUTO_RX_UNCONNECTED = 0u,
    RF_AUTO_RX_CONNECT_ACK_PENDING,
    RF_AUTO_RX_COMM,
    RF_AUTO_RX_PREPARED_DUAL,
    RF_AUTO_RX_RECOVERY_SCAN,
    RF_AUTO_RX_PAIRING,
    RF_AUTO_RX_PAIR_CONFIRM_WAIT
} rf_auto_rx_state_t;

typedef enum
{
    RF_RX_PENDING_PACKET = 0u,
    RF_RX_PENDING_CRCERR
} rf_rx_pending_kind_t;

typedef struct
{
    uint8_t kind;
    uint8_t len;
    uint8_t channel;
    uint32_t rx_tmr;
    uint8_t air[RFH_AIR_PACKET_LEN];
} rf_rx_pending_t;

typedef struct
{
    uint32_t window_start_clock;
    uint32_t sample_score_sum;
    uint32_t sample_count;
    uint8_t active;
} rf_score_window_t;

uint8_t taskID;

static rfRoleParam_t gParm;
#if (RF_AUTO_DEMO_SEND_ACK_ENABLE != 0u)
static rfipTx_t gTxParam;
#endif
static rfipRx_t gRxParam;
#if (RF_AUTO_DEMO_SEND_ACK_ENABLE != 0u)
__attribute__((__aligned__(4))) static uint8_t TxBuf[RF_AUTO_DEMO_DMA_LEN];
#endif
__attribute__((__aligned__(4))) static uint8_t RxBuf[RF_RX_DMA_SLOT_COUNT][264];

static rf_auto_demo_stat_t g_demo_stat;
static volatile uint8_t g_demo_config_ret = 0xFFu;
static volatile uint8_t g_demo_tx_start_ret = 0xFFu;
static volatile uint8_t g_demo_tx_parm_ret = 0xFFu;
static volatile uint8_t g_demo_rx_ret = 0xFFu;
static volatile uint8_t g_demo_rearm_pending = 0u;
static volatile uint8_t g_demo_rx_active = 0u;
static volatile uint8_t g_demo_rx_active_slot = 0u;
static uint8_t g_demo_rx_next_slot = 0u;
static volatile uint8_t g_demo_ack_pending = 0u;
static uint32_t g_demo_ack_delay_tmr = 1u;
static uint32_t g_demo_slot_tmr = 1u;
static volatile uint8_t g_demo_connect_stage = 0u;
static uint32_t g_demo_connect_until_clock = 0u;
static uint32_t g_demo_connect_next_tx_clock = 0u;
static uint8_t g_demo_last_ack_token = 0u;
static uint8_t g_demo_have_ack_token = 0u;
static uint16_t g_demo_report_hz = RF_AUTO_DEMO_REPORT_HZ;
static uint8_t g_demo_rate_code = RF_AUTO_DEMO_RATE_CODE;
static uint8_t g_demo_has_bond = 0u;
static uint8_t g_demo_bond_channel_a = RF_AUTO_DEMO_DISCOVERY_CHANNEL_A;
static uint8_t g_demo_bond_channel_b = RF_AUTO_DEMO_DISCOVERY_CHANNEL_B;
static uint32_t g_demo_local_id_hash = 0u;
static uint32_t g_demo_link_access_address = RFH_LINK_ACCESS_ADDRESS_DEFAULT;
static rfh_bond_record_t g_demo_bond;
static volatile uint32_t g_demo_last_data_tmr = 0u;
static volatile uint8_t g_demo_link_active = 0u;
static uint8_t g_demo_current_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
static uint8_t g_demo_old_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
static uint8_t g_demo_target_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
static uint8_t g_demo_hop_seq = 0u;
static rf_auto_rx_state_t g_demo_rx_state = RF_AUTO_RX_UNCONNECTED;
static uint8_t g_demo_pending_ack_cmd = RFH_CMD_NONE;
static uint8_t g_demo_pending_ack_seq = 0u;
static uint8_t g_demo_after_ack_action = 0u;
static uint8_t g_demo_confirm_ack_keep_count = 0u;
static uint8_t g_demo_confirm_ack_keep_seq = 0u;
static uint8_t g_demo_confirm_ack_keep_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
static uint8_t g_demo_have_data_seq = 0u;
static uint8_t g_demo_last_data_seq = 0u;
static uint32_t g_demo_window_expected = 0u;
static uint32_t g_demo_window_missing = 0u;
static uint32_t g_demo_window_rx_ok = 0u;
static uint32_t g_demo_window_crc = 0u;
static uint32_t g_demo_ack_irq_sum_us = 0u;
static uint8_t g_demo_ack_irq_count = 0u;
static uint16_t g_demo_ack_irq_max_us = 0u;
static uint32_t g_demo_dual_switch_clock = 0u;
static uint32_t g_demo_dual_deadline_clock = 0u;
static uint8_t g_demo_dual_side = 0u;
static uint32_t g_demo_recovery_scan_clock = 0u;
static uint8_t g_demo_recovery_scan_rank = 0u;
static volatile uint8_t g_demo_pair_tx_active = 0u;
static uint8_t g_demo_pair_after_tx_action = 0u;
static uint8_t g_demo_pair_scan_side = 0u;
static uint32_t g_demo_pair_scan_clock = 0u;
static uint32_t g_demo_pair_deadline_clock = 0u;
static uint32_t g_demo_pair_confirm_deadline_clock = 0u;
static uint32_t g_demo_pair_session = 0u;
static uint32_t g_demo_pair_tx_id_hash = 0u;
static uint32_t g_demo_pair_rx_id_hash = 0u;
static uint32_t g_demo_pair_link_access_address = 0u;
static uint32_t g_demo_pair_done_confirm32 = 0u;
static uint8_t g_demo_pair_done_repeat_left = 0u;
static uint32_t g_demo_hid_telemetry_seq = 0u;
static uint32_t g_demo_hid_last_clock = 0u;
static volatile uint16_t g_demo_hid_last_window_rx_ok = 0u;
static volatile uint16_t g_demo_hid_last_window_expected = 0u;
static volatile uint8_t g_demo_hid_last_window_errors = 0u;
static volatile uint8_t g_demo_hid_last_window_crc_errors = 0u;
static volatile uint8_t g_demo_hid_last_window_type_errors = 0u;
static volatile uint8_t g_demo_hid_last_window_timeout_errors = 0u;
static volatile uint32_t g_demo_hid_rx_ok = 0u;
static volatile uint32_t g_demo_hid_expected = 0u;
static volatile uint32_t g_demo_hid_bad = 0u;
static volatile uint32_t g_demo_hid_hop_events = 0u;
static volatile uint32_t g_demo_hid_errors = 0u;
static volatile uint32_t g_demo_hid_crc_errors = 0u;
static volatile uint32_t g_demo_hid_type_errors = 0u;
static volatile uint32_t g_demo_hid_timeout_errors = 0u;
static volatile uint32_t g_demo_hid_max_silent_cycles = 0u;
static volatile uint16_t g_demo_hid_link_lost_silent_ticks = 0u;
static volatile uint8_t g_demo_hid_hop_start_pending = 0u;
static volatile uint8_t g_demo_hid_hop_finish_pending = 0u;
static volatile uint16_t g_demo_hid_hop_start_score = 0u;
static volatile uint16_t g_demo_hid_hop_finish_duration_ms = 0u;
static volatile uint32_t g_demo_hid_input_key_mask = 0u;
static volatile uint32_t g_demo_hid_input_window_mask = 0u;
static volatile uint32_t g_demo_hid_input_report_seq = 0u;
static volatile uint8_t g_demo_hid_input_seq = 0u;
static volatile uint8_t g_demo_hid_input_flags = 0u;
static volatile uint8_t g_demo_hid_input_valid = 0u;
static volatile uint32_t g_demo_hid_input_sample_tick_us = 0u;
static volatile uint8_t g_demo_hid_input_sync_seq = 0u;
static volatile uint32_t g_demo_hid_input_sync_rx_tick_us = 0u;
static volatile uint32_t g_demo_hid_input_sync_tx_tick_us = 0u;
static volatile uint8_t g_demo_hid_latency_pending = 0u;
static volatile uint32_t g_demo_hid_latency_seq = 0u;
static volatile uint32_t g_demo_hid_latency_key_mask = 0u;
static volatile uint32_t g_demo_hid_latency_sample_tick_us = 0u;
static volatile uint16_t g_demo_hid_latency_stm32_us = 0u;
static volatile uint16_t g_demo_hid_latency_tx_us = 0u;
static volatile uint16_t g_demo_hid_latency_rx_us = 0u;
static volatile uint16_t g_demo_hid_latency_rx_irq_us = 0u;
static volatile uint16_t g_demo_hid_latency_rx_decode_us = 0u;
static volatile uint16_t g_demo_hid_latency_rx_epwait_us = 0u;
static volatile uint16_t g_demo_hid_latency_rx_submit_us = 0u;
static volatile uint8_t g_demo_hid_latency_stage_flags = 0u;
static volatile uint8_t g_demo_hid_latency_input_seq = 0u;
static volatile uint8_t g_demo_hid_latency_input_flags = 0u;
static volatile uint8_t g_demo_hid_latency_sync_seq = 0u;
static volatile uint32_t g_demo_hid_latency_sync_rx_tick_us = 0u;
static volatile uint32_t g_demo_hid_latency_sync_tx_tick_us = 0u;
static volatile uint8_t g_demo_hid_latency_v2 = 0u;
static volatile uint8_t g_demo_xinput_latency_pending = 0u;
static volatile uint8_t g_demo_xinput_latency_stm32_q8 = 0u;
static volatile uint8_t g_demo_xinput_latency_tx_q8 = 0u;
static volatile uint32_t g_demo_xinput_latency_rx_tmr = 0u;
static volatile uint32_t g_demo_xinput_latency_process_tmr = 0u;
static volatile uint32_t g_demo_xinput_latency_report_tmr = 0u;
static volatile uint32_t g_demo_xinput_latency_key_mask = 0u;
static volatile uint8_t g_demo_xinput_latency_input_seq = 0u;
static volatile uint8_t g_demo_xinput_latency_input_flags = 0u;
static uint32_t g_demo_air_diag_last_clock = 0u;
static volatile uint32_t g_demo_air_diag_rx_ok = 0u;
static volatile uint32_t g_demo_air_diag_seq_gap = 0u;
static volatile uint32_t g_demo_air_diag_crc_errors = 0u;
static volatile uint32_t g_demo_air_diag_type_errors = 0u;
static volatile uint32_t g_demo_air_diag_timeout_errors = 0u;
static uint8_t g_demo_hid_input_keepalive_div = 0u;
static volatile uint8_t g_demo_pending_input_payload[RF_INPUT_PAYLOAD_LEN];
static volatile uint8_t g_demo_pending_input_valid = 0u;
static volatile uint32_t g_demo_pending_input_gen = 0u;
static uint32_t g_demo_processed_input_gen = 0u;
static volatile uint8_t g_demo_xinput_pending = 0u;
static uint8_t g_demo_xinput_report[XINPUT_ENDPOINT_SIZE];
static uint32_t g_demo_hop_start_clock = 0u;
static uint8_t g_demo_hop_clock_valid = 0u;
#if (RF_AUTO_DEMO_SEND_ACK_ENABLE != 0u)
static uint8_t g_demo_ack_seq = 0u;
#endif
static uint16_t g_demo_channel_scores[RFH_HOP_CHANNEL_COUNT];
static rf_score_window_t g_demo_score_windows[RFH_HOP_CHANNEL_COUNT];
static uint32_t g_demo_hid_score_seq = 0u;
static uint8_t g_demo_hid_score_div = 0u;
static uint8_t g_demo_ack_score_hint_index = 0u;
static rf_rx_pending_t g_demo_rx_pending[RF_RX_PENDING_DEPTH];
static volatile uint8_t g_demo_rx_pending_head = 0u;
static volatile uint8_t g_demo_rx_pending_tail = 0u;
static volatile uint32_t g_demo_rx_pending_drop = 0u;
static volatile uint8_t g_demo_rx_pending_max_water = 0u;
static volatile int32_t g_demo_rssi_sum = 0;
static volatile uint32_t g_demo_rssi_count = 0u;
static volatile int8_t g_demo_rssi_last = 0;
static volatile int8_t g_demo_rssi_min = 127;
static volatile int8_t g_demo_rssi_max = -127;
static volatile uint8_t g_monitor_hid_enabled = 0u;
static volatile uint16_t g_monitor_hid_period_ms = RFMON_PERIOD_OFF;
static volatile uint8_t g_monitor_auto_hop_enabled = 1u;
static volatile uint8_t g_monitor_manual_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
static volatile uint8_t g_monitor_seq = 0u;
static volatile uint8_t g_monitor_rx_status = RFMON_APPLY_IDLE;
static volatile uint8_t g_monitor_tx_status = RFMON_APPLY_IDLE;
static volatile uint8_t g_monitor_tx_applied_seq = 0u;
static volatile uint8_t g_monitor_pending_seq = 0u;
static volatile uint32_t g_monitor_pending_flags = 0u;
static volatile uint8_t g_monitor_pending_retries = 0u;
static volatile uint8_t g_monitor_sync_pending_retries = 0u;
static volatile uint8_t g_monitor_sync_seq = 0u;

static uint8_t demo_hid_stats_enabled(void)
{
    return (g_monitor_hid_enabled != 0u) ? 1u : 0u;
}

static void demo_hid_clear_report_state(void)
{
    uint32_t irq_status;
    uint32_t now = TMOS_GetSystemClock();

    SYS_DisableAllIrq(&irq_status);
    g_demo_hid_last_clock = now;
    g_demo_hid_last_window_rx_ok = 0u;
    g_demo_hid_last_window_expected = 0u;
    g_demo_hid_last_window_errors = 0u;
    g_demo_hid_last_window_crc_errors = 0u;
    g_demo_hid_last_window_type_errors = 0u;
    g_demo_hid_last_window_timeout_errors = 0u;
    g_demo_hid_rx_ok = 0u;
    g_demo_hid_expected = 0u;
    g_demo_hid_bad = 0u;
    g_demo_hid_hop_events = 0u;
    g_demo_hid_errors = 0u;
    g_demo_hid_crc_errors = 0u;
    g_demo_hid_type_errors = 0u;
    g_demo_hid_timeout_errors = 0u;
    g_demo_hid_max_silent_cycles = 0u;
    g_demo_hid_link_lost_silent_ticks = 0u;
    g_demo_hid_hop_start_pending = 0u;
    g_demo_hid_hop_finish_pending = 0u;
    g_demo_hid_hop_start_score = 0u;
    g_demo_hid_hop_finish_duration_ms = 0u;
    g_demo_hid_input_key_mask = 0u;
    g_demo_hid_input_window_mask = 0u;
    g_demo_hid_input_seq = 0u;
    g_demo_hid_input_flags = 0u;
    g_demo_hid_input_valid = 0u;
    g_demo_hid_input_sample_tick_us = 0u;
    g_demo_hid_input_sync_seq = 0u;
    g_demo_hid_input_sync_rx_tick_us = 0u;
    g_demo_hid_input_sync_tx_tick_us = 0u;
    g_demo_hid_latency_pending = 0u;
    g_demo_hid_latency_key_mask = 0u;
    g_demo_hid_latency_sample_tick_us = 0u;
    g_demo_hid_latency_stm32_us = 0u;
    g_demo_hid_latency_tx_us = 0u;
    g_demo_hid_latency_rx_us = 0u;
    g_demo_hid_latency_rx_irq_us = 0u;
    g_demo_hid_latency_rx_decode_us = 0u;
    g_demo_hid_latency_rx_epwait_us = 0u;
    g_demo_hid_latency_rx_submit_us = 0u;
    g_demo_hid_latency_stage_flags = 0u;
    g_demo_hid_latency_input_seq = 0u;
    g_demo_hid_latency_input_flags = 0u;
    g_demo_hid_latency_sync_seq = 0u;
    g_demo_hid_latency_sync_rx_tick_us = 0u;
    g_demo_hid_latency_sync_tx_tick_us = 0u;
    g_demo_hid_latency_v2 = 0u;
    g_demo_air_diag_last_clock = 0u;
    g_demo_air_diag_rx_ok = 0u;
    g_demo_air_diag_seq_gap = 0u;
    g_demo_air_diag_crc_errors = 0u;
    g_demo_air_diag_type_errors = 0u;
    g_demo_air_diag_timeout_errors = 0u;
    g_demo_hid_input_keepalive_div = 0u;
    g_demo_hid_score_div = 0u;
    SYS_RecoverIrq(irq_status);
}

static uint32_t demo_hash_bytes(uint32_t hash, const uint8_t *bytes, uint8_t len)
{
    uint8_t i;

    for(i = 0u; i < len; i++)
    {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
    return hash;
}

static uint32_t demo_make_local_id_hash(void)
{
    static const uint8_t tag[] = "HBOX-RF-HOP:RX";
    uint8_t mac[8] __attribute__((aligned(4))) = {0};
    uint32_t hash = 2166136261UL;

    (void)GetMACAddress(mac);
    hash = demo_hash_bytes(hash, tag, (uint8_t)(sizeof(tag) - 1u));
    hash = demo_hash_bytes(hash, mac, 6u);
    hash = rfh_fnv1a32_mix_u32(hash, chip_info);
    return (hash == 0u) ? 0x52584A31UL : hash;
}

static void demo_select_unpaired_address(void)
{
    uint32_t seed;

    g_demo_has_bond = 0u;
    memset(&g_demo_bond, 0, sizeof(g_demo_bond));
    g_demo_bond_channel_a = RF_AUTO_DEMO_DISCOVERY_CHANNEL_A;
    g_demo_bond_channel_b = RF_AUTO_DEMO_DISCOVERY_CHANNEL_B;
#if (RFH_TEST_FIXED_BOND_ENABLE != 0u)
    rfh_bond_record_init(&g_demo_bond,
                         RFH_TEST_FIXED_ACCESS_ADDRESS,
                         RF_AUTO_DEMO_DISCOVERY_CHANNEL_A,
                         RF_AUTO_DEMO_DISCOVERY_CHANNEL_B,
                         g_demo_rate_code,
                         g_demo_local_id_hash,
                         0u,
                         0u,
                         0u);
    g_demo_has_bond = 1u;
    g_demo_link_access_address = RFH_TEST_FIXED_ACCESS_ADDRESS;
    return;
#endif
    seed = rfh_fnv1a32_mix_u32(g_demo_local_id_hash, 0x52584E42UL);
    g_demo_link_access_address = rfh_access_address_from_seed(seed);
    if(rfh_access_address_valid(g_demo_link_access_address) == 0u)
    {
        seed = rfh_fnv1a32_mix_u32(seed, TMOS_GetSystemClock());
        g_demo_link_access_address = rfh_access_address_from_seed(seed);
    }
}

static void demo_apply_loaded_bond(const rfh_bond_record_t *record)
{
    g_demo_bond = *record;
    g_demo_has_bond = 1u;
    g_demo_link_access_address = record->link_access_address;
    if((rfh_hop_channel_valid(record->channel_a) != 0u) &&
       (rfh_hop_channel_valid(record->channel_b) != 0u) &&
       (record->channel_a != record->channel_b))
    {
        g_demo_bond_channel_a = record->channel_a;
        g_demo_bond_channel_b = record->channel_b;
    }
    else
    {
        g_demo_bond_channel_a = RF_AUTO_DEMO_DISCOVERY_CHANNEL_A;
        g_demo_bond_channel_b = RF_AUTO_DEMO_DISCOVERY_CHANNEL_B;
    }
    if(record->rate_code <= RFH_RATE_8K)
    {
        g_demo_rate_code = record->rate_code;
        g_demo_report_hz = rfh_rate_hz_from_code(record->rate_code);
    }
}

static void demo_load_bond(void)
{
    rfh_bond_record_t record __attribute__((aligned(4)));

#if (RFH_TEST_FIXED_BOND_ENABLE != 0u)
    demo_select_unpaired_address();
    return;
#endif

    memset(&record, 0, sizeof(record));
    if((EEPROM_READ(RFH_BOND_EEPROM_ADDR_DEFAULT, &record, sizeof(record)) == 0u) &&
       (rfh_bond_record_valid(&record) != 0u) &&
       (record.local_id_hash == g_demo_local_id_hash))
    {
        demo_apply_loaded_bond(&record);
        return;
    }
    demo_select_unpaired_address();
}

static uint8_t demo_write_bond_record(const rfh_bond_record_t *record)
{
    rfh_bond_record_t write_record __attribute__((aligned(4)));
    rfh_bond_record_t verify __attribute__((aligned(4)));

    if((record == 0) || (rfh_bond_record_valid(record) == 0u))
    {
        return 0u;
    }

    memcpy(&write_record, record, sizeof(write_record));
    if(EEPROM_ERASE(RFH_BOND_EEPROM_ADDR_DEFAULT, RFH_BOND_EEPROM_ERASE_SIZE) != 0u)
    {
        return 0u;
    }
    if(EEPROM_WRITE(RFH_BOND_EEPROM_ADDR_DEFAULT,
                    &write_record,
                    sizeof(write_record)) != 0u)
    {
        return 0u;
    }
    memset(&verify, 0, sizeof(verify));
    if(EEPROM_READ(RFH_BOND_EEPROM_ADDR_DEFAULT, &verify, sizeof(verify)) != 0u)
    {
        return 0u;
    }
    return (memcmp(&verify, &write_record, sizeof(write_record)) == 0) ? 1u : 0u;
}

static uint8_t demo_save_bond(uint32_t link_access_address,
                              uint32_t peer_id_hash,
                              uint32_t bond_confirm32)
{
    rfh_bond_record_t record __attribute__((aligned(4)));
    uint32_t pair_counter = (g_demo_has_bond != 0u) ?
                            (g_demo_bond.pair_counter + 1u) : 1u;

#if (RFH_TEST_FIXED_BOND_ENABLE != 0u)
    (void)link_access_address;
    (void)peer_id_hash;
    (void)bond_confirm32;
    demo_select_unpaired_address();
    return 1u;
#endif

    rfh_bond_record_init(&record,
                         link_access_address,
                         RF_AUTO_DEMO_DISCOVERY_CHANNEL_A,
                         RF_AUTO_DEMO_DISCOVERY_CHANNEL_B,
                         g_demo_rate_code,
                         g_demo_local_id_hash,
                         peer_id_hash,
                         pair_counter,
                         bond_confirm32);
    if(demo_write_bond_record(&record) == 0u)
    {
        return 0u;
    }
    demo_apply_loaded_bond(&record);
    return 1u;
}

static uint8_t demo_apply_access_address(uint32_t access_address)
{
    if((access_address != RFH_PAIR_ACCESS_ADDRESS) &&
       (rfh_access_address_valid(access_address) == 0u))
    {
        return 0u;
    }

    (void)RFRole_Stop();
    g_demo_rx_active = 0u;
    gParm.accessAddress = access_address;
    RFRole_SetParam(&gParm);
#if (RF_AUTO_DEMO_SEND_ACK_ENABLE != 0u)
    gTxParam.accessAddress = access_address;
#endif
    gRxParam.accessAddress = access_address;
    return 1u;
}

static uint8_t demo_pair_is_active(void)
{
    return ((g_demo_rx_state == RF_AUTO_RX_PAIRING) ||
            (g_demo_rx_state == RF_AUTO_RX_PAIR_CONFIRM_WAIT)) ? 1u : 0u;
}

static uint8_t demo_pair_meta(uint8_t write_bond)
{
    uint8_t meta = (uint8_t)(((uint8_t)RFH_PAIR_PROTO_VERSION << RFH_PAIR_META_VERSION_SHIFT) |
                             (g_demo_rate_code & RFH_PAIR_META_RATE_MASK));
    if(write_bond != 0u)
    {
        meta |= RFH_PAIR_META_WRITE_BOND;
    }
    return meta;
}

static uint8_t demo_pair_meta_valid(uint8_t meta)
{
    return (((meta & RFH_PAIR_META_VERSION_MASK) >> RFH_PAIR_META_VERSION_SHIFT) ==
            RFH_PAIR_PROTO_VERSION) ? 1u : 0u;
}

static void monitor_put_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)(value >> 8);
}

static uint16_t monitor_get_u16(const uint8_t *src)
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static void monitor_put_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)(value >> 24);
}

static uint32_t monitor_get_u32(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static uint32_t monitor_current_flags(void)
{
    uint32_t flags = 0u;

    if(g_monitor_hid_enabled != 0u)
    {
        flags |= RFMON_FLAG_HID_TELEMETRY;
    }
    if(g_monitor_auto_hop_enabled != 0u)
    {
        flags |= RFMON_FLAG_AUTO_HOP;
    }
    return flags;
}

static uint8_t monitor_channel_valid(uint8_t channel)
{
    return rfh_hop_channel_valid(channel);
}

static void monitor_mark_remote_pending(uint8_t seq, uint8_t target, uint32_t flags, uint16_t period_ms)
{
    (void)period_ms;
    g_monitor_pending_seq = seq;
    g_monitor_pending_flags = flags & RFMON_FLAG_AUTO_HOP;
    g_monitor_pending_retries = 12u;
    if((target == RFMON_TARGET_ALL) || (target == RFMON_TARGET_TX))
    {
        g_monitor_tx_status = RFMON_APPLY_PENDING;
    }
    else
    {
        g_monitor_tx_status = RFMON_APPLY_APPLIED;
        g_monitor_tx_applied_seq = seq;
    }
}

uint16_t RF_GetTelemetryPeriodMs(void)
{
    if(g_monitor_hid_enabled == 0u)
    {
        return 0u;
    }
    return g_monitor_hid_period_ms;
}

uint8_t RF_IsTelemetryEnabled(void)
{
    return (g_monitor_hid_enabled != 0u) ? 1u : 0u;
}

uint8_t RF_MonitorControlHandleReport(const uint8_t *report, uint16_t len)
{
    uint32_t magic;
    uint32_t flags;
    uint16_t period_ms;
    uint16_t frame_crc;
    uint16_t calc_crc;
    uint8_t manual_channel;
    uint8_t version;
    uint8_t seq;
    uint8_t target;
    uint8_t cmd;
    uint8_t next_hid_enabled;
    uint8_t manual_channel_valid;

    if((report == 0) || (len < RFMON_CTL_FRAME_SIZE))
    {
        return 0u;
    }

    if((monitor_get_u32(&report[0]) != RFMON_CTL_MAGIC) &&
       (len >= (RFMON_CTL_FRAME_SIZE + 1u)) &&
       (report[0] == 0u) &&
       (monitor_get_u32(&report[1]) == RFMON_CTL_MAGIC))
    {
        report = &report[1];
        len--;
    }

    magic = monitor_get_u32(&report[0]);
    version = report[4];
    seq = report[5];
    target = report[6];
    cmd = report[7];
    flags = monitor_get_u32(&report[8]);
    period_ms = monitor_get_u16(&report[12]);
    frame_crc = monitor_get_u16(&report[14]);
    manual_channel = report[16];
    calc_crc = rfmon_crc16_ccitt(report, 14u);

    if((magic != RFMON_CTL_MAGIC) ||
       (version != RFMON_CTL_VERSION) ||
       (frame_crc != calc_crc) ||
       (rfmon_period_valid(period_ms) == 0u))
    {
        g_monitor_rx_status = RFMON_APPLY_FAILED;
        return 0u;
    }

    if(cmd == RFMON_CMD_GET_CONFIG)
    {
        return 1u;
    }
    if(cmd == RFMON_CMD_TIME_SYNC)
    {
        g_monitor_seq = seq;
        g_monitor_sync_seq = seq;
        g_monitor_sync_pending_retries = 6u;
        return 1u;
    }
    if(cmd != RFMON_CMD_SET_CONFIG)
    {
        g_monitor_rx_status = RFMON_APPLY_FAILED;
        return 0u;
    }
    if(target > RFMON_TARGET_TX)
    {
        g_monitor_rx_status = RFMON_APPLY_FAILED;
        return 0u;
    }

    manual_channel_valid = monitor_channel_valid(manual_channel);
    if(((flags & RFMON_FLAG_AUTO_HOP) == 0u) &&
       (manual_channel_valid == 0u))
    {
        g_monitor_rx_status = RFMON_APPLY_FAILED;
        return 0u;
    }

    g_monitor_seq = seq;
    if((target == RFMON_TARGET_ALL) || (target == RFMON_TARGET_RX))
    {
        next_hid_enabled = ((flags & RFMON_FLAG_HID_TELEMETRY) != 0u) ? 1u : 0u;
        g_monitor_hid_enabled = next_hid_enabled;
        g_monitor_hid_period_ms = next_hid_enabled ? period_ms : RFMON_PERIOD_OFF;
        demo_hid_clear_report_state();
    }
    if((target == RFMON_TARGET_ALL) ||
       (target == RFMON_TARGET_RX) ||
       (target == RFMON_TARGET_TX))
    {
        g_monitor_auto_hop_enabled = ((flags & RFMON_FLAG_AUTO_HOP) != 0u) ? 1u : 0u;
        if(manual_channel_valid != 0u)
        {
            g_monitor_manual_channel = manual_channel;
        }
    }
    g_monitor_rx_status = RFMON_APPLY_APPLIED;

    if((target == RFMON_TARGET_ALL) ||
       (target == RFMON_TARGET_TX))
    {
        monitor_mark_remote_pending(seq, target, flags, period_ms);
    }
    return 1u;
}

void RF_MonitorControlFillReport(uint8_t *report, uint16_t len)
{
    uint16_t crc;
    uint32_t flags;

    if((report == 0) || (len < RFMON_CTL_FRAME_SIZE))
    {
        return;
    }

    memset(report, 0, len);
    flags = monitor_current_flags();
    monitor_put_u32(&report[0], RFMON_CTL_MAGIC);
    report[4] = RFMON_CTL_VERSION;
    report[5] = g_monitor_seq;
    report[6] = g_monitor_rx_status;
    report[7] = g_monitor_tx_status;
    monitor_put_u32(&report[8], flags);
    monitor_put_u16(&report[12], g_monitor_hid_period_ms);
    report[15] = g_monitor_tx_applied_seq;
    report[17] = g_monitor_hid_enabled;
    crc = rfmon_crc16_ccitt(report, 18u);
    monitor_put_u16(&report[18], crc);
}

static uint32_t demo_us_to_tmr_cycles(uint32_t us)
{
    uint64_t cycles = (uint64_t)GetSysClock() * (uint64_t)us;

    cycles = (cycles + 999999u) / 1000000u;
    if(cycles == 0u)
    {
        return 1u;
    }
    return (cycles > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)cycles;
}

static uint32_t demo_rate_to_slot_tmr_cycles(uint16_t hz)
{
    uint64_t cycles;

    if(hz == 0u)
    {
        return demo_us_to_tmr_cycles(RFH_SLOT_US);
    }

    cycles = ((uint64_t)GetSysClock() + (uint64_t)hz - 1u) / (uint64_t)hz;
    if(cycles == 0u)
    {
        return 1u;
    }
    return (cycles > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)cycles;
}

static void demo_apply_rate_code(uint8_t rate_code)
{
    if(rate_code > RFH_RATE_8K)
    {
        return;
    }
    if(rate_code == g_demo_rate_code)
    {
        return;
    }

    g_demo_rate_code = rate_code;
    g_demo_report_hz = rfh_rate_hz_from_code(rate_code);
    g_demo_slot_tmr = demo_rate_to_slot_tmr_cycles(g_demo_report_hz);
}

static uint32_t demo_tmr0_elapsed_cycles(uint32_t start, uint32_t end)
{
    if(end >= start)
    {
        return end - start;
    }
    return (TMR0_FREE_RUN_WRAP - start) + end;
}

static uint16_t demo_tmr_cycles_to_system_ticks(uint32_t cycles)
{
    uint64_t denom = (uint64_t)GetSysClock() * (uint64_t)SYSTEM_TIME_MICROSEN;
    uint64_t ticks;

    if(denom == 0u)
    {
        return 0u;
    }
    ticks = (((uint64_t)cycles * 1000000u) + (denom - 1u)) / denom;
    return (ticks > (uint64_t)RX_HID_SILENT_TICKS_SAT) ?
           RX_HID_SILENT_TICKS_SAT : (uint16_t)ticks;
}

static uint32_t demo_tmr_cycles_to_us_saturated(uint32_t cycles)
{
    uint64_t us;
    uint32_t sys_clock = GetSysClock();

    if(sys_clock == 0u)
    {
        return 0u;
    }
    us = (((uint64_t)cycles * 1000000u) + ((uint64_t)sys_clock - 1u)) /
         (uint64_t)sys_clock;
    return (us > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)us;
}

static uint16_t demo_saturate_u16(uint32_t value)
{
    return (value > 0xFFFFu) ? 0xFFFFu : (uint16_t)value;
}

static uint16_t demo_latency_q8_decode(uint8_t code)
{
    if(code == 0u)
    {
        return 0u;
    }
    if(code <= 128u)
    {
        return (uint16_t)code * 4u;
    }
    if(code <= 224u)
    {
        return (uint16_t)(512u + (((uint16_t)code - 128u) * 16u));
    }
    return (uint16_t)(2048u + (((uint16_t)code - 224u) * 128u));
}

static uint8_t demo_snapshot_data_silent_cycles(uint32_t *cycles)
{
    uint32_t irq_status;
    uint32_t last_tmr;
    uint32_t now_tmr;
    uint8_t active;

    SYS_DisableAllIrq(&irq_status);
    active = g_demo_link_active;
    last_tmr = g_demo_last_data_tmr;
    now_tmr = TMR0_GetCurrentTimer();
    SYS_RecoverIrq(irq_status);

    if(active == 0u)
    {
        return 0u;
    }

    *cycles = demo_tmr0_elapsed_cycles(last_tmr, now_tmr);
    return 1u;
}

static uint16_t demo_clock_delta_ticks(uint32_t start, uint32_t end)
{
    int32_t delta = (int32_t)(end - start);

    if(delta <= 0)
    {
        return 0u;
    }

    return (delta > (int32_t)RX_HID_SILENT_TICKS_SAT) ?
           RX_HID_SILENT_TICKS_SAT : (uint16_t)delta;
}

static uint16_t demo_ticks_to_ms(uint16_t ticks)
{
    uint64_t us;
    uint32_t ms;

    if(ticks == 0u)
    {
        return 0u;
    }

    us = (uint64_t)ticks * (uint64_t)SYSTEM_TIME_MICROSEN;
    ms = (uint32_t)((us + 999u) / 1000u);
    return (ms > 0xFFFFu) ? 0xFFFFu : (uint16_t)ms;
}

static uint16_t demo_clock_delta_ms(uint32_t start, uint32_t end)
{
    return demo_ticks_to_ms(demo_clock_delta_ticks(start, end));
}

static void demo_note_hid_silent_cycles(uint32_t silent_cycles)
{
    if(demo_hid_stats_enabled() == 0u)
    {
        return;
    }
    if(silent_cycles > g_demo_hid_max_silent_cycles)
    {
        g_demo_hid_max_silent_cycles = silent_cycles;
    }
}

static uint16_t demo_quality_permille(void)
{
    uint32_t bad = g_demo_window_missing + g_demo_window_crc;

    if(g_demo_window_expected == 0u)
    {
        return 0u;
    }
    if(bad > g_demo_window_expected)
    {
        bad = g_demo_window_expected;
    }
    return (uint16_t)((bad * 1000u) / g_demo_window_expected);
}

static const rfh_score_weights_t g_demo_score_weights = {
    RF_AUTO_DEMO_SCORE_BASE,
    RF_AUTO_DEMO_SCORE_LOSS_WEIGHT,
    RF_AUTO_DEMO_SCORE_CRC_WEIGHT,
    RF_AUTO_DEMO_SCORE_TYPE_WEIGHT,
    RF_AUTO_DEMO_SCORE_TIMEOUT_WEIGHT,
    RF_AUTO_DEMO_SCORE_IRQ_WEIGHT
};

static uint16_t demo_score_from_metrics(uint16_t loss_permille,
                                        uint16_t crc_permille,
                                        uint16_t type_permille,
                                        uint16_t timeout_permille,
                                        uint16_t irq_permille)
{
    rfh_score_metrics_t metrics;

    metrics.loss_permille = loss_permille;
    metrics.crc_permille = crc_permille;
    metrics.type_permille = type_permille;
    metrics.timeout_permille = timeout_permille;
    metrics.irq_permille = irq_permille;
    return rfh_score_from_metrics(&metrics, &g_demo_score_weights);
}

static uint16_t demo_window_loss_score_sample(uint16_t loss_permille)
{
    return demo_score_from_metrics(loss_permille, 0u, 0u, 0u, 0u);
}

static uint16_t demo_crc_score_sample(void)
{
    return demo_score_from_metrics(0u, 1000u, 0u, 0u, 0u);
}

static uint16_t demo_type_score_sample(void)
{
    return demo_score_from_metrics(0u, 0u, 1000u, 0u, 0u);
}

static uint16_t demo_timeout_score_sample(void)
{
    return demo_score_from_metrics(0u, 0u, 0u, 1000u, 0u);
}

static void demo_reset_quality_window(void)
{
    g_demo_window_expected = 0u;
    g_demo_window_missing = 0u;
    g_demo_window_rx_ok = 0u;
    g_demo_window_crc = 0u;
    g_demo_ack_irq_sum_us = 0u;
    g_demo_ack_irq_count = 0u;
    g_demo_ack_irq_max_us = 0u;
}

static void demo_note_ack_irq_latency(uint16_t rx_irq_us)
{
    if(g_demo_ack_irq_count != 0xFFu)
    {
        g_demo_ack_irq_sum_us += rx_irq_us;
        g_demo_ack_irq_count++;
    }
    if(rx_irq_us > g_demo_ack_irq_max_us)
    {
        g_demo_ack_irq_max_us = rx_irq_us;
    }
}

static uint8_t demo_channel_index(uint8_t channel)
{
    uint8_t i;

    for(i = 0u; i < RFH_HOP_CHANNEL_COUNT; i++)
    {
        if(rfh_hop_channel_at(i) == channel)
        {
            return i;
        }
    }
    return 0xFFu;
}

static uint8_t demo_recovery_scan_channel_by_rank(uint8_t rank)
{
    uint8_t i;
    uint8_t selected = RF_AUTO_DEMO_INITIAL_CHANNEL;
    uint16_t selected_score = 0u;
    uint8_t selected_count = 0u;
    uint8_t count = RFH_HOP_CHANNEL_COUNT;

    if(count == 0u)
    {
        return RF_AUTO_DEMO_INITIAL_CHANNEL;
    }
    rank %= count;

    while(selected_count <= rank)
    {
        uint8_t best_index = 0xFFu;
        uint16_t best_score = 0xFFFFu;

        for(i = 0u; i < count; i++)
        {
            uint16_t score = g_demo_channel_scores[i];
            if((selected_count != 0u) && (score < selected_score))
            {
                continue;
            }
            if((selected_count != 0u) &&
               (score == selected_score) &&
               (rfh_hop_channel_at(i) <= selected))
            {
                continue;
            }
            if((best_index == 0xFFu) ||
               (score < best_score) ||
               ((score == best_score) &&
                (rfh_hop_channel_at(i) < rfh_hop_channel_at(best_index))))
            {
                best_index = i;
                best_score = score;
            }
        }

        if(best_index == 0xFFu)
        {
            break;
        }
        selected = rfh_hop_channel_at(best_index);
        selected_score = best_score;
        selected_count++;
    }

    return selected;
}

static uint8_t demo_next_recovery_channel(void)
{
    uint8_t count = RFH_HOP_CHANNEL_COUNT;
    uint8_t channel;

    if(count == 0u)
    {
        return RF_AUTO_DEMO_INITIAL_CHANNEL;
    }
    channel = demo_recovery_scan_channel_by_rank(g_demo_recovery_scan_rank);
    g_demo_recovery_scan_rank++;
    if(g_demo_recovery_scan_rank >= count)
    {
        g_demo_recovery_scan_rank = 0u;
    }
    return channel;
}

static void demo_score_window_reset_by_index(uint8_t idx, uint32_t now)
{
    if(idx >= RFH_HOP_CHANNEL_COUNT)
    {
        return;
    }

    g_demo_score_windows[idx].window_start_clock = now;
    g_demo_score_windows[idx].sample_score_sum = 0u;
    g_demo_score_windows[idx].sample_count = 0u;
    g_demo_score_windows[idx].active = 1u;
}

static void demo_channel_score_apply_sample_by_index(uint8_t idx, uint16_t sample)
{
    if(idx >= RFH_HOP_CHANNEL_COUNT)
    {
        return;
    }

    g_demo_channel_scores[idx] = rfh_score_clamp(sample);
}

static void demo_score_window_flush_by_index(uint8_t idx, uint32_t now, uint8_t force)
{
    uint32_t elapsed;

    if(idx >= RFH_HOP_CHANNEL_COUNT)
    {
        return;
    }

    if(g_demo_score_windows[idx].active == 0u)
    {
        demo_score_window_reset_by_index(idx, now);
        return;
    }

    elapsed = now - g_demo_score_windows[idx].window_start_clock;
    if((force == 0u) &&
       (elapsed < MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_SCORE_WINDOW_MS)))
    {
        return;
    }

    if(g_demo_score_windows[idx].sample_count != 0u)
    {
        uint16_t sample = rfh_score_clamp(
            (g_demo_score_windows[idx].sample_score_sum +
             (g_demo_score_windows[idx].sample_count / 2u)) /
            g_demo_score_windows[idx].sample_count);

        demo_channel_score_apply_sample_by_index(idx,
                                                 sample);
    }
    demo_score_window_reset_by_index(idx, now);
}

static void demo_score_window_reset_channel(uint8_t channel, uint32_t now)
{
    uint8_t idx = demo_channel_index(channel);

    if(idx != 0xFFu)
    {
        demo_score_window_reset_by_index(idx, now);
    }
}

static void demo_score_windows_service(uint32_t now)
{
    uint8_t idx = demo_channel_index(g_demo_current_channel);

    if(idx != 0xFFu)
    {
        demo_score_window_flush_by_index(idx, now, 0u);
    }
}

static void demo_channel_score_update(uint8_t channel, uint16_t sample)
{
    uint8_t idx = demo_channel_index(channel);
    uint32_t now = TMOS_GetSystemClock();

    if((idx == 0xFFu) || (channel != g_demo_current_channel))
    {
        return;
    }

    demo_score_window_flush_by_index(idx, now, 0u);
    if(g_demo_score_windows[idx].active == 0u)
    {
        demo_score_window_reset_by_index(idx, now);
    }
    g_demo_score_windows[idx].sample_score_sum += rfh_score_clamp(sample);
    g_demo_score_windows[idx].sample_count++;
}

static void demo_channel_scores_init(void)
{
    uint8_t i;
    uint32_t now = TMOS_GetSystemClock();

    for(i = 0u; i < RFH_HOP_CHANNEL_COUNT; i++)
    {
        g_demo_channel_scores[i] = RF_AUTO_DEMO_CHANNEL_SCORE_INIT;
        demo_score_window_reset_by_index(i, now);
    }
    i = demo_channel_index(RF_AUTO_DEMO_INITIAL_CHANNEL);
    if(i != 0xFFu)
    {
        g_demo_channel_scores[i] = RF_AUTO_DEMO_CHANNEL_SCORE_GOOD;
    }
}

static void demo_note_rssi(int8_t rssi)
{
    g_demo_rssi_last = rssi;
    g_demo_rssi_sum += rssi;
    g_demo_rssi_count++;
    if(rssi < g_demo_rssi_min)
    {
        g_demo_rssi_min = rssi;
    }
    if(rssi > g_demo_rssi_max)
    {
        g_demo_rssi_max = rssi;
    }
}

static uint8_t demo_rx_pending_next(uint8_t index)
{
    index++;
    return (index >= RF_RX_PENDING_DEPTH) ? 0u : index;
}

static uint8_t demo_rx_pending_water(uint8_t head, uint8_t tail)
{
    if(head >= tail)
    {
        return (uint8_t)(head - tail);
    }
    return (uint8_t)((RF_RX_PENDING_DEPTH - tail) + head);
}

static void demo_note_rx_pending_water(uint8_t head, uint8_t tail)
{
    uint8_t water = demo_rx_pending_water(head, tail);

    if(water > g_demo_rx_pending_max_water)
    {
        g_demo_rx_pending_max_water = water;
    }
}

static void demo_queue_rx_pending_packet(const uint8_t *rx_buf, uint32_t rx_tmr)
{
    uint8_t head;
    uint8_t next;
    rf_rx_pending_t *pending;

    if(rx_buf == 0)
    {
        return;
    }

    head = g_demo_rx_pending_head;
    next = demo_rx_pending_next(head);
    if(next == g_demo_rx_pending_tail)
    {
        g_demo_rx_pending_tail = demo_rx_pending_next(g_demo_rx_pending_tail);
        g_demo_rx_pending_drop++;
        g_demo_stat.pending_drop++;
        if(demo_hid_stats_enabled() != 0u)
        {
            g_demo_hid_errors++;
        }
    }

    pending = &g_demo_rx_pending[head];
    pending->kind = RF_RX_PENDING_PACKET;
    pending->len = rx_buf[1];
    pending->channel = g_demo_current_channel;
    pending->rx_tmr = rx_tmr;
    if(rx_buf[1] <= RF_AUTO_DEMO_PACKET_LEN)
    {
        memcpy(pending->air, &rx_buf[2], rx_buf[1]);
        if(rx_buf[1] < RF_AUTO_DEMO_PACKET_LEN)
        {
            memset(&pending->air[rx_buf[1]], 0, (uint8_t)(RF_AUTO_DEMO_PACKET_LEN - rx_buf[1]));
        }
    }
    else
    {
        memset(pending->air, 0, sizeof(pending->air));
    }
    g_demo_rx_pending_head = next;
    demo_note_rx_pending_water(next, g_demo_rx_pending_tail);
}

static void demo_queue_rx_pending_crcerr(uint32_t rx_tmr)
{
    uint8_t head = g_demo_rx_pending_head;
    uint8_t next = demo_rx_pending_next(head);
    rf_rx_pending_t *pending;

    if(next == g_demo_rx_pending_tail)
    {
        g_demo_rx_pending_tail = demo_rx_pending_next(g_demo_rx_pending_tail);
        g_demo_rx_pending_drop++;
        g_demo_stat.pending_drop++;
        if(demo_hid_stats_enabled() != 0u)
        {
            g_demo_hid_errors++;
        }
    }

    pending = &g_demo_rx_pending[head];
    pending->kind = RF_RX_PENDING_CRCERR;
    pending->len = 0u;
    pending->channel = g_demo_current_channel;
    pending->rx_tmr = rx_tmr;
    memset(pending->air, 0, sizeof(pending->air));
    g_demo_rx_pending_head = next;
    demo_note_rx_pending_water(next, g_demo_rx_pending_tail);
}

static uint8_t demo_pop_rx_pending(rf_rx_pending_t *pending)
{
    uint32_t irq_status;
    uint8_t tail;

    if(pending == 0)
    {
        return 0u;
    }

    SYS_DisableAllIrq(&irq_status);
    tail = g_demo_rx_pending_tail;
    if(tail == g_demo_rx_pending_head)
    {
        SYS_RecoverIrq(irq_status);
        return 0u;
    }
    memcpy(pending, &g_demo_rx_pending[tail], sizeof(*pending));
    g_demo_rx_pending_tail = demo_rx_pending_next(tail);
    SYS_RecoverIrq(irq_status);
    return 1u;
}

static void demo_set_channel(uint8_t channel)
{
    (void)RFRole_Stop();
    g_demo_rx_active = 0u;
    gParm.frequency = channel;
    RFRole_SetParam(&gParm);
#if (RF_AUTO_DEMO_SEND_ACK_ENABLE != 0u)
    gTxParam.frequency = channel;
    gTxParam.whiteChannel = channel;
#endif
    gRxParam.frequency = channel;
    gRxParam.whiteChannel = channel;
    g_demo_current_channel = channel;
    demo_score_window_reset_channel(channel, TMOS_GetSystemClock());
}

static char demo_rx_state_char(void)
{
    if(g_demo_rx_state == RF_AUTO_RX_UNCONNECTED)
    {
        return 'U';
    }
    if(g_demo_rx_state == RF_AUTO_RX_CONNECT_ACK_PENDING)
    {
        return 'A';
    }
    if(g_demo_rx_state == RF_AUTO_RX_PREPARED_DUAL)
    {
        return 'D';
    }
    if(g_demo_rx_state == RF_AUTO_RX_RECOVERY_SCAN)
    {
        return 'R';
    }
    if(g_demo_rx_state == RF_AUTO_RX_PAIRING)
    {
        return 'O';
    }
    if(g_demo_rx_state == RF_AUTO_RX_PAIR_CONFIRM_WAIT)
    {
        return 'F';
    }
    return 'M';
}

static char demo_rx_connect_stage_char(void)
{
    if(g_demo_connect_stage == RFH_CONNECT_STAGE_SYN)
    {
        return 's';
    }
    if(g_demo_rx_state == RF_AUTO_RX_CONNECT_ACK_PENDING)
    {
        return 'w';
    }
    return '-';
}

static void demo_ack_timer_cancel(void)
{
    TMR1_ITCfg(DISABLE, TMR0_3_IT_CYC_END);
    TMR1_ClearITFlag(TMR0_3_IT_CYC_END);
    TMR1_Disable();
}

static void demo_ack_timer_arm(uint32_t cycles)
{
    demo_ack_timer_cancel();
    TMR1_TimerInit(cycles);
    TMR1_ClearITFlag(TMR0_3_IT_CYC_END);
    TMR1_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
}

static void demo_fill_ack_packet(void)
{
    uint8_t *air = &TxBuf[2];
    uint8_t *data = &air[RFH_DATA_OFFSET];
    uint16_t quality = demo_quality_permille();
    uint16_t score_sample = demo_window_loss_score_sample(quality);
    uint16_t avg_irq_us = 0u;
    uint16_t max_irq_us = 0u;

    if(g_demo_ack_irq_count != 0u)
    {
        uint32_t avg_us = (g_demo_ack_irq_sum_us +
                           ((uint32_t)g_demo_ack_irq_count / 2u)) /
                          (uint32_t)g_demo_ack_irq_count;

        avg_irq_us = (avg_us > 0xFFFFu) ? 0xFFFFu : (uint16_t)avg_us;
        max_irq_us = g_demo_ack_irq_max_us;
    }

    memset(TxBuf, 0, sizeof(TxBuf));
    TxBuf[0] = RFH_WCH_PREAMBLE;
    TxBuf[1] = RF_AUTO_DEMO_PACKET_LEN;
    air[0] = rfh_make_header0(RFH_PKT_ACK, g_demo_rate_code, RFH_FLAG_LINK_OK);
    air[1] = g_demo_ack_seq;
    demo_channel_score_update(g_demo_current_channel, score_sample);
    rfh_put_u16(&data[RFH_ACK_LOSS_PERMILLE_LO], quality);
    rfh_put_u16(&data[RFH_ACK_AVG_IRQ_US_LO], avg_irq_us);
    rfh_put_u16(&data[RFH_ACK_MAX_IRQ_US_LO], max_irq_us);
    if(g_demo_pending_ack_cmd != RFH_CMD_NONE)
    {
        data[RFH_ACK_CMD_ID] = g_demo_pending_ack_cmd;
        data[RFH_ACK_FLAGS] = RFH_FLAG_CMD_ACK;
        data[RFH_ACK_CHANNEL] = g_demo_current_channel;
        data[RFH_ACK_STATUS] = g_demo_pending_ack_seq;
    }
    else if(g_demo_confirm_ack_keep_count != 0u)
    {
        data[RFH_ACK_CMD_ID] = RFH_CMD_HOP_CONFIRM;
        data[RFH_ACK_FLAGS] = RFH_FLAG_CMD_ACK;
        data[RFH_ACK_CHANNEL] = g_demo_confirm_ack_keep_channel;
        data[RFH_ACK_STATUS] = g_demo_confirm_ack_keep_seq;
        g_demo_confirm_ack_keep_count--;
    }
    else if(g_monitor_sync_pending_retries != 0u)
    {
        data[RFH_ACK_CMD_ID] = RFH_CMD_TIME_SYNC;
        data[RFH_ACK_FLAGS] = RFH_FLAG_CMD_ACK;
        data[RFH_ACK_CHANNEL] = g_demo_current_channel;
        data[RFH_ACK_STATUS] = g_monitor_sync_seq;
        g_monitor_sync_pending_retries--;
    }
    else if(g_monitor_pending_retries != 0u)
    {
        data[RFH_ACK_CMD_ID] = RFH_CMD_MONITOR_CONFIG;
        data[RFH_ACK_MON_FLAGS] = (uint8_t)(g_monitor_pending_flags & 0xFFu);
        data[RFH_ACK_MON_MANUAL_CHANNEL] = g_monitor_manual_channel;
        data[RFH_ACK_MON_SEQ] = g_monitor_pending_seq;
        g_monitor_pending_retries--;
        if(g_monitor_pending_retries == 0u)
        {
            if(g_monitor_tx_status == RFMON_APPLY_PENDING)
            {
                g_monitor_tx_status = RFMON_APPLY_FAILED;
            }
        }
    }
    else
    {
        if(g_demo_ack_score_hint_index < RFH_HOP_CHANNEL_COUNT)
        {
            uint16_t score = g_demo_channel_scores[g_demo_ack_score_hint_index];

            data[RFH_ACK_CMD_ID] = RFH_CMD_SCORE_HINT;
            data[RFH_ACK_FLAGS] = 0u;
            data[RFH_ACK_CHANNEL] = rfh_hop_channel_at(g_demo_ack_score_hint_index);
            data[RFH_ACK_STATUS] = (score >= 1000u) ? 250u :
                                   (uint8_t)((score + 2u) / 4u);
            g_demo_ack_score_hint_index++;
            if(g_demo_ack_score_hint_index >= RFH_HOP_CHANNEL_COUNT)
            {
                g_demo_ack_score_hint_index = 0u;
            }
        }
        else
        {
            data[RFH_ACK_CMD_ID] = RFH_CMD_NONE;
            data[RFH_ACK_FLAGS] = 0u;
            data[RFH_ACK_CHANNEL] = g_demo_current_channel;
            data[RFH_ACK_STATUS] = 0u;
        }
    }
    demo_reset_quality_window();
}

static void demo_arm_rx(void)
{
    bStatus_t ret;
    uint8_t slot;

    if(g_demo_config_ret != SUCCESS)
    {
        return;
    }
    if(g_demo_rx_active != 0u)
    {
        return;
    }

    g_demo_tx_parm_ret = SUCCESS;

    slot = g_demo_rx_next_slot;
    if(slot >= RF_RX_DMA_SLOT_COUNT)
    {
        slot = 0u;
    }
    gRxParam.rxDMA = (uint32_t)RxBuf[slot];
    ret = RFIP_SetRx(&gRxParam);
    g_demo_rx_ret = (uint8_t)ret;
    g_demo_stat.rx_arm++;
    if(ret == SUCCESS)
    {
        g_demo_rx_active = 1u;
        g_demo_rx_active_slot = slot;
        g_demo_rx_next_slot = (uint8_t)(slot + 1u);
        if(g_demo_rx_next_slot >= RF_RX_DMA_SLOT_COUNT)
        {
            g_demo_rx_next_slot = 0u;
        }
#if (RF_AUTO_DEMO_SEND_ACK_ENABLE != 0u)
        g_demo_ack_seq++;
#endif
    }
    else
    {
        g_demo_stat.rx_arm_fail++;
        g_demo_rearm_pending = 1u;
    }
}

static uint8_t demo_discovery_channel(uint8_t side)
{
    return ((side & 1u) == 0u) ?
           g_demo_bond_channel_b :
           g_demo_bond_channel_a;
}

static uint8_t demo_manual_fixed_channel(uint8_t *channel)
{
    if((g_monitor_auto_hop_enabled == 0u) &&
       (monitor_channel_valid(g_monitor_manual_channel) != 0u))
    {
        if(channel != 0)
        {
            *channel = g_monitor_manual_channel;
        }
        return 1u;
    }
    return 0u;
}

static void demo_enter_rx_unconnected(uint32_t now)
{
    uint8_t anchor_channel = demo_discovery_channel(0u);

    (void)demo_manual_fixed_channel(&anchor_channel);
    g_demo_link_active = 0u;
    g_demo_rx_state = RF_AUTO_RX_UNCONNECTED;
    g_demo_pending_ack_cmd = RFH_CMD_NONE;
    g_demo_pending_ack_seq = 0u;
    g_demo_after_ack_action = 0u;
    g_demo_confirm_ack_keep_count = 0u;
    g_demo_connect_stage = 0u;
    g_demo_connect_until_clock = 0u;
    g_demo_connect_next_tx_clock = 0u;
    g_demo_have_ack_token = 0u;
    g_demo_old_channel = anchor_channel;
    g_demo_target_channel = anchor_channel;
    g_demo_dual_side = 0u;
    g_demo_dual_switch_clock = now;
    if(g_demo_config_ret != SUCCESS)
    {
        return;
    }
    if(g_demo_has_bond == 0u)
    {
        (void)RFRole_Stop();
        g_demo_rx_active = 0u;
        return;
    }
    demo_set_channel(anchor_channel);
    demo_arm_rx();
}

static void demo_service_unconnected_scan(uint32_t now)
{
    uint8_t fixed_channel;

    if(g_demo_config_ret != SUCCESS)
    {
        return;
    }
    if(g_demo_rx_state != RF_AUTO_RX_UNCONNECTED)
    {
        return;
    }
    if(g_demo_has_bond == 0u)
    {
        return;
    }
    if(demo_manual_fixed_channel(&fixed_channel) != 0u)
    {
        if(g_demo_current_channel != fixed_channel)
        {
            demo_set_channel(fixed_channel);
            demo_arm_rx();
        }
        return;
    }
    if((uint32_t)(now - g_demo_dual_switch_clock) <
       MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_DISCOVERY_SCAN_DWELL_MS))
    {
        return;
    }

    g_demo_dual_switch_clock = now;
    g_demo_dual_side++;
    demo_set_channel(demo_discovery_channel(g_demo_dual_side));
    demo_arm_rx();
}

static void demo_send_ack(void)
{
    bStatus_t ret;

    g_demo_ack_pending = 0u;
    (void)RFRole_Stop();
    g_demo_rx_active = 0u;
    demo_fill_ack_packet();
    gTxParam.txDMA = (uint32_t)TxBuf;
    g_demo_tx_start_ret = (uint8_t)RFIP_SetTxStart();
    if(g_demo_tx_start_ret != SUCCESS)
    {
        g_demo_stat.ack_fail++;
        g_demo_rearm_pending = 1u;
        return;
    }

    ret = RFIP_SetTxParm(&gTxParam);
    g_demo_tx_parm_ret = (uint8_t)ret;
    if(ret != SUCCESS)
    {
        g_demo_stat.tx_parm_fail++;
        g_demo_stat.ack_fail++;
        g_demo_rearm_pending = 1u;
    }
}

static void demo_schedule_ack(uint8_t remaining_slots)
{
    g_demo_ack_pending = 1u;
    demo_ack_timer_arm(g_demo_ack_delay_tmr + ((uint32_t)remaining_slots * g_demo_slot_tmr));
}

static void demo_fill_pair_packet(uint8_t cmd, uint32_t arg32)
{
    uint8_t *air = &TxBuf[2];
    uint8_t *data = &air[RFH_DATA_OFFSET];
    uint8_t meta = demo_pair_meta(0u);

    memset(TxBuf, 0, sizeof(TxBuf));
    TxBuf[0] = RFH_WCH_PREAMBLE;
    TxBuf[1] = RF_AUTO_DEMO_PACKET_LEN;
    air[RFH_HDR0_OFFSET] = rfh_make_header0(RFH_PKT_PAIR, g_demo_rate_code, 0u);
    air[RFH_HDR1_OFFSET] = (uint8_t)g_demo_pair_session;
    data[RFH_PAIR_CMD_ID] = cmd;
    rfh_put_u32(&data[RFH_PAIR_SESSION0], g_demo_pair_session);
    rfh_put_u32(&data[RFH_PAIR_ARG0], arg32);
    if(cmd == RFH_CMD_PAIR_DONE)
    {
        meta = demo_pair_meta(1u);
    }
    data[RFH_PAIR_META] = meta;
}

static uint8_t demo_send_pair_packet(uint8_t cmd,
                                     uint32_t arg32,
                                     uint32_t access_address,
                                     uint8_t after_action)
{
    bStatus_t ret;

    if((g_demo_config_ret != SUCCESS) || (demo_pair_is_active() == 0u))
    {
        return 0u;
    }

    (void)RFRole_Stop();
    g_demo_rx_active = 0u;
    g_demo_pair_tx_active = 1u;
    g_demo_pair_after_tx_action = after_action;
    demo_fill_pair_packet(cmd, arg32);
    gTxParam.txDMA = (uint32_t)TxBuf;
    gTxParam.accessAddress = access_address;
    gTxParam.frequency = RFH_PAIR_CHANNEL_A;
    gTxParam.whiteChannel = RFH_PAIR_CHANNEL_A;
    g_demo_tx_start_ret = (uint8_t)RFIP_SetTxStart();
    if(g_demo_tx_start_ret != SUCCESS)
    {
        g_demo_pair_tx_active = 0u;
        g_demo_pair_after_tx_action = 0u;
        g_demo_stat.ack_fail++;
        return 0u;
    }
    ret = RFIP_SetTxParm(&gTxParam);
    g_demo_tx_parm_ret = (uint8_t)ret;
    if(ret != SUCCESS)
    {
        g_demo_pair_tx_active = 0u;
        g_demo_pair_after_tx_action = 0u;
        g_demo_stat.tx_parm_fail++;
        g_demo_stat.ack_fail++;
        return 0u;
    }
    return 1u;
}

static void demo_abort_pairing(uint32_t now)
{
    g_demo_pair_tx_active = 0u;
    g_demo_pair_after_tx_action = 0u;
    g_demo_pair_done_repeat_left = 0u;
    g_demo_ack_pending = 0u;
    demo_ack_timer_cancel();
    (void)demo_apply_access_address(g_demo_link_access_address);
    demo_enter_rx_unconnected(now);
}

static void demo_after_pair_tx_finish(void)
{
    uint8_t action = g_demo_pair_after_tx_action;

    g_demo_pair_tx_active = 0u;
    g_demo_pair_after_tx_action = 0u;

    if(action == RF_AUTO_DEMO_PAIR_AFTER_ACCEPT)
    {
        (void)demo_apply_access_address(RFH_PAIR_ACCESS_ADDRESS);
        demo_set_channel(RFH_PAIR_CHANNEL_A);
        demo_arm_rx();
    }
    else if(action == RF_AUTO_DEMO_PAIR_AFTER_DONE)
    {
        if(g_demo_pair_done_repeat_left != 0u)
        {
            g_demo_pair_done_repeat_left--;
        }
        if(g_demo_pair_done_repeat_left != 0u)
        {
            (void)demo_send_pair_packet(RFH_CMD_PAIR_DONE,
                                        g_demo_pair_done_confirm32,
                                        g_demo_pair_link_access_address,
                                        RF_AUTO_DEMO_PAIR_AFTER_DONE);
            return;
        }
        (void)demo_apply_access_address(g_demo_link_access_address);
        demo_enter_rx_unconnected(TMOS_GetSystemClock());
    }
    else if(action == RF_AUTO_DEMO_PAIR_AFTER_REJECT)
    {
        demo_abort_pairing(TMOS_GetSystemClock());
    }
}

static uint8_t demo_process_pair_packet(const rf_rx_pending_t *pending)
{
    const uint8_t *air;
    const uint8_t *data;
    uint8_t cmd;
    uint8_t meta;
    uint32_t session;
    uint32_t arg32;

    if((pending == 0) || (pending->len != RF_AUTO_DEMO_PACKET_LEN))
    {
        return 0u;
    }
    if(demo_pair_is_active() == 0u)
    {
        return 0u;
    }

    air = pending->air;
    data = &air[RFH_DATA_OFFSET];
    if(rfh_packet_type(air[RFH_HDR0_OFFSET]) != RFH_PKT_PAIR)
    {
        return 0u;
    }

    cmd = data[RFH_PAIR_CMD_ID];
    session = rfh_get_u32(&data[RFH_PAIR_SESSION0]);
    arg32 = rfh_get_u32(&data[RFH_PAIR_ARG0]);
    meta = data[RFH_PAIR_META];
    if(demo_pair_meta_valid(meta) == 0u)
    {
        (void)demo_send_pair_packet(RFH_CMD_PAIR_REJECT,
                                    RFH_PAIR_REJECT_BAD_VERSION,
                                    RFH_PAIR_ACCESS_ADDRESS,
                                    RF_AUTO_DEMO_PAIR_AFTER_REJECT);
        return 1u;
    }

    if((g_demo_rx_state == RF_AUTO_RX_PAIRING) &&
       (cmd == RFH_CMD_PAIR_OFFER) &&
       (arg32 != 0u))
    {
        g_demo_pair_session = session;
        g_demo_pair_tx_id_hash = arg32;
        g_demo_pair_rx_id_hash = g_demo_local_id_hash;
        g_demo_pair_link_access_address = 0u;
        g_demo_pair_done_confirm32 = 0u;
        g_demo_pair_done_repeat_left = 0u;
        g_demo_rx_state = RF_AUTO_RX_PAIR_CONFIRM_WAIT;
        g_demo_pair_confirm_deadline_clock =
            TMOS_GetSystemClock() + MS1_TO_SYSTEM_TIME(RFH_PAIR_CONFIRM_TIMEOUT_MS);
        (void)demo_send_pair_packet(RFH_CMD_PAIR_ACCEPT,
                                    g_demo_pair_rx_id_hash,
                                    RFH_PAIR_ACCESS_ADDRESS,
                                    RF_AUTO_DEMO_PAIR_AFTER_ACCEPT);
        g_demo_stat.hop_event++;
        return 1u;
    }

    if((g_demo_rx_state == RF_AUTO_RX_PAIR_CONFIRM_WAIT) &&
       (cmd == RFH_CMD_PAIR_CONFIRM) &&
       (session == g_demo_pair_session) &&
       ((meta & RFH_PAIR_META_WRITE_BOND) != 0u))
    {
        g_demo_pair_link_access_address = arg32;
        if(rfh_access_address_valid(g_demo_pair_link_access_address) == 0u)
        {
            (void)demo_send_pair_packet(RFH_CMD_PAIR_REJECT,
                                        RFH_PAIR_REJECT_BAD_ADDRESS,
                                        RFH_PAIR_ACCESS_ADDRESS,
                                        RF_AUTO_DEMO_PAIR_AFTER_REJECT);
            return 1u;
        }
        g_demo_pair_done_confirm32 =
            rfh_pair_confirm32(g_demo_pair_session,
                               g_demo_pair_tx_id_hash,
                               g_demo_pair_rx_id_hash,
                               g_demo_pair_link_access_address);
        if(demo_save_bond(g_demo_pair_link_access_address,
                          g_demo_pair_tx_id_hash,
                          g_demo_pair_done_confirm32) == 0u)
        {
            (void)demo_send_pair_packet(RFH_CMD_PAIR_REJECT,
                                        RFH_PAIR_REJECT_BOND_FAILED,
                                        RFH_PAIR_ACCESS_ADDRESS,
                                        RF_AUTO_DEMO_PAIR_AFTER_REJECT);
            return 1u;
        }
        g_demo_pair_done_repeat_left = RF_AUTO_DEMO_PAIR_DONE_REPEAT_COUNT;
        (void)demo_apply_access_address(g_demo_pair_link_access_address);
        (void)demo_send_pair_packet(RFH_CMD_PAIR_DONE,
                                    g_demo_pair_done_confirm32,
                                    g_demo_pair_link_access_address,
                                    RF_AUTO_DEMO_PAIR_AFTER_DONE);
        g_demo_stat.hop_event++;
        return 1u;
    }

    return 1u;
}

static void demo_service_pairing(uint32_t now)
{
    if(demo_pair_is_active() == 0u)
    {
        return;
    }
    if((int32_t)(now - g_demo_pair_deadline_clock) >= 0)
    {
        demo_abort_pairing(now);
        return;
    }
    if((g_demo_rx_state == RF_AUTO_RX_PAIR_CONFIRM_WAIT) &&
       (g_demo_pair_tx_active == 0u) &&
       ((int32_t)(now - g_demo_pair_confirm_deadline_clock) >= 0))
    {
        g_demo_rx_state = RF_AUTO_RX_PAIRING;
        g_demo_pair_session = 0u;
        g_demo_pair_tx_id_hash = 0u;
        g_demo_pair_link_access_address = 0u;
        g_demo_pair_done_repeat_left = 0u;
        (void)demo_apply_access_address(RFH_PAIR_ACCESS_ADDRESS);
        demo_set_channel(RFH_PAIR_CHANNEL_A);
        demo_arm_rx();
        return;
    }
    if((g_demo_rx_state == RF_AUTO_RX_PAIRING) &&
       (g_demo_pair_tx_active == 0u) &&
       ((uint32_t)(now - g_demo_pair_scan_clock) >=
        MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_DISCOVERY_SCAN_DWELL_MS)))
    {
        g_demo_pair_scan_clock = now;
        g_demo_pair_scan_side ^= 1u;
        demo_set_channel((g_demo_pair_scan_side == 0u) ?
                         RFH_PAIR_CHANNEL_A : RFH_PAIR_CHANNEL_B);
        demo_arm_rx();
    }
}

static uint8_t demo_note_data_seq(uint8_t seq)
{
    uint8_t diff;

    if(g_demo_have_data_seq == 0u)
    {
        g_demo_have_data_seq = 1u;
        g_demo_window_expected++;
        if(demo_hid_stats_enabled() != 0u)
        {
            g_demo_hid_expected++;
        }
    }
    else
    {
        diff = (uint8_t)(seq - g_demo_last_data_seq);
        if(diff == 0u)
        {
            return 0u;
        }
        g_demo_window_expected += diff;
        if(demo_hid_stats_enabled() != 0u)
        {
            g_demo_hid_expected += diff;
        }
        if(diff > 1u)
        {
            g_demo_window_missing += (uint32_t)(diff - 1u);
            g_demo_stat.seq_gap += (uint32_t)(diff - 1u);
            if(demo_hid_stats_enabled() != 0u)
            {
                g_demo_hid_bad += (uint32_t)(diff - 1u);
                g_demo_air_diag_seq_gap += (uint32_t)(diff - 1u);
            }
        }
    }

    g_demo_last_data_seq = seq;
    g_demo_window_rx_ok++;
    if(demo_hid_stats_enabled() != 0u)
    {
        g_demo_hid_rx_ok++;
        g_demo_air_diag_rx_ok++;
    }
    return 1u;
}

static uint8_t demo_input_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0u;
    uint8_t i;

    for(i = 0u; i < len; i++)
    {
        uint8_t bit;

        crc = (uint8_t)(crc ^ data[i]);
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

static uint32_t demo_input_key_mask(const uint8_t *payload)
{
    return monitor_get_u32(&payload[RF_INPUT_KEY_MASK_OFFSET]);
}

static uint32_t demo_input_sample_tick_us(const uint8_t *payload)
{
    return monitor_get_u32(&payload[RF_INPUT_SAMPLE_TICK_OFFSET]);
}

static uint8_t demo_input_stm32_age_q8(const uint8_t *payload)
{
    return payload[RF_INPUT_SAMPLE_TICK_OFFSET];
}

static uint8_t demo_input_tx_wait_q8(const uint8_t *payload)
{
    return payload[RF_INPUT_SAMPLE_TICK_OFFSET + 1u];
}

static uint8_t demo_decode_short_input_payload(uint8_t *dst, uint8_t seq, const uint8_t *src)
{
    if((dst == 0) || (src == 0))
    {
        return 0u;
    }

    dst[0] = seq;
    dst[1] = (uint8_t)((RF_INPUT_FORMAT_VERSION_V1 << RF_INPUT_FORMAT_VERSION_SHIFT) |
                       RF_INPUT_FLAG_PROCESSED);
    dst[2] = src[0];
    dst[3] = src[1];
    dst[4] = src[2];
    dst[5] = 0u;
    dst[6] = src[3];
    dst[7] = src[4];
    dst[8] = 0u;
    dst[RF_INPUT_CRC_OFFSET] = demo_input_crc8(dst, (uint8_t)(RF_INPUT_PAYLOAD_LEN - 1u));
    return 1u;
}

static uint8_t demo_decode_v1_input_payload(uint8_t *dst, const uint8_t *src)
{
    uint8_t crc;

    if((dst == 0) || (src == 0))
    {
        return 0u;
    }
    crc = demo_input_crc8(src, (uint8_t)(RFMON_INPUT_PAYLOAD_V1_LEN - 1u));
    if(crc != src[RFMON_INPUT_PAYLOAD_V1_LEN - 1u])
    {
        return 0u;
    }
    memset(dst, 0, RF_INPUT_PAYLOAD_LEN);
    memcpy(dst, src, RFMON_INPUT_PAYLOAD_V1_LEN - 1u);
    dst[RF_INPUT_CRC_OFFSET] = demo_input_crc8(dst, (uint8_t)(RF_INPUT_PAYLOAD_LEN - 1u));
    return 1u;
}

static void demo_queue_latency_sync_echo(uint8_t sync_seq,
                                         uint32_t sync_rx_tick_us,
                                         uint32_t sync_tx_tick_us)
{
    if(demo_hid_stats_enabled() == 0u)
    {
        return;
    }

    g_demo_hid_latency_key_mask = g_demo_hid_input_key_mask;
    g_demo_hid_latency_sample_tick_us = 0u;
    g_demo_hid_latency_stm32_us = 0u;
    g_demo_hid_latency_tx_us = 0u;
    g_demo_hid_latency_rx_us = 0u;
    g_demo_hid_latency_rx_irq_us = 0u;
    g_demo_hid_latency_rx_decode_us = 0u;
    g_demo_hid_latency_rx_epwait_us = 0u;
    g_demo_hid_latency_rx_submit_us = 0u;
    g_demo_hid_latency_stage_flags = 0u;
    g_demo_hid_latency_input_seq = g_demo_hid_input_seq;
    g_demo_hid_latency_input_flags =
        (uint8_t)((RF_INPUT_FORMAT_VERSION_V2 << RF_INPUT_FORMAT_VERSION_SHIFT) |
                  RF_INPUT_FLAG_PROCESSED |
                  RF_INPUT_FLAG_SYNC_ECHO);
    g_demo_hid_latency_sync_seq = sync_seq;
    g_demo_hid_latency_sync_rx_tick_us = sync_rx_tick_us;
    g_demo_hid_latency_sync_tx_tick_us = sync_tx_tick_us;
    g_demo_hid_latency_v2 = 0u;
    g_demo_hid_latency_pending = 1u;
}

static void demo_queue_latency_input(uint8_t input_seq,
                                     uint32_t key_mask,
                                     uint32_t latency_us,
                                     uint16_t stm32_us,
                                     uint16_t tx_us,
                                     uint16_t rx_us,
                                     uint8_t stage_flags,
                                     uint8_t input_flags)
{
    if(latency_us == 0u)
    {
        return;
    }
    if(demo_hid_stats_enabled() == 0u)
    {
        return;
    }
    g_demo_hid_latency_key_mask = key_mask & RF_INPUT_KEY_MASK_VALID;
    g_demo_hid_latency_sample_tick_us = latency_us;
    g_demo_hid_latency_stm32_us = stm32_us;
    g_demo_hid_latency_tx_us = tx_us;
    g_demo_hid_latency_rx_us = rx_us;
    g_demo_hid_latency_rx_irq_us = 0u;
    g_demo_hid_latency_rx_decode_us = 0u;
    g_demo_hid_latency_rx_epwait_us = 0u;
    g_demo_hid_latency_rx_submit_us = 0u;
    g_demo_hid_latency_stage_flags = stage_flags;
    g_demo_hid_latency_input_seq = input_seq;
    g_demo_hid_latency_input_flags = input_flags;
    g_demo_hid_latency_sync_seq = 0u;
    g_demo_hid_latency_sync_rx_tick_us = 0u;
    g_demo_hid_latency_sync_tx_tick_us = 0u;
    g_demo_hid_latency_v2 = 0u;
    g_demo_hid_latency_pending = 1u;
}

static void demo_queue_latency_input_v2(uint8_t input_seq,
                                        uint32_t key_mask,
                                        uint32_t latency_us,
                                        uint16_t stm32_us,
                                        uint16_t tx_us,
                                        uint16_t rx_us,
                                        uint16_t rx_irq_us,
                                        uint16_t rx_decode_us,
                                        uint16_t rx_epwait_us,
                                        uint16_t rx_submit_us,
                                        uint8_t stage_flags,
                                        uint8_t input_flags)
{
    if(demo_hid_stats_enabled() != 0u)
    {
        demo_queue_latency_input(input_seq,
                                 key_mask,
                                 latency_us,
                                 stm32_us,
                                 tx_us,
                                 rx_us,
                                 stage_flags,
                                 input_flags);
        g_demo_hid_latency_rx_irq_us = rx_irq_us;
        g_demo_hid_latency_rx_decode_us = rx_decode_us;
        g_demo_hid_latency_rx_epwait_us = rx_epwait_us;
        g_demo_hid_latency_rx_submit_us = rx_submit_us;
        g_demo_hid_latency_v2 = 1u;
    }
    demo_note_ack_irq_latency(rx_irq_us);
}

static void demo_queue_xinput_latency_pending(const uint8_t *payload,
                                              uint32_t rx_tmr,
                                              uint32_t process_tmr)
{
    uint8_t stm32_age_q8;
    uint8_t tx_wait_q8;
    uint32_t irq_status;

    if(payload == 0)
    {
        return;
    }
    stm32_age_q8 = demo_input_stm32_age_q8(payload);
    tx_wait_q8 = demo_input_tx_wait_q8(payload);
    if(stm32_age_q8 == 0u)
    {
        return;
    }

    SYS_DisableAllIrq(&irq_status);
    g_demo_xinput_latency_stm32_q8 = stm32_age_q8;
    g_demo_xinput_latency_tx_q8 = tx_wait_q8;
    g_demo_xinput_latency_rx_tmr = rx_tmr;
    g_demo_xinput_latency_process_tmr = process_tmr;
    g_demo_xinput_latency_report_tmr = 0u;
    g_demo_xinput_latency_key_mask = demo_input_key_mask(payload);
    g_demo_xinput_latency_input_seq = payload[RF_INPUT_SEQ_OFFSET];
    g_demo_xinput_latency_input_flags = payload[RF_INPUT_FLAGS_OFFSET];
    g_demo_xinput_latency_pending = 1u;
    SYS_RecoverIrq(irq_status);
}

static void demo_complete_xinput_latency_if_pending(uint32_t submit_tmr,
                                                    uint32_t submit_done_tmr)
{
    uint8_t stm32_age_q8;
    uint8_t tx_wait_q8;
    uint32_t rx_tmr;
    uint32_t process_tmr;
    uint32_t report_tmr;
    uint32_t key_mask;
    uint8_t input_seq;
    uint8_t input_flags;
    uint32_t irq_status;
    uint32_t wait_us;
    uint32_t irq_us;
    uint32_t decode_us;
    uint32_t epwait_us;
    uint32_t submit_us;
    uint16_t stm32_us;
    uint16_t tx_us;
    uint16_t rx_us;
    uint16_t rx_irq_us;
    uint16_t rx_decode_us;
    uint16_t rx_epwait_us;
    uint16_t rx_submit_us;
    uint8_t stage_flags = RX_LATENCY_STAGE_FLAG_SPLIT;
    uint64_t latency_us;

    SYS_DisableAllIrq(&irq_status);
    if(g_demo_xinput_latency_pending == 0u)
    {
        SYS_RecoverIrq(irq_status);
        return;
    }
    stm32_age_q8 = g_demo_xinput_latency_stm32_q8;
    tx_wait_q8 = g_demo_xinput_latency_tx_q8;
    rx_tmr = g_demo_xinput_latency_rx_tmr;
    process_tmr = g_demo_xinput_latency_process_tmr;
    report_tmr = g_demo_xinput_latency_report_tmr;
    key_mask = g_demo_xinput_latency_key_mask;
    input_seq = g_demo_xinput_latency_input_seq;
    input_flags = g_demo_xinput_latency_input_flags;
    g_demo_xinput_latency_pending = 0u;
    SYS_RecoverIrq(irq_status);

    if(report_tmr == 0u)
    {
        report_tmr = submit_tmr;
    }
    wait_us = demo_tmr_cycles_to_us_saturated(
        demo_tmr0_elapsed_cycles(rx_tmr, submit_tmr));
    irq_us = demo_tmr_cycles_to_us_saturated(
        demo_tmr0_elapsed_cycles(rx_tmr, process_tmr));
    decode_us = demo_tmr_cycles_to_us_saturated(
        demo_tmr0_elapsed_cycles(process_tmr, report_tmr));
    epwait_us = demo_tmr_cycles_to_us_saturated(
        demo_tmr0_elapsed_cycles(report_tmr, submit_tmr));
    submit_us = demo_tmr_cycles_to_us_saturated(
        demo_tmr0_elapsed_cycles(submit_tmr, submit_done_tmr));
    stm32_us = demo_latency_q8_decode(stm32_age_q8);
    tx_us = demo_latency_q8_decode(tx_wait_q8);
    rx_us = demo_saturate_u16(wait_us);
    rx_irq_us = demo_saturate_u16(irq_us);
    rx_decode_us = demo_saturate_u16(decode_us);
    rx_epwait_us = demo_saturate_u16(epwait_us);
    rx_submit_us = demo_saturate_u16(submit_us);
    if(stm32_age_q8 == 255u)
    {
        stage_flags |= RX_LATENCY_STAGE_FLAG_STM32_SAT;
    }
    if(tx_wait_q8 == 255u)
    {
        stage_flags |= RX_LATENCY_STAGE_FLAG_TX_SAT;
    }
    if(wait_us > 0xFFFFu)
    {
        stage_flags |= RX_LATENCY_STAGE_FLAG_RX_SAT;
    }
    if((irq_us > 0xFFFFu) || (decode_us > 0xFFFFu) ||
       (epwait_us > 0xFFFFu) || (submit_us > 0xFFFFu))
    {
        stage_flags |= RX_LATENCY_STAGE_FLAG_RX_SAT;
    }
    latency_us = (uint64_t)stm32_us + (uint64_t)tx_us + (uint64_t)rx_us;
    if(latency_us > 0xFFFFFFFFu)
    {
        latency_us = 0xFFFFFFFFu;
    }

    demo_queue_latency_input_v2(input_seq,
                                key_mask,
                                (uint32_t)latency_us,
                                stm32_us,
                                tx_us,
                                rx_us,
                                rx_irq_us,
                                rx_decode_us,
                                rx_epwait_us,
                                rx_submit_us,
                                stage_flags,
                                input_flags);
}

static void demo_queue_input_payload(const uint8_t *payload)
{
    uint32_t gen;
    uint8_t i;

    if(payload == 0)
    {
        return;
    }

    gen = g_demo_pending_input_gen + 1u;
    if((gen & 1u) == 0u)
    {
        gen++;
    }
    g_demo_pending_input_gen = gen;
    for(i = 0u; i < RF_INPUT_PAYLOAD_LEN; i++)
    {
        g_demo_pending_input_payload[i] = payload[i];
    }
    g_demo_pending_input_valid = 1u;
    g_demo_pending_input_gen = gen + 1u;
}

static uint8_t demo_snapshot_pending_input(uint8_t *payload, uint32_t *gen_out)
{
    uint32_t gen0;
    uint32_t gen1;
    uint8_t i;

    if((payload == 0) || (gen_out == 0))
    {
        return 0u;
    }

    gen0 = g_demo_pending_input_gen;
    if((gen0 & 1u) != 0u)
    {
        return 0u;
    }
    if(g_demo_pending_input_valid == 0u)
    {
        return 0u;
    }

    for(i = 0u; i < RF_INPUT_PAYLOAD_LEN; i++)
    {
        payload[i] = g_demo_pending_input_payload[i];
    }

    gen1 = g_demo_pending_input_gen;
    if((gen0 != gen1) || ((gen1 & 1u) != 0u))
    {
        return 0u;
    }

    *gen_out = gen1;
    return 1u;
}

static void demo_put_i16(uint8_t *dst, int16_t value)
{
    dst[0] = (uint8_t)((uint16_t)value & 0xFFu);
    dst[1] = (uint8_t)(((uint16_t)value >> 8) & 0xFFu);
}

static void demo_capture_xinput_report(const uint8_t *payload)
{
    uint8_t report[XINPUT_ENDPOINT_SIZE];
    uint8_t version;
    uint32_t key_mask;
    uint32_t previous_key_mask;
    uint32_t irq_status;

    if(payload == 0)
    {
        return;
    }

    version = (uint8_t)((payload[1] & RF_INPUT_FORMAT_VERSION_MASK) >>
                        RF_INPUT_FORMAT_VERSION_SHIFT);
    if(((version != RF_INPUT_FORMAT_VERSION_V1) &&
        (version != RF_INPUT_FORMAT_VERSION_V2)) ||
       ((payload[1] & RF_INPUT_FLAG_PROCESSED) == 0u))
    {
        return;
    }
    if((version == RF_INPUT_FORMAT_VERSION_V1) &&
       (demo_input_crc8(payload, (uint8_t)(RF_INPUT_PAYLOAD_LEN - 1u)) !=
        payload[RF_INPUT_PAYLOAD_LEN - 1u]))
    {
        return;
    }

    key_mask = demo_input_key_mask(payload) & RF_INPUT_KEY_MASK_VALID;

    if(demo_hid_stats_enabled() != 0u)
    {
        previous_key_mask = g_demo_hid_input_key_mask;
        g_demo_hid_input_key_mask = key_mask;
        g_demo_hid_input_window_mask |= key_mask;
        g_demo_hid_input_seq = payload[0];
        g_demo_hid_input_flags = payload[1];
        g_demo_hid_input_sample_tick_us =
            (version == RF_INPUT_FORMAT_VERSION_V2) ? demo_input_sample_tick_us(payload) : 0u;
        g_demo_hid_input_sync_seq = 0u;
        g_demo_hid_input_sync_rx_tick_us = 0u;
        g_demo_hid_input_sync_tx_tick_us = 0u;
        g_demo_hid_input_valid = 1u;
        if((key_mask != previous_key_mask) &&
           (version == RF_INPUT_FORMAT_VERSION_V2) &&
           (g_demo_hid_input_sample_tick_us != 0u))
        {
            g_demo_hid_latency_key_mask = key_mask;
            g_demo_hid_latency_sample_tick_us = g_demo_hid_input_sample_tick_us;
            g_demo_hid_latency_stm32_us = 0u;
            g_demo_hid_latency_tx_us = 0u;
            g_demo_hid_latency_rx_us = 0u;
            g_demo_hid_latency_rx_irq_us = 0u;
            g_demo_hid_latency_rx_decode_us = 0u;
            g_demo_hid_latency_rx_epwait_us = 0u;
            g_demo_hid_latency_rx_submit_us = 0u;
            g_demo_hid_latency_stage_flags = 0u;
            g_demo_hid_latency_input_seq = payload[0];
            g_demo_hid_latency_input_flags = payload[1];
            g_demo_hid_latency_sync_seq = 0u;
            g_demo_hid_latency_sync_rx_tick_us = 0u;
            g_demo_hid_latency_sync_tx_tick_us = 0u;
            g_demo_hid_latency_v2 = 0u;
            g_demo_hid_latency_pending = 1u;
        }
    }

    memset(report, 0, sizeof(report));
    report[0] = 0x00u;
    report[1] = XINPUT_ENDPOINT_SIZE;
    report[2] = (uint8_t)(((key_mask & HBOX_KEY_UP) != 0u) ? XBOX_MASK_UP : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_DOWN) != 0u) ? XBOX_MASK_DOWN : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_LEFT) != 0u) ? XBOX_MASK_LEFT : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_RIGHT) != 0u) ? XBOX_MASK_RIGHT : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_S2) != 0u) ? XBOX_MASK_START : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_S1) != 0u) ? XBOX_MASK_BACK : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_L3) != 0u) ? XBOX_MASK_LS : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_R3) != 0u) ? XBOX_MASK_RS : 0u);
    report[3] = (uint8_t)(((key_mask & HBOX_KEY_L1) != 0u) ? XBOX_MASK_LB : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_R1) != 0u) ? XBOX_MASK_RB : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_B1) != 0u) ? XBOX_MASK_A : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_B2) != 0u) ? XBOX_MASK_B : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_B3) != 0u) ? XBOX_MASK_X : 0u) |
                (uint8_t)(((key_mask & HBOX_KEY_B4) != 0u) ? XBOX_MASK_Y : 0u);
#if (DONGLE_RF_ENABLE_GUIDE_BUTTON != 0u)
    report[3] |= (uint8_t)(((key_mask & HBOX_KEY_A1) != 0u) ? XBOX_MASK_HOME : 0u);
#endif
    report[4] = ((key_mask & HBOX_KEY_L2) != 0u) ? 0xFFu : 0x00u;
    report[5] = ((key_mask & HBOX_KEY_R2) != 0u) ? 0xFFu : 0x00u;
    demo_put_i16(&report[6], 0);
    demo_put_i16(&report[8], 0);
    demo_put_i16(&report[10], 0);
    demo_put_i16(&report[12], 0);

    memcpy(g_demo_xinput_report, report, sizeof(report));
    g_demo_xinput_pending = 1u;
    SYS_DisableAllIrq(&irq_status);
    if((g_demo_xinput_latency_pending != 0u) &&
       (g_demo_xinput_latency_report_tmr == 0u))
    {
        g_demo_xinput_latency_report_tmr = TMR0_GetCurrentTimer();
    }
    SYS_RecoverIrq(irq_status);
}

static void demo_process_pending_input_payload(void)
{
    uint8_t payload[RF_INPUT_PAYLOAD_LEN];
    uint32_t gen;

    if(demo_snapshot_pending_input(payload, &gen) == 0u)
    {
        return;
    }
    if(gen == g_demo_processed_input_gen)
    {
        return;
    }

    g_demo_processed_input_gen = gen;
    demo_capture_xinput_report(payload);
}

static void demo_service_xinput_report(void)
{
    uint8_t report[XINPUT_ENDPOINT_SIZE];
    uint32_t irq_status;
    uint32_t submit_tmr;
    uint32_t submit_done_tmr;

    if(USBHS_DevEnumStatus == 0u)
    {
        return;
    }
    if((USBHS_Endp_Busy[DEF_UEP2] & DEF_UEP_BUSY) != 0u)
    {
        return;
    }

    SYS_DisableAllIrq(&irq_status);
    if(g_demo_xinput_pending == 0u)
    {
        SYS_RecoverIrq(irq_status);
        return;
    }
    memcpy(report, g_demo_xinput_report, sizeof(report));
    g_demo_xinput_pending = 0u;
    SYS_RecoverIrq(irq_status);

    submit_tmr = TMR0_GetCurrentTimer();
    if(USBHS_Endp_DataUp(DEF_UEP2,
                         report,
                         XINPUT_ENDPOINT_SIZE,
                         DEF_UEP_CPY_LOAD) == 0u)
    {
        submit_done_tmr = TMR0_GetCurrentTimer();
        demo_complete_xinput_latency_if_pending(submit_tmr, submit_done_tmr);
    }
}

static void demo_prepare_command_ack(uint8_t cmd, uint8_t seq)
{
    g_demo_pending_ack_cmd = cmd;
    g_demo_pending_ack_seq = seq;
}

static void demo_handle_command(const uint8_t *air, uint8_t rx_channel)
{
    const uint8_t *data = &air[RFH_DATA_OFFSET];
    uint8_t cmd = data[RFH_HOP_CMD_ID];
    uint8_t target = data[RFH_HOP_CMD_CHANNEL];
    uint8_t seq = data[RFH_HOP_CMD_SEQ];
    uint16_t score = rfh_get_u16(&data[RFH_HOP_CMD_SCORE_LO]);

    if(cmd == RFH_CMD_HOP_PREPARE)
    {
        g_demo_confirm_ack_keep_count = 0u;
        g_demo_old_channel = g_demo_current_channel;
        g_demo_target_channel = target;
        g_demo_hop_seq = seq;
        g_demo_dual_side = 0u;
        demo_prepare_command_ack(RFH_CMD_HOP_PREPARE, seq);
        g_demo_after_ack_action = 1u;
        g_demo_stat.hop_event++;
        g_demo_hop_start_clock = TMOS_GetSystemClock();
        g_demo_hop_clock_valid = 1u;
        if(demo_hid_stats_enabled() != 0u)
        {
            g_demo_hid_hop_events++;
            g_demo_hid_hop_start_score = score;
            g_demo_hid_hop_start_pending = 1u;
        }
    }
    else if(cmd == RFH_CMD_HOP_CONFIRM)
    {
        if((monitor_channel_valid(target) == 0u) || (target != rx_channel))
        {
            if(demo_hid_stats_enabled() != 0u)
            {
                g_demo_hid_errors++;
                g_demo_hid_type_errors++;
                g_demo_air_diag_type_errors++;
            }
            return;
        }
        g_demo_target_channel = target;
        g_demo_hop_seq = seq;
        if(g_demo_current_channel != target)
        {
            demo_set_channel(target);
        }
        g_demo_rx_state = RF_AUTO_RX_COMM;
        g_demo_old_channel = target;
        g_demo_confirm_ack_keep_count = RF_AUTO_DEMO_HOP_CONFIRM_ACK_KEEP_TOKENS;
        g_demo_confirm_ack_keep_seq = seq;
        g_demo_confirm_ack_keep_channel = target;
        demo_prepare_command_ack(RFH_CMD_HOP_CONFIRM, seq);
        g_demo_after_ack_action = 2u;
        g_demo_stat.hop_event++;
        if(demo_hid_stats_enabled() != 0u)
        {
            g_demo_hid_hop_events++;
        }
    }
    else if(cmd == RFH_CMD_RATE_UPDATE)
    {
        uint8_t rate_code = data[RFH_CMD_SLOT_ARG0];
        if(rate_code <= RFH_RATE_8K)
        {
            demo_apply_rate_code(rate_code);
            demo_prepare_command_ack(RFH_CMD_RATE_UPDATE, data[RFH_CMD_SLOT_ARG3]);
        }
        else
        {
            if(demo_hid_stats_enabled() != 0u)
            {
                g_demo_hid_errors++;
                g_demo_hid_type_errors++;
                g_demo_air_diag_type_errors++;
            }
        }
    }
    else if(cmd == RFH_CMD_MONITOR_CONFIG)
    {
        uint8_t status_seq = data[RFH_CMD_SLOT_ARG3];
        uint8_t status_flags = data[RFH_CMD_SLOT_ARG0];
        uint8_t manual_channel = data[RFH_CMD_SLOT_ARG1];

        (void)status_flags;
        (void)manual_channel;

        if(status_seq == g_monitor_pending_seq)
        {
            if(g_monitor_tx_status == RFMON_APPLY_PENDING)
            {
                g_monitor_tx_status = RFMON_APPLY_APPLIED;
                g_monitor_tx_applied_seq = status_seq;
            }
            g_monitor_pending_retries = 0u;
        }
    }
    else if(cmd == RFH_CMD_TIME_SYNC_ECHO)
    {
        demo_queue_latency_sync_echo(data[RFH_TIME_SYNC_ECHO_SEQ],
                                     rfh_get_u32(&data[RFH_TIME_SYNC_ECHO_RX_TICK]),
                                     rfh_get_u32(&data[RFH_TIME_SYNC_ECHO_TX_TICK]));
    }
    else if(cmd == RFH_CMD_LATENCY_INPUT)
    {
        demo_queue_latency_input(data[RFH_LATENCY_INPUT_SEQ],
                                 rfh_get_u32(&data[RFH_LATENCY_KEY_MASK]),
                                 rfh_get_u32(&data[RFH_LATENCY_SAMPLE_TICK]),
                                 0u,
                                 0u,
                                 0u,
                                 0u,
                                 (uint8_t)((RF_INPUT_FORMAT_VERSION_V2 << RF_INPUT_FORMAT_VERSION_SHIFT) |
                                           RF_INPUT_FLAG_PROCESSED));
    }
}

static void demo_after_ack_finish(void)
{
    uint32_t now = TMOS_GetSystemClock();

    if(g_demo_pending_ack_cmd == RFH_CMD_CONNECT_REQ)
    {
        /* CONNECT ACK completion must not mark the DATA link recovered.
         * SYN only opens the ACK window; FINAL enters COMM; DATA sets
         * link_active. Keeping those edges separate avoids a 100ms false
         * Link Lost while TX is still sending FINAL packets.
         */
    }
    else if(g_demo_after_ack_action == 1u)
    {
        g_demo_rx_state = RF_AUTO_RX_PREPARED_DUAL;
        g_demo_dual_switch_clock = now;
        g_demo_dual_deadline_clock = now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_DUAL_TIMEOUT_MS);
        g_demo_dual_side = 0u;
    }
    else if(g_demo_after_ack_action == 2u)
    {
        uint16_t duration_ms = 0u;

        if(g_demo_hop_clock_valid != 0u)
        {
            duration_ms = demo_clock_delta_ms(g_demo_hop_start_clock, now);
        }
        g_demo_rx_state = RF_AUTO_RX_COMM;
        demo_set_channel(g_demo_target_channel);
        g_demo_old_channel = g_demo_target_channel;
        g_demo_stat.hop_event++;
        if(demo_hid_stats_enabled() != 0u)
        {
            g_demo_hid_hop_finish_duration_ms = duration_ms;
            g_demo_hid_hop_finish_pending = 1u;
        }
        g_demo_hop_clock_valid = 0u;
    }

    g_demo_after_ack_action = 0u;
    g_demo_pending_ack_cmd = RFH_CMD_NONE;
    g_demo_pending_ack_seq = 0u;
}

static void demo_service_connect_handshake(uint32_t now)
{
    if((g_demo_rx_state != RF_AUTO_RX_CONNECT_ACK_PENDING) ||
       ((g_demo_connect_stage != RFH_CONNECT_STAGE_SYN) &&
        (g_demo_connect_stage != 0u)))
    {
        return;
    }
    if(g_demo_connect_stage == 0u)
    {
        if((int32_t)(now - g_demo_connect_until_clock) >= 0)
        {
            demo_enter_rx_unconnected(now);
        }
        return;
    }
    if((int32_t)(now - g_demo_connect_until_clock) >= 0)
    {
        g_demo_connect_stage = 0u;
        g_demo_connect_until_clock = now + MS1_TO_SYSTEM_TIME(RFH_CONNECT_FINAL_WAIT_MS);
        demo_arm_rx();
        return;
    }
    if((int32_t)(now - g_demo_connect_next_tx_clock) < 0)
    {
        return;
    }
    if((g_demo_ack_pending != 0u) || (g_demo_pair_tx_active != 0u))
    {
        return;
    }

    demo_prepare_command_ack(RFH_CMD_CONNECT_REQ, RFH_ACK_STATUS_CONNECTED);
    g_demo_connect_next_tx_clock =
        now + MS1_TO_SYSTEM_TIME(RFH_CONNECT_RESPONSE_INTERVAL_MS);
    demo_send_ack();
}

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
    if(g_demo_ack_pending != 0u)
    {
        demo_send_ack();
    }
}

static uint8_t demo_process_connect_packet(const rf_rx_pending_t *pending)
{
    const uint8_t *air;
    const uint8_t *data;
    uint8_t flags;
    uint8_t rate_code;
    uint8_t channel_a;
    uint8_t channel_b;
    uint8_t remaining;
    uint8_t token;
    uint8_t connect_stage;
    uint32_t now = TMOS_GetSystemClock();

    if(g_demo_has_bond == 0u)
    {
        return 0u;
    }
    if((pending == 0) || (pending->len != RF_AUTO_DEMO_PACKET_LEN))
    {
        g_demo_stat.data_type_err++;
        if(demo_hid_stats_enabled() != 0u)
        {
            g_demo_hid_errors++;
            g_demo_hid_type_errors++;
            g_demo_air_diag_type_errors++;
        }
        return 0u;
    }

    air = pending->air;
    data = &air[RFH_DATA_OFFSET];
    flags = rfh_flags(air[RFH_HDR0_OFFSET]);
    rate_code = data[RFH_CONNECT_RATE];
    channel_a = data[RFH_CONNECT_CH_A];
    channel_b = data[RFH_CONNECT_CH_B];
    connect_stage = data[RFH_CONNECT_OPTIONS];

    if((rfh_get_u32(&data[RFH_CONNECT_SESSION0]) != RFH_CONNECT_SESSION_ID) ||
       (rate_code > RFH_RATE_8K) ||
       (monitor_channel_valid(channel_a) == 0u) ||
       (monitor_channel_valid(channel_b) == 0u) ||
       (channel_a == channel_b) ||
       (data[RFH_CONNECT_ACK_WINDOW_MS] == 0u))
    {
        g_demo_stat.data_type_err++;
        demo_channel_score_update(pending->channel,
                                  demo_type_score_sample());
        if(demo_hid_stats_enabled() != 0u)
        {
            g_demo_hid_errors++;
            g_demo_hid_type_errors++;
            g_demo_air_diag_type_errors++;
        }
        return 0u;
    }

    if((flags & RFH_FLAG_CMD_ACK) == 0u)
    {
        return 0u;
    }

    demo_apply_rate_code(rate_code);

    if(connect_stage == RFH_CONNECT_STAGE_FINAL)
    {
        if(g_demo_rx_state != RF_AUTO_RX_CONNECT_ACK_PENDING)
        {
            return 0u;
        }
        g_demo_rx_state = RF_AUTO_RX_COMM;
        g_demo_link_active = 0u;
        g_demo_old_channel = pending->channel;
        g_demo_target_channel = pending->channel;
        g_demo_last_data_tmr = pending->rx_tmr;
        g_demo_have_data_seq = 0u;
        g_demo_connect_stage = 0u;
        g_demo_ack_pending = 0u;
        demo_ack_timer_cancel();
        demo_reset_quality_window();
        return 0u;
    }

    if(connect_stage != RFH_CONNECT_STAGE_SYN)
    {
        return 0u;
    }

    g_demo_rx_state = RF_AUTO_RX_CONNECT_ACK_PENDING;
    g_demo_link_active = 0u;
    g_demo_old_channel = pending->channel;
    g_demo_target_channel = pending->channel;
    g_demo_last_data_tmr = pending->rx_tmr;
    g_demo_have_data_seq = 0u;
    g_demo_connect_stage = RFH_CONNECT_STAGE_SYN;
    g_demo_connect_until_clock = now + MS1_TO_SYSTEM_TIME(RFH_CONNECT_SUPERFRAME_MS);
    g_demo_connect_next_tx_clock = now;
    demo_reset_quality_window();
    demo_prepare_command_ack(RFH_CMD_CONNECT_REQ, RFH_ACK_STATUS_CONNECTED);

    token = air[RF_AUTO_DEMO_ACK_TOKEN_OFFSET];
    remaining = air[RF_AUTO_DEMO_ACK_REMAIN_OFFSET];
    if((g_demo_have_ack_token == 0u) || (token != g_demo_last_ack_token))
    {
        g_demo_have_ack_token = 1u;
        g_demo_last_ack_token = token;
        g_demo_stat.ack_req++;
    }
    (void)remaining;
    (void)token;
    return 0u;
}

static uint8_t demo_process_rx_pending_packet(const rf_rx_pending_t *pending)
{
    const uint8_t *air;
    uint32_t data_tmr = 0u;
    uint32_t process_tmr;
    uint8_t request_ack = 0u;
    uint8_t flags = 0u;
    uint8_t rate_code = 0u;
    uint8_t packet_type = 0u;
    uint8_t input_queued = 0u;
    uint8_t input_payload[RF_INPUT_PAYLOAD_LEN];

    if(pending == 0)
    {
        return 0u;
    }

    process_tmr = TMR0_GetCurrentTimer();
    if(pending->kind == RF_RX_PENDING_CRCERR)
    {
        g_demo_stat.data_crc_err++;
        demo_channel_score_update(pending->channel,
                                  demo_crc_score_sample());
        g_demo_window_expected++;
        g_demo_window_crc++;
        if(demo_hid_stats_enabled() != 0u)
        {
            g_demo_hid_expected++;
            g_demo_hid_bad++;
            g_demo_hid_errors++;
            g_demo_hid_crc_errors++;
            g_demo_air_diag_crc_errors++;
        }
        return 0u;
    }

    if((pending->len != RF_AUTO_DEMO_PACKET_LEN) &&
       (pending->len != (uint8_t)(RFH_DATA_OFFSET + RFMON_INPUT_PAYLOAD_V1_LEN)) &&
       (pending->len != RFH_INPUT_AIR_PACKET_LEN))
    {
        g_demo_stat.data_type_err++;
        demo_channel_score_update(pending->channel,
                                  demo_type_score_sample());
        if(demo_hid_stats_enabled() != 0u)
        {
            g_demo_hid_bad++;
            g_demo_hid_errors++;
            g_demo_hid_type_errors++;
            g_demo_air_diag_type_errors++;
        }
        return 0u;
    }

    air = pending->air;
    packet_type = rfh_packet_type(air[RFH_HDR0_OFFSET]);
    if(packet_type == RFH_PKT_PAIR)
    {
        return demo_process_pair_packet(pending);
    }
    if(packet_type == RFH_PKT_CONNECT)
    {
        return demo_process_connect_packet(pending);
    }
    if((g_demo_rx_state == RF_AUTO_RX_UNCONNECTED) ||
       (g_demo_rx_state == RF_AUTO_RX_CONNECT_ACK_PENDING))
    {
        return 0u;
    }
    if(packet_type != RFH_PKT_DATA)
    {
        g_demo_stat.data_type_err++;
        demo_channel_score_update(pending->channel,
                                  demo_type_score_sample());
        if(demo_hid_stats_enabled() != 0u)
        {
            g_demo_hid_bad++;
            g_demo_hid_errors++;
            g_demo_hid_type_errors++;
            g_demo_air_diag_type_errors++;
        }
        return 0u;
    }

    rate_code = rfh_rate_code(air[RFH_HDR0_OFFSET]);
    demo_apply_rate_code(rate_code);
    flags = rfh_flags(air[RFH_HDR0_OFFSET]);
    if((pending->len == RFH_INPUT_AIR_PACKET_LEN) &&
       ((flags & (RFH_FLAG_CMD_PRESENT | RFH_FLAG_CMD_ACK)) != 0u))
    {
        g_demo_stat.data_type_err++;
        demo_channel_score_update(pending->channel,
                                  demo_type_score_sample());
        if(demo_hid_stats_enabled() != 0u)
        {
            g_demo_hid_bad++;
            g_demo_hid_errors++;
            g_demo_hid_type_errors++;
            g_demo_air_diag_type_errors++;
        }
        return 0u;
    }
    g_demo_stat.data_ok++;
    {
        data_tmr = pending->rx_tmr;
        if(g_demo_have_data_seq != 0u)
        {
            demo_note_hid_silent_cycles(
                demo_tmr0_elapsed_cycles(g_demo_last_data_tmr, data_tmr));
        }
        g_demo_link_active = 1u;
        g_demo_rx_state = RF_AUTO_RX_COMM;
        g_demo_old_channel = pending->channel;
        g_demo_target_channel = pending->channel;
        g_demo_last_data_tmr = data_tmr;
    }
    demo_note_data_seq(air[RFH_HDR1_OFFSET]);
    if((flags & (RFH_FLAG_CMD_PRESENT | RFH_FLAG_CMD_ACK)) == 0u)
    {
        if(pending->len == RFH_INPUT_AIR_PACKET_LEN)
        {
            if(demo_decode_short_input_payload(input_payload,
                                               air[RFH_HDR1_OFFSET],
                                               &air[RFH_DATA_OFFSET]) == 0u)
            {
                g_demo_stat.data_type_err++;
                demo_channel_score_update(pending->channel,
                                          demo_type_score_sample());
                if(demo_hid_stats_enabled() != 0u)
                {
                    g_demo_hid_bad++;
                    g_demo_hid_errors++;
                    g_demo_hid_type_errors++;
                    g_demo_air_diag_type_errors++;
                }
                return 0u;
            }
            demo_queue_input_payload(input_payload);
            demo_queue_xinput_latency_pending(input_payload, data_tmr, process_tmr);
            input_queued = 1u;
        }
        else
        {
            if(demo_decode_v1_input_payload(input_payload,
                                             &air[RFH_DATA_OFFSET]) == 0u)
            {
                /*
                 * Full packets without a command can also be idle/fill traffic.
                 * Only legacy v1 input with payload CRC is accepted here; v2
                 * latency uses RFH_CMD_LATENCY_INPUT so fill bytes cannot
                 * accidentally become Guide/Home or other buttons.
                 */
                return 0u;
            }
            demo_queue_input_payload(input_payload);
            input_queued = 1u;
        }
    }

    if((flags & RFH_FLAG_CMD_PRESENT) != 0u)
    {
        demo_handle_command(air, pending->channel);
    }

    if((flags & RFH_FLAG_CMD_ACK) != 0u)
    {
        request_ack = 1u;
    }

    if(request_ack != 0u)
    {
        uint8_t token = air[RF_AUTO_DEMO_ACK_TOKEN_OFFSET];
        uint8_t remaining = air[RF_AUTO_DEMO_ACK_REMAIN_OFFSET];
        if((g_demo_have_ack_token == 0u) || (token != g_demo_last_ack_token))
        {
            g_demo_have_ack_token = 1u;
            g_demo_last_ack_token = token;
            g_demo_stat.ack_req++;
        }
        demo_schedule_ack(remaining);
    }

    return input_queued;
}

static void demo_service_xinput_fast_path(void)
{
    demo_process_pending_input_payload();
    demo_service_xinput_report();
}

static void demo_process_pending_rx_packets(void)
{
    uint8_t i;
    uint8_t chunk_count = 0u;
    uint8_t input_seen = 0u;
    rf_rx_pending_t pending;

    for(i = 0u; i < RF_RX_PENDING_DRAIN_MAX; i++)
    {
        if(demo_pop_rx_pending(&pending) == 0u)
        {
            if(input_seen != 0u)
            {
                demo_service_xinput_fast_path();
            }
            return;
        }
        if(demo_process_rx_pending_packet(&pending) != 0u)
        {
            input_seen = 1u;
        }
        chunk_count++;
        if(chunk_count >= RF_RX_PENDING_REPORT_CHUNK)
        {
            chunk_count = 0u;
            if(input_seen != 0u)
            {
                input_seen = 0u;
                demo_service_xinput_fast_path();
            }
        }
    }

    if(input_seen != 0u)
    {
        demo_service_xinput_fast_path();
    }
}

__HIGH_CODE
void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id)
{
    (void)id;

    if(sta & RF_STATE_RX)
    {
        uint8_t completed_slot = g_demo_rx_active_slot;
        uint8_t *rx_buf;
        uint32_t rx_tmr = TMR0_GetCurrentTimer();
        int8_t rssi = RFIP_ReadRssi();

        demo_note_rssi(rssi);
        g_demo_rx_active = 0u;
        if(completed_slot >= RF_RX_DMA_SLOT_COUNT)
        {
            completed_slot = 0u;
        }
        rx_buf = RxBuf[completed_slot];
        demo_arm_rx();
        demo_queue_rx_pending_packet(rx_buf, rx_tmr);
    }
    if(sta & RF_STATE_RX_CRCERR)
    {
        uint32_t rx_tmr = TMR0_GetCurrentTimer();
        int8_t rssi = RFIP_ReadRssi();

        demo_note_rssi(rssi);
        g_demo_rx_active = 0u;
        demo_arm_rx();
        demo_queue_rx_pending_crcerr(rx_tmr);
    }
    if(sta & RF_STATE_TX_FINISH)
    {
        if(g_demo_pair_tx_active != 0u)
        {
            demo_after_pair_tx_finish();
            return;
        }
        g_demo_stat.ack_finish++;
        demo_after_ack_finish();
        demo_arm_rx();
    }
    if(sta & RF_STATE_TIMEOUT)
    {
        g_demo_rx_active = 0u;
        if(demo_pair_is_active() != 0u)
        {
            demo_arm_rx();
            return;
        }
        g_demo_stat.ack_fail++;
        demo_channel_score_update(g_demo_current_channel,
                                  demo_timeout_score_sample());
        if(demo_hid_stats_enabled() != 0u)
        {
            g_demo_hid_errors++;
            g_demo_hid_timeout_errors++;
            g_demo_air_diag_timeout_errors++;
        }
        g_demo_after_ack_action = 0u;
        g_demo_pending_ack_cmd = RFH_CMD_NONE;
        g_demo_pending_ack_seq = 0u;
        if(g_demo_rx_state == RF_AUTO_RX_CONNECT_ACK_PENDING)
        {
            demo_arm_rx();
        }
        else
        {
            demo_arm_rx();
        }
    }
}

void RF_Service(void)
{
    uint32_t now = TMOS_GetSystemClock();
    uint32_t data_silent_cycles = 0u;
    uint8_t enter_unconnected = 0u;

    demo_service_xinput_fast_path();
    demo_process_pending_rx_packets();
    demo_score_windows_service(now);

    if(demo_pair_is_active() != 0u)
    {
        demo_service_pairing(now);
        return;
    }

    if((g_demo_rx_state == RF_AUTO_RX_COMM) &&
       (demo_snapshot_data_silent_cycles(&data_silent_cycles) != 0u) &&
       (data_silent_cycles >= demo_us_to_tmr_cycles(RFH_RX_PACKET_TIMEOUT_MS_DEFAULT * 1000u)))
    {
        uint32_t irq_status;

        SYS_DisableAllIrq(&irq_status);
        if(g_demo_link_active != 0u)
        {
            uint32_t verify_cycles = demo_tmr0_elapsed_cycles(g_demo_last_data_tmr,
                                                              TMR0_GetCurrentTimer());
            if(verify_cycles >= demo_us_to_tmr_cycles(RFH_RX_PACKET_TIMEOUT_MS_DEFAULT * 1000u))
            {
                uint16_t silent_ticks = demo_tmr_cycles_to_system_ticks(verify_cycles);
                g_demo_link_active = 0u;
                if(demo_hid_stats_enabled() != 0u)
                {
                    g_demo_hid_link_lost_silent_ticks = silent_ticks;
                    demo_note_hid_silent_cycles(verify_cycles);
                    g_demo_hid_errors++;
                    g_demo_hid_timeout_errors++;
                    g_demo_air_diag_timeout_errors++;
                }
                enter_unconnected = 1u;
            }
        }
        SYS_RecoverIrq(irq_status);
    }

    if(enter_unconnected != 0u)
    {
        demo_enter_rx_unconnected(now);
    }

    if(g_demo_rearm_pending != 0u)
    {
        g_demo_rearm_pending = 0u;
        demo_arm_rx();
    }

    demo_service_xinput_fast_path();
    demo_service_connect_handshake(now);
    demo_service_unconnected_scan(now);

    if(g_demo_rx_state == RF_AUTO_RX_PREPARED_DUAL)
    {
        if((int32_t)(now - g_demo_dual_deadline_clock) >= 0)
        {
            uint8_t fixed_channel;
            if(demo_manual_fixed_channel(&fixed_channel) != 0u)
            {
                g_demo_rx_state = RF_AUTO_RX_UNCONNECTED;
                g_demo_link_active = 0u;
                g_demo_old_channel = fixed_channel;
                g_demo_target_channel = fixed_channel;
                demo_set_channel(fixed_channel);
            }
            else
            {
                g_demo_rx_state = RF_AUTO_RX_COMM;
                demo_set_channel(g_demo_old_channel);
            }
            demo_arm_rx();
            g_demo_stat.hop_event++;
        }
        else if((uint32_t)(now - g_demo_dual_switch_clock) >=
                MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_DUAL_DWELL_MS))
        {
            g_demo_dual_switch_clock = now;
            g_demo_dual_side ^= 1u;
            demo_set_channel((g_demo_dual_side == 0u) ?
                             g_demo_old_channel : g_demo_target_channel);
            demo_arm_rx();
        }
    }
    else if((g_demo_rx_state == RF_AUTO_RX_RECOVERY_SCAN) &&
            (g_demo_link_active == 0u) &&
            ((uint32_t)(now - g_demo_recovery_scan_clock) >=
             MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_RECOVERY_DWELL_MS)))
    {
        g_demo_recovery_scan_clock = now;
        demo_set_channel(demo_next_recovery_channel());
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
    uint32_t now = TMOS_GetSystemClock();

    if(g_demo_config_ret != SUCCESS)
    {
        return 0u;
    }
    if(demo_pair_is_active() != 0u)
    {
        return 1u;
    }

    g_demo_link_active = 0u;
    g_demo_pair_tx_active = 0u;
    g_demo_pair_after_tx_action = 0u;
    g_demo_pair_scan_side = 0u;
    g_demo_pair_scan_clock = now;
    g_demo_pair_deadline_clock = now + MS1_TO_SYSTEM_TIME(RFH_PAIR_WINDOW_MS);
    g_demo_pair_confirm_deadline_clock = 0u;
    g_demo_pair_session = 0u;
    g_demo_pair_tx_id_hash = 0u;
    g_demo_pair_rx_id_hash = g_demo_local_id_hash;
    g_demo_pair_link_access_address = 0u;
    g_demo_pair_done_confirm32 = 0u;
    g_demo_pair_done_repeat_left = 0u;
    g_demo_have_ack_token = 0u;
    g_demo_pending_ack_cmd = RFH_CMD_NONE;
    g_demo_after_ack_action = 0u;
    g_demo_ack_pending = 0u;
    demo_ack_timer_cancel();
    g_demo_rx_state = RF_AUTO_RX_PAIRING;
    (void)demo_apply_access_address(RFH_PAIR_ACCESS_ADDRESS);
    demo_set_channel(RFH_PAIR_CHANNEL_A);
    demo_arm_rx();
    return 1u;
}

uint8_t RF_StopPairing(void)
{
    if(demo_pair_is_active() != 0u)
    {
        demo_abort_pairing(TMOS_GetSystemClock());
    }
    return 1u;
}

uint8_t RF_IsPairingActive(void)
{
    return demo_pair_is_active();
}

rf_indicator_mode_t RF_GetIndicatorMode(void)
{
    if(g_demo_config_ret != SUCCESS)
    {
        return RF_INDICATOR_OFF;
    }
    if(demo_pair_is_active() != 0u)
    {
        return RF_INDICATOR_PAIRING;
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
    uint32_t rssi_count;
    int32_t rssi_avg;
    int8_t rssi_min;
    int8_t rssi_max;

    if((buf == NULL) || (len == 0u))
    {
        return 0u;
    }

    rssi_count = g_demo_rssi_count;
    rssi_avg = (rssi_count == 0u) ? 0 : (g_demo_rssi_sum / (int32_t)rssi_count);
    rssi_min = (rssi_count == 0u) ? 0 : g_demo_rssi_min;
    rssi_max = (rssi_count == 0u) ? 0 : g_demo_rssi_max;

    written = snprintf(buf,
                       len,
                       "R8 c%u S%c g%c h%u>%u hz%u d%lu gap%lu q%lu a%lu/%lu e%lu/%lu p%lu w%u/%u rssi%ld/%d/%d/%d H%lu x%u/%u/%u v%u\r\n",
                       (unsigned int)g_demo_config_ret,
                       demo_rx_state_char(),
                       demo_rx_connect_stage_char(),
                       (unsigned int)g_demo_current_channel,
                       (unsigned int)g_demo_target_channel,
                       (unsigned int)g_demo_report_hz,
                       (unsigned long)g_demo_stat.data_ok,
                       (unsigned long)g_demo_stat.seq_gap,
                       (unsigned long)g_demo_stat.ack_req,
                       (unsigned long)g_demo_stat.ack_finish,
                       (unsigned long)g_demo_stat.ack_fail,
                       (unsigned long)g_demo_stat.data_crc_err,
                       (unsigned long)g_demo_stat.data_type_err,
                       (unsigned long)g_demo_stat.pending_drop,
                       (unsigned int)demo_rx_pending_water(g_demo_rx_pending_head,
                                                           g_demo_rx_pending_tail),
                       (unsigned int)g_demo_rx_pending_max_water,
                       (long)rssi_avg,
                       (int)rssi_min,
                       (int)rssi_max,
                       (int)g_demo_rssi_last,
                       (unsigned long)g_demo_stat.hop_event,
                       (unsigned int)g_demo_rx_ret,
                       (unsigned int)g_demo_tx_start_ret,
                       (unsigned int)g_demo_tx_parm_ret,
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
    g_demo_stat.ack_req = 0u;
    g_demo_stat.ack_finish = 0u;
    g_demo_stat.ack_fail = 0u;
    g_demo_stat.tx_parm_fail = 0u;
    g_demo_stat.hop_event = 0u;
    g_demo_stat.seq_gap = 0u;
    g_demo_stat.pending_drop = 0u;
    g_demo_rx_pending_max_water = demo_rx_pending_water(g_demo_rx_pending_head,
                                                        g_demo_rx_pending_tail);
    g_demo_rssi_sum = 0;
    g_demo_rssi_count = 0u;
    g_demo_rssi_min = 127;
    g_demo_rssi_max = -127;

    return (uint16_t)((written >= (int)len) ? (len - 1u) : (uint16_t)written);
}

static void demo_put_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)(value >> 8);
}

static void demo_put_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)(value >> 24);
}

static uint32_t demo_expected_from_elapsed(uint16_t elapsed_ms)
{
    uint32_t expected;

    if((elapsed_ms == 0u) || (g_demo_report_hz == 0u))
    {
        return 0u;
    }

    expected = (((uint32_t)g_demo_report_hz * (uint32_t)elapsed_ms) + 500u) / 1000u;
    return (expected == 0u) ? 1u : expected;
}

static uint16_t demo_rx_loss_permille(uint32_t rx_ok, uint32_t expected)
{
    if(expected == 0u)
    {
        return 0u;
    }
    if(rx_ok >= expected)
    {
        return 0u;
    }
    return (uint16_t)(((expected - rx_ok) * 1000u) / expected);
}

static uint16_t demo_hid_elapsed_ms(void)
{
    uint32_t now = TMOS_GetSystemClock();
    uint32_t delta;
    uint32_t elapsed;

    if(g_demo_hid_last_clock == 0u)
    {
        g_demo_hid_last_clock = now;
        return 100u;
    }

    delta = now - g_demo_hid_last_clock;
    g_demo_hid_last_clock = now;

    elapsed = ((delta * (uint32_t)SYSTEM_TIME_MICROSEN) + 999u) / 1000u;
    if(elapsed == 0u)
    {
        elapsed = 1u;
    }
    return (elapsed > 0xFFFFu) ? 0xFFFFu : (uint16_t)elapsed;
}

static uint8_t demo_hid_state_code(void)
{
    if(demo_pair_is_active() != 0u)
    {
        return 1u;
    }
    if(g_demo_link_active == 0u)
    {
        if(g_demo_rx_state == RF_AUTO_RX_RECOVERY_SCAN)
        {
            return 5u;
        }
        return 0u;
    }
    return (g_demo_rx_state == RF_AUTO_RX_PREPARED_DUAL) ? 3u : 2u;
}

static uint8_t demo_submit_hid_report(const uint8_t *report)
{
    if(USBHS_DevEnumStatus == 0u)
    {
        return 0u;
    }
    if((USBHS_Endp_Busy[DEF_UEP6] & DEF_UEP_BUSY) != 0u)
    {
        return 0u;
    }

    memcpy(HID_Report_Buffer, report, HID_ENDPOINT_SIZE);
    return (USBHS_Endp_DataUp(DEF_UEP6,
                              (uint8_t *)report,
                              HID_ENDPOINT_SIZE,
                              DEF_UEP_CPY_LOAD) == 0u) ? 1u : 0u;
}

static uint8_t demo_try_send_score_report(void)
{
    uint8_t report[HID_ENDPOINT_SIZE];
    uint8_t i;
    uint8_t count = RFH_HOP_CHANNEL_COUNT;
    uint8_t active_index = demo_channel_index(g_demo_current_channel);

    memset(report, 0, sizeof(report));
    demo_put_u32(&report[0], RX_HID_SCORE_MAGIC);
    demo_put_u32(&report[4], ++g_demo_hid_score_seq);
    report[8] = count;
    for(i = 0u; i < count; i++)
    {
        uint8_t offset = (uint8_t)(9u + (i * 3u));
        report[offset] = rfh_hop_channel_at(i);
        demo_put_u16(&report[(uint8_t)(offset + 1u)], g_demo_channel_scores[i]);
    }
    report[30] = (active_index == 0xFFu) ? g_demo_current_channel :
                 rfh_hop_channel_at(active_index);
    report[31] = 1u;

    return demo_submit_hid_report(report);
}

static uint8_t demo_try_send_latency_report(void)
{
    uint8_t report[HID_ENDPOINT_SIZE];
    uint32_t key_mask;
    uint32_t sample_tick_us;
    uint16_t stm32_us;
    uint16_t tx_us;
    uint16_t rx_us;
    uint16_t rx_irq_us;
    uint16_t rx_decode_us;
    uint16_t rx_epwait_us;
    uint16_t rx_submit_us;
    uint8_t stage_flags;
    uint8_t input_seq;
    uint8_t input_flags;
    uint8_t sync_seq;
    uint8_t latency_v2;
    uint32_t irq_status;

    SYS_DisableAllIrq(&irq_status);
    if(g_demo_hid_latency_pending == 0u)
    {
        SYS_RecoverIrq(irq_status);
        return 0u;
    }
    key_mask = g_demo_hid_latency_key_mask;
    sample_tick_us = g_demo_hid_latency_sample_tick_us;
    stm32_us = g_demo_hid_latency_stm32_us;
    tx_us = g_demo_hid_latency_tx_us;
    rx_us = g_demo_hid_latency_rx_us;
    rx_irq_us = g_demo_hid_latency_rx_irq_us;
    rx_decode_us = g_demo_hid_latency_rx_decode_us;
    rx_epwait_us = g_demo_hid_latency_rx_epwait_us;
    rx_submit_us = g_demo_hid_latency_rx_submit_us;
    stage_flags = g_demo_hid_latency_stage_flags;
    input_seq = g_demo_hid_latency_input_seq;
    input_flags = g_demo_hid_latency_input_flags;
    sync_seq = g_demo_hid_latency_sync_seq;
    latency_v2 = g_demo_hid_latency_v2;
    SYS_RecoverIrq(irq_status);

    memset(report, 0, sizeof(report));
    if(latency_v2 != 0u)
    {
        demo_put_u32(&report[0], RX_HID_LATENCY_V2_MAGIC);
        demo_put_u32(&report[4], g_demo_hid_latency_seq + 1u);
        report[8] = input_seq;
        report[9] = input_flags;
        demo_put_u32(&report[10], key_mask);
        demo_put_u16(&report[14], stm32_us);
        demo_put_u16(&report[16], tx_us);
        demo_put_u16(&report[18], rx_us);
        demo_put_u16(&report[20], rx_irq_us);
        demo_put_u16(&report[22], rx_decode_us);
        demo_put_u16(&report[24], rx_epwait_us);
        demo_put_u16(&report[26], rx_submit_us);
        report[28] = stage_flags;
        report[29] = demo_hid_state_code();
        report[30] = g_demo_current_channel;
    }
    else
    {
        demo_put_u32(&report[0], RX_HID_LATENCY_MAGIC);
        demo_put_u32(&report[4], g_demo_hid_latency_seq + 1u);
        report[8] = input_seq;
        report[9] = input_flags;
        demo_put_u32(&report[10], key_mask);
        demo_put_u32(&report[14], sample_tick_us);
        demo_put_u16(&report[18], stm32_us);
        demo_put_u16(&report[20], tx_us);
        demo_put_u16(&report[22], rx_us);
        report[24] = stage_flags;
        report[25] = sync_seq;
        report[26] = 0u;
        report[27] = demo_hid_state_code();
        report[28] = g_demo_current_channel;
        report[29] = g_demo_rate_code;
        report[30] = g_demo_link_active;
    }
    report[31] = (uint8_t)demo_input_crc8(report, 31u);

    if(demo_submit_hid_report(report) == 0u)
    {
        return 0u;
    }

    SYS_DisableAllIrq(&irq_status);
    g_demo_hid_latency_pending = 0u;
    SYS_RecoverIrq(irq_status);
    g_demo_hid_latency_seq++;
    return 1u;
}

static uint8_t demo_try_send_input_report(void)
{
    uint8_t report[HID_ENDPOINT_SIZE];
    uint32_t key_mask = g_demo_hid_input_key_mask;
    uint32_t report_key_mask = g_demo_hid_input_window_mask | key_mask;
    uint32_t diag_now = TMOS_GetSystemClock();
    uint32_t diag_delta;
    uint32_t diag_elapsed_ms;
    uint32_t diag_expected;
    uint32_t diag_rx_ok = g_demo_air_diag_rx_ok;
    uint32_t diag_seq_gap = g_demo_air_diag_seq_gap;
    uint32_t diag_crc_errors = g_demo_air_diag_crc_errors;
    uint32_t diag_type_errors = g_demo_air_diag_type_errors;
    uint32_t diag_timeout_errors = g_demo_air_diag_timeout_errors;
    uint8_t input_seq = g_demo_hid_input_seq;
    uint8_t input_flags = g_demo_hid_input_flags;

    if(g_demo_hid_input_valid == 0u)
    {
        return 0u;
    }

    if(g_demo_air_diag_last_clock == 0u)
    {
        g_demo_air_diag_last_clock = diag_now;
    }
    diag_delta = diag_now - g_demo_air_diag_last_clock;
    diag_elapsed_ms = ((diag_delta * (uint32_t)SYSTEM_TIME_MICROSEN) + 999u) / 1000u;
    if(diag_elapsed_ms == 0u)
    {
        diag_elapsed_ms = 1u;
    }
    diag_expected = (((uint32_t)g_demo_report_hz * diag_elapsed_ms) + 500u) / 1000u;
    if(diag_expected == 0u)
    {
        diag_expected = 1u;
    }

    memset(report, 0, sizeof(report));
    demo_put_u32(&report[0], RX_HID_INPUT_MAGIC);
    demo_put_u32(&report[4], g_demo_hid_input_report_seq + 1u);
    demo_put_u32(&report[8], report_key_mask);
    report[12] = input_seq;
    report[13] = input_flags;
    report[14] = demo_hid_state_code();
    report[15] = g_demo_current_channel;
    demo_put_u16(&report[16], g_demo_report_hz);
    report[18] = g_demo_rate_code;
    report[19] = g_demo_link_active;
    report[20] = g_demo_last_data_seq;
    demo_put_u16(&report[21],
                 (g_demo_rx_pending_drop > 0xFFFFu) ? 0xFFFFu : (uint16_t)g_demo_rx_pending_drop);
    report[23] = demo_rx_pending_water(g_demo_rx_pending_head, g_demo_rx_pending_tail);
    report[24] = g_demo_rx_pending_max_water;
    demo_put_u16(&report[25],
                 (diag_rx_ok > 0xFFFFu) ? 0xFFFFu : (uint16_t)diag_rx_ok);
    demo_put_u16(&report[27],
                 (diag_expected > 0xFFFFu) ? 0xFFFFu : (uint16_t)diag_expected);
    demo_put_u16(&report[29],
                 (diag_crc_errors > 0xFFFFu) ? 0xFFFFu : (uint16_t)diag_crc_errors);
    report[31] = (uint8_t)(diag_seq_gap > 0xFFu ? 0xFFu : diag_seq_gap);

    if(demo_submit_hid_report(report) == 0u)
    {
        return 0u;
    }

    {
        uint32_t irq_status;

        SYS_DisableAllIrq(&irq_status);
        g_demo_air_diag_rx_ok = (g_demo_air_diag_rx_ok >= diag_rx_ok) ?
                                (g_demo_air_diag_rx_ok - diag_rx_ok) : 0u;
        g_demo_air_diag_seq_gap = (g_demo_air_diag_seq_gap >= diag_seq_gap) ?
                                  (g_demo_air_diag_seq_gap - diag_seq_gap) : 0u;
        g_demo_air_diag_crc_errors = (g_demo_air_diag_crc_errors >= diag_crc_errors) ?
                                     (g_demo_air_diag_crc_errors - diag_crc_errors) : 0u;
        g_demo_air_diag_type_errors = (g_demo_air_diag_type_errors >= diag_type_errors) ?
                                      (g_demo_air_diag_type_errors - diag_type_errors) : 0u;
        g_demo_air_diag_timeout_errors = (g_demo_air_diag_timeout_errors >= diag_timeout_errors) ?
                                         (g_demo_air_diag_timeout_errors - diag_timeout_errors) : 0u;
        g_demo_air_diag_last_clock = diag_now;
        SYS_RecoverIrq(irq_status);
    }
    g_demo_hid_input_report_seq++;
    g_demo_hid_input_window_mask = key_mask;
    return 1u;
}

uint8_t RF_TrySendTelemetryReport(void)
{
    uint8_t report[HID_ENDPOINT_SIZE];
    uint32_t rx_ok = g_demo_hid_rx_ok;
    uint32_t seq_expected = g_demo_hid_expected;
    uint32_t seq_bad = g_demo_hid_bad;
    uint32_t hop_events = g_demo_hid_hop_events;
    uint32_t errors = g_demo_hid_errors;
    uint32_t crc_errors = g_demo_hid_crc_errors;
    uint32_t type_errors = g_demo_hid_type_errors;
    uint32_t timeout_errors = g_demo_hid_timeout_errors;
    uint32_t max_silent_cycles = g_demo_hid_max_silent_cycles;
    uint16_t max_silent_ticks;
    uint16_t link_lost_silent_ticks = g_demo_hid_link_lost_silent_ticks;
    uint8_t hop_event_code = RX_HID_HOP_EVENT_NONE;
    uint16_t hop_event_value = 0u;
    uint16_t elapsed_ms;
    uint32_t expected;
    uint16_t loss;

    if(g_monitor_hid_enabled == 0u)
    {
        return 0u;
    }

    if(demo_try_send_latency_report() != 0u)
    {
        return 1u;
    }

    if((g_demo_link_active != 0u) &&
       (g_demo_hid_hop_start_pending == 0u) &&
       (g_demo_hid_hop_finish_pending == 0u) &&
       (g_demo_hid_input_valid != 0u))
    {
        g_demo_hid_input_keepalive_div++;
        if(g_demo_hid_input_keepalive_div >= RX_HID_INPUT_KEEPALIVE_DIV)
        {
            if(demo_try_send_input_report() != 0u)
            {
                g_demo_hid_input_keepalive_div = 0u;
                return 1u;
            }
        }
    }

    g_demo_hid_score_div++;
    if(g_demo_hid_score_div >= 5u)
    {
        g_demo_hid_score_div = 0u;
        if((g_demo_hid_hop_start_pending == 0u) &&
           (g_demo_hid_hop_finish_pending == 0u) &&
           (demo_try_send_score_report() != 0u))
        {
            return 1u;
        }
    }

    elapsed_ms = demo_hid_elapsed_ms();
    expected = demo_expected_from_elapsed(elapsed_ms);
    loss = demo_rx_loss_permille(rx_ok, expected);
    if((g_demo_have_data_seq != 0u) && (g_demo_link_active != 0u))
    {
        uint32_t current_silent_cycles = 0u;
        if(demo_snapshot_data_silent_cycles(&current_silent_cycles) != 0u)
        {
            if(current_silent_cycles > max_silent_cycles)
            {
                max_silent_cycles = current_silent_cycles;
            }
        }
    }
    max_silent_ticks = demo_tmr_cycles_to_system_ticks(max_silent_cycles);

    if(g_demo_hid_hop_start_pending != 0u)
    {
        hop_event_code = RX_HID_HOP_EVENT_START;
        hop_event_value = g_demo_hid_hop_start_score;
    }
    else if(g_demo_hid_hop_finish_pending != 0u)
    {
        hop_event_code = RX_HID_HOP_EVENT_FINISH;
        hop_event_value = g_demo_hid_hop_finish_duration_ms;
    }

    memset(report, 0, sizeof(report));
    demo_put_u32(&report[0], RX_HID_TELEMETRY_MAGIC);
    demo_put_u32(&report[4], g_demo_hid_telemetry_seq + 1u);
    demo_put_u16(&report[8], elapsed_ms);
    demo_put_u16(&report[10], g_demo_report_hz);
    demo_put_u32(&report[12], rx_ok);
    demo_put_u32(&report[16], expected);
    demo_put_u16(&report[20], loss);
    report[22] = demo_hid_state_code();
    report[23] = g_demo_current_channel;
    report[24] = g_demo_old_channel;
    report[25] = g_demo_target_channel;
    report[26] = g_demo_rate_code;
    report[27] = (uint8_t)(hop_events > 0xFFu ? 0xFFu : hop_events);
    report[28] = (uint8_t)(errors > 0xFFu ? 0xFFu : errors);
    report[29] = hop_event_code;
    demo_put_u16(&report[30],
                 (hop_event_code == RX_HID_HOP_EVENT_NONE) ?
                 ((link_lost_silent_ticks != 0u) ? link_lost_silent_ticks : max_silent_ticks) :
                 hop_event_value);

    if(demo_submit_hid_report(report) == 0u)
    {
        return 0u;
    }

    g_demo_hid_last_window_rx_ok = (rx_ok > 0xFFFFu) ? 0xFFFFu : (uint16_t)rx_ok;
    g_demo_hid_last_window_expected = (expected > 0xFFFFu) ? 0xFFFFu : (uint16_t)expected;
    g_demo_hid_last_window_errors = (errors > 0xFFu) ? 0xFFu : (uint8_t)errors;
    g_demo_hid_last_window_crc_errors = (crc_errors > 0xFFu) ? 0xFFu : (uint8_t)crc_errors;
    g_demo_hid_last_window_type_errors = (type_errors > 0xFFu) ? 0xFFu : (uint8_t)type_errors;
    g_demo_hid_last_window_timeout_errors = (timeout_errors > 0xFFu) ? 0xFFu : (uint8_t)timeout_errors;
    g_demo_hid_telemetry_seq++;
    g_demo_hid_rx_ok -= rx_ok;
    g_demo_hid_expected -= seq_expected;
    g_demo_hid_bad -= seq_bad;
    g_demo_hid_hop_events -= hop_events;
    g_demo_hid_errors -= errors;
    g_demo_hid_crc_errors -= crc_errors;
    g_demo_hid_type_errors -= type_errors;
    g_demo_hid_timeout_errors -= timeout_errors;
    g_demo_hid_max_silent_cycles = 0u;
    g_demo_hid_link_lost_silent_ticks = 0u;
    if(hop_event_code == RX_HID_HOP_EVENT_START)
    {
        g_demo_hid_hop_start_pending = 0u;
    }
    else if(hop_event_code == RX_HID_HOP_EVENT_FINISH)
    {
        g_demo_hid_hop_finish_pending = 0u;
    }
    return 1u;
}

void RF_Init(void)
{
    rfRoleConfig_t conf;

    taskID = TMOS_ProcessEventRegister(RF_ProcessEvent);
    PFIC_EnableIRQ(BLEB_IRQn);
    PFIC_EnableIRQ(BLEL_IRQn);

    TMR0_TimerInit(TMR0_FREE_RUN_WRAP - 1u);
    g_demo_hid_last_clock = TMOS_GetSystemClock();
    g_demo_ack_delay_tmr = demo_us_to_tmr_cycles(RF_AUTO_DEMO_ACK_TX_DELAY_US);
    g_demo_slot_tmr = demo_rate_to_slot_tmr_cycles(g_demo_report_hz);
    demo_ack_timer_cancel();
    PFIC_SetPriority(TMR1_IRQn, 0x80);
    PFIC_EnableIRQ(TMR1_IRQn);

    memset(&conf, 0, sizeof(conf));
    conf.TxPower = RF_AUTO_DEMO_TX_POWER;
    conf.rfProcessCB = RF_ProcessCallBack;
    conf.processMask = RF_STATE_RX | RF_STATE_RX_CRCERR |
                       RF_STATE_TX_FINISH | RF_STATE_TIMEOUT | RF_STATE_TX_IDLE;
    g_demo_config_ret = (uint8_t)RFRole_BasicInit(&conf);
    demo_channel_scores_init();

    g_demo_local_id_hash = demo_make_local_id_hash();
    demo_load_bond();
    g_demo_slot_tmr = demo_rate_to_slot_tmr_cycles(g_demo_report_hz);

    memset(&gParm, 0, sizeof(gParm));
    gParm.accessAddress = g_demo_link_access_address;
    gParm.crcInit = RF_LINK_CRC_INIT;
    gParm.frequency = demo_discovery_channel(0u);
    gParm.properties = RF_AUTO_DEMO_PHY_PROPS | RF_AUTO_DEMO_ACK_BIT;
    gParm.rxMaxLen = RF_AUTO_DEMO_PACKET_LEN;
    gParm.sendTime = RFH_TX_SEND_TIME_UNITS;
    RFRole_SetParam(&gParm);

#if (RF_AUTO_DEMO_SEND_ACK_ENABLE != 0u)
    memset(&gTxParam, 0, sizeof(gTxParam));
    gTxParam.accessAddress = gParm.accessAddress;
    gTxParam.crcInit = gParm.crcInit;
    gTxParam.frequency = gParm.frequency;
    gTxParam.properties = RF_AUTO_DEMO_PHY_PROPS;
    gTxParam.whiteChannel = gParm.frequency;
    gTxParam.sendTime = (uint8_t)gParm.sendTime;
    gTxParam.sendCount = 1u;
    gTxParam.txDMA = (uint32_t)TxBuf;
#endif

    memset(&gRxParam, 0, sizeof(gRxParam));
    gRxParam.accessAddress = gParm.accessAddress;
    gRxParam.crcInit = gParm.crcInit;
    gRxParam.frequency = gParm.frequency;
    gRxParam.properties = RF_AUTO_DEMO_PHY_PROPS | RF_AUTO_DEMO_ACK_BIT;
    gRxParam.rxDMA = (uint32_t)RxBuf[0];
    gRxParam.whiteChannel = gParm.frequency;
    gRxParam.rxMaxLen = RF_AUTO_DEMO_PACKET_LEN;
    gRxParam.timeOut = 0u;

    g_demo_current_channel = gParm.frequency;
    g_demo_old_channel = demo_discovery_channel(0u);
    g_demo_target_channel = demo_discovery_channel(0u);
    g_demo_hid_last_window_rx_ok = 0u;
    g_demo_hid_last_window_expected = 0u;
    g_demo_hid_last_window_errors = 0u;
    g_demo_hid_last_window_crc_errors = 0u;
    g_demo_hid_last_window_type_errors = 0u;
    g_demo_hid_last_window_timeout_errors = 0u;
    g_demo_air_diag_last_clock = TMOS_GetSystemClock();
    g_demo_air_diag_rx_ok = 0u;
    g_demo_air_diag_seq_gap = 0u;
    g_demo_air_diag_crc_errors = 0u;
    g_demo_air_diag_type_errors = 0u;
    g_demo_air_diag_timeout_errors = 0u;
    g_demo_rx_active_slot = 0u;
    g_demo_rx_next_slot = 0u;
    g_demo_rx_pending_head = 0u;
    g_demo_rx_pending_tail = 0u;
    g_demo_rx_pending_drop = 0u;
    g_demo_rx_pending_max_water = 0u;
    g_demo_last_data_tmr = TMR0_GetCurrentTimer();
    g_demo_link_active = 0u;
    g_demo_rx_state = RF_AUTO_RX_UNCONNECTED;
    g_demo_pair_tx_active = 0u;
    g_demo_pair_after_tx_action = 0u;
    g_demo_pair_done_repeat_left = 0u;
    g_demo_dual_side = 0u;
    g_demo_dual_switch_clock = TMOS_GetSystemClock();

    if(g_demo_has_bond != 0u)
    {
        demo_arm_rx();
    }
}
