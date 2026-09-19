#include "rf_short_transport.h"
/********************************** (C) COPYRIGHT *******************************
 * File Name          : RF_PHY.c
 * Description        : RX side for RF PHY DATA + 100ms ACK control protocol.
 *******************************************************************************/

#include "CONFIG.h"
#include "RF_PHY.h"
#include "HAL.h"
#include "wchrf.h"
#include "rf_hop_protocol.h"
#include "rf_pairing_protocol.h"
#include "rf_hop_bond.h"
#include "rf_hop_bond_journal.h"
#include "rf_hop_score.h"
#include "rf_monitor_control.h"
#include "dongle_config.h"
#include "ch585_usbhs_device.h"
#include "usbd_compatibility_hid.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "rf_link_policy.h"
#include "rf_trace_sync.h"
#define RF_LINK_CLOCK_IMPLEMENTATION
#include "rf_link_clock.h"

#define RF_AUTO_DEMO_PACKET_LEN        RFH_AIR_PACKET_LEN
#define RF_AUTO_DEMO_DMA_LEN           (RF_AUTO_DEMO_PACKET_LEN + 2u)
#define RF_AUTO_DEMO_DATA_TYPE         0xFFu
#define RF_AUTO_DEMO_ACK_TYPE          0xFFu
#define RF_AUTO_DEMO_CHANNEL           39u
#define RF_AUTO_DEMO_FREQUENCY_KHZ     2480000UL
#ifndef RF_AUTO_DEMO_TX_POWER
#define RF_AUTO_DEMO_TX_POWER          LL_TX_POWEER_4_DBM
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
#define RF_AUTO_DEMO_HOP_DUAL_TIMEOUT_MS 200u
#define RF_AUTO_DEMO_HOP_CONFIRM_ACK_KEEP_TOKENS 6u
#define RF_AUTO_DEMO_RECOVERY_DWELL_MS 20u
#define RF_AUTO_DEMO_RECOVERY_SCAN_TIMEOUT_MS 40u
#define RF_AUTO_DEMO_PAIR_TX_REJECT_REASON_DEFAULT RFH_PAIR_REJECT_BAD_STATE
#define RF_AUTO_DEMO_PAIR_AFTER_ACCEPT 1u
#define RF_AUTO_DEMO_PAIR_AFTER_DONE   2u
#define RF_AUTO_DEMO_PAIR_AFTER_REJECT 3u
#define RF_AUTO_DEMO_PAIR_DONE_REPEAT_COUNT 6u
#define RF_AUTO_DEMO_PAIR_DONE_RETRY_MS 100u
#define RF_AUTO_DEMO_FIRST_DATA_TIMEOUT_MS 150u
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
#define RF_RX_PENDING_REPORT_CHUNK     1u
#define RX_HID_TELEMETRY_MAGIC         0x314D4852UL
#define RX_HID_SCORE_MAGIC             0x31534852UL
#define RX_HID_RSSI_MAGIC              0x31524852UL
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
    RF_AUTO_RX_PAIR_CONFIRM_WAIT,
    RF_AUTO_RX_PAIR_COMMIT_WAIT
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
    uint32_t access_address;
    uint32_t generation;
    uint16_t measure_seq;
    uint8_t measure_valid;
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
static rfh_bond_journal_state_t g_demo_bond_store;
static uint8_t g_demo_pair_candidate_pending = 0u;
static volatile uint32_t g_demo_last_data_tmr = 0u;
static volatile uint8_t g_demo_link_active = 0u;
static uint32_t g_demo_first_data_deadline_clock = 0u;
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
static uint32_t g_demo_recovery_scan_deadline_clock = 0u;
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
static uint32_t g_demo_pair_done_retry_clock = 0u;
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
static volatile uint8_t g_demo_hid_input_battery_code = 0u;
static volatile uint8_t g_demo_hid_input_battery_flags = 0u;
static volatile uint32_t g_demo_hid_input_sample_tick_us = 0u;
static volatile uint8_t g_demo_hid_input_sync_seq = 0u;
static volatile uint32_t g_demo_hid_input_sync_rx_tick_us = 0u;
static volatile uint32_t g_demo_hid_input_sync_tx_tick_us = 0u;
typedef struct {
    uint16_t wire, row, event; uint8_t tag, len; volatile uint8_t flags, dirty, revision;
    uint32_t mask, previous, rx, process, ready, submit, born, source[4], tx; volatile uint32_t done;
} relative_rx_t;
static relative_rx_t g_relative_rx[64];
static uint16_t g_relative_wire, g_relative_row, g_relative_session;
static uint8_t g_relative_wire_valid, g_relative_air_seq, g_relative_tag, g_relative_prepared, g_relative_inflight;
static uint16_t g_relative_inflight_row;
static uint32_t g_relative_air_clock;
static void short_rx_edge(const rf_rx_pending_t *p,uint32_t process);
static void short_rx_trace(const uint8_t *p);
static uint8_t short_send_trace(void);
static void short_dirty(relative_rx_t *r){r->revision=(r->revision+1u)&127u;r->dirty=3;}
static rfh_aux_rx_t g_aux_rx;
static uint32_t g_aux_rx_clock;
static uint32_t g_short_tx_stats[6],g_short_stats_seq;
static uint8_t g_short_stats_pending;
static uint8_t g_short_measure;
static uint32_t g_short_capture_refresh;
static void demo_accept_aux(const uint8_t *fragment);
static uint8_t g_trace_hid[16][32];
static uint8_t g_trace_hid_head, g_trace_hid_tail;
static uint32_t g_trace_hid_drops;
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
static volatile uint8_t g_demo_xinput_pending = 0u;
static volatile uint8_t g_demo_neutral_pending = 0u;
static volatile uint8_t g_demo_input_stale = 0u;
static volatile uint8_t g_demo_have_valid_input = 0u;
static volatile uint32_t g_demo_last_input_tmr = 0u;
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
static uint32_t g_demo_hid_rssi_seq = 0u;
static uint8_t g_demo_hid_rssi_div = 0u;
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
static volatile rfh_trace_sync_t g_trace_sync_wait[32];

static volatile uint32_t g_demo_radio_generation;
static volatile uint8_t g_demo_radio_reconfiguring;
static volatile uint8_t g_demo_ack_tx_active;
static uint32_t g_demo_ack_tx_clock;
static uint32_t g_demo_sys_clock;
static rfh_sequence_t g_demo_air_sequence;
static rfh_hop_transaction_t g_demo_hop_transaction;
static uint8_t g_demo_hop_confirmed;
static uint8_t g_demo_ack_snapshot_ready;
static uint8_t g_demo_ack_completion_cmd;
static uint8_t g_demo_ack_completion_action;
static uint32_t g_demo_ack_due_tmr;
static uint32_t g_demo_ack_late, g_demo_ack_duplicate, g_demo_ack_watchdog;
static uint32_t g_demo_rf_ready_clock, g_demo_link_seek_clock;
static uint16_t g_demo_last_connect_ms;
static uint32_t g_demo_connect_count;
static uint32_t g_demo_diag_clock, g_demo_diag_seq;
static uint8_t g_demo_diag_page;
static uint16_t g_demo_usb_ready_ms = 0xFFFFu;
static uint32_t g_demo_total_crc;
static uint32_t g_demo_cycles_per_us;
static uint8_t g_demo_rf_ready;
static uint32_t g_demo_housekeeping_clock;
static uint8_t g_demo_housekeeping_valid;
static uint16_t g_demo_peer_window_ms, g_demo_peer_due, g_demo_peer_started, g_demo_peer_dropped;
static uint32_t g_demo_peer_diag_clock;
static uint8_t g_demo_peer_diag_valid;
static uint32_t g_demo_air_total_received, g_demo_air_total_missing;
static uint32_t g_demo_edge_drop;
static uint32_t g_demo_rx_arm_fail_total, g_demo_ack_fail_total;
static uint32_t g_demo_rx_rearm_max_cycles, g_demo_rx_callback_max_cycles;
static uint32_t g_demo_input_commit_max_cycles, g_demo_input_capture_max_cycles;
static uint32_t g_demo_short_decoded;

__HIGH_CODE
static void demo_note_max_cycles(uint32_t *maximum, uint32_t start)
{
    uint32_t elapsed = SysTick->CNT - start;
    if(elapsed > *maximum) *maximum = elapsed;
}

static uint8_t g_demo_input_fifo[32][RF_INPUT_PAYLOAD_LEN];
static uint32_t g_demo_input_rx_tmr[32], g_demo_input_process_tmr[32];
static uint32_t g_demo_input_generation[32];
static volatile uint32_t g_demo_input_epoch; /* neutral/reset invalidates in-flight construction */
static uint8_t g_demo_input_head, g_demo_input_tail;
static uint8_t g_demo_last_queued_keys[4], g_demo_last_queued_valid;
static uint32_t g_demo_last_queued_generation;
static void demo_ack_timer_cancel(void);
static void demo_cancel_ack(void);
static uint8_t demo_fast_rx_packet(const uint8_t *buf, uint32_t rx_tmr);
static uint8_t demo_note_air_packet(const uint8_t *air, uint32_t rx_tmr);

static void demo_queue_neutral_xinput_report(uint8_t force);

__HIGH_CODE
static uint8_t demo_hid_stats_enabled(void)
{
    return (g_monitor_hid_enabled != 0u) ? 1u : 0u;
}

static void demo_hid_clear_report_state(void)
{
    uint32_t irq_status;
    uint32_t now = RF_LinkClockNow();

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
    g_demo_hid_input_battery_code = 0u;
    g_demo_hid_input_battery_flags = 0u;
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
    g_demo_hid_rssi_div = 0u;
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
        seed = rfh_fnv1a32_mix_u32(seed, RF_LinkClockNow());
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

static uint8_t demo_bond_flash_read(uint32_t address, void *data, uint32_t length)
{
    return EEPROM_READ(address, data, length);
}

static uint8_t demo_bond_flash_write(uint32_t address, const void *data, uint32_t length)
{
    return EEPROM_WRITE(address, (void *)data, length);
}

static uint8_t demo_bond_flash_erase(uint32_t address, uint32_t length)
{
    return EEPROM_ERASE(address, length);
}

static const rfh_bond_journal_backend_t g_demo_bond_backend = {
    demo_bond_flash_read,
    demo_bond_flash_write,
    demo_bond_flash_erase
};

static void demo_load_bond(void)
{
#if (RFH_TEST_FIXED_BOND_ENABLE != 0u)
    demo_select_unpaired_address();
    return;
#endif

    memset(&g_demo_bond_store, 0, sizeof(g_demo_bond_store));
    if(rfh_bond_journal_load(&g_demo_bond_backend,
                             g_demo_local_id_hash,
                             &g_demo_bond_store) == 0u)
    {
        demo_select_unpaired_address();
        return;
    }
    if(g_demo_bond_store.has_active != 0u)
    {
        demo_apply_loaded_bond(&g_demo_bond_store.active);
    }
    else
    {
        demo_select_unpaired_address();
    }
    if(g_demo_bond_store.has_pending != 0u)
    {
        /* A prepared candidate survives reset and participates in dual-address
         * recovery.  It is not made active until candidate CONNECT arrives. */
        g_demo_pair_candidate_pending = 1u;
        g_demo_has_bond = 1u;
        g_demo_link_access_address = g_demo_bond_store.pending.link_access_address;
        g_demo_bond_channel_a = g_demo_bond_store.pending.channel_a;
        g_demo_bond_channel_b = g_demo_bond_store.pending.channel_b;
    }
}

static uint8_t demo_prepare_bond(uint32_t link_access_address,
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
    if(rfh_bond_journal_prepare(&g_demo_bond_backend,
                                &g_demo_bond_store,
                                g_demo_local_id_hash,
                                &record) == 0u)
    {
        return 0u;
    }
    g_demo_pair_candidate_pending = 1u;
    g_demo_has_bond = 1u;
    g_demo_link_access_address = record.link_access_address;
    g_demo_bond_channel_a = record.channel_a;
    g_demo_bond_channel_b = record.channel_b;
    return 1u;
}

static uint8_t demo_commit_prepared_bond(void)
{
#if (RFH_TEST_FIXED_BOND_ENABLE != 0u)
    g_demo_pair_candidate_pending = 0u;
    return 1u;
#else
    if(g_demo_pair_candidate_pending == 0u)
    {
        return 1u;
    }
    if(rfh_bond_journal_commit_pending(&g_demo_bond_backend,
                                       &g_demo_bond_store,
                                       g_demo_local_id_hash) == 0u)
    {
        return 0u;
    }
    if(g_demo_bond_store.has_active == 0u)
    {
        return 0u;
    }
    demo_apply_loaded_bond(&g_demo_bond_store.active);
    g_demo_pair_candidate_pending = 0u;
    return 1u;
#endif
}

static uint8_t demo_abort_prepared_bond(void)
{
#if (RFH_TEST_FIXED_BOND_ENABLE != 0u)
    g_demo_pair_candidate_pending = 0u;
    return 1u;
#else
    if(g_demo_bond_store.has_pending != 0u)
    {
        if(rfh_bond_journal_abort_pending(&g_demo_bond_backend,
                                          &g_demo_bond_store,
                                          g_demo_local_id_hash) == 0u)
        {
            return 0u;
        }
    }
    if(g_demo_bond_store.has_active != 0u)
    {
        demo_apply_loaded_bond(&g_demo_bond_store.active);
    }
    else
    {
        demo_select_unpaired_address();
    }
    g_demo_pair_candidate_pending = 0u;
    return 1u;
#endif
}

static uint8_t demo_apply_access_address(uint32_t access_address)
{
    if((access_address != RFH_PAIR_ACCESS_ADDRESS) &&
       (rfh_access_address_valid(access_address) == 0u))
    {
        return 0u;
    }

    g_demo_radio_reconfiguring = 1u;
    (void)RFRole_Stop();
    g_demo_rx_active = 0u;
    gParm.accessAddress = access_address;
    RFRole_SetParam(&gParm);
#if (RF_AUTO_DEMO_SEND_ACK_ENABLE != 0u)
    gTxParam.accessAddress = access_address;
#endif
    gRxParam.accessAddress = access_address;
    g_demo_radio_reconfiguring = 0u;
    return 1u;
}

__HIGH_CODE
static uint8_t demo_pair_is_active(void)
{
    return ((g_demo_rx_state == RF_AUTO_RX_PAIRING) ||
            (g_demo_rx_state == RF_AUTO_RX_PAIR_CONFIRM_WAIT) ||
            (g_demo_rx_state == RF_AUTO_RX_PAIR_COMMIT_WAIT)) ? 1u : 0u;
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

__HIGH_CODE
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
    if(g_short_measure)flags|=RFH_MEASUREMENT_FLAG;
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
    g_monitor_pending_flags = flags & (RFMON_FLAG_AUTO_HOP | RFH_MEASUREMENT_FLAG);
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

__HIGH_CODE
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
    if(cmd==4u) { if(g_short_measure)g_short_capture_refresh=SysTick->CNT;return 1u; }
    if(cmd == RFMON_CMD_TIME_SYNC) return 0u; /* retired in protocol v2 */
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

    if(g_short_measure != ((flags&RFH_MEASUREMENT_FLAG)?1u:0u)) {
        g_short_measure=(flags&RFH_MEASUREMENT_FLAG)?1u:0u;
        memset(g_relative_rx,0,sizeof(g_relative_rx));memset(&g_aux_rx,0,sizeof(g_aux_rx));
        g_relative_tag=g_relative_prepared=g_relative_inflight=0;g_relative_session++;
    }
    if(g_short_measure)g_short_capture_refresh=SysTick->CNT;
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
    uint64_t cycles;
    if(g_demo_cycles_per_us && g_demo_sys_clock % 1000000u == 0u) {
        if(us > 0xFFFFFFFFu / g_demo_cycles_per_us) return 0xFFFFFFFFu;
        return us ? us * g_demo_cycles_per_us : 1u;
    }
    cycles = (uint64_t)g_demo_sys_clock * (uint64_t)us;

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

    cycles = ((uint64_t)g_demo_sys_clock + (uint64_t)hz - 1u) / (uint64_t)hz;
    if(cycles == 0u)
    {
        return 1u;
    }
    return (cycles > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)cycles;
}

/* These helpers run on the per-packet path, including while interrupts are
 * masked. Keep their instructions in RAM too, not only the RF ISR entry. */
__HIGH_CODE
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

__HIGH_CODE
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
    uint64_t denom = (uint64_t)g_demo_sys_clock * (uint64_t)SYSTEM_TIME_MICROSEN;
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
    uint32_t sys_clock = g_demo_sys_clock;
    if(g_demo_cycles_per_us && sys_clock % 1000000u == 0u)
        return (cycles + g_demo_cycles_per_us - 1u) / g_demo_cycles_per_us;

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

static uint16_t demo_ticks_to_ms(uint32_t ticks)
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
    return demo_ticks_to_ms((uint32_t)(end - start));
}

__HIGH_CODE
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
    uint32_t bad = g_demo_window_missing;

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
    uint32_t now = RF_LinkClockNow();

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
    uint32_t now = RF_LinkClockNow();

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

__HIGH_CODE
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

/* Small, potentially unaligned FIFO/report copies must not call Flash libc
 * while RF interrupts are masked. Volatile byte stores prevent the compiler
 * from replacing these loops with an out-of-line memcpy/memset call. */
__HIGH_CODE
static void demo_copy_bytes(void *dst, const void *src, uint32_t len)
{
    volatile uint8_t *out = (volatile uint8_t *)dst;
    const uint8_t *in = (const uint8_t *)src;
    while(len--) *out++ = *in++;
}

__HIGH_CODE
static void demo_zero_bytes(void *dst, uint32_t len)
{
    volatile uint8_t *out = (volatile uint8_t *)dst;
    while(len--) *out++ = 0u;
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

__HIGH_CODE
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
    pending->access_address = gRxParam.accessAddress;
    pending->generation = g_demo_radio_generation;
    pending->measure_seq=g_relative_wire;
    pending->measure_valid=g_relative_wire_valid;
    if(rx_buf[1] <= RF_AUTO_DEMO_PACKET_LEN)
    {
        demo_copy_bytes(pending->air, &rx_buf[2], rx_buf[1]);
        if(rx_buf[1] < RF_AUTO_DEMO_PACKET_LEN)
        {
            demo_zero_bytes(&pending->air[rx_buf[1]], (uint8_t)(RF_AUTO_DEMO_PACKET_LEN - rx_buf[1]));
        }
    }
    else
    {
        demo_zero_bytes(pending->air, sizeof(pending->air));
    }
    g_demo_rx_pending_head = next;
    demo_note_rx_pending_water(next, g_demo_rx_pending_tail);
}

static void demo_queue_rx_pending_crcerr(uint32_t rx_tmr)
{
    (void)rx_tmr;
    g_demo_stat.data_crc_err++;
    g_demo_total_crc++;
    g_demo_window_crc++;
    if(demo_hid_stats_enabled()) {
        g_demo_hid_errors++; g_demo_hid_crc_errors++; g_demo_air_diag_crc_errors++;
    }
}

__HIGH_CODE
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
    demo_copy_bytes(pending, &g_demo_rx_pending[tail], sizeof(*pending));
    g_demo_rx_pending_tail = demo_rx_pending_next(tail);
    SYS_RecoverIrq(irq_status);
    return 1u;
}

static void demo_set_channel(uint8_t channel)
{
    g_demo_radio_reconfiguring = 1u;
    demo_cancel_ack();
    g_demo_radio_generation++;
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
    demo_score_window_reset_channel(channel, RF_LinkClockNow());
    g_demo_radio_reconfiguring = 0u;
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

static void demo_cancel_ack(void)
{
    demo_ack_timer_cancel();
    g_demo_ack_pending = 0u;
    g_demo_ack_tx_active = 0u;
    g_demo_ack_snapshot_ready = 0u;
    g_demo_ack_completion_cmd = RFH_CMD_NONE;
    g_demo_ack_completion_action = 0u;
    g_demo_pending_ack_cmd = RFH_CMD_NONE;
    g_demo_pending_ack_seq = 0u;
    g_demo_after_ack_action = 0u;
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

    /* Completion belongs to this frozen packet, not a later queued command. */
    g_demo_ack_completion_cmd = g_demo_pending_ack_cmd;
    g_demo_ack_completion_action = g_demo_after_ack_action;

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
    else {
        TxBuf[1]=RFH_SHORT_ACK_LEN;
        data[0]=g_demo_last_ack_token;rfh_put_u16(data+1,quality);
    }
    air[1]=g_demo_last_ack_token;
    if(TxBuf[1]==RFH_AIR_PACKET_LEN)data[RFH_ACK_FLAGS]|=RFH_SHORT_ACK_VERSION;
    g_demo_pending_ack_cmd = RFH_CMD_NONE;
    g_demo_pending_ack_seq = 0u;
    g_demo_after_ack_action = 0u;
    demo_reset_quality_window();
}

__HIGH_CODE
static void demo_arm_rx(void)
{
    bStatus_t ret;
    uint8_t slot;

    if(g_demo_config_ret != SUCCESS)
    {
        return;
    }
    if(g_demo_ack_tx_active != 0u) return;
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
        g_demo_rx_arm_fail_total++;
        g_demo_rearm_pending = 1u;
    }
}

static uint8_t demo_discovery_channel(uint8_t side)
{
    return ((side & 1u) == 0u) ?
           g_demo_bond_channel_b :
           g_demo_bond_channel_a;
}

static void demo_select_unconnected_address(uint8_t side)
{
    const rfh_bond_record_t *record = 0;

    if(g_demo_pair_candidate_pending != 0u)
    {
        if(((side & 0x02u) == 0u) || (g_demo_bond_store.has_active == 0u))
        {
            record = &g_demo_bond_store.pending;
        }
        else
        {
            record = &g_demo_bond_store.active;
        }
    }
    else if(g_demo_bond_store.has_active != 0u)
    {
        record = &g_demo_bond_store.active;
    }
    else if(g_demo_has_bond != 0u)
    {
        record = &g_demo_bond;
    }

    if(record == 0)
    {
        return;
    }
    g_demo_link_access_address = record->link_access_address;
    g_demo_bond_channel_a = rfh_hop_channel_valid(record->channel_a) ? record->channel_a : RFH_DISCOVERY_CHANNEL_A;
    g_demo_bond_channel_b = rfh_hop_channel_valid(record->channel_b) ? record->channel_b : RFH_DISCOVERY_CHANNEL_B;
    if(record->rate_code <= RFH_RATE_8K)
    {
        g_demo_rate_code = record->rate_code;
        g_demo_report_hz = rfh_rate_hz_from_code(record->rate_code);
    }
    if(gRxParam.accessAddress != record->link_access_address) {
        demo_cancel_ack();
        g_demo_radio_generation++;
        (void)demo_apply_access_address(record->link_access_address);
    }
}

static void demo_enter_rx_unconnected(uint32_t now)
{
    uint8_t anchor_channel;

    demo_cancel_ack();
    g_demo_hop_transaction.valid = 0u;
    g_demo_hop_confirmed = 0u;
    g_demo_air_sequence.valid = 0u;
    if(g_demo_rx_state != RF_AUTO_RX_RECOVERY_SCAN) g_demo_link_seek_clock = now;
    demo_queue_neutral_xinput_report(1u);
    demo_select_unconnected_address(0u);
    anchor_channel = demo_discovery_channel(0u);
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

static void demo_enter_rx_recovery_scan(uint32_t now)
{
    uint8_t first_channel = g_demo_current_channel;
    demo_cancel_ack();
    g_demo_link_seek_clock = now;

    demo_queue_neutral_xinput_report(1u);
    if((g_demo_config_ret != SUCCESS) || (g_demo_has_bond == 0u))
    {
        demo_enter_rx_unconnected(now);
        return;
    }
    {
        if(monitor_channel_valid(first_channel) == 0u)
        {
            first_channel = g_demo_current_channel;
        }
        if(monitor_channel_valid(first_channel) == 0u)
        {
            first_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
        }
    }

    g_demo_link_active = 0u;
    g_demo_rx_state = RF_AUTO_RX_RECOVERY_SCAN;
    g_demo_pending_ack_cmd = RFH_CMD_NONE;
    g_demo_pending_ack_seq = 0u;
    g_demo_after_ack_action = 0u;
    g_demo_confirm_ack_keep_count = 0u;
    g_demo_have_ack_token = 0u;
    g_demo_recovery_scan_rank = demo_channel_index(first_channel);
    if(g_demo_recovery_scan_rank == 0xFFu)
    {
        g_demo_recovery_scan_rank = 0u;
    }
    else
    {
        g_demo_recovery_scan_rank++;
        if(g_demo_recovery_scan_rank >= RFH_HOP_CHANNEL_COUNT)
        {
            g_demo_recovery_scan_rank = 0u;
        }
    }
    g_demo_recovery_scan_clock = now;
    g_demo_recovery_scan_deadline_clock =
        now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_RECOVERY_SCAN_TIMEOUT_MS);
    demo_set_channel(first_channel);
    demo_arm_rx();
}

static void demo_service_unconnected_scan(uint32_t now)
{
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
    if((uint32_t)(now - g_demo_dual_switch_clock) <
       MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_DISCOVERY_SCAN_DWELL_MS))
    {
        return;
    }

    g_demo_dual_switch_clock = now;
    g_demo_dual_side++;
    demo_select_unconnected_address(g_demo_dual_side);
    demo_set_channel(demo_discovery_channel(g_demo_dual_side));
    demo_arm_rx();
}

static void demo_send_ack(void)
{
    bStatus_t ret;

    if(g_demo_ack_tx_active) return;
    g_demo_ack_pending = 0u;
    g_demo_radio_reconfiguring = 1u;
    (void)RFRole_Stop();
    g_demo_rx_active = 0u;
    if(!g_demo_ack_snapshot_ready) demo_fill_ack_packet();
    g_demo_ack_snapshot_ready = 0u;
    g_demo_ack_tx_active = 1u;
    g_demo_ack_tx_clock = RF_LinkClockNow();
    gTxParam.txDMA = (uint32_t)TxBuf;
    g_demo_tx_start_ret = (uint8_t)RFIP_SetTxStart();
    if(g_demo_tx_start_ret != SUCCESS)
    {
        demo_cancel_ack();
        (void)RFRole_Stop();
        g_demo_radio_reconfiguring = 0u;
        g_demo_stat.ack_fail++; g_demo_ack_fail_total++;
        g_demo_rearm_pending = 1u;
        return;
    }

    ret = RFIP_SetTxParm(&gTxParam);
    g_demo_tx_parm_ret = (uint8_t)ret;
    if(ret != SUCCESS)
    {
        g_demo_stat.tx_parm_fail++;
        demo_cancel_ack();
        (void)RFRole_Stop();
        g_demo_radio_reconfiguring = 0u;
        g_demo_stat.ack_fail++; g_demo_ack_fail_total++;
        g_demo_rearm_pending = 1u;
    }
    g_demo_radio_reconfiguring = 0u;
}

static void demo_schedule_ack(uint8_t remaining_slots)
{
    /* CONNECT FINAL has no request token and is handled independently. */
    if(g_demo_ack_pending || g_demo_ack_tx_active) return;
    demo_fill_ack_packet();
    g_demo_ack_snapshot_ready = 1u;
    g_demo_ack_pending = 1u;
    g_demo_ack_due_tmr = (TMR0_GetCurrentTimer() + g_demo_ack_delay_tmr +
                         (uint32_t)remaining_slots * g_demo_slot_tmr) % TMR0_FREE_RUN_WRAP;
    demo_ack_timer_arm(g_demo_ack_delay_tmr + (uint32_t)remaining_slots * g_demo_slot_tmr);
}

static void demo_fill_pair_packet(uint8_t cmd, uint32_t arg32)
{
    uint8_t *air = &TxBuf[2];
    uint8_t write_bond = (cmd == RFH_CMD_PAIR_DONE) ? 1u : 0u;

    memset(TxBuf, 0, sizeof(TxBuf));
    TxBuf[0] = RFH_WCH_PREAMBLE;
    TxBuf[1] = RF_AUTO_DEMO_PACKET_LEN;
    (void)rf_pair_encode_air(air,
                             g_demo_rate_code,
                             (uint8_t)g_demo_pair_session,
                             cmd,
                             g_demo_pair_session,
                             arg32,
                             write_bond);
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
        g_demo_stat.ack_fail++; g_demo_ack_fail_total++;
        return 0u;
    }
    ret = RFIP_SetTxParm(&gTxParam);
    g_demo_tx_parm_ret = (uint8_t)ret;
    if(ret != SUCCESS)
    {
        g_demo_pair_tx_active = 0u;
        g_demo_pair_after_tx_action = 0u;
        g_demo_stat.tx_parm_fail++;
        g_demo_stat.ack_fail++; g_demo_ack_fail_total++;
        return 0u;
    }
    return 1u;
}

static void demo_abort_pairing(uint32_t now)
{
    g_demo_pair_tx_active = 0u;
    g_demo_pair_after_tx_action = 0u;
    g_demo_pair_done_repeat_left = 0u;
    g_demo_pair_deadline_clock = 0u;
    g_demo_pair_confirm_deadline_clock = 0u;
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
        g_demo_rx_state = RF_AUTO_RX_PAIR_COMMIT_WAIT;
        g_demo_pair_scan_side = 0u;
        g_demo_pair_scan_clock = RF_LinkClockNow();
        g_demo_pair_done_retry_clock =
            g_demo_pair_scan_clock +
            MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_PAIR_DONE_RETRY_MS);
        demo_select_unconnected_address(0u);
        demo_set_channel(demo_discovery_channel(0u));
        demo_arm_rx();
    }
    else if(action == RF_AUTO_DEMO_PAIR_AFTER_REJECT)
    {
        demo_abort_pairing(RF_LinkClockNow());
    }
}

static uint8_t demo_process_pair_packet(const rf_rx_pending_t *pending)
{
    const uint8_t *air;
    rf_pair_packet_t packet;

    if((pending == 0) || (pending->len != RF_AUTO_DEMO_PACKET_LEN))
    {
        return 0u;
    }
    if(demo_pair_is_active() == 0u)
    {
        return 0u;
    }

    air = pending->air;
    if(rfh_packet_type(air[RFH_HDR0_OFFSET]) != RFH_PKT_PAIR)
    {
        return 0u;
    }

    if(rf_pair_decode_air(air, pending->len, &packet) == 0u)
    {
        (void)demo_send_pair_packet(RFH_CMD_PAIR_REJECT,
                                    RFH_PAIR_REJECT_BAD_VERSION,
                                    RFH_PAIR_ACCESS_ADDRESS,
                                    RF_AUTO_DEMO_PAIR_AFTER_REJECT);
        return 1u;
    }

    if(((g_demo_rx_state == RF_AUTO_RX_PAIRING) ||
        (g_demo_rx_state == RF_AUTO_RX_PAIR_COMMIT_WAIT)) &&
       (packet.cmd == RFH_CMD_PAIR_OFFER) &&
       (packet.arg != 0u))
    {
        if((g_demo_pair_candidate_pending != 0u) &&
           (demo_abort_prepared_bond() == 0u))
        {
            (void)demo_send_pair_packet(RFH_CMD_PAIR_REJECT,
                                        RFH_PAIR_REJECT_BOND_FAILED,
                                        RFH_PAIR_ACCESS_ADDRESS,
                                        RF_AUTO_DEMO_PAIR_AFTER_REJECT);
            return 1u;
        }
        g_demo_pair_session = packet.session;
        g_demo_pair_tx_id_hash = packet.arg;
        g_demo_pair_rx_id_hash = g_demo_local_id_hash;
        g_demo_pair_link_access_address = 0u;
        g_demo_pair_done_confirm32 = 0u;
        g_demo_pair_done_repeat_left = 0u;
        g_demo_rx_state = RF_AUTO_RX_PAIR_CONFIRM_WAIT;
        g_demo_pair_confirm_deadline_clock =
            RF_LinkClockNow() + MS1_TO_SYSTEM_TIME(RFH_PAIR_CONFIRM_TIMEOUT_MS);
        (void)demo_send_pair_packet(RFH_CMD_PAIR_ACCEPT,
                                    g_demo_pair_rx_id_hash,
                                    RFH_PAIR_ACCESS_ADDRESS,
                                    RF_AUTO_DEMO_PAIR_AFTER_ACCEPT);
        g_demo_stat.hop_event++;
        return 1u;
    }

    if((g_demo_rx_state == RF_AUTO_RX_PAIR_CONFIRM_WAIT) &&
       (packet.cmd == RFH_CMD_PAIR_CONFIRM) &&
       (packet.session == g_demo_pair_session) &&
       (packet.write_bond != 0u))
    {
        g_demo_pair_link_access_address = packet.arg;
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
        if(demo_prepare_bond(g_demo_pair_link_access_address,
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
    if((g_demo_pair_deadline_clock != 0u) &&
       ((int32_t)(now - g_demo_pair_deadline_clock) >= 0))
    {
        /* A prepared candidate is deliberately retained.  Ordinary
         * unconnected scanning will let the first valid old/candidate CONNECT
         * choose commit versus rollback after either side resets or times out. */
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
    if((g_demo_rx_state == RF_AUTO_RX_PAIR_COMMIT_WAIT) &&
       (g_demo_pair_tx_active == 0u))
    {
        if((int32_t)(now - g_demo_pair_done_retry_clock) >= 0)
        {
            g_demo_pair_done_repeat_left = 1u;
            g_demo_pair_done_retry_clock =
                now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_PAIR_DONE_RETRY_MS);
            (void)demo_apply_access_address(g_demo_pair_link_access_address);
            (void)demo_send_pair_packet(RFH_CMD_PAIR_DONE,
                                        g_demo_pair_done_confirm32,
                                        g_demo_pair_link_access_address,
                                        RF_AUTO_DEMO_PAIR_AFTER_DONE);
            return;
        }
        if((uint32_t)(now - g_demo_pair_scan_clock) >=
           MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_DISCOVERY_SCAN_DWELL_MS))
        {
            g_demo_pair_scan_clock = now;
            g_demo_pair_scan_side++;
            if(g_demo_pair_scan_side >= 5u)
            {
                g_demo_pair_scan_side = 0u;
            }
            if(g_demo_pair_scan_side == 4u)
            {
                (void)demo_apply_access_address(RFH_PAIR_ACCESS_ADDRESS);
                demo_set_channel(RFH_PAIR_CHANNEL_A);
            }
            else
            {
                demo_select_unconnected_address(g_demo_pair_scan_side);
                demo_set_channel(demo_discovery_channel(g_demo_pair_scan_side));
            }
            demo_arm_rx();
        }
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

static uint8_t demo_note_air_packet(const uint8_t *air, uint32_t rx_tmr)
{
    uint8_t diff;
    uint32_t now = RF_LinkClockNow();
    uint32_t half_wrap = MS1_TO_SYSTEM_TIME(128000u / g_demo_report_hz);
    diff = rfh_sequence_accept(&g_demo_air_sequence, air[RFH_HDR1_OFFSET], now, half_wrap);
    if(!diff) return 0u;
    if(g_relative_wire_valid) {
        uint8_t delta=(uint8_t)(air[1]-g_relative_air_seq);
        if((uint32_t)(now-g_relative_air_clock)>=MS1_TO_SYSTEM_TIME(128000u/g_demo_report_hz) || !delta)
            g_relative_wire_valid=0;
        else g_relative_wire+=delta;
    }
    g_relative_air_seq=air[1];g_relative_air_clock=now;
    g_demo_air_total_received++;
    g_demo_air_total_missing += diff - 1u;
    g_demo_window_expected += diff;
    g_demo_window_missing += diff - 1u;
    g_demo_window_rx_ok++;
    g_demo_stat.seq_gap += diff - 1u;
    g_demo_stat.data_ok++;
    if(demo_hid_stats_enabled()) {
        g_demo_hid_expected += diff;
        g_demo_hid_bad += diff - 1u;
        g_demo_hid_rx_ok++;
        g_demo_air_diag_rx_ok++;
        g_demo_air_diag_seq_gap += diff - 1u;
    }
    if(g_demo_have_data_seq) demo_note_hid_silent_cycles(demo_tmr0_elapsed_cycles(g_demo_last_data_tmr, rx_tmr));
    g_demo_have_data_seq = 1u;
    g_demo_last_data_seq = air[RFH_HDR1_OFFSET];
    g_demo_last_data_tmr = rx_tmr;
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
    dst[6] = 0u;
    dst[7] = 0u;
    dst[8] = 0u;
    /* Private FIFO data, never a legacy wire frame. Hardware CRC already
     * passed; no consumer uses a synthesized software CRC. */
    dst[RF_INPUT_CRC_OFFSET] = 0u;
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

static void demo_queue_trace(uint32_t magic, uint8_t seq, uint32_t a, uint32_t b)
{
    uint8_t next = (uint8_t)((g_trace_hid_head + 1u) % 16u);
    uint8_t *p;
    if(!g_monitor_hid_enabled) return;
    if(next == g_trace_hid_tail) { ++g_trace_hid_drops; return; }
    p = g_trace_hid[g_trace_hid_head];
    memset(p, 0, 32u);
    rfh_put_u32(p, magic); p[4] = 3u; p[5] = seq;
    rfh_put_u32(p + 8, a); rfh_put_u32(p + 12, b);
    rfh_put_u32(p + 16, g_trace_hid_drops);
    if(magic == 0x33434852u) {
        const volatile rfh_trace_sync_t *w = &g_trace_sync_wait[seq & 31u];
        if(w->valid && w->sent && w->seq == seq) {
            rfh_put_u32(p, 0x34434852u); p[4]=4u;
            rfh_put_u32(p+20, w->wait_us);
        }
    }
    p[31] = demo_input_crc8(p, 31u);
    g_trace_hid_head = next;
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

__HIGH_CODE
static void demo_queue_input_payload(const uint8_t *payload, uint32_t rx_tmr, uint32_t process_tmr)
{
    uint32_t irq_status;

    if(payload == 0)
    {
        return;
    }

    SYS_DisableAllIrq(&irq_status);
    {
        /* Remember the last accepted state across FIFO/prepared/in-flight USB.
         * Repeating it is a keepalive, not another report ahead of the next edge.
         * Include the measurement tag, so a new capture epoch can establish itself. */
        if(g_demo_last_queued_valid && g_demo_last_queued_generation==g_demo_radio_generation &&
           g_demo_last_queued_keys[0]==payload[2] &&
           g_demo_last_queued_keys[1]==payload[3] &&
           g_demo_last_queued_keys[2]==payload[4] &&
           g_demo_last_queued_keys[3]==payload[5]) {
            g_demo_last_input_tmr=rx_tmr;g_demo_have_valid_input=1u;g_demo_input_stale=0u;
            SYS_RecoverIrq(irq_status);return;
        }
        uint8_t previous = (uint8_t)((g_demo_input_head + 31u) % 32u);
        uint8_t next = (uint8_t)((g_demo_input_head + 1u) % 32u);
        uint8_t stored = previous;
        if(g_demo_input_head != g_demo_input_tail &&
           g_demo_input_fifo[previous][2] == payload[2] &&
           g_demo_input_fifo[previous][3] == payload[3] &&
           g_demo_input_fifo[previous][4] == payload[4] &&
           g_demo_input_fifo[previous][5] == payload[5]) {
            g_demo_last_input_tmr=rx_tmr;g_demo_have_valid_input=1u;g_demo_input_stale=0u;
            SYS_RecoverIrq(irq_status);return;
        } else if(next != g_demo_input_tail) {
            stored = g_demo_input_head;
            demo_copy_bytes(g_demo_input_fifo[g_demo_input_head], payload, RF_INPUT_PAYLOAD_LEN);
            g_demo_input_head = next;
        } else {
            /* Keep the oldest edges and final state; overflow remains observable. */
            demo_copy_bytes(g_demo_input_fifo[previous], payload, RF_INPUT_PAYLOAD_LEN);
            g_demo_edge_drop++;
        }
        g_demo_input_generation[stored] = g_demo_radio_generation;
        g_demo_input_rx_tmr[stored] = rx_tmr;
        g_demo_input_process_tmr[stored] = process_tmr;
        demo_copy_bytes(g_demo_last_queued_keys,payload+2,4u);
        g_demo_last_queued_valid=1u;
        g_demo_last_queued_generation=g_demo_radio_generation;
    }
    g_demo_last_input_tmr = rx_tmr;
    g_demo_have_valid_input = 1u;
    g_demo_input_stale = 0u;
    SYS_RecoverIrq(irq_status);
}

static void demo_queue_neutral_xinput_report(uint8_t force)
{
    uint8_t report[XINPUT_ENDPOINT_SIZE];
    uint32_t irq_status;

    if((force == 0u) && (g_demo_input_stale != 0u))
    {
        return;
    }

    memset(report, 0, sizeof(report));
    report[0] = 0x00u;
    report[1] = XINPUT_ENDPOINT_SIZE;

    SYS_DisableAllIrq(&irq_status);
    g_demo_input_epoch++;
    g_demo_input_head = g_demo_input_tail = 0u;
    g_demo_last_queued_valid=0u;
    g_demo_hid_input_key_mask = 0u;
    g_demo_hid_input_window_mask = 0u;
    g_demo_hid_input_valid = 0u;
    g_demo_hid_latency_pending = 0u;
    g_demo_xinput_latency_pending = 0u;
    g_demo_xinput_latency_report_tmr = 0u;
    memcpy(g_demo_xinput_report, report, sizeof(report));
    g_demo_xinput_pending = 1u;
    g_demo_neutral_pending = 1u;
    g_demo_input_stale = 1u;
    SYS_RecoverIrq(irq_status);
}

static void demo_service_input_stale(void)
{
    uint32_t last_input_tmr;
    uint8_t have_input;
    uint8_t stale;
    uint32_t irq_status;

    SYS_DisableAllIrq(&irq_status);
    last_input_tmr = g_demo_last_input_tmr;
    have_input = g_demo_have_valid_input;
    stale = g_demo_input_stale;
    SYS_RecoverIrq(irq_status);

    if((have_input != 0u) && (stale == 0u) &&
       (demo_tmr0_elapsed_cycles(last_input_tmr,
                                 TMR0_GetCurrentTimer()) >=
        demo_us_to_tmr_cycles(INPUT_STALE_TIMEOUT_US)))
    {
        demo_queue_neutral_xinput_report(0u);
    }
}




/* Pure mapping: safe to preempt; publication happens only after revalidation. */
static uint8_t demo_build_xinput_report(const uint8_t *payload, uint8_t *report)
{
    uint8_t version;
    uint32_t key_mask;
    if(!payload || !report) return 0u;
    version = (uint8_t)((payload[1] & RF_INPUT_FORMAT_VERSION_MASK) >> RF_INPUT_FORMAT_VERSION_SHIFT);
    if((version != RF_INPUT_FORMAT_VERSION_V1 && version != RF_INPUT_FORMAT_VERSION_V2) ||
       !(payload[1] & RF_INPUT_FLAG_PROCESSED)) return 0u;
    key_mask = demo_input_key_mask(payload) & RF_INPUT_KEY_MASK_VALID;
    memset(report, 0, XINPUT_ENDPOINT_SIZE);
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

    return 1u;
}

static void demo_capture_xinput_metadata(const uint8_t *payload)
{
    uint8_t version = (uint8_t)((payload[1] & RF_INPUT_FORMAT_VERSION_MASK) >> RF_INPUT_FORMAT_VERSION_SHIFT);
    uint32_t key_mask = demo_input_key_mask(payload) & RF_INPUT_KEY_MASK_VALID;
    uint32_t previous_key_mask;
    if(demo_hid_stats_enabled() != 0u)
    {
        previous_key_mask = g_demo_hid_input_key_mask;
        g_demo_hid_input_key_mask = key_mask;
        g_demo_hid_input_window_mask |= key_mask;
        g_demo_hid_input_seq = payload[0];
        g_demo_hid_input_flags = payload[1];
        if((payload[1] & RFMON_INPUT_FLAG_BATTERY_CODE) != 0u)
        {
            g_demo_hid_input_battery_code = payload[RFMON_INPUT_BATTERY_CODE_OFFSET];
            g_demo_hid_input_battery_flags =
                (uint8_t)(payload[1] & (RFMON_INPUT_FLAG_BATTERY_CODE |
                                        RFMON_INPUT_FLAG_BATTERY_H2));
        }
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

}


__HIGH_CODE
static void demo_process_pending_input_payload(void)
{
    uint8_t payload[RF_INPUT_PAYLOAD_LEN], report[XINPUT_ENDPOINT_SIZE];
    uint8_t tail, valid;
    uint32_t irq_status, capture_start, generation, epoch, rx_tmr, process_tmr;
    SYS_DisableAllIrq(&irq_status);
    if(g_demo_neutral_pending || g_demo_xinput_pending ||
       g_demo_input_head == g_demo_input_tail) {
        SYS_RecoverIrq(irq_status);
        return;
    }
    capture_start = SysTick->CNT;
    tail = g_demo_input_tail;
    generation = g_demo_input_generation[tail];
    if(generation != g_demo_radio_generation) {
        g_demo_input_tail = (uint8_t)((tail + 1u) % 32u);
        SYS_RecoverIrq(irq_status);
        return;
    }
    epoch = g_demo_input_epoch;
    demo_copy_bytes(payload, g_demo_input_fifo[tail], sizeof(payload));
    rx_tmr = g_demo_input_rx_tmr[tail];
    process_tmr = g_demo_input_process_tmr[tail];
    demo_note_max_cycles(&g_demo_input_capture_max_cycles, capture_start);
    SYS_RecoverIrq(irq_status);

    valid = demo_build_xinput_report(payload, report);

    SYS_DisableAllIrq(&irq_status);
    capture_start = SysTick->CNT;
    /* Neutral input wins even if it reset the FIFO to this same tail index. */
    if(epoch != g_demo_input_epoch || tail != g_demo_input_tail ||
       g_demo_neutral_pending || g_demo_xinput_pending) {
        SYS_RecoverIrq(irq_status);
        return;
    }
    g_demo_input_tail = (uint8_t)((tail + 1u) % 32u);
    if(valid && generation == g_demo_radio_generation) {
        g_relative_prepared=0;
        if(g_short_measure) {
            uint8_t tag=payload[4]>>2;relative_rx_t *r=&g_relative_rx[tag];
            if(tag && r->tag==tag && !(r->flags&4u)) {
                g_relative_prepared=tag;
                /* Repeated reports can queue while the first IN is in flight.
                 * Keep the first report-ready boundary for this event. */
                if(!(r->flags&16u)) {r->ready=TMR0_GetCurrentTimer();r->flags|=16u;}
            }
        }
        demo_capture_xinput_metadata(payload);
        demo_queue_xinput_latency_pending(payload, rx_tmr, process_tmr);
        demo_copy_bytes(g_demo_xinput_report, report, sizeof(report));
        g_demo_xinput_pending = 1u;
        if(g_demo_xinput_latency_pending && !g_demo_xinput_latency_report_tmr)
            g_demo_xinput_latency_report_tmr = TMR0_GetCurrentTimer();
    }
    demo_note_max_cycles(&g_demo_input_capture_max_cycles, capture_start);
    SYS_RecoverIrq(irq_status);
}

__HIGH_CODE
static void demo_service_xinput_report(void)
{
    uint8_t report[XINPUT_ENDPOINT_SIZE];
    uint32_t irq_status;
    uint32_t submit_tmr;
    uint32_t submit_done_tmr;
    uint8_t was_neutral;

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
    demo_copy_bytes(report, g_demo_xinput_report, sizeof(report));
    was_neutral = g_demo_neutral_pending;
    g_demo_xinput_pending = 0u;
    SYS_RecoverIrq(irq_status);

    SYS_DisableAllIrq(&irq_status);
    submit_tmr = TMR0_GetCurrentTimer();
    g_relative_inflight=was_neutral ? 0u : g_relative_prepared;
    if(g_relative_inflight) {
        relative_rx_t *r=&g_relative_rx[g_relative_inflight];
        g_relative_inflight_row=r->row;r->submit=submit_tmr;
    }
    if(USBHS_Endp_DataUp(DEF_UEP2,
                         report,
                         XINPUT_ENDPOINT_SIZE,
                         DEF_UEP_CPY_LOAD) == 0u)
    {
        SYS_RecoverIrq(irq_status);
        submit_done_tmr = TMR0_GetCurrentTimer();
        if(was_neutral != 0u)
        {
            SYS_DisableAllIrq(&irq_status);
            g_demo_neutral_pending = 0u;
            SYS_RecoverIrq(irq_status);
        }
        demo_complete_xinput_latency_if_pending(submit_tmr, submit_done_tmr);
    }
    else
    {
        g_relative_inflight=0;SYS_RecoverIrq(irq_status);
        /* Endpoint submission can fail after the busy check.  Preserve this
         * exact report (especially a neutral release) until a successful
         * USB transaction, unless a newer neutral report is already queued. */
        SYS_DisableAllIrq(&irq_status);
        if(g_demo_xinput_pending == 0u)
        {
            demo_copy_bytes(g_demo_xinput_report, report, sizeof(report));
            g_demo_xinput_pending = 1u;
            if(was_neutral != 0u)
            {
                g_demo_neutral_pending = 1u;
            }
        }
        SYS_RecoverIrq(irq_status);
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
        uint8_t accepted;
        uint8_t old = data[RFH_HOP_CONFIRM_OLD_CHANNEL];
        if(!monitor_channel_valid(target) || !monitor_channel_valid(old) || rx_channel != old) return;
        accepted = rfh_hop_prepare(&g_demo_hop_transaction, seq, old, target,
                                  RF_LinkClockNow(), MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_DUAL_TIMEOUT_MS));
        if(!accepted) return;
        if(accepted == 2u) {
            demo_prepare_command_ack(RFH_CMD_HOP_PREPARE, seq);
            if(!g_demo_hop_confirmed) g_demo_after_ack_action = 1u;
            return;
        }
        g_demo_hop_confirmed = 0u;
        g_demo_confirm_ack_keep_count = 0u;
        g_demo_old_channel = old;
        g_demo_target_channel = target;
        g_demo_hop_seq = seq;
        g_demo_dual_side = 0u;
        demo_prepare_command_ack(RFH_CMD_HOP_PREPARE, seq);
        g_demo_after_ack_action = 1u;
        g_demo_stat.hop_event++;
        g_demo_hop_start_clock = RF_LinkClockNow();
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
        if(!g_demo_hop_confirmed &&
           (int32_t)(RF_LinkClockNow() - g_demo_hop_transaction.deadline) >= 0) return;
        if(!rfh_hop_confirm(&g_demo_hop_transaction, seq, target, rx_channel))
        {
            if(demo_hid_stats_enabled() != 0u)
            {
                g_demo_hid_errors++;
                g_demo_hid_type_errors++;
                g_demo_air_diag_type_errors++;
            }
            return;
        }
        if(g_demo_hop_confirmed) {
            demo_prepare_command_ack(RFH_CMD_HOP_CONFIRM, seq);
            return;
        }
        g_demo_target_channel = target;
        g_demo_hop_seq = seq;
        if(g_demo_current_channel != target)
        {
            demo_set_channel(target);
        }
        g_demo_hop_confirmed = 1u;
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
        if(data[RFH_TIME_SYNC_ECHO_SEQ] == g_monitor_sync_seq) g_monitor_sync_pending_retries = 0u;
        demo_queue_trace(0x33434852u, data[RFH_TIME_SYNC_ECHO_SEQ],
                                     rfh_get_u32(&data[RFH_TIME_SYNC_ECHO_RX_TICK]),
                                     rfh_get_u32(&data[RFH_TIME_SYNC_ECHO_TX_TICK]));
    }
    else if(cmd == RFH_CMD_LATENCY_INPUT)
    {
        if((rfh_get_u32(&data[RFH_LATENCY_KEY_MASK]) & 0x80000000u) != 0u) {
            demo_queue_trace(0x33454852u, data[RFH_LATENCY_INPUT_SEQ],
                             rfh_get_u32(&data[RFH_LATENCY_KEY_MASK]),
                             rfh_get_u32(&data[RFH_LATENCY_SAMPLE_TICK]));
            return;
        }
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
    else if(cmd == RFH_CMD_TX_DIAGNOSTIC)
    {
        if(data[1]) {
            g_demo_peer_window_ms = (uint16_t)data[1] * 10u;
            g_demo_peer_due = rfh_get_u16(&data[2]);
            g_demo_peer_started = rfh_get_u16(&data[4]);
            g_demo_peer_dropped = rfh_get_u16(&data[6]);
            g_demo_peer_diag_clock = RF_LinkClockNow();
            g_demo_peer_diag_valid = 1u;
        }
    }
    else if(cmd == RFH_CMD_BATTERY_STATUS)
    {
        uint8_t status = data[RFH_CMD_SLOT_ARG0];
        uint8_t code = (uint8_t)(status & RFMON_INPUT_BATTERY_SHORT_CODE_MASK);

        if(code != 0u)
        {
            g_demo_hid_input_battery_code = code;
            g_demo_hid_input_battery_flags = RFMON_INPUT_FLAG_BATTERY_CODE;
            if((status & RFMON_INPUT_BATTERY_SHORT_H2_MASK) != 0u)
            {
                g_demo_hid_input_battery_flags =
                    (uint8_t)(g_demo_hid_input_battery_flags | RFMON_INPUT_FLAG_BATTERY_H2);
            }
        }
    }
}

static void demo_accept_aux(const uint8_t *fragment)
{
    uint32_t now=RF_LinkClockNow(),generation=g_demo_radio_generation;
    if((uint32_t)(now-g_aux_rx_clock)>MS1_TO_SYSTEM_TIME(500u))memset(&g_aux_rx,0,sizeof(g_aux_rx));
    g_aux_rx_clock=now;
    if(rfh_aux_receive(&g_aux_rx,fragment) && generation==g_demo_radio_generation) {
        uint8_t air[RFH_AIR_PACKET_LEN]={0};
        uint8_t type=g_aux_rx.data[0],len=g_aux_rx.data[1];
        if(type==RFH_AUX_TRACE && len==54u)short_rx_trace(g_aux_rx.data+6);
        if(type==RFH_AUX_STATS && len==32u) {
            for(unsigned i=0;i<6;i++)g_short_tx_stats[i]=rfh_get_u32(g_aux_rx.data+14+4*i);
            g_short_stats_pending=1;len=8;
        }
        if(type>=RFH_AUX_BATTERY && type<=RFH_AUX_RATE && type!=RFH_AUX_TRACE && len<=10u) {
            memcpy(air+2,g_aux_rx.data+6,len);demo_handle_command(air,g_demo_current_channel);
        }
    }
}

static void demo_after_ack_finish(void)
{
    uint32_t now = RF_LinkClockNow();

    if(g_demo_ack_completion_cmd == RFH_CMD_CONNECT_REQ)
    {
        /* CONNECT ACK completion must not mark the DATA link recovered.
         * SYN only opens the ACK window; FINAL enters COMM; DATA sets
         * link_active. Keeping those edges separate avoids a 100ms false
         * Link Lost while TX is still sending FINAL packets.
         */
    }
    else if(g_demo_ack_completion_action == 1u)
    {
        g_demo_rx_state = RF_AUTO_RX_PREPARED_DUAL;
        g_demo_dual_switch_clock = now;
        g_demo_dual_deadline_clock = g_demo_hop_transaction.deadline;
        g_demo_dual_side = 0u;
    }
    else if(g_demo_ack_completion_action == 2u)
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

    g_demo_ack_completion_action = 0u;
    g_demo_ack_completion_cmd = RFH_CMD_NONE;
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
    uint32_t irq_status;
    if(TMR1_GetITFlag(TMR0_3_IT_CYC_END) == 0u)
    {
        return;
    }

    TMR1_ClearITFlag(TMR0_3_IT_CYC_END);
    TMR1_ITCfg(DISABLE, TMR0_3_IT_CYC_END);
    SYS_DisableAllIrq(&irq_status);
    if(g_demo_ack_pending != 0u)
    {
        if(demo_tmr0_elapsed_cycles(g_demo_ack_due_tmr, TMR0_GetCurrentTimer()) > demo_us_to_tmr_cycles(100u)) {
            g_demo_ack_late++;
            demo_cancel_ack();
            SYS_RecoverIrq(irq_status);
            return;
        }
        demo_send_ack();
    }
    SYS_RecoverIrq(irq_status);
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
    uint32_t now = RF_LinkClockNow();

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
       (data[RFH_CONNECT_VERSION] != RFH_PROTOCOL_VERSION) ||
       ((connect_stage != RFH_CONNECT_STAGE_SYN) &&
        (connect_stage != RFH_CONNECT_STAGE_FINAL)) ||
       (rate_code > RFH_RATE_8K) ||
       (monitor_channel_valid(channel_a) == 0u) ||
       (monitor_channel_valid(channel_b) == 0u) ||
       (channel_a == channel_b))
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

    if((connect_stage == RFH_CONNECT_STAGE_SYN) &&
       (g_demo_pair_candidate_pending != 0u))
    {
        if((g_demo_bond_store.has_pending != 0u) &&
           (pending->access_address ==
            g_demo_bond_store.pending.link_access_address))
        {
            if(demo_commit_prepared_bond() == 0u)
            {
                return 0u;
            }
        }
        else if((g_demo_bond_store.has_active != 0u) &&
                (pending->access_address ==
                 g_demo_bond_store.active.link_access_address))
        {
            /* The peer is still using the old bond, which proves PAIR_DONE was
             * not durably observed there.  Roll back the candidate. */
            if(demo_abort_prepared_bond() == 0u)
            {
                return 0u;
            }
        }
        else
        {
            return 0u;
        }
        g_demo_pair_deadline_clock = 0u;
        g_demo_pair_confirm_deadline_clock = 0u;
        g_demo_pair_tx_active = 0u;
        g_demo_pair_after_tx_action = 0u;
        (void)demo_apply_access_address(g_demo_link_access_address);
    }

    g_relative_wire=((uint16_t)data[RFH_CONNECT_ACK_WINDOW_MS]<<8)|air[1];
    g_relative_air_seq=air[1];g_relative_wire_valid=1;g_relative_air_clock=now;
    if(connect_stage==RFH_CONNECT_STAGE_SYN) {
        memset(g_relative_rx,0,sizeof(g_relative_rx));memset(&g_aux_rx,0,sizeof(g_aux_rx));
        g_relative_tag=g_relative_prepared=g_relative_inflight=0;g_relative_session++;
    }
    demo_apply_rate_code(rate_code);

    if(connect_stage == RFH_CONNECT_STAGE_FINAL)
    {
        uint8_t entering_comm =
            (g_demo_rx_state == RF_AUTO_RX_CONNECT_ACK_PENDING) ? 1u : 0u;
        uint8_t duplicate_final =
            ((g_demo_rx_state == RF_AUTO_RX_COMM) &&
             (g_demo_link_active == 0u) &&
             (pending->channel == g_demo_current_channel)) ? 1u : 0u;

        if((entering_comm == 0u) && (duplicate_final == 0u))
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
        if(entering_comm)
            g_demo_first_data_deadline_clock =
                now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_FIRST_DATA_TIMEOUT_MS);
        g_demo_ack_pending = 0u;
        demo_ack_timer_cancel();
        if(entering_comm != 0u)
        {
            demo_reset_quality_window();
            /* Reapply RX-owned runtime configuration after either peer restarts. */
            monitor_mark_remote_pending(g_monitor_seq, RFMON_TARGET_ALL,
                                        monitor_current_flags(), g_monitor_hid_period_ms);
        }
        demo_prepare_command_ack(RFH_CMD_CONNECT_REQ, RFH_ACK_STATUS_FINAL_READY);
        demo_schedule_ack(0u);
        return 0u;
    }

    if(connect_stage != RFH_CONNECT_STAGE_SYN)
    {
        return 0u;
    }

    if(g_demo_rx_state != RF_AUTO_RX_CONNECT_ACK_PENDING) {
        /* A peer restart can send SYN before our DATA timeout expires. */
        if(g_demo_link_active) g_demo_link_seek_clock = now;
        demo_cancel_ack();
        g_demo_radio_generation++;
        g_demo_air_sequence.valid = 0u;
        g_demo_hop_transaction.valid = 0u;
        g_demo_hop_confirmed = 0u;
    }
    g_demo_rx_state = RF_AUTO_RX_CONNECT_ACK_PENDING;
    g_demo_link_active = 0u;
    g_demo_old_channel = pending->channel;
    g_demo_target_channel = pending->channel;
    g_demo_last_data_tmr = pending->rx_tmr;
    g_demo_have_data_seq = 0u;
    g_demo_connect_stage = RFH_CONNECT_STAGE_SYN;
    g_demo_first_data_deadline_clock = 0u;
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

    if(pending == 0 || pending->generation != g_demo_radio_generation) return 0u;

    process_tmr = TMR0_GetCurrentTimer();
    if((pending->len != RF_AUTO_DEMO_PACKET_LEN) &&
       (pending->len != (uint8_t)(RFH_DATA_OFFSET + RFMON_INPUT_PAYLOAD_V1_LEN)) &&
       (!rfh_is_short(pending->len)))
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
    if((rfh_is_short(pending->len)) &&
       ((flags & RFH_FLAG_CMD_PRESENT) != 0u))
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
    data_tmr = pending->rx_tmr;
    if((flags & RFH_FLAG_CMD_PRESENT) == 0u)
    {
        if(rfh_is_short(pending->len))
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
            demo_queue_input_payload(input_payload, data_tmr, process_tmr);
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
            demo_queue_input_payload(input_payload, data_tmr, process_tmr);
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

    (void)request_ack;

    return input_queued;
}

static void demo_service_xinput_fast_path(void)
{
    demo_process_pending_input_payload();
    demo_service_xinput_report();
}

/* Pure decode/CRC is preemptible; only the final generation check and FIFO
 * commit are atomic. A SYN/channel change during decode cannot inject old input. */
__HIGH_CODE
static uint8_t demo_process_short_input(const rf_rx_pending_t *pending)
{
    uint8_t payload[RF_INPUT_PAYLOAD_LEN];
    uint32_t irq_status, commit_start;
    uint32_t process_tmr = TMR0_GetCurrentTimer();
    if(pending->generation != g_demo_radio_generation) return 0u;
    short_rx_edge(pending,process_tmr);
    if(pending->len==RFH_AUX_LEN && !(rfh_flags(pending->air[0])&RFH_FLAG_CMD_PRESENT)) demo_accept_aux(pending->air+5);
    if(!demo_decode_short_input_payload(payload, pending->air[RFH_HDR1_OFFSET],
                                       &pending->air[RFH_DATA_OFFSET])) return 0u;
    SYS_DisableAllIrq(&irq_status);
    commit_start = SysTick->CNT;
    if(pending->generation != g_demo_radio_generation ||
       g_demo_rx_state == RF_AUTO_RX_UNCONNECTED ||
       g_demo_rx_state == RF_AUTO_RX_CONNECT_ACK_PENDING) {
        SYS_RecoverIrq(irq_status);
        return 0u;
    }
    demo_queue_input_payload(payload, pending->rx_tmr, process_tmr);
    g_demo_short_decoded++;
    demo_note_max_cycles(&g_demo_input_commit_max_cycles, commit_start);
    SYS_RecoverIrq(irq_status);
    return 1u;
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
        if(rfh_is_short(pending.len) &&
           rfh_packet_type(pending.air[RFH_HDR0_OFFSET]) == RFH_PKT_DATA) {
            if(demo_process_short_input(&pending)) input_seen = 1u;
        } else if(rfh_packet_type(pending.air[RFH_HDR0_OFFSET]) == RFH_PKT_DATA) {
            /* Infrequent commands retain atomic transaction handling. */
            uint32_t irq_status;
            SYS_DisableAllIrq(&irq_status);
            if(demo_process_rx_pending_packet(&pending)) input_seen = 1u;
            SYS_RecoverIrq(irq_status);
        } else if(demo_process_rx_pending_packet(&pending)) input_seen = 1u;
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

static uint8_t demo_fast_rx_packet(const uint8_t *buf, uint32_t rx_tmr)
{
    const uint8_t *air = &buf[2];
    uint8_t type, flags, token, remaining;
    uint32_t delay;
    if(buf[1] != RFH_AIR_PACKET_LEN && !rfh_is_short(buf[1])) return 0u;
    type = rfh_packet_type(air[0]);
    if(type == RFH_PKT_CONNECT && !g_demo_pair_candidate_pending && !demo_pair_is_active()) {
        rf_rx_pending_t pending;
        memset(&pending, 0, sizeof(pending));
        pending.len = buf[1]; pending.channel = g_demo_current_channel;
        pending.rx_tmr = rx_tmr; pending.access_address = gRxParam.accessAddress;
        memcpy(pending.air, air, buf[1]);
        (void)demo_process_connect_packet(&pending);
        return 1u;
    }
    if(type != RFH_PKT_DATA) return 0u;
    if(g_demo_rx_state == RF_AUTO_RX_UNCONNECTED || g_demo_rx_state == RF_AUTO_RX_CONNECT_ACK_PENDING || demo_pair_is_active()) return 1u;
    flags = rfh_flags(air[0]);
    if(buf[1]==RFH_SHORT_LEN && (flags & RFH_FLAG_CMD_PRESENT)) return 1u;
    demo_apply_rate_code(rfh_rate_code(air[0]));
    if(!demo_note_air_packet(air, rx_tmr)) return 1u;
    if(buf[1]==RFH_AUX_LEN && (flags&RFH_FLAG_CMD_PRESENT)) {
        uint16_t counter=rfh_get_u16(air+5);
        if((uint8_t)counter!=air[1])return 1u;
        g_relative_wire=counter;g_relative_wire_valid=1;
    }
    if(rfh_is_short(buf[1])) {
        if(!g_demo_link_active) {
            g_demo_last_connect_ms = demo_clock_delta_ms(g_demo_link_seek_clock, RF_LinkClockNow());
            g_demo_connect_count++;
        }
        g_demo_link_active = 1u;
        g_demo_first_data_deadline_clock = 0u;
        if(g_demo_rx_state == RF_AUTO_RX_RECOVERY_SCAN) g_demo_rx_state = RF_AUTO_RX_COMM;
        /* PREPARED_DUAL keeps the immutable old/target transaction. */
        if(!(flags & RFH_FLAG_CMD_ACK)) return 0u;
    }
    if(!(flags & RFH_FLAG_CMD_ACK)) return 0u;
    token = rfh_is_short(buf[1]) ? air[1] : air[RF_AUTO_DEMO_ACK_TOKEN_OFFSET];
    remaining = rfh_is_short(buf[1]) ? rfh_ack_guard_slots(g_demo_report_hz) : air[RF_AUTO_DEMO_ACK_REMAIN_OFFSET];
    if(remaining > 2u) return 1u;
    if(g_demo_have_ack_token && token == g_demo_last_ack_token) { g_demo_ack_duplicate++; return rfh_is_short(buf[1]) ? 0u : 1u; }
    if(g_demo_ack_pending || g_demo_ack_tx_active) return rfh_is_short(buf[1]) ? 0u : 1u;
    g_demo_have_ack_token = 1u; g_demo_last_ack_token = token;
    g_demo_stat.ack_req++;
    if(!rfh_is_short(buf[1]) && (flags & RFH_FLAG_CMD_PRESENT)) demo_handle_command(air, g_demo_current_channel);
    demo_fill_ack_packet();
    delay = g_demo_ack_delay_tmr + (uint32_t)remaining * g_demo_slot_tmr;
    g_demo_ack_due_tmr = (rx_tmr + delay) % TMR0_FREE_RUN_WRAP;
    delay = rfh_ack_delay(rx_tmr, TMR0_GetCurrentTimer(), TMR0_FREE_RUN_WRAP, delay);
    if(!delay) { g_demo_ack_late++; demo_cancel_ack(); return rfh_is_short(buf[1]) ? 0u : 1u; }
    g_demo_ack_snapshot_ready = 1u;
    g_demo_ack_pending = 1u;
    demo_ack_timer_arm(delay);
    return rfh_is_short(buf[1]) ? 0u : 1u;
}

__HIGH_CODE
void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id)
{
    (void)id;
    if(g_demo_radio_reconfiguring) return;
    if(!g_demo_rx_active) sta &= ~(RF_STATE_RX | RF_STATE_RX_CRCERR);

    if(sta & RF_STATE_RX)
    {
        uint32_t callback_start = SysTick->CNT;
        uint8_t completed_slot = g_demo_rx_active_slot;
        uint8_t *rx_buf;
        uint32_t rx_tmr = TMR0_GetCurrentTimer();
        int8_t rssi = RFIP_ReadRssi();

        g_demo_rx_active = 0u;
        if(completed_slot >= RF_RX_DMA_SLOT_COUNT)
        {
            completed_slot = 0u;
        }
        rx_buf = RxBuf[completed_slot];
        demo_arm_rx();
        demo_note_max_cycles(&g_demo_rx_rearm_max_cycles, callback_start);
        /* RSSI must be read before rearming, but its summary can wait until
         * the receiver is listening again. */
        demo_note_rssi(rssi);
        if(!demo_fast_rx_packet(rx_buf, rx_tmr)) demo_queue_rx_pending_packet(rx_buf, rx_tmr);
        demo_note_max_cycles(&g_demo_rx_callback_max_cycles, callback_start);
    }
    if(sta & RF_STATE_RX_CRCERR)
    {
        uint32_t rx_tmr = TMR0_GetCurrentTimer();
        int8_t rssi = RFIP_ReadRssi();

        g_demo_rx_active = 0u;
        demo_arm_rx();
        demo_note_rssi(rssi);
        demo_queue_rx_pending_crcerr(rx_tmr);
    }
    if(sta & RF_STATE_TX_FINISH)
    {
        if(g_demo_pair_tx_active != 0u)
        {
            demo_after_pair_tx_finish();
            return;
        }
        if(!g_demo_ack_tx_active) return;
        g_demo_ack_tx_active = 0u;
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
        g_demo_ack_tx_active = 0u;
        g_demo_stat.ack_fail++; g_demo_ack_fail_total++;
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

static uint8_t demo_housekeeping_due(uint32_t now)
{
    if(g_demo_housekeeping_valid && now == g_demo_housekeeping_clock) return 0u;
    g_demo_housekeeping_clock = now;
    g_demo_housekeeping_valid = 1u;
    return 1u;
}

void RF_Service(void)
{
    /* The lease is renewed over USB only; a closed/crashed monitor cannot
     * leave extra instrumentation enabled indefinitely. */
    if(g_short_measure && (uint32_t)(SysTick->CNT-g_short_capture_refresh)>GetSysClock()*3u) {
        g_short_measure=0;g_relative_inflight=g_relative_prepared=0;
        memset(g_relative_rx,0,sizeof(g_relative_rx));memset(&g_aux_rx,0,sizeof(g_aux_rx));
        monitor_mark_remote_pending(++g_monitor_seq,RFMON_TARGET_ALL,monitor_current_flags(),g_monitor_hid_period_ms);
    }
    uint32_t now = RF_LinkClockNow();
    uint32_t data_silent_cycles = 0u;
    uint8_t enter_unconnected = 0u;

    if(!g_demo_rf_ready) return;
    if(g_demo_usb_ready_ms == 0xFFFFu && USBHS_DevEnumStatus)
        g_demo_usb_ready_ms = demo_ticks_to_ms(RF_LinkClockNow());
    demo_service_xinput_fast_path();
    demo_process_pending_rx_packets();
    demo_service_input_stale();
    /* Input/USB servicing above remains every iteration. Only the masked,
     * tick-based radio housekeeping is coalesced to one pass per 625 us. */
    if(!demo_housekeeping_due(RF_LinkClockNow())) return;
    uint32_t irq_status;
    SYS_DisableAllIrq(&irq_status);
    now = RF_LinkClockNow(); /* Do not compare a pre-ISR time with a new ACK start. */
    if(g_demo_ack_tx_active && (uint32_t)(now - g_demo_ack_tx_clock) >= MS1_TO_SYSTEM_TIME(10u)) {
        g_demo_ack_watchdog++;
        demo_cancel_ack();
        (void)RFRole_Stop();
        g_demo_rx_active = 0u;
        g_demo_rearm_pending = 1u;
    }
    demo_score_windows_service(now);

    if(demo_pair_is_active() != 0u)
    {
        SYS_RecoverIrq(irq_status);
        demo_service_pairing(now);
        return;
    }

    if((g_demo_rx_state == RF_AUTO_RX_COMM) &&
       (g_demo_link_active == 0u) &&
       (g_demo_first_data_deadline_clock != 0u) &&
       ((int32_t)(now - g_demo_first_data_deadline_clock) >= 0))
    {
        g_demo_first_data_deadline_clock = 0u;
        demo_enter_rx_unconnected(now);
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
        demo_enter_rx_recovery_scan(now);
    }

    if(g_demo_rearm_pending != 0u)
    {
        g_demo_rearm_pending = 0u;
        demo_arm_rx();
    }

    demo_service_connect_handshake(now);
    demo_service_unconnected_scan(now);

    if(g_demo_rx_state == RF_AUTO_RX_PREPARED_DUAL)
    {
        if((int32_t)(now - g_demo_dual_deadline_clock) >= 0)
        {
            demo_enter_rx_unconnected(now);
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
    else if(g_demo_rx_state == RF_AUTO_RX_RECOVERY_SCAN && !g_demo_link_active &&
            (int32_t)(now - g_demo_recovery_scan_deadline_clock) >= 0) {
        demo_enter_rx_unconnected(now);
    }
    SYS_RecoverIrq(irq_status);
    demo_service_xinput_fast_path();
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
    uint32_t now = RF_LinkClockNow();

    if(g_demo_config_ret != SUCCESS)
    {
        return 0u;
    }
    if(demo_pair_is_active() != 0u)
    {
        return 1u;
    }

    demo_queue_neutral_xinput_report(1u);
    g_demo_link_active = 0u;
    g_demo_pair_tx_active = 0u;
    g_demo_pair_after_tx_action = 0u;
    g_demo_pair_scan_side = 0u;
    g_demo_pair_scan_clock = now;
    g_demo_pair_deadline_clock =
        now + MS1_TO_SYSTEM_TIME(RFH_PAIR_WINDOW_MS);
    g_demo_pair_confirm_deadline_clock = 0u;
    g_demo_pair_session = 0u;
    g_demo_pair_tx_id_hash = 0u;
    g_demo_pair_rx_id_hash = g_demo_local_id_hash;
    g_demo_pair_link_access_address = 0u;
    g_demo_pair_done_confirm32 = 0u;
    g_demo_pair_done_repeat_left = 0u;
    g_demo_pair_done_retry_clock = 0u;
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
        demo_abort_pairing(RF_LinkClockNow());
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
    uint32_t now = RF_LinkClockNow();
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
        if(g_demo_has_bond != 0u)
        {
            return 4u;
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

static uint8_t demo_try_send_rssi_report(void)
{
    uint8_t report[HID_ENDPOINT_SIZE];
    uint32_t irq_status;
    int32_t rssi_sum;
    uint32_t rssi_count;
    int8_t rssi_avg;
    int8_t rssi_min;
    int8_t rssi_max;
    int8_t rssi_last;

    SYS_DisableAllIrq(&irq_status);
    rssi_sum = g_demo_rssi_sum;
    rssi_count = g_demo_rssi_count;
    rssi_min = g_demo_rssi_min;
    rssi_max = g_demo_rssi_max;
    rssi_last = g_demo_rssi_last;
    SYS_RecoverIrq(irq_status);

    if(rssi_count == 0u)
    {
        rssi_avg = rssi_last;
        rssi_min = rssi_last;
        rssi_max = rssi_last;
    }
    else
    {
        rssi_avg = (int8_t)(rssi_sum / (int32_t)rssi_count);
    }

    memset(report, 0, sizeof(report));
    demo_put_u32(&report[0], RX_HID_RSSI_MAGIC);
    demo_put_u32(&report[4], ++g_demo_hid_rssi_seq);
    demo_put_u16(&report[8],
                 (rssi_count > 0xFFFFu) ? 0xFFFFu : (uint16_t)rssi_count);
    report[10] = (uint8_t)rssi_avg;
    report[11] = (uint8_t)rssi_min;
    report[12] = (uint8_t)rssi_max;
    report[13] = (uint8_t)rssi_last;
    report[14] = demo_hid_state_code();
    report[15] = g_demo_current_channel;
    report[16] = g_demo_old_channel;
    report[17] = g_demo_target_channel;
    demo_put_u16(&report[18], g_demo_report_hz);
    report[20] = g_demo_rate_code;
    report[21] = g_demo_link_active;

    if(demo_submit_hid_report(report) == 0u)
    {
        return 0u;
    }

    SYS_DisableAllIrq(&irq_status);
    if(g_demo_rssi_count >= rssi_count)
    {
        g_demo_rssi_sum -= rssi_sum;
        g_demo_rssi_count -= rssi_count;
    }
    else
    {
        g_demo_rssi_sum = 0;
        g_demo_rssi_count = 0u;
    }
    if(g_demo_rssi_count == 0u)
    {
        g_demo_rssi_min = 127;
        g_demo_rssi_max = -127;
    }
    SYS_RecoverIrq(irq_status);
    return 1u;
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
    uint32_t diag_now = RF_LinkClockNow();
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
    uint8_t input_battery_code = g_demo_hid_input_battery_code;
    uint8_t input_battery_flags = g_demo_hid_input_battery_flags;

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
    report[13] = (uint8_t)(input_flags | input_battery_flags);
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
    report[31] = ((input_battery_flags & RFMON_INPUT_FLAG_BATTERY_CODE) != 0u) ?
                 input_battery_code :
                 (uint8_t)(diag_seq_gap > 0xFFu ? 0xFFu : diag_seq_gap);

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

/* RHD1: versioned diagnostics, alternating timing and cumulative counters.
 * It never changes the meaning/layout of existing RHM1/RHI1 frames. */
static uint8_t demo_try_send_diagnostic(void)
{
    uint8_t report[HID_ENDPOINT_SIZE] = {0};
    uint32_t now = RF_LinkClockNow(), irq_status;
    if((uint32_t)(now - g_demo_diag_clock) < MS1_TO_SYSTEM_TIME(500u)) return 0u;
    demo_put_u32(report, 0x31444852UL);
    demo_put_u32(&report[4], g_demo_diag_seq + 1u);
    report[8] = 1u; report[9] = g_demo_diag_page;
    report[10] = demo_hid_state_code(); report[11] = g_demo_current_channel;
    SYS_DisableAllIrq(&irq_status);
    if(g_demo_diag_page == 0u) {
        demo_put_u16(&report[12], demo_ticks_to_ms(g_demo_rf_ready_clock));
        demo_put_u16(&report[14], g_demo_usb_ready_ms);
        demo_put_u16(&report[16], g_demo_last_connect_ms);
        demo_put_u16(&report[18], g_demo_hid_hop_finish_duration_ms);
        demo_put_u32(&report[20], g_demo_connect_count);
        demo_put_u32(&report[24], g_demo_ack_watchdog);
        report[28] = g_demo_rx_pending_max_water;
        report[29] = demo_rx_pending_water(g_demo_rx_pending_head, g_demo_rx_pending_tail);
        demo_put_u16(&report[30], 0x1913u); /* HS negotiation and edge-only USB queue. */
    } else if(g_demo_diag_page == 1u) {
        demo_put_u32(&report[12], g_demo_ack_late);
        demo_put_u32(&report[16], g_demo_ack_duplicate);
        demo_put_u32(&report[20], g_demo_rx_pending_drop);
        demo_put_u32(&report[24], g_demo_edge_drop);
        demo_put_u32(&report[28], g_demo_total_crc);
    } else if(g_demo_diag_page == 2u) {
        demo_put_u16(&report[12], g_demo_peer_window_ms);
        demo_put_u16(&report[14], g_demo_peer_due);
        demo_put_u16(&report[16], g_demo_peer_started);
        demo_put_u16(&report[18], g_demo_peer_dropped);
        demo_put_u16(&report[20], demo_clock_delta_ms(g_demo_peer_diag_clock, now));
        demo_put_u32(&report[22], g_demo_air_total_missing);
        demo_put_u32(&report[26], g_demo_air_total_received);
        report[30] = g_demo_rx_state;
        report[31] = g_demo_peer_diag_valid;
    } else {
        demo_put_u32(&report[12], g_demo_rx_arm_fail_total);
        demo_put_u16(&report[16], demo_tmr_cycles_to_us_saturated(g_demo_rx_rearm_max_cycles));
        demo_put_u16(&report[18], demo_tmr_cycles_to_us_saturated(g_demo_rx_callback_max_cycles));
        demo_put_u16(&report[20], demo_tmr_cycles_to_us_saturated(g_demo_input_commit_max_cycles));
        demo_put_u16(&report[22], demo_tmr_cycles_to_us_saturated(g_demo_input_capture_max_cycles));
        demo_put_u32(&report[24], g_demo_ack_fail_total);
        demo_put_u32(&report[28], g_demo_short_decoded);
    }
    SYS_RecoverIrq(irq_status);
    if(!demo_submit_hid_report(report)) return 0u;
    g_demo_diag_clock = now; g_demo_diag_seq++; g_demo_diag_page = (g_demo_diag_page + 1u) % 4u;
    return 1u;
}

/* Trace events must not share the 100 ms statistics pacing. Input service runs
 * first; this independent diagnostic endpoint only submits when it is free. */
uint8_t RF_TrySendTraceReport(void)
{
    if(g_monitor_hid_enabled && g_short_stats_pending) {
        uint8_t report[32]={0};rfh_put_u32(report,0x32504852u);rfh_put_u32(report+4,g_short_stats_seq);
        for(unsigned i=0;i<6;i++)rfh_put_u32(report+8+4*i,g_short_tx_stats[i]);
        if(demo_submit_hid_report(report)){g_short_stats_pending=0;g_short_stats_seq++;return 1;}
    }
    if(g_monitor_hid_enabled && g_short_measure && short_send_trace())return 1u;
    if(g_monitor_hid_enabled == 0u) {
        g_trace_hid_tail = g_trace_hid_head;
        return 0u;
    }
    if(g_trace_hid_head != g_trace_hid_tail) {
        if(demo_submit_hid_report(g_trace_hid[g_trace_hid_tail])) {
            g_trace_hid_tail = (uint8_t)((g_trace_hid_tail + 1u) % 16u);
            return 1u;
        }
    }
    return 0u;
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

    if(demo_try_send_diagnostic() != 0u) return 1u;

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

    g_demo_hid_rssi_div++;
    if(g_demo_hid_rssi_div >= 4u)
    {
        g_demo_hid_rssi_div = 0u;
        if((g_demo_hid_hop_start_pending == 0u) &&
           (g_demo_hid_hop_finish_pending == 0u) &&
           (demo_try_send_rssi_report() != 0u))
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

    g_demo_sys_clock = GetSysClock();
    g_demo_cycles_per_us = g_demo_sys_clock / 1000000u;
    g_demo_rf_ready_clock = RF_LinkClockNow();
    g_demo_link_seek_clock = g_demo_rf_ready_clock;
    taskID = TMOS_ProcessEventRegister(RF_ProcessEvent);
    PFIC_EnableIRQ(BLEB_IRQn);
    PFIC_EnableIRQ(BLEL_IRQn);

    TMR0_TimerInit(TMR0_FREE_RUN_WRAP - 1u);
    g_demo_hid_last_clock = RF_LinkClockNow();
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
    g_demo_air_diag_last_clock = RF_LinkClockNow();
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
    g_demo_dual_switch_clock = RF_LinkClockNow();

    g_demo_have_valid_input = 0u;
    g_demo_input_stale = 0u;
    g_demo_neutral_pending = 0u;
    g_demo_last_input_tmr = TMR0_GetCurrentTimer();
    demo_queue_neutral_xinput_report(1u);

    g_demo_rf_ready_clock = RF_LinkClockNow();
    g_demo_link_seek_clock = g_demo_rf_ready_clock;
    g_demo_rf_ready = 1u;
    if(g_demo_has_bond != 0u)
    {
        demo_arm_rx();
    }
}

static void short_rx_edge(const rf_rx_pending_t *p,uint32_t process) {
    if(!g_short_measure)return;
    uint8_t tag=p->air[4]>>2;
    if(tag==g_relative_tag)return;
    uint32_t previous=g_relative_rx[g_relative_tag].mask;
    g_relative_tag=tag;if(!tag)return;
    relative_rx_t *r=&g_relative_rx[tag];memset(r,0,sizeof(*r));r->previous=previous;
    r->tag=tag;r->row=++g_relative_row;r->wire=p->measure_seq;r->len=p->len;
    r->mask=(uint32_t)p->air[2]|((uint32_t)p->air[3]<<8)|((uint32_t)(p->air[4]&3u)<<16);
    r->rx=p->rx_tmr;r->process=process;r->born=RF_LinkClockNow();
    if(!p->measure_valid)r->flags|=8u;
    short_dirty(r);
}
static void short_rx_trace(const uint8_t *p) {
    uint16_t event=rfh_get_u16(p);uint8_t tag=event&63u;
    if(!g_short_measure || !tag || p[5]>6u)return;
    relative_rx_t *r=&g_relative_rx[tag];
    uint32_t mask=(uint32_t)p[2]|((uint32_t)p[3]<<8)|((uint32_t)p[4]<<16);
    if(r->tag!=tag || r->mask!=mask || (uint32_t)(RF_LinkClockNow()-r->born)>MS1_TO_SYSTEM_TIME(1000u))return;
    /* Receiving a candidate is distinct from proving that its source stages
     * belong to this event. Publish no source durations until an exact match. */
    r->flags|=32u;short_dirty(r);
    /* Only the RF attempt that supplied this first received edge may match. */
    for(unsigned i=0;i<p[5];i++) {
        const uint8_t *a=p+22+5*i;
        if(rfh_get_u16(a)!=r->wire)continue;
        r->event=event;
        for(unsigned j=0;j<4;j++)r->source[j]=rfh_get_u32(p+6+4*j);
        r->tx=(uint32_t)a[2]|((uint32_t)a[3]<<8)|((uint32_t)a[4]<<16);
        r->flags|=1u;if(r->tx<=1000000u && !p[52] && !(r->flags&8u))r->flags|=2u;
        short_dirty(r);return;
    }
}
__HIGH_CODE
void RF_RelativeUsbComplete(uint32_t tick) {
    uint8_t tag=g_relative_inflight;g_relative_inflight=0;
    if(!tag)return;
    relative_rx_t *r=&g_relative_rx[tag];
    if(r->row!=g_relative_inflight_row || r->flags&12u)return;
    r->done=tick;r->flags|=4u;short_dirty(r);
}
void RF_RelativeUsbReset(void) {
    // A re-enumerated host needs a fresh current state even with no new edge.
    g_demo_last_queued_valid=0u;
    if(g_relative_inflight) {relative_rx_t *r=&g_relative_rx[g_relative_inflight];r->flags|=8u;short_dirty(r);}
    g_relative_inflight=g_relative_prepared=0;
}
static uint8_t short_send_trace(void) {
    static uint8_t scan;
    for(unsigned i=0;i<63;i++) {
        scan=(scan%63u)+1u;relative_rx_t *r=&g_relative_rx[scan];
        if(r->tag && !(r->flags&12u) && (uint32_t)(RF_LinkClockNow()-r->born)>MS1_TO_SYSTEM_TIME(1000u)) {
            r->flags|=8u;short_dirty(r);
        }
        if(!r->dirty)continue;
        uint8_t page,revision;
        uint8_t report[32]={0};uint32_t lock;
        SYS_DisableAllIrq(&lock);
        page=(r->dirty&1u)?0u:1u;revision=r->revision;
        rfh_put_u32(report,0x32544c52u); /* RLT2, local durations, no PC clock */
        rfh_put_u16(report+4,g_relative_session);rfh_put_u16(report+6,r->row);
        report[8]=(uint8_t)r->mask;report[9]=(uint8_t)(r->mask>>8);report[10]=(uint8_t)(r->mask>>16);
        report[11]=(revision<<1)|page;report[12]=r->flags;report[13]=(uint8_t)r->previous;report[14]=(uint8_t)(r->previous>>8);report[15]=(uint8_t)(r->previous>>16);
        if(page==0)for(unsigned j=0;j<4;j++)rfh_put_u32(report+16+4*j,r->source[j]);
        else {
            rfh_put_u32(report+16,r->tx);
            /* Launch-to-RX boundary model: packet airtime plus configured ramp.
             * ISR latency is not calibrated; desktop labels the total estimated. */
            rfh_put_u32(report+20,(r->len+11u)*((RF_AUTO_DEMO_PHY_PROPS==LLE_MODE_PHY_2M)?4u:8u)+24u+(RFH_INPUT_TX_SEND_TIME_UNITS+1u)/2u);
            if(r->flags&4u) {
                rfh_put_u32(report+24,demo_tmr0_elapsed_cycles(r->rx,r->ready)/g_demo_cycles_per_us);
                rfh_put_u32(report+28,demo_tmr0_elapsed_cycles(r->ready,r->done)/g_demo_cycles_per_us);
            }
        }
        SYS_RecoverIrq(lock);
        if(!demo_submit_hid_report(report))return 0;
        SYS_DisableAllIrq(&lock);if(revision==r->revision)r->dirty&=~(1u<<page);SYS_RecoverIrq(lock);
        return 1;
    }
    return 0;
}
