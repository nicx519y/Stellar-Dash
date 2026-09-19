#include "rf_short_transport.h"
/********************************** (C) COPYRIGHT *******************************
 * File Name          : RF_PHY.c
 * Description        : TX side for RF PHY DATA + 100ms ACK control protocol.
 *******************************************************************************/

#include "CONFIG.h"
#include "RF_PHY.h"
#include "HAL.h"
#include "wchrf.h"
#include "rfm_config.h"
#include "rfm_cold_boot.h"
#include "rfm_spi_bridge.h"
#include "rfm_spi_port_internal.h"
#include "rf_hop_protocol.h"
#include "rf_pairing_protocol.h"
#include "rf_hop_bond.h"
#include "rf_hop_bond_journal.h"
#include "rf_hop_score.h"
#include "rf_monitor_control.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "rf_link_policy.h"
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
#define RF_AUTO_DEMO_WAIT_ACK_ENABLE   0u
#define RF_AUTO_DEMO_ACK_BIT           (RF_AUTO_DEMO_WAIT_ACK_ENABLE ? PROP_WAIT_ACK : 0u)
#define RF_AUTO_DEMO_REPORT_HZ         RFM_COLD_BOOT_INITIAL_REPORT_HZ
#define RF_AUTO_DEMO_RATE_CODE         RFM_COLD_BOOT_INITIAL_RATE_CODE
#define RF_AUTO_DEMO_LOG_PERIOD_MS     5000u
#define RF_AUTO_DEMO_PENDING_MAX       4u
#define RF_AUTO_DEMO_TX_STUCK_MS       10u
#define RF_AUTO_DEMO_TX_IN_ISR         1u
#define RF_AUTO_DEMO_ACK_INTERVAL_MS   100u
#define RF_AUTO_DEMO_ACK_REQUEST_BURST 1u
#define RF_AUTO_DEMO_CONNECT_REQUEST_BURST 16u
#ifndef RF_AUTO_DEMO_ACK_RX_TIMEOUT_US
#define RF_AUTO_DEMO_ACK_RX_TIMEOUT_US 1200u
#endif
#ifndef RF_AUTO_DEMO_CONNECT_ACK_RX_TIMEOUT_US
#define RF_AUTO_DEMO_CONNECT_ACK_RX_TIMEOUT_US 5000u
#endif
#define RF_AUTO_DEMO_ACK_RX_TIMEOUT_UNITS (RF_AUTO_DEMO_ACK_RX_TIMEOUT_US * 2u)
#define RF_AUTO_DEMO_CONNECT_ACK_RX_TIMEOUT_UNITS (RF_AUTO_DEMO_CONNECT_ACK_RX_TIMEOUT_US * 2u)
#ifndef RF_AUTO_DEMO_TX_SEND_TIME_UNITS
#define RF_AUTO_DEMO_TX_SEND_TIME_UNITS RFH_INPUT_TX_SEND_TIME_UNITS
#endif
#define RF_AUTO_DEMO_ACK_TOKEN_OFFSET  10u
#define RF_AUTO_DEMO_ACK_REMAIN_OFFSET 11u
#define RF_AUTO_DEMO_DISCOVERY_CHANNEL_A RFH_DISCOVERY_CHANNEL_A
#define RF_AUTO_DEMO_DISCOVERY_CHANNEL_B RFH_DISCOVERY_CHANNEL_B
#define RF_AUTO_DEMO_INITIAL_CHANNEL   RF_AUTO_DEMO_DISCOVERY_CHANNEL_B
#define RF_AUTO_DEMO_DISCOVERY_DWELL_MS RFH_CONNECT_DWELL_MS

/*
 * 自动跳频条件可调项：
 * - 丢包率单位是 permille，50 = 5%，1000 = 100%。
 * - 分数越低越好，越高越差，评分达到阈值说明当前频道风险较高。
 * - 自动跳频只由 1 秒质量窗口触发，避免单次 ACK 抖动导致连续跳频。
 */
#ifndef RF_AUTO_DEMO_AUTO_HOP_ENABLE
#define RF_AUTO_DEMO_AUTO_HOP_ENABLE   1u    /* 1=启用自动跳频，0=只接受手动切频道 */
#endif
#define RF_AUTO_DEMO_HOP_ACK_MISS_THRESHOLD 8u /* ACK miss 质量窗口阈值基准；不再直接触发普通跳频 */
#define RF_AUTO_DEMO_LINK_ACK_MISS_LIMIT RFH_ACK_MISS_LIMIT_DEFAULT
#define RF_AUTO_DEMO_HOP_SCORE_THRESHOLD 180u /* ACK 超时/评分兜底跳频阈值 */
#define RF_AUTO_DEMO_HOP_IRQ_GOOD_US   800u  /* IRQ 延迟恢复到该值以下时清除异常窗口 */
#define RF_AUTO_DEMO_HOP_IRQ_WARN_US    1000u /* IRQ 评分开始升高的延迟 */
#define RF_AUTO_DEMO_HOP_IRQ_BAD_US     2500u /* IRQ 评分达到满坏分的延迟 */
#define RF_AUTO_DEMO_HOP_SCORE_IMPROVE_MIN 150u /* 候选频道至少低这么多分才优先切换 */
#define RF_AUTO_DEMO_HOP_COOLDOWN_MS   30000u /* 一次自动跳频完成后的普通冷却时间 */
#define RF_AUTO_DEMO_HOP_NO_TARGET_COOLDOWN_MS 5000u /* 无可信目标时的短冷却 */
#define RF_AUTO_DEMO_CHANNEL_COOLDOWN_MS 10000u /* 频道被打满坏分后，暂不选回的时间 */
#define RF_AUTO_DEMO_CHANNEL_QUARANTINE_MS 60000u /* 跳频/试用失败后的目标频道隔离时间 */
#define RF_AUTO_DEMO_HOP_STABLE_LOSS_MAX_PERMILLE 50u /* 稳定保护：10 秒内丢包率一直低于 5% 则不跳频 */
#define RF_AUTO_DEMO_HOP_STABLE_IRQ_AVG_US 1200u /* 稳定保护：10 秒内按键/IRQ 平均延迟低于 1.2ms 则不跳频 */
#define RF_AUTO_DEMO_RANK_PROMOTE_ENABLE 0u /* 稳定优先：默认关闭排行榜主动升档跳频 */
#define RF_AUTO_DEMO_RANK_PROMOTE_MS   RF_AUTO_DEMO_HOP_COOLDOWN_MS /* 停留在排行榜后半区超过该时间则尝试上移 */
#define RF_AUTO_DEMO_FRONT_HALF_COUNT  ((RFH_HOP_CHANNEL_COUNT + 1u) / 2u) /* 排行榜前半区频道数量 */
#define RF_AUTO_DEMO_HOP_PREPARE_TIMEOUT_MS 100u /* HOP_PREPARE 等 ACK 的超时 */
#define RF_AUTO_DEMO_HOP_CONFIRM_TIMEOUT_MS 100u /* HOP_CONFIRM 等 ACK 的超时，覆盖多次 ACK 机会 */
#define RF_AUTO_DEMO_HOP_RECOVERY_TIMEOUT_MS 200u /* 跳频握手失败后的双频道恢复总时长 */
#define RF_AUTO_DEMO_HOP_RECOVERY_DWELL_MS 10u /* 恢复期在旧/新频道之间切换的停留时间 */
#define RF_AUTO_DEMO_HOP_CMD_RETRY_MS  10u /* 跳频控制包重试节流，间隔内继续发送输入 */
#define RF_AUTO_DEMO_HOP_PROBATION_MS  3000u /* 自动跳频成功后的试用期 */
#define RF_AUTO_DEMO_QUALITY_WINDOW_MS 1000u /* 自动跳频质量判断窗口 */
#define RF_AUTO_DEMO_BAD_AVG_LOSS_PERMILLE 80u
#define RF_AUTO_DEMO_BAD_MAX_LOSS_PERMILLE 150u
#define RF_AUTO_DEMO_BAD_AVG_IRQ_US    2000u
#define RF_AUTO_DEMO_BAD_ACK_TIMEOUTS  2u
#define RF_AUTO_DEMO_BAD_WINDOWS_TO_HOP 3u
#define RF_AUTO_DEMO_GOOD_MAX_LOSS_PERMILLE 30u
#define RF_AUTO_DEMO_GOOD_AVG_IRQ_US   1000u
#define RF_AUTO_DEMO_GOOD_WINDOWS_TO_NORMAL 2u
#define RF_AUTO_DEMO_SCORE_EMA_OLD_WEIGHT 3u
#define RF_AUTO_DEMO_SCORE_EMA_NEW_WEIGHT 1u
#define RF_AUTO_DEMO_PAIR_RX_TIMEOUT_US 30000u
#define RF_AUTO_DEMO_PAIR_RX_TIMEOUT_UNITS (RF_AUTO_DEMO_PAIR_RX_TIMEOUT_US * 2u)
/*
 * 频道评分可调项：
 * 分数越低越好，越高越差，最终限制在 0..1000。
 * 指标值按 0..1000 归一化后，以“指标值 * WEIGHT / 100”累加到 SCORE_BASE。
 * 调大某项 WEIGHT 会放大该指标对坏分的影响。
 */
#define RF_AUTO_DEMO_SCORE_BASE        0u    /* 无异常时坏分为 0 */
#define RF_AUTO_DEMO_SCORE_LOSS_WEIGHT 200u  /* 丢包/坏包率权重 */
#define RF_AUTO_DEMO_SCORE_CRC_WEIGHT  100u  /* CRC 错误权重 */
#define RF_AUTO_DEMO_SCORE_TYPE_WEIGHT 100u  /* 包类型/格式错误权重 */
#define RF_AUTO_DEMO_SCORE_TIMEOUT_WEIGHT 40u /* ACK/链路超时权重 */
#define RF_AUTO_DEMO_SCORE_IRQ_WEIGHT  100u  /* RX IRQ 延迟权重 */
#define RF_AUTO_DEMO_SCORE_WINDOW_MS   10000u /* 活动频道评分时间窗口：10 秒内所有事件样本求平均后更新一次分数 */
#define RF_AUTO_DEMO_CHANNEL_SCORE_INIT RF_AUTO_DEMO_SCORE_BASE /* 初始频道坏分 */
#define RF_AUTO_DEMO_CHANNEL_SCORE_GOOD RF_AUTO_DEMO_SCORE_BASE /* 明确好样本坏分 */
#define RF_AUTO_DEMO_CHANNEL_SCORE_UNKNOWN 600u /* TX 未测量/未同步频道分 */
#define RF_AUTO_DEMO_CHANNEL_SCORE_BAD 1000u /* 饱和坏分 */
#define RF_LINK_CRC_INIT               0x555555UL
#ifndef RF_TX_FORCE_INPUT_PAYLOAD_TEST
#define RF_TX_FORCE_INPUT_PAYLOAD_TEST 0u
#endif
#define RF_TX_DIRECT_INPUT_TEST_KEY_MASK 0x00000010UL

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

typedef enum
{
    RF_AUTO_TX_UNCONNECTED = 0u,
    RF_AUTO_TX_PROVISIONAL,
    RF_AUTO_TX_COMM
} rf_auto_tx_link_state_t;

typedef enum
{
    RF_AUTO_LINK_QUALITY_NORMAL = 0u,
    RF_AUTO_LINK_QUALITY_DEGRADED,
    RF_AUTO_LINK_QUALITY_PROBATION
} rf_auto_link_quality_state_t;

typedef enum
{
    RF_AUTO_CONNECT_SYN_TX = 0u,
    RF_AUTO_CONNECT_SYN_ACK_RX,
    RF_AUTO_CONNECT_FINAL_TX
} rf_auto_connect_phase_t;

typedef enum
{
    RF_AUTO_PAIR_IDLE = 0u,
    RF_AUTO_PAIR_OFFERING,
    RF_AUTO_PAIR_CONFIRM_WAIT
} rf_auto_pair_state_t;

typedef struct
{
    uint32_t window_start_clock;
    uint32_t sample_score_sum;
    uint32_t sample_count;
    uint16_t max_loss_permille;
    uint32_t irq_sum_us;
    uint32_t quality_sample_count;
    uint8_t active;
    uint8_t last_window_stable;
} rf_score_window_t;

typedef struct
{
    uint32_t window_start_clock;
    uint32_t loss_sum_permille;
    uint32_t irq_sum_us;
    uint16_t max_loss_permille;
    uint8_t sample_count;
    uint8_t ack_timeout_count;
    uint8_t bad_window_count;
    uint8_t good_window_count;
    uint8_t active;
} rf_link_quality_window_t;

uint8_t taskID;

static rfRoleParam_t gParm;
static rfipTx_t gTxParam;
static rfipRx_t gRxParam;
__attribute__((__aligned__(4))) static uint8_t TxBuf[RF_AUTO_DEMO_DMA_LEN];
/* TxBuf is a CPU packet template. Timer-paced DATA uses alternating DMA
 * storage so assembling the next packet never rewrites the previous one. */
__attribute__((__aligned__(4))) static uint8_t TxDmaBuf[2][16];
static uint8_t g_demo_tx_dma_slot;
static uint32_t g_demo_tx_launch_cycles, g_demo_tx_guard_cycles, g_demo_tx_wait_limit;
static uint32_t g_demo_tx_control_guard_cycles;
static uint32_t g_demo_short_guard_cycles, g_demo_aux_guard_cycles;
static uint8_t g_demo_tx_launch_valid;
static uint32_t g_demo_housekeeping_clock;
static uint8_t g_demo_housekeeping_valid;
static uint32_t g_demo_diag_due, g_demo_diag_started, g_demo_diag_dropped;
static uint32_t g_demo_diag_clock, g_demo_diag_last_due, g_demo_diag_last_started, g_demo_diag_last_dropped;
static uint16_t g_demo_diag_due_window, g_demo_diag_started_window, g_demo_diag_dropped_window;
static uint8_t g_demo_diag_pending, g_demo_diag_window_10ms;

__HIGH_CODE
static uint32_t demo_tx_cycle_now(void) { return SysTick->CNT; }
__attribute__((__aligned__(4))) static uint8_t RxBuf[264];

static rf_auto_demo_stat_t g_demo_stat;
static volatile uint8_t g_demo_config_ret = 0xFFu;
static volatile uint8_t g_demo_tx_start_ret = 0xFFu;
static volatile uint8_t g_demo_tx_parm_ret = 0xFFu;
static volatile uint8_t g_demo_rx_ret = 0xFFu;
static volatile uint8_t g_demo_tx_busy = 0u;
static volatile uint8_t g_demo_ack_rx_active = 0u;
static volatile uint8_t g_demo_wait_ack_after_tx = 0u;
static volatile uint32_t g_demo_ack_rx_start_clock = 0u;
static volatile uint32_t g_demo_ack_rx_start_cycles;
static volatile uint8_t g_demo_pause_tx = 0u;
static volatile uint8_t g_demo_force_ack_burst = 0u;
static volatile uint8_t g_pending_event_state_code = 0u;
static volatile rf_auto_connect_phase_t g_demo_connect_phase = RF_AUTO_CONNECT_SYN_TX;
static volatile uint32_t g_demo_connect_phase_clock = 0u;
static volatile uint8_t g_demo_connect_packet_stage = RFH_CONNECT_STAGE_SYN;
#if (RF_AUTO_DEMO_TX_IN_ISR == 0u)
static volatile uint32_t g_demo_pending_reports = 0u;
#endif
extern void rfm_spi_port_measure_enable(uint8_t enable);
extern uint8_t rfm_spi_port_input_end(uint8_t tag,uint8_t seq,uint32_t* cycles);
extern uint8_t rfm_spi_port_measure_diag(void);
typedef struct { uint8_t tag, spi; volatile uint8_t count; uint8_t source, sent, end_valid; uint16_t event, seq[6]; uint32_t mask, launch[6], end, born, stage[4]; } relative_tx_t;
static relative_tx_t g_relative_tx[64];
static uint8_t g_relative_tag, g_relative_scan;
static uint16_t g_short_wire_seq;
static uint32_t g_demo_radio_generation;
static uint32_t g_relative_overflow;
static uint32_t g_short_anchor_serial;
static uint32_t g_short_packet_count[3], g_short_ack_slots, g_short_control_slots;
static void short_note_input(const uint8_t *payload);
static void short_note_launch(uint8_t tag);
static uint8_t short_prepare_trace(void);
static rfh_aux_tx_t g_aux_tx;
static uint8_t g_short_aux_sent, g_short_ack_wait, g_short_measure;
static uint32_t g_short_ack_launch;
static uint8_t g_demo_seq = 0u;
static uint8_t g_demo_ack_burst_left = 0u;
static uint8_t g_demo_ack_token = 0u;
static uint8_t g_demo_active_ack_token = 0u;
static uint16_t g_demo_report_hz = RF_AUTO_DEMO_REPORT_HZ;
static uint8_t g_demo_rate_code = RF_AUTO_DEMO_RATE_CODE;
static uint8_t g_demo_input_off = RFM_COLD_BOOT_INITIAL_INPUT_OFF;
static uint8_t g_demo_rate_update_pending = 0u;
static uint8_t g_demo_rate_update_seq = 0u;
static uint8_t g_demo_ack_clock_armed = 0u;
static uint32_t g_demo_next_ack_clock = 0u;
static uint8_t g_demo_has_bond = 0u;
static uint8_t g_demo_bond_channel_a = RF_AUTO_DEMO_DISCOVERY_CHANNEL_A;
static uint8_t g_demo_bond_channel_b = RF_AUTO_DEMO_DISCOVERY_CHANNEL_B;
static uint32_t g_demo_local_id_hash = 0u;
static uint32_t g_demo_link_access_address = RFH_LINK_ACCESS_ADDRESS_DEFAULT;
static rfh_bond_record_t g_demo_bond;
static rfh_bond_journal_state_t g_demo_bond_store;
static uint8_t g_demo_pair_commit_pending = 0u;
static volatile uint8_t g_demo_current_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
static uint8_t g_demo_old_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
static uint8_t g_demo_target_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
static uint8_t g_demo_hop_seq = 0u;
static rf_auto_hop_state_t g_demo_hop_state = RF_AUTO_HOP_COMM;
static volatile rf_auto_tx_link_state_t g_demo_link_state = RF_AUTO_TX_UNCONNECTED;
static uint8_t g_demo_reconnecting = 0u;
static rf_auto_link_quality_state_t g_demo_link_quality_state = RF_AUTO_LINK_QUALITY_NORMAL;
static rf_link_quality_window_t g_demo_link_quality_window;
static uint32_t g_demo_channel_enter_clock = 0u;
static uint32_t g_demo_discovery_switch_clock = 0u;
static uint8_t g_demo_discovery_side = 0u;
static uint16_t g_demo_last_quality = 0u;
static uint16_t g_demo_last_avg_irq_us = 0u;
static uint16_t g_demo_last_max_irq_us = 0u;
static uint16_t g_demo_hop_reason_score = 0u;
static uint8_t g_demo_ack_miss_count = 0u;
static uint8_t g_demo_irq_bad_window_count = 0u;
static uint32_t g_demo_hop_deadline_clock = 0u;
static uint32_t g_demo_hop_recovery_deadline;
static uint8_t g_demo_hop_recovery_started;
static uint32_t g_demo_hop_cooldown_until = 0u;
static uint32_t g_demo_hop_cmd_retry_clock = 0u;
static uint32_t g_demo_probation_deadline_clock = 0u;
static uint8_t g_demo_probation_old_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
static uint8_t g_demo_hop_manual = 0u;
static uint32_t g_demo_recovery_switch_clock = 0u;
static uint8_t g_demo_recovery_side = 0u;
static volatile rf_auto_pair_state_t g_demo_pair_state = RF_AUTO_PAIR_IDLE;
static volatile uint8_t g_demo_pair_wait_rx_after_tx = 0u;
static volatile uint8_t g_demo_pair_done_pending = 0u;
static uint16_t g_demo_pair_tx_ticks_remaining = 0u;
static uint32_t g_demo_pair_started_clock = 0u;
static uint32_t g_demo_pair_deadline_clock = 0u;
static uint32_t g_demo_pair_confirm_deadline_clock = 0u;
static uint32_t g_demo_pair_session = 0u;
static uint32_t g_demo_pair_tx_id_hash = 0u;
static uint32_t g_demo_pair_rx_id_hash = 0u;
static uint32_t g_demo_pair_link_access_address = 0u;
static uint32_t g_demo_pair_done_confirm32 = 0u;
static volatile uint32_t g_demo_tx_start_clock = 0u;
static rfh_health_t g_demo_health;
static uint8_t g_demo_recovery_reason;
static uint8_t g_demo_emergency_windows;
static uint32_t g_demo_emergency_after;
static uint32_t g_demo_emergency_window;
static uint32_t g_demo_explore_after;
static uint8_t g_demo_explore_cursor;
static uint8_t g_demo_hop_rollback;
static uint16_t g_demo_probation_baseline;
static uint32_t g_demo_probation_score_sum;
static uint16_t g_demo_probation_samples;
static uint32_t g_demo_last_log_clock = 0u;
static uint8_t g_demo_last_payload[RFM_RF_INPUT_PAYLOAD_LEN] = {0};
static volatile uint8_t g_demo_have_payload = 0u;
static volatile uint32_t g_demo_tmr_epoch_cycles = 0u;
static uint32_t g_demo_report_tmr_cycles = 1u;
static uint32_t g_demo_last_payload_tmr = 0u;
static uint8_t g_demo_last_payload_tmr_valid = 0u;
typedef struct { uint8_t seq; uint32_t mask, stamp; } rf_trace_edge_t;
static rf_trace_edge_t g_trace_queue[16];
static uint8_t g_trace_head, g_trace_tail;
static uint8_t g_trace_last_valid;
static rf_trace_edge_t g_trace_last;
static volatile uint8_t g_monitor_latency_pending = 0u;
static uint8_t g_monitor_latency_input_seq = 0u;
static uint32_t g_monitor_latency_key_mask = 0u;
static uint32_t g_monitor_latency_sample_tick_us = 0u;
static volatile uint8_t g_monitor_auto_hop_enabled = 1u;
static volatile uint8_t g_monitor_manual_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
static volatile uint8_t g_monitor_manual_pending = 0u;
static uint8_t g_monitor_manual_seq = 0u;
static uint8_t g_monitor_manual_applied_seq = 0u;
static volatile uint8_t g_monitor_status_pending = 0u;
static uint8_t g_monitor_status_seq = 0u;
static uint8_t g_monitor_status_flags = 0u;
static uint8_t g_monitor_status_manual_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
static uint8_t g_monitor_status_result = RFMON_APPLY_IDLE;
static volatile uint8_t g_monitor_sync_echo_pending = 0u;
static uint8_t g_sync_air_count;
static uint8_t g_source_spi_received;
/* Reserved SPI status byte: capture enable/IRQ/boundary and trace progress.
 * No additional air diagnostics or timing requests are generated. */
uint8_t RF_GetSyncAirCount(void) { return g_sync_air_count | rfm_spi_port_measure_diag(); }
uint8_t RF_IsMeasurementEnabled(void) { return g_short_measure; }
uint8_t RF_GetSourceFrameCount(void) { return g_source_spi_received; }
static uint8_t g_monitor_sync_echo_seq = 0u;
static uint32_t g_monitor_sync_echo_rx_tick_us = 0u;
static uint32_t g_monitor_sync_echo_tx_tick_us = 0u;
static volatile uint8_t g_monitor_battery_pending = 0u;
static uint8_t g_monitor_battery_status = 0u;
static uint32_t g_monitor_battery_last_queue_clock = 0u;

static uint16_t g_demo_channel_scores[RFH_HOP_CHANNEL_COUNT];
static uint32_t g_demo_channel_measured_at[RFH_HOP_CHANNEL_COUNT];
static uint32_t g_demo_channel_cooldown_until[RFH_HOP_CHANNEL_COUNT];
static rf_score_window_t g_demo_score_windows[RFH_HOP_CHANNEL_COUNT];
static uint8_t g_demo_channel_tried_mask = 0u;
static uint8_t g_demo_channel_score_known_mask = 0u;

static void demo_link_quality_reset(uint32_t now,
                                    rf_auto_link_quality_state_t state);
static void demo_abort_radio(void);

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
    static const uint8_t tag[] = "HBOX-RF-HOP:TX";
    uint8_t mac[8] __attribute__((aligned(4))) = {0};
    uint32_t hash = 2166136261UL;

    (void)GetMACAddress(mac);
    hash = demo_hash_bytes(hash, tag, (uint8_t)(sizeof(tag) - 1u));
    hash = demo_hash_bytes(hash, mac, 6u);
    hash = rfh_fnv1a32_mix_u32(hash, chip_info);
    return (hash == 0u) ? 0x54584A31UL : hash;
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
    seed = rfh_fnv1a32_mix_u32(g_demo_local_id_hash, 0x54584E42UL);
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
#if (RFM_COLD_BOOT_WAIT_HOST_RATE == 0u)
    if(record->rate_code <= RFH_RATE_8K)
    {
        g_demo_rate_code = record->rate_code;
        g_demo_report_hz = rfh_rate_hz_from_code(record->rate_code);
    }
#endif
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
        /* A prepared TX bond means PAIR_DONE was received before reset.  Keep
         * reconnecting on the candidate address until the peer proves it. */
        g_demo_pair_commit_pending = 1u;
        g_demo_link_access_address = g_demo_bond_store.pending.link_access_address;
        g_demo_bond_channel_a = g_demo_bond_store.pending.channel_a;
        g_demo_bond_channel_b = g_demo_bond_store.pending.channel_b;
        g_demo_has_bond = 1u;
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
    g_demo_pair_commit_pending = 1u;
    g_demo_link_access_address = record.link_access_address;
    g_demo_bond_channel_a = record.channel_a;
    g_demo_bond_channel_b = record.channel_b;
    g_demo_has_bond = 1u;
    return 1u;
}

static uint8_t demo_commit_prepared_bond(void)
{
#if (RFH_TEST_FIXED_BOND_ENABLE != 0u)
    g_demo_pair_commit_pending = 0u;
    return 1u;
#else
    if(g_demo_pair_commit_pending == 0u)
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
    g_demo_pair_commit_pending = 0u;
    return 1u;
#endif
}

static uint8_t demo_abort_prepared_bond(void)
{
#if (RFH_TEST_FIXED_BOND_ENABLE != 0u)
    g_demo_pair_commit_pending = 0u;
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
    g_demo_pair_commit_pending = 0u;
    return 1u;
#endif
}

static uint8_t demo_clear_bond(void)
{
#if (RFH_TEST_FIXED_BOND_ENABLE == 0u)
    if(rfh_bond_journal_write_tombstone(&g_demo_bond_backend,
                                        &g_demo_bond_store,
                                        g_demo_local_id_hash) == 0u)
    {
        return 0u;
    }
#endif
    g_demo_pair_commit_pending = 0u;
    demo_select_unpaired_address();
    return 1u;
}

static uint8_t demo_apply_access_address(uint32_t access_address)
{
    if((access_address != RFH_PAIR_ACCESS_ADDRESS) &&
       (rfh_access_address_valid(access_address) == 0u))
    {
        return 0u;
    }

    demo_abort_radio();
    gParm.accessAddress = access_address;
    RFRole_SetParam(&gParm);
    gTxParam.accessAddress = access_address;
    gRxParam.accessAddress = access_address;
    g_demo_ack_rx_active = 0u;
    g_demo_wait_ack_after_tx = 0u;
    g_demo_tx_busy = 0u;
    g_demo_pause_tx = 0u;
    return 1u;
}

static uint8_t demo_pair_is_active(void)
{
    return (g_demo_pair_state != RF_AUTO_PAIR_IDLE) ? 1u : 0u;
}

static uint16_t demo_pair_ticks_for_ms(uint16_t ms)
{
    uint32_t hz = (g_demo_report_hz == 0u) ? RF_AUTO_DEMO_REPORT_HZ : g_demo_report_hz;
    uint32_t ticks = (hz * (uint32_t)ms + 999u) / 1000u;

    if(ticks == 0u)
    {
        ticks = 1u;
    }
    return (ticks > 0xFFFFu) ? 0xFFFFu : (uint16_t)ticks;
}

static uint32_t demo_make_pair_session(void)
{
    static const uint8_t tag[] = "HBOX_PAIR_SESSION";
    uint32_t hash = rfh_fnv1a32_bytes(tag, (uint32_t)(sizeof(tag) - 1u));

    hash = rfh_fnv1a32_mix_u32(hash, g_demo_local_id_hash);
    hash = rfh_fnv1a32_mix_u32(hash, RF_LinkClockNow());
    hash = rfh_fnv1a32_mix_u32(hash, TMR0_GetCurrentTimer());
    hash = rfh_fnv1a32_mix_u32(hash, g_demo_bond.pair_counter + 1u);
    return (hash == 0u) ? 1u : hash;
}

static uint32_t demo_make_pair_link_access_address(uint32_t rx_id_hash)
{
    static const uint8_t tag[] = "HBOX_LINK_AA_V1";
    uint32_t hash = rfh_fnv1a32_bytes(tag, (uint32_t)(sizeof(tag) - 1u));
    uint32_t aa;

    hash = rfh_fnv1a32_mix_u32(hash, g_demo_pair_session);
    hash = rfh_fnv1a32_mix_u32(hash, g_demo_pair_tx_id_hash);
    hash = rfh_fnv1a32_mix_u32(hash, rx_id_hash);
    hash = rfh_fnv1a32_mix_u32(hash, g_demo_bond.pair_counter + 1u);
    hash = rfh_fnv1a32_mix_u32(hash, RF_LinkClockNow());
    hash = rfh_fnv1a32_mix_u32(hash, TMR0_GetCurrentTimer());
    aa = rfh_access_address_from_seed(hash);
    return (rfh_access_address_valid(aa) != 0u) ? aa : 0u;
}

static uint8_t demo_rate_valid(uint16_t hz)
{
    return ((hz == 0u) ||
            (hz == 1000u) ||
            (hz == 2000u) ||
            (hz == 4000u) ||
            (hz == 8000u)) ? 1u : 0u;
}

static void demo_arm_next_ack_clock(uint32_t now)
{
    g_demo_next_ack_clock = now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_ACK_INTERVAL_MS);
    g_demo_ack_clock_armed = 1u;
}

static void demo_reconfigure_report_timer(uint16_t hz)
{
    uint32_t tick_per_evt;

    TMR0_ITCfg(DISABLE, TMR0_3_IT_CYC_END);
    if(hz == 0u)
    {
        return;
    }

    tick_per_evt = GetSysClock() / hz;
    if(tick_per_evt == 0u)
    {
        tick_per_evt = 1u;
    }
    g_demo_report_tmr_cycles = tick_per_evt;
    g_demo_tmr_epoch_cycles = 0u;
    g_demo_last_payload_tmr_valid = 0u;
    TMR0_TimerInit(tick_per_evt);
    TMR0_ClearITFlag(TMR0_3_IT_CYC_END);
    TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
}

static uint16_t tx_saturate_u16(uint32_t value)
{
    return (value > 0xFFFFu) ? 0xFFFFu : (uint16_t)value;
}

static uint8_t tx_latency_q8_encode(uint32_t us)
{
    uint32_t code;

    if(us == 0u)
    {
        return 0u;
    }
    if(us <= 512u)
    {
        code = (us + 2u) / 4u;
        return (code == 0u) ? 1u : (uint8_t)((code > 128u) ? 128u : code);
    }
    if(us <= 2048u)
    {
        code = 128u + ((us - 512u + 8u) / 16u);
        return (uint8_t)((code > 224u) ? 224u : code);
    }

    code = 224u + ((us - 2048u + 64u) / 128u);
    return (uint8_t)((code > 255u) ? 255u : code);
}

static uint32_t tx_cycles_to_us_saturated(uint32_t cycles)
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

static uint32_t tx_now_cycles(void)
{
    return g_demo_tmr_epoch_cycles + TMR0_GetCurrentTimer();
}

static uint8_t demo_input_payload_same_input(const uint8_t *a, const uint8_t *b)
{
    if((a == 0) || (b == 0))
    {
        return 0u;
    }
    return ((a[RFMON_INPUT_SEQ_OFFSET] == b[RFMON_INPUT_SEQ_OFFSET]) &&
            (a[RFMON_INPUT_KEY_MASK_OFFSET] == b[RFMON_INPUT_KEY_MASK_OFFSET]) &&
            (a[RFMON_INPUT_KEY_MASK_OFFSET + 1u] == b[RFMON_INPUT_KEY_MASK_OFFSET + 1u]) &&
            (a[RFMON_INPUT_KEY_MASK_OFFSET + 2u] == b[RFMON_INPUT_KEY_MASK_OFFSET + 2u])) ? 1u : 0u;
}

static void demo_store_last_payload(const uint8_t *payload, uint32_t now_cycles)
{
    if(payload == 0)
    {
        return;
    }
    /* Edge identity is captured at SPI acceptance. Re-consuming an older
     * snapshot here must not reset a newer edge's source/attempt record. */
    memcpy(g_demo_last_payload, payload, RFM_RF_INPUT_PAYLOAD_LEN);
    g_demo_last_payload_tmr = now_cycles;
    g_demo_last_payload_tmr_valid = 1u;
    g_demo_have_payload = 1u;
}

static void demo_note_battery_status(const uint8_t *payload)
{
    uint32_t now;
    uint8_t status;

    if(payload == 0)
    {
        return;
    }
    if(((payload[RFMON_INPUT_FLAGS_OFFSET] & RFMON_INPUT_FLAG_BATTERY_CODE) == 0u) ||
       ((payload[RFMON_INPUT_BATTERY_CODE_OFFSET] & RFMON_INPUT_BATTERY_SHORT_CODE_MASK) == 0u))
    {
        return;
    }

    status = (uint8_t)(payload[RFMON_INPUT_BATTERY_CODE_OFFSET] &
                       RFMON_INPUT_BATTERY_SHORT_CODE_MASK);
    if((payload[RFMON_INPUT_FLAGS_OFFSET] & RFMON_INPUT_FLAG_BATTERY_H2) != 0u)
    {
        status = (uint8_t)(status | RFMON_INPUT_BATTERY_SHORT_H2_MASK);
    }
    uint8_t changed = (g_monitor_battery_status != status);
    g_monitor_battery_status = status;
    now = RF_LinkClockNow();
    if((g_monitor_battery_pending == 0u) &&
       (changed || (g_monitor_battery_last_queue_clock == 0u) ||
        ((uint32_t)(now - g_monitor_battery_last_queue_clock) >= MS1_TO_SYSTEM_TIME(5000u))))
    {
        g_monitor_battery_pending = 1u;
        g_monitor_battery_last_queue_clock = now;
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

static uint16_t demo_score_timeout_sample(void)
{
    return demo_score_from_metrics(0u, 0u, 0u, 1000u, 0u);
}

static uint16_t demo_score_type_sample(void)
{
    return demo_score_from_metrics(0u, 0u, 1000u, 0u, 0u);
}

static uint16_t demo_score_loss_sample(uint16_t loss_permille)
{
    return demo_score_from_metrics(loss_permille, 0u, 0u, 0u, 0u);
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
    g_demo_score_windows[idx].max_loss_permille = 0u;
    g_demo_score_windows[idx].irq_sum_us = 0u;
    g_demo_score_windows[idx].quality_sample_count = 0u;
    g_demo_score_windows[idx].active = 1u;
    g_demo_score_windows[idx].last_window_stable = 0u;
}

static void demo_channel_score_apply_sample_by_index(uint8_t idx,
                                                     uint16_t sample,
                                                     uint32_t now)
{
    uint8_t known;

    if(idx >= RFH_HOP_CHANNEL_COUNT)
    {
        return;
    }

    g_demo_channel_measured_at[idx] = now;
    known = ((idx < 8u) &&
             ((g_demo_channel_score_known_mask & (uint8_t)(1u << idx)) != 0u)) ? 1u : 0u;
    if(known != 0u)
    {
        g_demo_channel_scores[idx] =
            rfh_score_ema(g_demo_channel_scores[idx],
                          sample,
                          RF_AUTO_DEMO_SCORE_EMA_OLD_WEIGHT,
                          RF_AUTO_DEMO_SCORE_EMA_NEW_WEIGHT);
    }
    else
    {
        g_demo_channel_scores[idx] = rfh_score_clamp(sample);
    }
    if(idx < 8u)
    {
        g_demo_channel_score_known_mask |= (uint8_t)(1u << idx);
    }
    if(g_demo_channel_scores[idx] >= RF_AUTO_DEMO_CHANNEL_SCORE_BAD)
    {
        uint32_t until = now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_CHANNEL_COOLDOWN_MS);
        if((int32_t)(until - g_demo_channel_cooldown_until[idx]) > 0)
            g_demo_channel_cooldown_until[idx] = until;
    }
}

static void demo_channel_score_force_by_index(uint8_t idx,
                                              uint16_t score,
                                              uint32_t now)
{
    if(idx >= RFH_HOP_CHANNEL_COUNT)
    {
        return;
    }

    g_demo_channel_measured_at[idx] = now;
    g_demo_channel_scores[idx] = rfh_score_clamp(score);
    if(idx < 8u)
    {
        g_demo_channel_score_known_mask |= (uint8_t)(1u << idx);
    }
    demo_score_window_reset_by_index(idx, now);
    if(g_demo_channel_scores[idx] >= RF_AUTO_DEMO_CHANNEL_SCORE_BAD)
    {
        uint32_t until = now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_CHANNEL_COOLDOWN_MS);
        if((int32_t)(until - g_demo_channel_cooldown_until[idx]) > 0)
            g_demo_channel_cooldown_until[idx] = until;
    }
}

static void demo_channel_score_force(uint8_t channel,
                                     uint16_t score,
                                     uint32_t now)
{
    uint8_t idx = demo_channel_index(channel);

    if(idx != 0xFFu)
    {
        demo_channel_score_force_by_index(idx, score, now);
    }
}

static void demo_channel_quarantine(uint8_t channel, uint32_t now)
{
    uint8_t idx = demo_channel_index(channel);

    if(idx == 0xFFu)
    {
        return;
    }

    demo_channel_score_force_by_index(idx, RF_AUTO_DEMO_CHANNEL_SCORE_BAD, now);
    g_demo_channel_cooldown_until[idx] =
        now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_CHANNEL_QUARANTINE_MS);
}



static void demo_score_window_flush_by_index(uint8_t idx, uint32_t now, uint8_t force)
{
    uint32_t elapsed;
    uint8_t stable = 0u;

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
                                                 sample,
                                                 now);
    }
    if(g_demo_score_windows[idx].quality_sample_count != 0u)
    {
        uint32_t avg_irq_us =
            (g_demo_score_windows[idx].irq_sum_us +
             (g_demo_score_windows[idx].quality_sample_count / 2u)) /
            g_demo_score_windows[idx].quality_sample_count;

        if((g_demo_score_windows[idx].max_loss_permille < RF_AUTO_DEMO_HOP_STABLE_LOSS_MAX_PERMILLE) &&
           (avg_irq_us < RF_AUTO_DEMO_HOP_STABLE_IRQ_AVG_US))
        {
            stable = 1u;
        }
    }
    demo_score_window_reset_by_index(idx, now);
    g_demo_score_windows[idx].last_window_stable = stable;
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

static void demo_channel_score_update(uint8_t channel, uint16_t sample, uint32_t now)
{
    uint8_t idx = demo_channel_index(channel);

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

static void demo_score_window_note_quality(uint8_t channel,
                                           uint16_t loss_permille,
                                           uint16_t irq_us,
                                           uint32_t now)
{
    uint8_t idx = demo_channel_index(channel);

    if((idx == 0xFFu) || (channel != g_demo_current_channel))
    {
        return;
    }

    demo_score_window_flush_by_index(idx, now, 0u);
    if(g_demo_score_windows[idx].active == 0u)
    {
        demo_score_window_reset_by_index(idx, now);
    }
    if(loss_permille > g_demo_score_windows[idx].max_loss_permille)
    {
        g_demo_score_windows[idx].max_loss_permille = loss_permille;
    }
    g_demo_score_windows[idx].irq_sum_us += irq_us;
    g_demo_score_windows[idx].quality_sample_count++;
}

static uint8_t demo_stable_window_blocks_hop(void)
{
    uint8_t idx = demo_channel_index(g_demo_current_channel);
    uint32_t now;
    uint32_t elapsed;

    if(idx == 0xFFu)
    {
        return 0u;
    }
    if((g_demo_score_windows[idx].quality_sample_count != 0u) &&
       (g_demo_score_windows[idx].max_loss_permille >= RF_AUTO_DEMO_HOP_STABLE_LOSS_MAX_PERMILLE))
    {
        return 0u;
    }
    if(g_demo_score_windows[idx].quality_sample_count != 0u)
    {
        uint32_t avg_irq_us =
            (g_demo_score_windows[idx].irq_sum_us +
             (g_demo_score_windows[idx].quality_sample_count / 2u)) /
            g_demo_score_windows[idx].quality_sample_count;

        if(avg_irq_us >= RF_AUTO_DEMO_HOP_STABLE_IRQ_AVG_US)
        {
            return 0u;
        }
    }

    now = RF_LinkClockNow();
    elapsed = now - g_demo_score_windows[idx].window_start_clock;
    if(elapsed < MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_SCORE_WINDOW_MS))
    {
        return 1u;
    }

    return (g_demo_score_windows[idx].last_window_stable != 0u) ? 1u : 0u;
}

static void demo_channel_mark_tried(uint8_t channel)
{
    uint8_t idx = demo_channel_index(channel);

    if((idx != 0xFFu) && (idx < 8u))
    {
        g_demo_channel_tried_mask |= (uint8_t)(1u << idx);
    }
}

static uint8_t demo_all_channels_tried(void)
{
    uint8_t all_mask = (uint8_t)((1u << RFH_HOP_CHANNEL_COUNT) - 1u);

    return ((g_demo_channel_tried_mask & all_mask) == all_mask) ? 1u : 0u;
}



static uint16_t demo_channel_score_get(uint8_t channel)
{
    uint8_t idx = demo_channel_index(channel);

    if(idx == 0xFFu)
    {
        return RF_AUTO_DEMO_CHANNEL_SCORE_BAD;
    }
    return g_demo_channel_scores[idx];
}

#if (RF_AUTO_DEMO_RANK_PROMOTE_ENABLE != 0u)
static uint16_t demo_channel_score_effective_by_index(uint8_t idx)
{
    if(idx >= RFH_HOP_CHANNEL_COUNT)
    {
        return RF_AUTO_DEMO_CHANNEL_SCORE_BAD;
    }
    if(rfh_hop_channel_at(idx) == g_demo_current_channel)
    {
        return g_demo_channel_scores[idx];
    }
    if((idx >= 8u) ||
       ((g_demo_channel_score_known_mask & (uint8_t)(1u << idx)) == 0u))
    {
        return RF_AUTO_DEMO_CHANNEL_SCORE_UNKNOWN;
    }
    return g_demo_channel_scores[idx];
}



#endif

static void demo_channel_scores_init(void)
{
    uint8_t i;
    uint32_t now = RF_LinkClockNow();

    for(i = 0u; i < RFH_HOP_CHANNEL_COUNT; i++)
    {
        g_demo_channel_scores[i] = RF_AUTO_DEMO_CHANNEL_SCORE_INIT;
        g_demo_channel_cooldown_until[i] = 0u;
        demo_score_window_reset_by_index(i, now);
    }
    i = demo_channel_index(RF_AUTO_DEMO_INITIAL_CHANNEL);
    if(i != 0xFFu)
    {
        g_demo_channel_scores[i] = RF_AUTO_DEMO_CHANNEL_SCORE_GOOD;
    }
    g_demo_channel_score_known_mask = 0u;
    /* No channel is considered measured merely because it is the boot anchor. */
}

#if 0
static char demo_tx_state_char(void)
{
    if(g_demo_pair_state == RF_AUTO_PAIR_OFFERING)
    {
        return 'O';
    }
    if(g_demo_pair_state == RF_AUTO_PAIR_CONFIRM_WAIT)
    {
        return 'F';
    }
    if(g_demo_hop_state == RF_AUTO_HOP_PREPARE_ACK_WAIT)
    {
        return 'P';
    }
    if(g_demo_hop_state == RF_AUTO_HOP_CONFIRM_ACK_WAIT)
    {
        return 'C';
    }
    if(g_demo_hop_state == RF_AUTO_HOP_RECOVERY_DUAL)
    {
        return 'R';
    }
    return 'M';
}

static char demo_tx_connect_phase_char(void)
{
    if(g_demo_connect_phase == RF_AUTO_CONNECT_SYN_TX)
    {
        return 's';
    }
    if(g_demo_connect_phase == RF_AUTO_CONNECT_SYN_ACK_RX)
    {
        return 'a';
    }
    if(g_demo_connect_phase == RF_AUTO_CONNECT_FINAL_TX)
    {
        return 'f';
    }
    return '-';
}
#endif

#if (RF_AUTO_DEMO_AUTO_HOP_ENABLE != 0u)
static uint8_t demo_next_channel(uint8_t current,
                                 uint32_t now,
                                 uint16_t current_risk_score,
                                 uint8_t allow_best_available)
{
    uint8_t i, best = current;
    uint16_t best_score = 1001u;
    (void)allow_best_available;
    for(i = 0; i < RFH_HOP_CHANNEL_COUNT; ++i) {
        uint8_t ch = rfh_hop_channel_at(i);
        if(ch == current || (int32_t)(now-g_demo_channel_cooldown_until[i]) < 0) continue;
        if(!(g_demo_channel_score_known_mask & (1u << i)) ||
           (uint32_t)(now-g_demo_channel_measured_at[i]) >= MS1_TO_SYSTEM_TIME(60000u)) continue;
        if(g_demo_channel_scores[i] < best_score) { best_score=g_demo_channel_scores[i]; best=ch; }
    }
    if(best != current && (uint32_t)best_score + RF_AUTO_DEMO_HOP_SCORE_IMPROVE_MIN <= current_risk_score) return best;
    if((int32_t)(now-g_demo_explore_after) < 0) return current;
    for(i=0; i<RFH_HOP_CHANNEL_COUNT; ++i) {
        uint8_t idx=g_demo_explore_cursor++ % RFH_HOP_CHANNEL_COUNT;
        uint8_t ch=rfh_hop_channel_at(idx);
        if(ch == current || (int32_t)(now-g_demo_channel_cooldown_until[idx]) < 0) continue;
        if(!(g_demo_channel_score_known_mask & (1u << idx)) ||
           (uint32_t)(now-g_demo_channel_measured_at[idx]) >= MS1_TO_SYSTEM_TIME(60000u)) {
            g_demo_explore_after=now+MS1_TO_SYSTEM_TIME(30000u);
            return ch;
        }
    }
    return current;
}

#if (RF_AUTO_DEMO_RANK_PROMOTE_ENABLE != 0u)
static uint8_t demo_channel_rank_by_index(uint8_t idx)
{
    uint8_t i;
    uint8_t rank = 0u;
    uint8_t channel;
    uint16_t score;

    if(idx >= RFH_HOP_CHANNEL_COUNT)
    {
        return RFH_HOP_CHANNEL_COUNT;
    }

    channel = rfh_hop_channel_at(idx);
    score = demo_channel_score_effective_by_index(idx);
    for(i = 0u; i < RFH_HOP_CHANNEL_COUNT; i++)
    {
        uint8_t other_channel;
        uint16_t other_score;

        if(i == idx)
        {
            continue;
        }
        other_channel = rfh_hop_channel_at(i);
        other_score = demo_channel_score_effective_by_index(i);
        if((other_score < score) ||
           ((other_score == score) && (other_channel < channel)))
        {
            rank++;
        }
    }
    return rank;
}

static uint8_t demo_best_front_half_channel(uint8_t current,
                                            uint32_t now,
                                            uint8_t *target_channel,
                                            uint16_t *reason_score)
{
    uint8_t i;
    uint8_t current_idx = demo_channel_index(current);
    uint8_t current_rank;
    uint8_t best_idx = 0xFFu;
    uint16_t current_score;
    uint16_t best_score = 0xFFFFu;

    if((target_channel == 0) || (reason_score == 0) ||
       (current_idx == 0xFFu))
    {
        return 0u;
    }

    current_rank = demo_channel_rank_by_index(current_idx);
    if(current_rank < RF_AUTO_DEMO_FRONT_HALF_COUNT)
    {
        return 0u;
    }

    current_score = demo_channel_score_effective_by_index(current_idx);
    for(i = 0u; i < RFH_HOP_CHANNEL_COUNT; i++)
    {
        uint8_t channel = rfh_hop_channel_at(i);
        uint16_t score = demo_channel_score_effective_by_index(i);

        if(channel == current)
        {
            continue;
        }
        if(demo_channel_rank_by_index(i) >= RF_AUTO_DEMO_FRONT_HALF_COUNT)
        {
            continue;
        }
        if(score >= current_score)
        {
            continue;
        }
        if((int32_t)(now - g_demo_channel_cooldown_until[i]) < 0)
        {
            continue;
        }
        if((best_idx == 0xFFu) ||
           (score < best_score) ||
           ((score == best_score) &&
            (channel < rfh_hop_channel_at(best_idx))))
        {
            best_idx = i;
            best_score = score;
        }
    }

    if(best_idx == 0xFFu)
    {
        return 0u;
    }

    *target_channel = rfh_hop_channel_at(best_idx);
    *reason_score = current_score;
    return 1u;
}
#endif
#endif

/* Callers serialize state transitions against TMR0 and BLE callbacks. */
static void demo_abort_radio(void)
{
    ++g_demo_radio_generation;
    g_short_ack_wait=0;
    g_demo_pause_tx = 1u;
    (void)RFRole_Stop();
    g_demo_tx_busy = 0u;
    g_demo_wait_ack_after_tx = 0u;
    g_demo_ack_rx_active = 0u;
    g_demo_ack_burst_left = 0u;
    g_demo_force_ack_burst = 0u;
}

static void demo_apply_channel(uint8_t channel)
{
    demo_abort_radio();

    gParm.frequency = channel;
    RFRole_SetParam(&gParm);

    gTxParam.frequency = channel;
    gTxParam.whiteChannel = channel;
    gRxParam.frequency = channel;
    gRxParam.whiteChannel = channel;

    g_demo_current_channel = channel;
    g_demo_channel_enter_clock = RF_LinkClockNow();
    demo_score_window_reset_channel(channel, g_demo_channel_enter_clock);

    g_demo_ack_rx_active = 0u;
    g_demo_wait_ack_after_tx = 0u;
    g_demo_pause_tx = 0u;
}

static uint8_t demo_discovery_channel(uint8_t side)
{
    return ((side & 1u) == 0u) ?
           g_demo_bond_channel_b :
           g_demo_bond_channel_a;
}

static void demo_enter_tx_unconnected(uint32_t now)
{
    memset(&g_aux_tx,0,sizeof(g_aux_tx));memset(g_relative_tx,0,sizeof(g_relative_tx));
    g_relative_tag=0;g_short_measure=0;g_short_wire_seq=0;g_demo_seq=0;g_short_anchor_serial=0;
    (void)rfm_spi_bridge_emit_time_sync(0);
    rfm_spi_port_measure_enable(0);
    uint8_t anchor_channel = demo_discovery_channel(0u);

    demo_abort_radio();
    g_demo_hop_rollback = 0u;
    g_demo_emergency_windows = 0u;
    rfh_health_start(&g_demo_health, now);
    g_demo_hop_recovery_started = 0u;
    g_demo_link_state = RF_AUTO_TX_UNCONNECTED;
    g_demo_hop_state = RF_AUTO_HOP_COMM;
    g_demo_hop_manual = 0u;
    g_demo_ack_miss_count = 0u;
    g_demo_irq_bad_window_count = 0u;
    demo_link_quality_reset(now, RF_AUTO_LINK_QUALITY_NORMAL);
    g_demo_rate_update_pending = 0u;
    g_demo_ack_burst_left = 0u;
    g_demo_ack_rx_active = 0u;
    g_demo_wait_ack_after_tx = 0u;
    g_demo_force_ack_burst = 1u;
    g_demo_connect_phase = RF_AUTO_CONNECT_SYN_TX;
    g_demo_connect_phase_clock = now;
    g_demo_connect_packet_stage = RFH_CONNECT_STAGE_SYN;
    g_demo_discovery_side = 0u;
    g_demo_discovery_switch_clock = now;
    g_demo_old_channel = anchor_channel;
    g_demo_target_channel = anchor_channel;
    g_pending_event_state_code = (g_demo_has_bond == 0u) ?
                                 RF_LINK_STATE_IDLE :
                                 ((g_demo_reconnecting != 0u) ?
                                  RF_LINK_STATE_RECONNECTING :
                                  RF_LINK_STATE_CONNECTING);
    g_demo_pause_tx = 0u;
    if(g_demo_has_bond == 0u)
    {
        return;
    }
    if(g_demo_current_channel != anchor_channel)
    {
        demo_apply_channel(anchor_channel);
    }
}

static void demo_enter_tx_comm(uint32_t now, uint8_t channel)
{
    if(demo_channel_index(channel) == 0xFFu)
    {
        channel = g_demo_current_channel;
    }
    if(channel != g_demo_current_channel)
    {
        demo_apply_channel(channel);
    }
    g_demo_link_state = RF_AUTO_TX_COMM;
    if(!g_monitor_auto_hop_enabled && demo_channel_index(g_monitor_manual_channel) != 0xFFu)
        g_monitor_manual_pending = 1u;
    g_demo_reconnecting = 0u;
    g_demo_hop_state = RF_AUTO_HOP_COMM;
    g_demo_hop_manual = 0u;
    g_demo_old_channel = channel;
    g_demo_target_channel = channel;
    g_demo_ack_miss_count = 0u;
    g_demo_irq_bad_window_count = 0u;
    demo_link_quality_reset(now, RF_AUTO_LINK_QUALITY_NORMAL);
    g_demo_force_ack_burst = 0u;
    g_demo_ack_burst_left = 0u;
    g_demo_wait_ack_after_tx = 0u;
    g_demo_ack_rx_active = 0u;
    g_demo_channel_enter_clock = now;
    rfh_health_start(&g_demo_health, now);
    rfh_health_ack(&g_demo_health, now);
    demo_arm_next_ack_clock(now);
    g_pending_event_state_code = RF_LINK_STATE_CONNECTED;
}

static void demo_enter_tx_provisional(uint32_t now, uint8_t channel)
{
    if(demo_channel_index(channel) == 0xFFu)
    {
        channel = g_demo_current_channel;
    }
    if(channel != g_demo_current_channel)
    {
        demo_apply_channel(channel);
    }
    demo_abort_radio();
    g_demo_pause_tx = 0u;
    rfh_health_start(&g_demo_health, now);
    g_demo_link_state = RF_AUTO_TX_PROVISIONAL;
    g_demo_reconnecting = 0u;
    g_demo_hop_state = RF_AUTO_HOP_COMM;
    g_demo_old_channel = channel;
    g_demo_target_channel = channel;
    g_demo_ack_miss_count = 0u;
    g_demo_force_ack_burst = 1u;
    g_demo_ack_burst_left = 0u;
    g_demo_wait_ack_after_tx = 0u;
    g_demo_ack_rx_active = 0u;
    demo_arm_next_ack_clock(now);
    g_pending_event_state_code = RF_LINK_STATE_CONNECTING;
}

static void demo_service_link(uint32_t now)
{
    uint8_t anchor_channel;

    if(g_demo_link_state != RF_AUTO_TX_UNCONNECTED)
    {
        return;
    }
    if(g_demo_has_bond == 0u)
    {
        return;
    }
    if(g_demo_connect_phase != RF_AUTO_CONNECT_SYN_TX)
    {
        return;
    }
    /* Fixed DATA channels also rendezvous on the common discovery pair. */
    {
        if((g_demo_tx_busy != 0u) ||
           (g_demo_ack_rx_active != 0u) ||
           (g_demo_wait_ack_after_tx != 0u) ||
           (g_demo_pause_tx != 0u))
        {
            return;
        }
        if((uint32_t)(now - g_demo_discovery_switch_clock) <
           MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_DISCOVERY_DWELL_MS))
        {
            return;
        }
        g_demo_discovery_switch_clock = now;
        g_demo_discovery_side ^= 1u;
        anchor_channel = demo_discovery_channel(g_demo_discovery_side);
    }
    if(g_demo_current_channel != anchor_channel)
    {
        demo_apply_channel(anchor_channel);
    }
}

static void demo_prepare_aux(void)
{
    uint8_t p[40]={0},type=0,len=0;uint32_t lock,generation;
    if(g_aux_tx.active)return;
    SYS_DisableAllIrq(&lock);generation=g_demo_radio_generation;
    if(g_monitor_status_pending) {
        p[0]=RFH_CMD_MONITOR_CONFIG;p[1]=g_monitor_status_flags;p[2]=g_monitor_status_manual_channel;
        p[3]=g_monitor_status_result;p[4]=g_monitor_status_seq;
        type=RFH_AUX_CONFIG;len=5;g_monitor_status_pending=0;
    } else if(g_demo_rate_update_pending) {
        p[0]=RFH_CMD_RATE_UPDATE;p[1]=g_demo_rate_code;p[4]=g_demo_rate_update_seq;
        type=RFH_AUX_RATE;len=5;g_demo_rate_update_pending=0;
    }
    SYS_RecoverIrq(lock);
    if(!type && g_short_measure && short_prepare_trace())return;
    SYS_DisableAllIrq(&lock);
    if(!type && g_monitor_battery_pending) {
        p[0]=RFH_CMD_BATTERY_STATUS;p[1]=g_monitor_battery_status;
        type=RFH_AUX_BATTERY;len=2;g_monitor_battery_pending=0;
    } else if(!type && g_demo_diag_pending) {
        p[0]=RFH_CMD_TX_DIAGNOSTIC;p[1]=g_demo_diag_window_10ms;
        rfh_put_u16(p+2,g_demo_diag_due_window);rfh_put_u16(p+4,g_demo_diag_started_window);
        rfh_put_u16(p+6,g_demo_diag_dropped_window);
        for(unsigned i=0;i<3;i++)rfh_put_u32(p+8+4*i,g_short_packet_count[i]);
        rfh_put_u32(p+20,g_short_ack_slots);rfh_put_u32(p+24,g_short_control_slots);
        rfh_put_u32(p+28,g_relative_overflow);
        type=RFH_AUX_STATS;len=32;g_demo_diag_pending=0;
    }
    SYS_RecoverIrq(lock);
    if(type) {
        rfh_aux_tx_t next=g_aux_tx;rfh_aux_begin(&next,type,p,len);
        SYS_DisableAllIrq(&lock);
        if(generation==g_demo_radio_generation && !g_aux_tx.active)g_aux_tx=next;
        SYS_RecoverIrq(lock);
    }
}
static uint8_t demo_active_hop_cmd(void)
{
    if(g_demo_link_state != RF_AUTO_TX_COMM) return RFH_CMD_NONE;
    if(g_demo_hop_state == RF_AUTO_HOP_PREPARE_ACK_WAIT) return RFH_CMD_HOP_PREPARE;
    if(g_demo_hop_state == RF_AUTO_HOP_CONFIRM_ACK_WAIT) return RFH_CMD_HOP_CONFIRM;
    if(g_demo_hop_state == RF_AUTO_HOP_RECOVERY_DUAL)
        return g_demo_current_channel == g_demo_old_channel ? RFH_CMD_HOP_PREPARE : RFH_CMD_HOP_CONFIRM;
    return RFH_CMD_NONE;
}

static uint8_t demo_is_hop_control_cmd(uint8_t cmd)
{
    return ((cmd == RFH_CMD_HOP_PREPARE) ||
            (cmd == RFH_CMD_HOP_CONFIRM)) ? 1u : 0u;
}

static void demo_schedule_hop_control_retry(uint32_t now)
{
    g_demo_force_ack_burst = 0u;
    g_demo_hop_cmd_retry_clock =
        now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_CMD_RETRY_MS);
}

static void demo_request_hop_control_now(uint32_t now)
{
    g_demo_force_ack_burst = 1u;
    g_demo_hop_cmd_retry_clock =
        now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_CMD_RETRY_MS);
}

static uint8_t demo_begin_hop_prepare_common(uint32_t now,
                                             uint8_t target_channel,
                                             uint16_t reason_score,
                                             uint8_t bypass_stable_block,
                                             uint8_t manual)
{
    if((demo_channel_index(target_channel) == 0xFFu) ||
       (target_channel == g_demo_current_channel))
    {
        return 0u;
    }
    if((bypass_stable_block == 0u) &&
       (demo_stable_window_blocks_hop() != 0u))
    {
        return 0u;
    }

    g_demo_old_channel = g_demo_current_channel;
    g_demo_target_channel = target_channel;
    g_demo_hop_reason_score = reason_score;
    demo_channel_mark_tried(g_demo_old_channel);
    demo_channel_mark_tried(g_demo_target_channel);
    g_demo_hop_seq++;
    g_demo_hop_recovery_started = 0u;
    if(g_demo_hop_seq == 0u)
    {
        g_demo_hop_seq = 1u;
    }
    g_demo_hop_manual = (manual != 0u) ? 1u : 0u;
    g_demo_hop_state = RF_AUTO_HOP_PREPARE_ACK_WAIT;
    g_demo_hop_deadline_clock = now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_PREPARE_TIMEOUT_MS);
    demo_request_hop_control_now(now);
    g_demo_stat.hop_event++;
    return 1u;
}

#if (RF_AUTO_DEMO_RANK_PROMOTE_ENABLE != 0u)
static uint8_t demo_begin_hop_prepare(uint32_t now,
                                      uint8_t target_channel,
                                      uint16_t reason_score)
{
    return demo_begin_hop_prepare_common(now, target_channel, reason_score, 0u, 0u);
}
#endif

static uint8_t demo_begin_manual_hop_prepare(uint32_t now,
                                             uint8_t target_channel,
                                             uint16_t reason_score)
{
    return demo_begin_hop_prepare_common(now, target_channel, reason_score, 1u, 1u);
}

static void demo_start_hop_prepare(uint32_t now, uint16_t reason_score)
{
#if (RF_AUTO_DEMO_AUTO_HOP_ENABLE == 0u)
    (void)now;
    (void)reason_score;
    return;
#else
    if(g_monitor_auto_hop_enabled == 0u)
    {
        return;
    }
    if(g_demo_link_state != RF_AUTO_TX_COMM)
    {
        return;
    }
    if(g_demo_hop_state != RF_AUTO_HOP_COMM)
    {
        return;
    }
    if(g_demo_link_quality_state != RF_AUTO_LINK_QUALITY_DEGRADED)
    {
        return;
    }
    if((int32_t)(now - g_demo_hop_cooldown_until) < 0)
    {
        return;
    }

    g_demo_old_channel = g_demo_current_channel;
    demo_channel_score_update(g_demo_current_channel, reason_score, now);
    g_demo_target_channel = demo_next_channel(g_demo_current_channel,
                                              now,
                                              reason_score,
                                              demo_all_channels_tried());
    if(g_demo_target_channel == g_demo_old_channel)
    {
        g_demo_hop_cooldown_until =
            now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_NO_TARGET_COOLDOWN_MS);
        return;
    }
    (void)demo_begin_hop_prepare_common(now,
                                        g_demo_target_channel,
                                        reason_score,
                                        1u,
                                        0u);
#endif
}

static void demo_link_quality_window_reset(uint32_t now)
{
    g_demo_link_quality_window.window_start_clock = now;
    g_demo_link_quality_window.loss_sum_permille = 0u;
    g_demo_link_quality_window.irq_sum_us = 0u;
    g_demo_link_quality_window.max_loss_permille = 0u;
    g_demo_link_quality_window.sample_count = 0u;
    g_demo_link_quality_window.ack_timeout_count = 0u;
    g_demo_link_quality_window.active = 1u;
}

static void demo_link_quality_reset(uint32_t now,
                                    rf_auto_link_quality_state_t state)
{
    g_demo_link_quality_state = state;
    g_demo_link_quality_window.bad_window_count = 0u;
    g_demo_link_quality_window.good_window_count = 0u;
    demo_link_quality_window_reset(now);
}

static uint16_t demo_link_quality_score(uint16_t avg_loss_permille,
                                        uint16_t max_loss_permille,
                                        uint16_t avg_irq_us,
                                        uint8_t ack_timeout_count)
{
    uint16_t score = RF_AUTO_DEMO_CHANNEL_SCORE_GOOD;
    uint16_t sample;

    sample = demo_score_loss_sample(avg_loss_permille);
    if(sample > score)
    {
        score = sample;
    }
    sample = demo_score_loss_sample(max_loss_permille);
    if(sample > score)
    {
        score = sample;
    }
    (void)avg_irq_us;
    if(ack_timeout_count != 0u)
    {
        sample = demo_score_timeout_sample();
        if(sample > score)
        {
            score = sample;
        }
    }
    return score;
}

static void demo_link_quality_probation_success(uint32_t now)
{
    demo_channel_score_force(g_demo_current_channel,
                             (uint16_t)(g_demo_probation_score_sum / (g_demo_probation_samples ? g_demo_probation_samples : 1u)),
                             now);
    g_demo_old_channel = g_demo_current_channel;
    g_demo_target_channel = g_demo_current_channel;
    g_demo_ack_miss_count = 0u;
    g_demo_irq_bad_window_count = 0u;
    g_demo_hop_cooldown_until =
        now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_COOLDOWN_MS);
    demo_link_quality_reset(now, RF_AUTO_LINK_QUALITY_NORMAL);
}

static void demo_link_quality_probation_failed(uint32_t now)
{
    uint8_t failed = g_demo_current_channel, rollback = g_demo_probation_old_channel;
    demo_channel_quarantine(failed, now);
    demo_link_quality_reset(now, RF_AUTO_LINK_QUALITY_NORMAL);
    g_demo_hop_cooldown_until = now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_COOLDOWN_MS);
    if(demo_begin_hop_prepare_common(now, rollback, g_demo_hop_reason_score, 1u, 0u)) {
        g_demo_hop_rollback = 1u;
    } else {
        g_demo_reconnecting = 1u;
        g_demo_recovery_reason = 4u;
        demo_enter_tx_unconnected(now);
    }
}

static void demo_link_quality_enter_probation(uint32_t now, uint8_t old_channel)
{
    g_demo_probation_old_channel = old_channel;
    g_demo_probation_baseline = g_demo_hop_reason_score;
    g_demo_probation_score_sum = 0u;
    g_demo_probation_samples = 0u;
    g_demo_probation_deadline_clock =
        now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_PROBATION_MS);
    demo_link_quality_reset(now, RF_AUTO_LINK_QUALITY_PROBATION);
}

static void demo_link_quality_service(uint32_t now)
{
    uint32_t elapsed;
    uint16_t avg_loss = 0u;
    uint16_t avg_irq = 0u;
    uint16_t risk_score;
    uint8_t bad_window;
    uint8_t good_window;
    uint8_t sample_count;
    uint8_t timeout_count;

    if(g_demo_link_quality_window.active == 0u)
    {
        demo_link_quality_window_reset(now);
        return;
    }

    elapsed = now - g_demo_link_quality_window.window_start_clock;
    if(elapsed < MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_QUALITY_WINDOW_MS))
    {
        if((g_demo_link_quality_state == RF_AUTO_LINK_QUALITY_PROBATION) &&
           ((int32_t)(now - g_demo_probation_deadline_clock) >= 0))
        {
            if(g_demo_probation_samples >= 2u &&
               g_demo_probation_score_sum / g_demo_probation_samples + RF_AUTO_DEMO_HOP_SCORE_IMPROVE_MIN <= g_demo_probation_baseline)
                demo_link_quality_probation_success(now);
            else demo_link_quality_probation_failed(now);
        }
        return;
    }

    sample_count = g_demo_link_quality_window.sample_count;
    timeout_count = g_demo_link_quality_window.ack_timeout_count;
    if(sample_count != 0u)
    {
        uint32_t avg =
            (g_demo_link_quality_window.loss_sum_permille +
             ((uint32_t)sample_count / 2u)) / (uint32_t)sample_count;
        avg_loss = (avg > 0xFFFFu) ? 0xFFFFu : (uint16_t)avg;

        avg = (g_demo_link_quality_window.irq_sum_us +
               ((uint32_t)sample_count / 2u)) / (uint32_t)sample_count;
        avg_irq = (avg > 0xFFFFu) ? 0xFFFFu : (uint16_t)avg;
    }

    bad_window =
        (((sample_count != 0u) &&
          ((avg_loss >= RF_AUTO_DEMO_BAD_AVG_LOSS_PERMILLE) ||
           (g_demo_link_quality_window.max_loss_permille >=
            RF_AUTO_DEMO_BAD_MAX_LOSS_PERMILLE) ||
           (0u /* queue latency is not RF interference */))) ||
         (timeout_count >= RF_AUTO_DEMO_BAD_ACK_TIMEOUTS)) ? 1u : 0u;
    good_window =
        ((sample_count != 0u) &&
         (g_demo_link_quality_window.max_loss_permille <=
          RF_AUTO_DEMO_GOOD_MAX_LOSS_PERMILLE) &&
         (timeout_count == 0u)) ? 1u : 0u;

    risk_score = demo_link_quality_score(avg_loss,
                                         g_demo_link_quality_window.max_loss_permille,
                                         avg_irq,
                                         timeout_count);
    if((bad_window != 0u) && (risk_score < RF_AUTO_DEMO_HOP_SCORE_THRESHOLD))
    {
        risk_score = RF_AUTO_DEMO_HOP_SCORE_THRESHOLD;
    }

    demo_link_quality_window_reset(now);

    if(g_demo_link_quality_state == RF_AUTO_LINK_QUALITY_PROBATION)
    {
        if(sample_count != 0u) { g_demo_probation_score_sum += risk_score; g_demo_probation_samples++; }
        if(bad_window != 0u)
        {
            demo_link_quality_probation_failed(now);
            return;
        }
        if((int32_t)(now - g_demo_probation_deadline_clock) >= 0)
        {
            if(g_demo_probation_samples >= 2u &&
               g_demo_probation_score_sum / g_demo_probation_samples + RF_AUTO_DEMO_HOP_SCORE_IMPROVE_MIN <= g_demo_probation_baseline)
                demo_link_quality_probation_success(now);
            else demo_link_quality_probation_failed(now);
        }
        return;
    }

    if(bad_window != 0u)
    {
        if(g_demo_link_quality_window.bad_window_count != 0xFFu)
        {
            g_demo_link_quality_window.bad_window_count++;
        }
        g_demo_link_quality_window.good_window_count = 0u;
        if(g_demo_link_quality_window.bad_window_count >=
           RF_AUTO_DEMO_BAD_WINDOWS_TO_HOP)
        {
            g_demo_link_quality_state = RF_AUTO_LINK_QUALITY_DEGRADED;
            demo_start_hop_prepare(now, risk_score);
        }
        return;
    }

    g_demo_link_quality_window.bad_window_count = 0u;
    if(good_window != 0u)
    {
        if(g_demo_link_quality_window.good_window_count != 0xFFu)
        {
            g_demo_link_quality_window.good_window_count++;
        }
        if(g_demo_link_quality_window.good_window_count >=
           RF_AUTO_DEMO_GOOD_WINDOWS_TO_NORMAL)
        {
            g_demo_link_quality_state = RF_AUTO_LINK_QUALITY_NORMAL;
        }
    }
    else
    {
        g_demo_link_quality_window.good_window_count = 0u;
    }
}

static void demo_link_quality_note_sample(uint16_t loss_permille,
                                          uint16_t irq_us,
                                          uint32_t now)
{
    uint8_t sample_channel = g_demo_current_channel;

    if((g_demo_link_state != RF_AUTO_TX_COMM) ||
       (g_demo_current_channel != sample_channel))
    {
        return;
    }
    if(g_demo_link_quality_window.active == 0u)
    {
        demo_link_quality_window_reset(now);
    }

    g_demo_link_quality_window.loss_sum_permille += loss_permille;
    g_demo_link_quality_window.irq_sum_us += irq_us;
    if(loss_permille > g_demo_link_quality_window.max_loss_permille)
    {
        g_demo_link_quality_window.max_loss_permille = loss_permille;
    }
    if(g_demo_link_quality_window.sample_count != 0xFFu)
    {
        g_demo_link_quality_window.sample_count++;
    }
}

static void demo_link_quality_note_ack_timeout(uint32_t now)
{
    uint8_t timeout_channel = g_demo_current_channel;

    if((g_demo_link_state != RF_AUTO_TX_COMM) ||
       (g_demo_current_channel != timeout_channel))
    {
        return;
    }
    if(g_demo_link_quality_window.active == 0u)
    {
        demo_link_quality_window_reset(now);
    }
    if(g_demo_link_quality_window.ack_timeout_count != 0xFFu)
    {
        g_demo_link_quality_window.ack_timeout_count++;
    }
}

static void demo_queue_manual_hop(uint8_t seq, uint8_t target_channel)
{
    if(demo_channel_index(target_channel) == 0xFFu)
    {
        return;
    }
    if(seq == g_monitor_manual_applied_seq && target_channel == g_demo_current_channel)
    {
        return;
    }
    g_monitor_manual_channel = target_channel;
    g_monitor_manual_seq = seq;
    g_monitor_manual_pending = 1u;
}

static void demo_service_manual_hop(void)
{
    if(g_monitor_manual_pending == 0u)
    {
        return;
    }
    if(g_monitor_auto_hop_enabled != 0u)
    {
        g_monitor_manual_pending = 0u;
        return;
    }
    if(g_demo_link_state != RF_AUTO_TX_COMM)
    {
        return;
    }
    if(g_demo_hop_state != RF_AUTO_HOP_COMM)
    {
        return;
    }
    if(g_monitor_manual_channel == g_demo_current_channel)
    {
        g_monitor_manual_pending = 0u;
        g_monitor_manual_applied_seq = g_monitor_manual_seq;
        return;
    }

    g_demo_channel_tried_mask = 0u;
    if(demo_begin_manual_hop_prepare(RF_LinkClockNow(),
                                     g_monitor_manual_channel,
                                     demo_channel_score_get(g_demo_current_channel)) == 0u)
    {
        return;
    }
    g_monitor_status_pending = 1u;
    g_monitor_manual_pending = 0u;
    g_monitor_manual_applied_seq = g_monitor_manual_seq;
}

static void demo_finish_hop(uint32_t now)
{
    uint8_t previous_channel = g_demo_old_channel;

    g_demo_hop_state = RF_AUTO_HOP_COMM;
    g_demo_target_channel = g_demo_current_channel;
    demo_channel_mark_tried(g_demo_current_channel);
    g_demo_ack_miss_count = 0u;
    g_demo_irq_bad_window_count = 0u;
    g_demo_force_ack_burst = 0u;
    if((g_demo_hop_manual != 0u) || (g_demo_hop_rollback != 0u))
    {
        g_demo_old_channel = g_demo_current_channel;
        demo_channel_score_force(g_demo_current_channel,
                                 demo_score_loss_sample(g_demo_last_quality),
                                 now);
        demo_link_quality_reset(now, RF_AUTO_LINK_QUALITY_NORMAL);
        g_demo_hop_cooldown_until =
            now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_COOLDOWN_MS);
    }
    else
    {
        demo_link_quality_enter_probation(now, previous_channel);
    }
    g_demo_hop_manual = 0u;
    g_demo_hop_rollback = 0u;
    g_demo_stat.hop_event++;
}

static void demo_enter_recovery(uint32_t now)
{
    uint8_t failed_channel = g_demo_target_channel;

    g_demo_hop_state = RF_AUTO_HOP_RECOVERY_DUAL;
    if(!g_demo_hop_recovery_started) {
        g_demo_hop_recovery_started = 1u;
        g_demo_hop_recovery_deadline = now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_RECOVERY_TIMEOUT_MS);
    }
    g_demo_hop_deadline_clock = g_demo_hop_recovery_deadline;
    g_demo_hop_cmd_retry_clock = now;
    g_demo_recovery_switch_clock = now;
    g_demo_recovery_side = 0u;
    demo_channel_quarantine(failed_channel, now);
    demo_apply_channel(g_demo_old_channel);
    demo_link_quality_reset(now, RF_AUTO_LINK_QUALITY_NORMAL);
    demo_request_hop_control_now(now);
    g_demo_stat.hop_event++;
}

static void demo_handle_command_ack(uint8_t cmd, uint8_t seq, uint8_t channel)
{
    uint32_t now = RF_LinkClockNow();

    if((cmd == RFH_CMD_RATE_UPDATE) &&
       (g_demo_rate_update_pending != 0u) &&
       (seq == g_demo_rate_update_seq))
    {
        g_demo_rate_update_pending = 0u;
        return;
    }

    if(seq != g_demo_hop_seq ||
       (cmd == RFH_CMD_HOP_PREPARE && (channel != g_demo_old_channel || channel != g_demo_current_channel)) ||
       (cmd == RFH_CMD_HOP_CONFIRM && (channel != g_demo_target_channel || channel != g_demo_current_channel)))
    {
        return;
    }

    if((cmd == RFH_CMD_HOP_PREPARE || cmd == RFH_CMD_HOP_CONFIRM) &&
       g_demo_hop_state != RF_AUTO_HOP_COMM &&
       ((int32_t)(now - g_demo_hop_deadline_clock) >= 0 ||
        (g_demo_hop_recovery_started && (int32_t)(now - g_demo_hop_recovery_deadline) >= 0))) return;

    if((g_demo_hop_state == RF_AUTO_HOP_PREPARE_ACK_WAIT) &&
       (cmd == RFH_CMD_HOP_PREPARE))
    {
        demo_apply_channel(g_demo_target_channel);
        g_demo_hop_state = RF_AUTO_HOP_CONFIRM_ACK_WAIT;
        g_demo_hop_deadline_clock = now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_CONFIRM_TIMEOUT_MS);
        demo_request_hop_control_now(now);
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
            demo_request_hop_control_now(now);
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
    uint8_t ack_flags;
    uint8_t seq;
    uint8_t channel;
    uint16_t irq_score;
    uint16_t risk_score;
    uint32_t now = RF_LinkClockNow();

    if(RxBuf[1] == RFH_SHORT_ACK_LEN && g_demo_link_state != RF_AUTO_TX_UNCONNECTED) {
        uint8_t token=data[0];uint16_t quality=rfh_get_u16(data+1);
        if(rfh_packet_type(air[0])!=RFH_PKT_ACK || !(rfh_flags(air[0])&RFH_FLAG_LINK_OK) ||
           token!=g_demo_active_ack_token || rfh_rate_code(air[0])!=g_demo_rate_code || quality>1000u) return;
        memset(RxBuf+4,0,10);rfh_put_u16(RxBuf+4,quality);
        RxBuf[4+RFH_ACK_CHANNEL]=g_demo_current_channel;
        RxBuf[4+RFH_ACK_FLAGS]=RFH_SHORT_ACK_VERSION;RxBuf[1]=RF_AUTO_DEMO_PACKET_LEN;
    } else if(RxBuf[1]==RF_AUTO_DEMO_PACKET_LEN) {
        if(!(data[RFH_ACK_FLAGS]&RFH_SHORT_ACK_VERSION)) return;
        if(g_demo_link_state!=RF_AUTO_TX_UNCONNECTED && air[1]!=g_demo_active_ack_token)return;
    }
    if((RxBuf[1] != RF_AUTO_DEMO_PACKET_LEN) ||
       (rfh_packet_type(air[RFH_HDR0_OFFSET]) != RFH_PKT_ACK) ||
       ((rfh_flags(air[RFH_HDR0_OFFSET]) & RFH_FLAG_LINK_OK) == 0u))
    {
        g_demo_stat.ack_type_err++;
        demo_channel_score_update(g_demo_current_channel,
                                  demo_score_type_sample(),
                                  now);
        if(g_demo_hop_state != RF_AUTO_HOP_COMM)
        {
            demo_schedule_hop_control_retry(now);
        }
        return;
    }

    g_demo_last_quality = rfh_get_u16(&data[RFH_ACK_LOSS_PERMILLE_LO]);
    g_demo_last_avg_irq_us = rfh_get_u16(&data[RFH_ACK_AVG_IRQ_US_LO]);
    g_demo_last_max_irq_us = rfh_get_u16(&data[RFH_ACK_MAX_IRQ_US_LO]);
    cmd = data[RFH_ACK_CMD_ID];
    ack_flags = data[RFH_ACK_FLAGS];
    channel = data[RFH_ACK_CHANNEL];
    seq = data[RFH_ACK_STATUS];

    if((rfh_rate_code(air[RFH_HDR0_OFFSET]) != g_demo_rate_code) ||
       (rfh_channel_valid(channel) == 0u) ||
       ((g_demo_link_state == RF_AUTO_TX_PROVISIONAL) &&
        (cmd != RFH_CMD_SCORE_HINT) && (cmd != RFH_CMD_MONITOR_CONFIG) &&
        (channel != g_demo_current_channel)))
    {
        g_demo_stat.ack_type_err++;
        return;
    }

    g_demo_stat.ack_ok++;

    if(g_demo_link_state == RF_AUTO_TX_UNCONNECTED)
    {
        if((g_demo_connect_phase == RF_AUTO_CONNECT_SYN_ACK_RX) &&
           (cmd == RFH_CMD_CONNECT_REQ) &&
           ((ack_flags & RFH_FLAG_CMD_ACK) != 0u) &&
           (seq == RFH_ACK_STATUS_CONNECTED))
        {
            if(g_demo_pair_commit_pending != 0u)
            {
                if(demo_commit_prepared_bond() == 0u)
                {
                    g_demo_reconnecting = 0u;
                    g_pending_event_state_code = RF_LINK_STATE_PAIR_FAILED;
                    rfm_spi_bridge_emit_state_changed(0x02u);
                    demo_enter_tx_unconnected(now);
                    return;
                }
                g_pending_event_state_code = RF_LINK_STATE_PAIR_OK;
                rfm_spi_bridge_emit_state_changed(0x02u);
            }
            if(rfh_channel_valid(channel) != 0u)
            {
                demo_apply_channel(channel);
            }
            g_demo_connect_phase = RF_AUTO_CONNECT_FINAL_TX;
            g_demo_connect_phase_clock = now;
            g_demo_connect_packet_stage = RFH_CONNECT_STAGE_FINAL;
        }
        else if((g_demo_connect_phase == RF_AUTO_CONNECT_FINAL_TX) &&
                (cmd == RFH_CMD_CONNECT_REQ) &&
                ((ack_flags & RFH_FLAG_CMD_ACK) != 0u) &&
                (seq == RFH_ACK_STATUS_FINAL_READY))
        {
            if(rfh_channel_valid(channel) != 0u)
            {
                demo_enter_tx_comm(now, channel);
            }
            else
            {
                demo_enter_tx_comm(now, g_demo_current_channel);
            }
        }
        else
        {
            g_demo_stat.ack_type_err++;
            g_demo_force_ack_burst = 1u;
        }
        return;
    }

    if(g_demo_link_state == RF_AUTO_TX_PROVISIONAL)
    {
        demo_enter_tx_comm(now, g_demo_current_channel);
    }

    rfh_health_ack(&g_demo_health, now);
#if (RF_AUTO_DEMO_AUTO_HOP_ENABLE != 0u)
    if((uint32_t)(now - g_demo_emergency_window) >= MS1_TO_SYSTEM_TIME(100u)) {
        g_demo_emergency_window = now;
        if(g_demo_last_quality < 500u) g_demo_emergency_windows = 0u;
        else if(g_demo_emergency_windows < 3u) g_demo_emergency_windows++;
        if(g_demo_emergency_windows >= 3u && g_monitor_auto_hop_enabled &&
           g_demo_hop_state == RF_AUTO_HOP_COMM && g_demo_link_quality_state != RF_AUTO_LINK_QUALITY_PROBATION &&
           (int32_t)(now - g_demo_emergency_after) >= 0) {
            uint8_t target = demo_next_channel(g_demo_current_channel, now, demo_score_loss_sample(g_demo_last_quality), 1u);
            g_demo_emergency_after = now + MS1_TO_SYSTEM_TIME(5000u);
            g_demo_emergency_windows = 0u;
            (void)demo_begin_hop_prepare_common(now, target, demo_score_loss_sample(g_demo_last_quality), 1u, 0u);
        }
    }
#endif
    demo_score_window_note_quality(g_demo_current_channel,
                                   g_demo_last_quality,
                                   g_demo_last_avg_irq_us,
                                   now);
    demo_link_quality_note_sample(g_demo_last_quality,
                                  g_demo_last_avg_irq_us,
                                  now);
    g_demo_ack_miss_count = 0u;
    risk_score = demo_score_loss_sample(g_demo_last_quality);
    irq_score = 0u; /* software queue wait cannot justify a channel change */
    if(irq_score > risk_score)
    {
        risk_score = irq_score;
    }
    if(g_demo_last_avg_irq_us <= RF_AUTO_DEMO_HOP_IRQ_GOOD_US)
    {
        g_demo_irq_bad_window_count = 0u;
    }
    demo_channel_score_update(g_demo_current_channel,
                              risk_score,
                              now);

    if(cmd == RFH_CMD_MONITOR_CONFIG)
    {
        uint8_t flags = data[RFH_ACK_MON_FLAGS];
        uint8_t manual_channel = data[RFH_ACK_MON_MANUAL_CHANNEL];
        uint8_t manual_channel_valid =
            (demo_channel_index(manual_channel) != 0xFFu) ? 1u : 0u;

        if(((flags & RFMON_FLAG_AUTO_HOP) == 0u) &&
           (manual_channel_valid == 0u))
        {
            g_monitor_status_seq = seq;
            g_monitor_status_flags =
                (g_monitor_auto_hop_enabled != 0u) ? RFMON_FLAG_AUTO_HOP : 0u;
            g_monitor_status_manual_channel = g_monitor_manual_channel;
            g_monitor_status_result = RFMON_APPLY_FAILED;
            g_monitor_status_pending = 1u;
            return;
        }

        g_monitor_auto_hop_enabled = ((flags & RFMON_FLAG_AUTO_HOP) != 0u) ? 1u : 0u;
        if(manual_channel_valid != 0u)
        {
            g_monitor_manual_channel = manual_channel;
        }
        g_monitor_status_seq = seq;
        if(g_short_measure != ((flags & RFH_MEASUREMENT_FLAG) ? 1u : 0u)) {
            g_short_measure=(flags & RFH_MEASUREMENT_FLAG) ? 1u : 0u;
            g_sync_air_count=0;
            g_source_spi_received=0;
            memset(g_relative_tx,0,sizeof(g_relative_tx));g_relative_tag=0;
            if(g_aux_tx.active && g_aux_tx.data[0]==RFH_AUX_TRACE)g_aux_tx.active=0;
        }
        rfm_spi_port_measure_enable(g_short_measure);
        (void)rfm_spi_bridge_emit_time_sync(g_short_measure); /* capture enable, no timestamps */
        g_monitor_status_flags = flags;
        g_monitor_status_manual_channel = g_monitor_manual_channel;
        g_monitor_status_result = RFMON_APPLY_APPLIED;
        g_monitor_status_pending = 1u;
        if(g_monitor_auto_hop_enabled == 0u)
        {
            demo_queue_manual_hop(seq, g_monitor_manual_channel);
            demo_service_manual_hop();
        }
        return;
    }

    if(cmd == RFH_CMD_TIME_SYNC)
    {
        /* Clock synchronization was retired in v2. */
        return;
    }

    if(cmd == RFH_CMD_SCORE_HINT)
    {
        /* Remote historical hints have no age; do not make them fresh measurements. */
        cmd = RFH_CMD_NONE;
    }

    if((cmd == RFH_CMD_HOP_PREPARE) ||
       (cmd == RFH_CMD_HOP_CONFIRM) ||
       (cmd == RFH_CMD_RATE_UPDATE))
    {
        if(ack_flags & RFH_FLAG_CMD_ACK) demo_handle_command_ack(cmd, seq, channel);
        if(g_demo_hop_state != RF_AUTO_HOP_COMM)
        {
            demo_schedule_hop_control_retry(now);
        }
    }
    else if(g_demo_hop_state != RF_AUTO_HOP_COMM)
    {
        demo_schedule_hop_control_retry(now);
    }
}

#if (RF_TX_FORCE_INPUT_PAYLOAD_TEST != 0u)
static uint8_t demo_input_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0u;
    uint8_t i;

    for(i = 0u; i < len; ++i)
    {
        uint8_t bit;

        crc = (uint8_t)(crc ^ data[i]);
        for(bit = 0u; bit < 8u; ++bit)
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

static void demo_fill_direct_input_payload(uint8_t *data)
{
    static uint8_t s_direct_input_seq;

    memset(data, 0, RFM_RF_INPUT_PAYLOAD_LEN);
    data[0] = s_direct_input_seq++;
    data[1] = 0x11u;
    rfh_put_u32(&data[2], RF_TX_DIRECT_INPUT_TEST_KEY_MASK);
    data[RFMON_INPUT_CRC_OFFSET] = demo_input_crc8(data, (uint8_t)(RFM_RF_INPUT_PAYLOAD_LEN - 1u));
}
#endif

static void demo_encode_short_input_payload(uint8_t *dst, const uint8_t *src)
{
    if(!dst || !src)return;
    dst[0]=src[RFMON_INPUT_KEY_MASK_OFFSET];
    dst[1]=src[RFMON_INPUT_KEY_MASK_OFFSET+1u];
    dst[2]=src[RFMON_INPUT_KEY_MASK_OFFSET+2u];
}

static void demo_fill_short_packet(uint8_t request_ack)
{
    uint8_t input_payload[RFM_RF_INPUT_PAYLOAD_LEN];
    uint8_t *air=TxBuf+2,*data=air+RFH_DATA_OFFSET;
        if(rfm_spi_port_peek_latest_input(input_payload,RFM_RF_INPUT_PAYLOAD_LEN)) {
            if(!g_demo_have_payload || !demo_input_payload_same_input(g_demo_last_payload,input_payload))
                demo_store_last_payload(input_payload,tx_now_cycles());
        }
        demo_encode_short_input_payload(data,g_demo_last_payload);
        /* CRC/reassembly runs in main context, not the 8K interrupt. */
        g_short_aux_sent=(uint8_t)rfh_aux_peek(&g_aux_tx,data+3);
        /* Each pass starts with an explicit packet-counter anchor. This is
         * carried alongside input and has no clock or standalone air packet. */
        if(g_aux_tx.active && g_aux_tx.index==0u &&
           g_short_anchor_serial!=g_aux_tx.serial*4u+g_aux_tx.pass) {
            rfh_put_u16(data+3,g_short_wire_seq);g_short_aux_sent=2;
        }
        air[0]=rfh_make_header0(RFH_PKT_DATA,g_demo_rate_code,
            (uint8_t)(RFH_FLAG_LINK_OK | (request_ack ? RFH_FLAG_CMD_ACK : 0u) |
                      (g_short_aux_sent==2u ? RFH_FLAG_CMD_PRESENT : 0u)));
        air[1]=g_demo_seq;
        TxBuf[1]=g_short_aux_sent ? RFH_AUX_LEN : RFH_SHORT_LEN;
        if(request_ack) g_demo_active_ack_token=g_demo_seq;

}

static void demo_fill_tx_packet(uint8_t request_ack, uint8_t ack_token, uint8_t ack_burst_left)
{
    uint8_t *air=TxBuf+2,*data=air+RFH_DATA_OFFSET;
    uint8_t hop_cmd=demo_active_hop_cmd();
    memset(TxBuf,0,sizeof(TxBuf));TxBuf[0]=RFH_WCH_PREAMBLE;
    TxBuf[1]=RFH_AIR_PACKET_LEN;
    if(g_demo_link_state==RF_AUTO_TX_UNCONNECTED) {
        air[0]=rfh_make_header0(RFH_PKT_CONNECT,g_demo_rate_code,RFH_FLAG_DUAL_REDUNDANT|RFH_FLAG_CMD_ACK);
        air[1]=g_demo_seq;
        rfh_put_u32(data+RFH_CONNECT_SESSION0,RFH_CONNECT_SESSION_ID);
        data[RFH_CONNECT_RATE]=g_demo_rate_code;
        data[RFH_CONNECT_CH_A]=RF_AUTO_DEMO_DISCOVERY_CHANNEL_A;
        data[RFH_CONNECT_CH_B]=RF_AUTO_DEMO_DISCOVERY_CHANNEL_B;
        data[RFH_CONNECT_ACK_WINDOW_MS]=(uint8_t)(g_short_wire_seq>>8);
        data[RFH_CONNECT_OPTIONS]=g_demo_connect_packet_stage;
        data[RFH_CONNECT_VERSION]=RFH_PROTOCOL_VERSION;
        return;
    }
    if(!request_ack || hop_cmd==RFH_CMD_NONE) {demo_fill_short_packet(request_ack);return;}
    air[0]=rfh_make_header0(RFH_PKT_DATA,g_demo_rate_code,RFH_FLAG_LINK_OK|RFH_FLAG_CMD_PRESENT|RFH_FLAG_CMD_ACK);
    air[1]=g_demo_seq;data[RFH_HOP_CMD_ID]=hop_cmd;
    data[RFH_HOP_CMD_CHANNEL]=g_demo_target_channel;
    data[RFH_HOP_CMD_SEQ]=g_demo_hop_seq;
    data[RFH_HOP_CONFIRM_OLD_CHANNEL]=g_demo_old_channel;
    rfh_put_u16(data+RFH_HOP_CMD_SCORE_LO,g_demo_hop_reason_score);
    air[RF_AUTO_DEMO_ACK_TOKEN_OFFSET]=ack_token;
    air[RF_AUTO_DEMO_ACK_REMAIN_OFFSET]=ack_burst_left;
}

static void demo_fill_pair_packet(uint8_t cmd)
{
    uint8_t *air = &TxBuf[2];
    uint32_t arg32 = 0u;
    uint8_t write_bond = 0u;

    memset(TxBuf, 0, sizeof(TxBuf));
    TxBuf[0] = RFH_WCH_PREAMBLE;
    TxBuf[1] = RF_AUTO_DEMO_PACKET_LEN;

    if(cmd == RFH_CMD_PAIR_OFFER)
    {
        arg32 = g_demo_pair_tx_id_hash;
    }
    else if(cmd == RFH_CMD_PAIR_CONFIRM)
    {
        arg32 = g_demo_pair_link_access_address;
        write_bond = 1u;
    }
    else if(cmd == RFH_CMD_PAIR_REJECT)
    {
        arg32 = RFH_PAIR_REJECT_BAD_STATE;
    }

    (void)rf_pair_encode_air(air,
                             g_demo_rate_code,
                             (uint8_t)g_demo_pair_session,
                             cmd,
                             g_demo_pair_session,
                             arg32,
                             write_bond);
}

static void demo_arm_pair_rx(void)
{
    if((g_demo_config_ret != SUCCESS) || (demo_pair_is_active() == 0u))
    {
        return;
    }
    if((g_demo_pair_state == RF_AUTO_PAIR_CONFIRM_WAIT) &&
       (rfh_access_address_valid(g_demo_pair_link_access_address) == 0u))
    {
        return;
    }

    gRxParam.rxDMA = (uint32_t)RxBuf;
    gRxParam.accessAddress = (g_demo_pair_state == RF_AUTO_PAIR_CONFIRM_WAIT) ?
                             g_demo_pair_link_access_address :
                             RFH_PAIR_ACCESS_ADDRESS;
    gRxParam.frequency = RFH_PAIR_CHANNEL_A;
    gRxParam.whiteChannel = RFH_PAIR_CHANNEL_A;
    gRxParam.timeOut = RF_AUTO_DEMO_PAIR_RX_TIMEOUT_UNITS;
    g_demo_rx_ret = (uint8_t)RFIP_SetRx(&gRxParam);
    if(g_demo_rx_ret == SUCCESS)
    {
        g_demo_ack_rx_start_clock = RF_LinkClockNow();
        g_demo_ack_rx_start_cycles = demo_tx_cycle_now();
        g_demo_ack_rx_active = 1u;
    }
    else
    {
        g_demo_stat.ack_timeout++;
        g_demo_pair_tx_ticks_remaining = 0u;
    }
}

static void demo_send_pair_packet_from_isr(void)
{
    bStatus_t ret;
    uint8_t cmd;

    if((g_demo_config_ret != SUCCESS) ||
       (demo_pair_is_active() == 0u) ||
       (g_demo_pair_done_pending != 0u))
    {
        return;
    }
    if((g_demo_pause_tx != 0u) ||
       (g_demo_tx_busy != 0u) ||
       (g_demo_ack_rx_active != 0u) ||
       (g_demo_wait_ack_after_tx != 0u))
    {
        g_demo_stat.report_drop++;
        return;
    }
    if(g_demo_pair_tx_ticks_remaining != 0u)
    {
        g_demo_pair_tx_ticks_remaining--;
        return;
    }

    cmd = (g_demo_pair_state == RF_AUTO_PAIR_CONFIRM_WAIT) ?
          RFH_CMD_PAIR_CONFIRM : RFH_CMD_PAIR_OFFER;
    demo_fill_pair_packet(cmd);
    gTxParam.txDMA = (uint32_t)TxBuf;
    gTxParam.accessAddress = RFH_PAIR_ACCESS_ADDRESS;
    gTxParam.frequency = RFH_PAIR_CHANNEL_A;
    gTxParam.whiteChannel = RFH_PAIR_CHANNEL_A;
    g_demo_tx_busy = 1u;
    g_demo_pair_wait_rx_after_tx = 1u;
    g_demo_stat.tx_start++;
    g_demo_tx_start_ret = (uint8_t)RFIP_SetTxStart();
    ret = RFIP_SetTxParm(&gTxParam);
    g_demo_tx_parm_ret = (uint8_t)ret;
    if((g_demo_tx_start_ret != SUCCESS) || (ret != SUCCESS))
    {
        g_demo_tx_busy = 0u;
        g_demo_pair_wait_rx_after_tx = 0u;
        g_demo_stat.tx_fail++;
        g_demo_pair_tx_ticks_remaining = demo_pair_ticks_for_ms(RFH_PAIR_RESPONSE_BURST_MS);
        return;
    }

    g_demo_seq++;
    g_demo_pair_tx_ticks_remaining =
        demo_pair_ticks_for_ms((g_demo_pair_state == RF_AUTO_PAIR_CONFIRM_WAIT) ?
                               RFH_PAIR_CONFIRM_CYCLE_MS :
                               RFH_PAIR_DISCOVERY_CYCLE_MS);
}

static void demo_handle_pair_packet(void)
{
    const uint8_t *air = &RxBuf[2];
    rf_pair_packet_t packet;

    if((demo_pair_is_active() == 0u) ||
       (rf_pair_decode_air(air, RxBuf[1], &packet) == 0u))
    {
        g_demo_stat.ack_type_err++;
        return;
    }

    if(packet.session != g_demo_pair_session)
    {
        g_demo_stat.ack_type_err++;
        return;
    }

    if((g_demo_pair_state == RF_AUTO_PAIR_OFFERING) &&
       (packet.cmd == RFH_CMD_PAIR_ACCEPT) &&
       (packet.arg != 0u))
    {
        g_demo_pair_rx_id_hash = packet.arg;
        g_demo_pair_link_access_address = demo_make_pair_link_access_address(packet.arg);
        if(g_demo_pair_link_access_address == 0u)
        {
            g_pending_event_state_code = RF_LINK_STATE_PAIR_FAILED;
            g_demo_pair_done_pending = 1u;
            return;
        }
        g_demo_pair_done_confirm32 =
            rfh_pair_confirm32(g_demo_pair_session,
                               g_demo_pair_tx_id_hash,
                               g_demo_pair_rx_id_hash,
                               g_demo_pair_link_access_address);
        g_demo_pair_state = RF_AUTO_PAIR_CONFIRM_WAIT;
        g_demo_pair_confirm_deadline_clock =
            RF_LinkClockNow() +
            MS1_TO_SYSTEM_TIME(RFH_PAIR_CONFIRM_TIMEOUT_MS);
        g_demo_pair_tx_ticks_remaining = 0u;
        g_demo_stat.hop_event++;
        return;
    }

    if((g_demo_pair_state == RF_AUTO_PAIR_CONFIRM_WAIT) &&
       (packet.cmd == RFH_CMD_PAIR_DONE) &&
       (packet.arg == g_demo_pair_done_confirm32))
    {
        g_demo_pair_done_pending = 1u;
        g_demo_pause_tx = 1u;
        g_demo_stat.hop_event++;
        return;
    }

    if(packet.cmd == RFH_CMD_PAIR_REJECT)
    {
        g_pending_event_state_code = RF_LINK_STATE_PAIR_FAILED;
        g_demo_pair_done_pending = 1u;
        return;
    }

    g_demo_stat.ack_type_err++;
}

static void demo_finish_pairing(uint32_t now, uint8_t state_code, uint8_t emit_state_changed)
{
    g_demo_reconnecting = 0u;
    g_demo_pair_state = RF_AUTO_PAIR_IDLE;
    g_demo_pair_wait_rx_after_tx = 0u;
    g_demo_pair_done_pending = 0u;
    g_demo_pair_tx_ticks_remaining = 0u;
    g_demo_pair_deadline_clock = 0u;
    g_demo_pair_confirm_deadline_clock = 0u;
    g_demo_ack_rx_active = 0u;
    g_demo_wait_ack_after_tx = 0u;
    g_demo_tx_busy = 0u;
    g_demo_pause_tx = 0u;
    (void)demo_apply_access_address(g_demo_link_access_address);
    demo_enter_tx_unconnected(now);
    g_pending_event_state_code = state_code;
    if(emit_state_changed != 0u)
    {
        rfm_spi_bridge_emit_state_changed(0x02u);
    }
}

static void demo_service_pairing(uint32_t now)
{
    if(demo_pair_is_active() == 0u)
    {
        return;
    }

    if(g_demo_pair_done_pending != 0u)
    {
        if(g_pending_event_state_code == RF_LINK_STATE_PAIR_FAILED)
        {
            demo_finish_pairing(now, RF_LINK_STATE_PAIR_FAILED, 1u);
            return;
        }
        if(demo_prepare_bond(g_demo_pair_link_access_address,
                             g_demo_pair_rx_id_hash,
                             g_demo_pair_done_confirm32) != 0u)
        {
            /* PAIR_DONE only proves that both peers know the candidate.  The
             * first candidate CONNECT ACK is the commit point and the only
             * place where PairOk may be reported. */
            demo_finish_pairing(now, RF_LINK_STATE_CONNECTING, 1u);
        }
        else
        {
            demo_finish_pairing(now, RF_LINK_STATE_PAIR_FAILED, 1u);
        }
        return;
    }

    if((g_demo_pair_deadline_clock != 0u) &&
       ((int32_t)(now - g_demo_pair_deadline_clock) >= 0))
    {
        demo_finish_pairing(now, RF_LINK_STATE_PAIR_TIMEOUT, 1u);
        return;
    }

    if((g_demo_pair_state == RF_AUTO_PAIR_CONFIRM_WAIT) &&
       (g_demo_pair_confirm_deadline_clock != 0u) &&
       ((int32_t)(now - g_demo_pair_confirm_deadline_clock) >= 0))
    {
        /* A lost DONE must not leave the TX stuck forever on one candidate.
         * Start a fresh public pairing session while preserving the overall
         * 60 second deadline. */
        g_demo_pair_session = demo_make_pair_session();
        g_demo_pair_rx_id_hash = 0u;
        g_demo_pair_link_access_address = 0u;
        g_demo_pair_done_confirm32 = 0u;
        g_demo_pair_confirm_deadline_clock = 0u;
        g_demo_pair_state = RF_AUTO_PAIR_OFFERING;
        g_demo_pair_tx_ticks_remaining = 0u;
        g_demo_pair_wait_rx_after_tx = 0u;
        g_demo_ack_rx_active = 0u;
        g_demo_tx_busy = 0u;
        (void)demo_apply_access_address(RFH_PAIR_ACCESS_ADDRESS);
        demo_apply_channel(RFH_PAIR_CHANNEL_A);
    }
}

static void demo_log_stats(uint32_t now)
{
    uint32_t elapsed_ticks;
    unsigned long elapsed_ms;
#if 0
    uint32_t ack_fail;
#endif

    if((uint32_t)(now - g_demo_last_log_clock) < MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_LOG_PERIOD_MS))
    {
        return;
    }
    elapsed_ticks = now - g_demo_last_log_clock;
    g_demo_last_log_clock = now;
    elapsed_ms = (unsigned long)(((elapsed_ticks * (uint32_t)SYSTEM_TIME_MICROSEN) + 999u) / 1000u);
#if 0
    rfm_spi_bridge_diag_emit(elapsed_ms);

    ack_fail = g_demo_stat.ack_timeout +
               g_demo_stat.ack_crc_err +
               g_demo_stat.ack_type_err;
    PRINT("[RF][TX][%lums] c%u S%c p%c h%u>%u hz%u q%u irq%u/%u sc%u due%lu tx%lu fin%lu dr%lu aq%lu ack%lu/%lu to%lu ce%lu te%lu fail%lu miss%u H%lu b%u rx%u rt%u/%u/%u\r\n",
          elapsed_ms,
          (unsigned int)g_demo_config_ret,
          demo_tx_state_char(),
          demo_tx_connect_phase_char(),
          (unsigned int)g_demo_current_channel,
          (unsigned int)g_demo_target_channel,
          (unsigned int)g_demo_report_hz,
          (unsigned int)g_demo_last_quality,
          (unsigned int)g_demo_last_avg_irq_us,
          (unsigned int)g_demo_last_max_irq_us,
          (unsigned int)demo_channel_score_get(g_demo_current_channel),
          (unsigned long)g_demo_stat.report_due,
          (unsigned long)g_demo_stat.tx_start,
          (unsigned long)g_demo_stat.tx_finish,
          (unsigned long)g_demo_stat.report_drop,
          (unsigned long)g_demo_stat.ack_req,
          (unsigned long)g_demo_stat.ack_ok,
          (unsigned long)ack_fail,
          (unsigned long)g_demo_stat.ack_timeout,
          (unsigned long)g_demo_stat.ack_crc_err,
          (unsigned long)g_demo_stat.ack_type_err,
          (unsigned long)g_demo_stat.tx_fail,
          (unsigned int)g_demo_ack_miss_count,
          (unsigned long)g_demo_stat.hop_event,
          (unsigned int)g_demo_tx_busy,
          (unsigned int)g_demo_ack_rx_active,
          (unsigned int)g_demo_tx_start_ret,
          (unsigned int)g_demo_tx_parm_ret,
          (unsigned int)g_demo_rx_ret);
#else
    (void)elapsed_ms;
#endif

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

    demo_abort_radio();
    g_demo_pause_tx = 0u;
    g_demo_stat.tx_stuck++;
    g_demo_recovery_reason = 1u;
    /* A missing SYN completion must not restart a 20 ms discovery phase
     * every 10 ms: that prevents ever reaching the ACK listening phase. */
    if(g_demo_link_state == RF_AUTO_TX_UNCONNECTED) return;
    g_demo_reconnecting = (g_demo_link_state == RF_AUTO_TX_COMM);
    demo_enter_tx_unconnected(now);
    rfm_spi_bridge_emit_state_changed(0x02u);
}

#if (RF_AUTO_DEMO_TX_IN_ISR == 0u)
static void demo_try_send(void)
{
    bStatus_t ret;

    if((g_demo_config_ret != SUCCESS) ||
       (g_demo_input_off != 0u) ||
       (g_demo_has_bond == 0u))
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
    g_demo_tx_start_clock = RF_LinkClockNow();
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
    if((g_demo_config_ret != SUCCESS) || (g_demo_input_off != 0u))
    {
        return;
    }

    gRxParam.rxDMA = (uint32_t)RxBuf;
    gRxParam.timeOut = (g_demo_link_state == RF_AUTO_TX_UNCONNECTED) ?
                       RF_AUTO_DEMO_CONNECT_ACK_RX_TIMEOUT_UNITS :
                       RF_AUTO_DEMO_ACK_RX_TIMEOUT_UNITS;
    g_demo_rx_ret = (uint8_t)RFIP_SetRx(&gRxParam);
    if(g_demo_rx_ret == SUCCESS)
    {
        g_demo_ack_rx_start_clock = RF_LinkClockNow();
        g_demo_ack_rx_start_cycles = demo_tx_cycle_now();
        g_demo_ack_rx_active = 1u;
    }
    else
    {
        g_demo_stat.ack_timeout++;
        g_demo_force_ack_burst = 1u;
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

static void demo_ack_control_service(uint32_t now)
{
    if((g_demo_input_off != 0u) || (g_demo_config_ret != SUCCESS))
    {
        return;
    }

    if(g_demo_ack_clock_armed == 0u)
    {
        demo_arm_next_ack_clock(now);
        return;
    }

    if((int32_t)(now - g_demo_next_ack_clock) < 0)
    {
        return;
    }

    do
    {
        g_demo_next_ack_clock += MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_ACK_INTERVAL_MS);
    } while((int32_t)(now - g_demo_next_ack_clock) >= 0);

    g_demo_force_ack_burst = 1u;
}

static void demo_note_ack_timeout(void)
{
    uint32_t now = RF_LinkClockNow();
    if(g_demo_link_state == RF_AUTO_TX_UNCONNECTED) return;
    rfh_health_failed(&g_demo_health, now, MS1_TO_SYSTEM_TIME(10u));
    if(g_demo_hop_state != RF_AUTO_HOP_COMM) demo_schedule_hop_control_retry(now);
}

static void demo_check_ack_rx_stuck(uint32_t now)
{
    (void)now;
    if(g_demo_ack_rx_active == 0u)
    {
        return;
    }
    // The SDK timeout is in half-microseconds. Allow a short callback margin,
    // not a fixed 20ms input blackout after a 1.2ms receive reservation.
    const uint32_t budget_us=((uint32_t)gRxParam.timeOut+1u)/2u+250u;
    if((uint32_t)(demo_tx_cycle_now()-g_demo_ack_rx_start_cycles) <
       budget_us*(GetSysClock()/1000000u))
    {
        return;
    }

    demo_abort_radio();
    g_demo_pause_tx = 0u;
    g_demo_stat.ack_timeout++;
    demo_note_ack_timeout();
}

static void demo_service_connect_phase(uint32_t now)
{
    if((g_demo_link_state != RF_AUTO_TX_UNCONNECTED) ||
       (g_demo_has_bond == 0u) ||
       (demo_pair_is_active() != 0u))
    {
        return;
    }

    if(g_demo_connect_phase == RF_AUTO_CONNECT_SYN_TX)
    {
        if((uint32_t)(now - g_demo_connect_phase_clock) <
           MS1_TO_SYSTEM_TIME(RFH_CONNECT_WINDOW_MS))
        {
            return;
        }
        demo_abort_radio();
        g_demo_pause_tx = 0u;
        g_demo_connect_phase = RF_AUTO_CONNECT_SYN_ACK_RX;
        g_demo_connect_phase_clock = now;
        return;
    }

    if(g_demo_connect_phase == RF_AUTO_CONNECT_SYN_ACK_RX)
    {
        uint8_t listen_channel;

        if((uint32_t)(now - g_demo_connect_phase_clock) >=
           MS1_TO_SYSTEM_TIME(RFH_CONNECT_WINDOW_MS))
        {
            demo_abort_radio();
            g_demo_pause_tx = 0u;
            g_demo_connect_phase = RF_AUTO_CONNECT_SYN_TX;
            g_demo_connect_phase_clock = now;
            g_demo_connect_packet_stage = RFH_CONNECT_STAGE_SYN;
            return;
        }

        {
            if((uint32_t)(now - g_demo_discovery_switch_clock) >=
               MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_DISCOVERY_DWELL_MS))
            {
                g_demo_discovery_switch_clock = now;
                g_demo_discovery_side ^= 1u;
            }
            listen_channel = demo_discovery_channel(g_demo_discovery_side);
        }
        if((g_demo_ack_rx_active == 0u) &&
           (g_demo_tx_busy == 0u) &&
           (g_demo_current_channel != listen_channel))
        {
            demo_apply_channel(listen_channel);
        }
        if((g_demo_ack_rx_active == 0u) &&
           (g_demo_tx_busy == 0u) &&
           (g_demo_wait_ack_after_tx == 0u))
        {
            demo_arm_ack_rx();
        }
        return;
    }

    if((uint32_t)(now - g_demo_connect_phase_clock) >=
       MS1_TO_SYSTEM_TIME(RFH_CONNECT_FINAL_TX_MS))
    {
        demo_enter_tx_provisional(now, g_demo_current_channel);
    }
}

static void demo_service_rank_promotion(uint32_t now)
{
#if ((RF_AUTO_DEMO_AUTO_HOP_ENABLE == 0u) || (RF_AUTO_DEMO_RANK_PROMOTE_ENABLE == 0u))
    (void)now;
#else
    uint8_t target_channel = g_demo_current_channel;
    uint16_t reason_score = 0u;

    if((g_monitor_auto_hop_enabled == 0u) ||
       (g_demo_link_state != RF_AUTO_TX_COMM) ||
       (g_demo_hop_state != RF_AUTO_HOP_COMM) ||
       (g_demo_pair_state != RF_AUTO_PAIR_IDLE) ||
       (g_demo_input_off != 0u))
    {
        return;
    }
    if((uint32_t)(now - g_demo_channel_enter_clock) <
       MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_RANK_PROMOTE_MS))
    {
        return;
    }
    if((int32_t)(now - g_demo_hop_cooldown_until) < 0)
    {
        return;
    }
    if(demo_stable_window_blocks_hop() != 0u)
    {
        return;
    }
    if(demo_best_front_half_channel(g_demo_current_channel,
                                    now,
                                    &target_channel,
                                    &reason_score) == 0u)
    {
        return;
    }

    g_demo_channel_tried_mask = 0u;
    demo_begin_hop_prepare(now, target_channel, reason_score);
#endif
}

static void demo_service_hop(uint32_t now)
{
    if(g_demo_link_state != RF_AUTO_TX_COMM)
    {
        return;
    }

    /* Repeated PREPARE ACKs may revisit CONFIRM, but never restart recovery. */
    if(g_demo_hop_state != RF_AUTO_HOP_COMM && g_demo_hop_recovery_started &&
       (int32_t)(now - g_demo_hop_recovery_deadline) >= 0) {
        g_demo_reconnecting = 1u;
        g_demo_recovery_reason = 3u;
        demo_enter_tx_unconnected(now);
        return;
    }

    if((g_demo_hop_state != RF_AUTO_HOP_COMM) &&
       (g_demo_force_ack_burst == 0u) &&
       (g_demo_ack_burst_left == 0u) &&
       (g_demo_ack_rx_active == 0u) &&
       (g_demo_wait_ack_after_tx == 0u) &&
       ((int32_t)(now - g_demo_hop_cmd_retry_clock) >= 0))
    {
        demo_request_hop_control_now(now);
    }

    if((g_demo_hop_state == RF_AUTO_HOP_PREPARE_ACK_WAIT) &&
       ((int32_t)(now - g_demo_hop_deadline_clock) >= 0)) {
        demo_channel_quarantine(g_demo_target_channel, now);
        g_demo_reconnecting = 1u;
        g_demo_recovery_reason = 3u;
        demo_enter_tx_unconnected(now);
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
            g_demo_reconnecting = 1u;
            g_demo_recovery_reason = 3u;
            demo_enter_tx_unconnected(now);
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
    g_demo_tmr_epoch_cycles += g_demo_report_tmr_cycles;
    if(demo_pair_is_active() != 0u)
    {
        g_demo_stat.report_due++; g_demo_diag_due++;
        demo_send_pair_packet_from_isr();
        return;
    }
    if(g_demo_input_off != 0u)
    {
        return;
    }
    if(g_demo_has_bond == 0u)
    {
        return;
    }

    if(g_short_ack_wait && rfh_tx_guard_remaining(g_short_ack_launch,demo_tx_cycle_now(),g_demo_tx_guard_cycles)==0u) {
        g_short_ack_wait=0;g_demo_wait_ack_after_tx=0;demo_arm_ack_rx();
    }
    g_demo_stat.report_due++; g_demo_diag_due++;
#if (RF_AUTO_DEMO_TX_IN_ISR != 0u)
    if(g_demo_config_ret == SUCCESS)
    {
        bStatus_t ret;
        uint8_t request_ack = 0u;
        uint8_t ack_token = 0u;
        uint8_t ack_burst_left = 0u;

        if((g_demo_pause_tx != 0u) ||
           (g_demo_tx_busy != 0u) ||
           (g_demo_ack_rx_active != 0u) ||
           (g_demo_wait_ack_after_tx != 0u))
        {
            if(g_demo_ack_rx_active || g_demo_wait_ack_after_tx)++g_short_ack_slots;
            g_demo_stat.report_drop++; g_demo_diag_dropped++;
            return;
        }

        /* A delayed timer may be followed immediately by another pending
         * tick. Never truncate an in-flight short packet to catch up. Check
         * before consuming commands/edges. Only a <=64 us tail may be waited
         * out below, after packet preparation in separate DMA storage. */
        /* Drain the preceding short packet's delayed TX_FINISH before giving
         * a full control packet callback ownership. Otherwise that old finish
         * can switch to ACK RX while the new request is still transmitting. */
        if(g_demo_tx_launch_valid &&
           (g_demo_link_state == RF_AUTO_TX_UNCONNECTED ||
            demo_active_hop_cmd() != RFH_CMD_NONE) &&
           rfh_tx_guard_remaining(g_demo_tx_launch_cycles, demo_tx_cycle_now(),
                                  g_demo_tx_control_guard_cycles)) {
            ++g_short_control_slots;
            g_demo_stat.report_drop++; g_demo_diag_dropped++;
            return;
        }
        if(g_demo_tx_launch_valid &&
           rfh_tx_guard_remaining(g_demo_tx_launch_cycles, demo_tx_cycle_now(),
                                  g_demo_tx_guard_cycles) > g_demo_tx_wait_limit) {
            g_demo_stat.report_drop++; g_demo_diag_dropped++;
            return;
        }

        if(g_demo_link_state == RF_AUTO_TX_UNCONNECTED)
        {
            if(g_demo_connect_phase == RF_AUTO_CONNECT_SYN_ACK_RX)
            {
                return;
            }
            request_ack = 1u;
            ack_token = 0u;
            ack_burst_left = 0u;
            g_demo_connect_packet_stage =
                (g_demo_connect_phase == RF_AUTO_CONNECT_FINAL_TX) ?
                RFH_CONNECT_STAGE_FINAL :
                RFH_CONNECT_STAGE_SYN;
            g_demo_wait_ack_after_tx =
                (g_demo_connect_phase == RF_AUTO_CONNECT_FINAL_TX) ? 1u : 0u;
            g_demo_force_ack_burst = 0u;
        }
        else if(g_demo_ack_burst_left != 0u)
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
        }

        /* One request followed by a quiet receive reservation. With three
         * full-length packets, TX_FINISH/busy can skip a 125 us timer slot,
         * but RX used remaining_slots to predict the end of that burst.
         * Its ACK then collided with a later request. Retain the existing
         * wire field as reserved silent slots (>=250 us at 4K/8K), and open
         * RX immediately after this sole packet. Failed requests still get
         * two independently tokened probes, 10 ms apart. */
        if(request_ack && g_demo_link_state != RF_AUTO_TX_UNCONNECTED)
            ack_burst_left = rfh_ack_guard_slots(g_demo_report_hz);
        demo_fill_tx_packet(request_ack, ack_token, ack_burst_left);
        g_demo_tx_busy = rfh_is_short(TxBuf[1]) ? 0u : 1u;
        /* The busy watchdog owns control packets only. Short DATA has the
         * cycle guard below; do not update its unused watchdog clock at 8K. */
        if(g_demo_tx_busy)g_demo_tx_start_clock = RF_LinkClockNow();
        g_demo_stat.tx_start++; g_demo_diag_started++;
        g_demo_tx_dma_slot ^= 1u;
        memcpy(TxDmaBuf[g_demo_tx_dma_slot], TxBuf, sizeof(TxBuf));
        gTxParam.txDMA = (uint32_t)TxDmaBuf[g_demo_tx_dma_slot];
        while(g_demo_tx_launch_valid &&
              rfh_tx_guard_remaining(g_demo_tx_launch_cycles, demo_tx_cycle_now(),
                                     g_demo_tx_guard_cycles)) { }
        g_demo_tx_launch_cycles = demo_tx_cycle_now();
        g_demo_tx_launch_valid = 1u;
        /* Guard belongs to the packet just launched, not the next packet. */
        if(rfh_is_short(TxBuf[1]))g_demo_tx_guard_cycles =
            TxBuf[1]==RFH_SHORT_LEN ? g_demo_short_guard_cycles : g_demo_aux_guard_cycles;
        g_demo_tx_start_ret = (uint8_t)RFIP_SetTxStart();
        ret = RFIP_SetTxParm(&gTxParam);
        g_demo_tx_parm_ret = (uint8_t)ret;
        if((g_demo_tx_start_ret != SUCCESS) || (ret != SUCCESS))
        {
            g_demo_tx_busy = 0u;
            g_demo_wait_ack_after_tx = 0u;
            g_demo_ack_rx_active = 0u;
            g_demo_force_ack_burst = 1u;
            g_demo_stat.tx_fail++;
        }
        else if(rfh_is_short(TxBuf[1])) {
            ++g_short_packet_count[TxBuf[1]==5u ? 0u : 1u];
            /* Use the immutable packet handed to RF DMA, not the newest
             * SPI edge which may have arrived during packet preparation. */
            short_note_launch(TxDmaBuf[g_demo_tx_dma_slot][2u+RFH_DATA_OFFSET+2u]>>2);
            if(g_short_aux_sent==2u)g_short_anchor_serial=g_aux_tx.serial*4u+g_aux_tx.pass;
            else if(g_short_aux_sent)rfh_aux_commit(&g_aux_tx);
            if(request_ack){g_short_ack_launch=g_demo_tx_launch_cycles;g_short_ack_wait=1;}
        }
        if(g_demo_tx_start_ret==SUCCESS && ret==SUCCESS && TxBuf[1]==12u)++g_short_packet_count[2];
        g_demo_seq++;g_short_wire_seq++;
    }
#else
    if(g_demo_pending_reports < RF_AUTO_DEMO_PENDING_MAX)
    {
        g_demo_pending_reports++;
    }
    else
    {
        g_demo_stat.report_drop++; g_demo_diag_dropped++;
    }
#endif
}

__HIGH_CODE
void RF_ProcessCallBack(rfRole_States_t sta, uint8_t id)
{
    (void)id;
    if(g_demo_pause_tx) return;
    if(!g_demo_tx_busy) sta &= ~RF_STATE_TX_FINISH;
    if(!g_demo_ack_rx_active) sta &= ~(RF_STATE_RX | RF_STATE_RX_CRCERR | RF_STATE_TIMEOUT);

    if(sta & RF_STATE_TX_FINISH)
    {
        g_demo_tx_busy = 0u;
        g_demo_stat.tx_finish++;
        if((demo_pair_is_active() != 0u) && (g_demo_pair_wait_rx_after_tx != 0u))
        {
            g_demo_pair_wait_rx_after_tx = 0u;
            demo_arm_pair_rx();
            return;
        }
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
        if(demo_pair_is_active() != 0u)
        {
            demo_handle_pair_packet();
            return;
        }
        demo_handle_ack_packet();
    }
    if(sta & RF_STATE_RX_CRCERR)
    {
        g_demo_tx_busy = 0u;
        g_demo_ack_rx_active = 0u;
        g_demo_stat.ack_crc_err++;
        if(demo_pair_is_active() != 0u)
        {
            g_demo_pair_tx_ticks_remaining = 0u;
            return;
        }
        demo_note_ack_timeout();
    }
    if(sta & RF_STATE_TIMEOUT)
    {
        g_demo_tx_busy = 0u;
        g_demo_ack_rx_active = 0u;
        g_demo_stat.ack_timeout++;
        if(demo_pair_is_active() != 0u)
        {
            g_demo_pair_tx_ticks_remaining = 0u;
            return;
        }
        demo_note_ack_timeout();
    }
}

static void demo_snapshot_tx_diagnostic(uint32_t now)
{
    uint32_t elapsed = now - g_demo_diag_clock;
    uint32_t due, started, dropped;
    if(elapsed < MS1_TO_SYSTEM_TIME(1000u)) return;
    due = g_demo_diag_due - g_demo_diag_last_due;
    started = g_demo_diag_started - g_demo_diag_last_started;
    dropped = g_demo_diag_dropped - g_demo_diag_last_dropped;
    g_demo_diag_window_10ms = (elapsed <= MS1_TO_SYSTEM_TIME(2550u) &&
                              due <= 65535u && started <= 65535u && dropped <= 65535u) ?
        (uint8_t)(elapsed / MS1_TO_SYSTEM_TIME(10u)) : 0u;
    g_demo_diag_due_window = (uint16_t)due;
    g_demo_diag_started_window = (uint16_t)started;
    g_demo_diag_dropped_window = (uint16_t)dropped;
    g_demo_diag_last_due = g_demo_diag_due;
    g_demo_diag_last_started = g_demo_diag_started;
    g_demo_diag_last_dropped = g_demo_diag_dropped;
    g_demo_diag_clock = now;
    g_demo_diag_pending = 1u;
}

static uint8_t demo_housekeeping_due(uint32_t now)
{
#if (RF_AUTO_DEMO_TX_IN_ISR != 0u)
    if(g_demo_housekeeping_valid && now == g_demo_housekeeping_clock) return 0u;
    g_demo_housekeeping_clock = now;
    g_demo_housekeeping_valid = 1u;
#else
    (void)now;
#endif
    return 1u;
}

void RF_TxMainLoopProcess(void)
{
    if(!g_aux_tx.active)demo_prepare_aux();
    uint32_t now = RF_LinkClockNow();

    /* All housekeeping deadlines use 625 us ticks. Re-running the whole
     * IRQ-masked state machine many times within one tick only delays TMR0
     * and creates late/back-to-back 8K slots. DATA and ACK RX stay in ISRs. */
    if(!demo_housekeeping_due(now)) return;

    /* Battery changes and the five-second refresh do not belong in the RF
     * launch ISR. Read the current snapshot even if key state did not change. */
    uint8_t battery_input[RFM_RF_INPUT_PAYLOAD_LEN];
    if(rfm_spi_port_peek_latest_input(battery_input,sizeof(battery_input)))
        demo_note_battery_status(battery_input);

    if(demo_pair_is_active() != 0u)
    {
        demo_service_pairing(now);
        demo_log_stats(now);
        return;
    }

    uint32_t irq_status;
    SYS_DisableAllIrq(&irq_status);
    now = RF_LinkClockNow(); /* ACK ISR may have advanced last_ack before the lock. */
    demo_snapshot_tx_diagnostic(now);
    demo_check_tx_stuck(now);
    if(g_demo_input_off == 0u && g_demo_link_state != RF_AUTO_TX_UNCONNECTED) {
        uint8_t old_misses = g_demo_health.misses;
        if(rfh_health_poll(&g_demo_health, now, MS1_TO_SYSTEM_TIME(100u), MS1_TO_SYSTEM_TIME(500u))) {
            g_demo_recovery_reason = 2u;
            g_demo_reconnecting = (g_demo_link_state == RF_AUTO_TX_COMM);
            demo_enter_tx_unconnected(now);
            rfm_spi_bridge_emit_state_changed(0x02u);
        } else {
            g_demo_ack_miss_count = g_demo_health.misses;
            if(g_demo_health.misses > old_misses) demo_link_quality_note_ack_timeout(now);
            if(rfh_health_retry(&g_demo_health, now)) g_demo_force_ack_burst = 1u;
        }
    }
#if (RF_AUTO_DEMO_TX_IN_ISR == 0u)
    demo_try_send();
#endif
    demo_check_ack_rx_stuck(now);
    demo_service_connect_phase(now);
    demo_score_windows_service(now);
    demo_link_quality_service(now);
    demo_service_link(now);
    demo_ack_control_service(now);
    demo_service_hop(now);
    demo_service_rank_promotion(now);
    demo_service_manual_hop();
    SYS_RecoverIrq(irq_status);
    demo_log_stats(now);
}

bool RF_SPI_WriteTrace(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    uint32_t irq_status;
    if(!g_short_measure || cmd!=0x09u || !payload || len!=20u || payload[19]!=1u)return false;
    ++g_source_spi_received;
    uint16_t event=rfh_get_u16(payload+1);uint8_t tag=event&63u;
    if(!tag)return false;
    SYS_DisableAllIrq(&irq_status);
    g_sync_air_count|=8u;
    relative_tx_t *r=&g_relative_tx[tag];
    if(r->tag==tag && r->spi==payload[0]) {
        if(!r->end_valid)r->end_valid=rfm_spi_port_input_end(tag,payload[0],&r->end);
        g_sync_air_count|=16u;
        if(!r->end_valid)g_sync_air_count|=64u;
        r->event=event;
        for(unsigned i=0;i<4;i++)r->stage[i]=rfh_get_u32(payload+3+4*i);
        r->source=1;
    } else g_sync_air_count|=128u;
    SYS_RecoverIrq(irq_status);return true;
}

void RF_SPI_InputComplete(uint8_t tag,uint8_t seq,uint32_t cycles)
{
    if(!g_short_measure || !tag || tag>=64u)return;
    uint32_t lock;SYS_DisableAllIrq(&lock);
    relative_tx_t *r=&g_relative_tx[tag];
    // The parser can finish before NSS rises. Complete that exact pending
    // boundary here, instead of depending on an 8ms lookup when metadata arrives.
    if(r->tag==tag && r->spi==seq && !r->end_valid && !r->sent &&
       (uint32_t)(cycles-r->born)<=GetSysClock()/1000u) {
        r->end=cycles;r->end_valid=1;
    }
    SYS_RecoverIrq(lock);
}

bool RF_SPI_FastWriteInput(const uint8_t *payload, uint8_t len)
{
    uint32_t irq_status;

    if((payload == 0) || (len != RFM_RF_INPUT_PAYLOAD_LEN))
    {
        return false;
    }

    SYS_DisableAllIrq(&irq_status);
    demo_store_last_payload(payload, tx_now_cycles());
    /* A normal 10-byte input cannot contain the old 20-byte sync extension. */
    SYS_RecoverIrq(irq_status);
    demo_note_battery_status(payload);
    return true;
}

__HIGH_CODE
void RF_SPI_RecordInputEdge(const uint8_t *payload)
{
    /* Called under the SPI snapshot publication lock. Repeated samples do
     * no RF state publication, battery handling or link-clock accounting. */
    if(!g_short_measure)return;
    if((payload[4]>>2)!=g_relative_tag)short_note_input(payload);
    if(g_relative_tag) {
        relative_tx_t *r=&g_relative_tx[g_relative_tag];
        /* A parser that beat NSS completes the boundary on the next SPI
         * sample, before the timestamp cache expires or aux becomes idle. */
        if(!r->end_valid && !r->sent)
            r->end_valid=rfm_spi_port_input_end(r->tag,r->spi,&r->end);
    }
}

static bool demo_set_report_rate(uint16_t hz)
{
    uint32_t now = RF_LinkClockNow();
    uint8_t was_off = g_demo_input_off;

#if 0
    PRINT("[RF][CTRL] SET_RATE hz:%u was_off:%u state:%u bond:%u\r\n",
          (unsigned int)hz,
          (unsigned int)was_off,
          (unsigned int)g_demo_link_state,
          (unsigned int)g_demo_has_bond);
#endif

    if(demo_rate_valid(hz) == 0u)
    {
#if 0
        PRINT("[RF][CTRL] SET_RATE reject invalid:%u\r\n",
              (unsigned int)hz);
#endif
        return false;
    }

    if(hz == 0u)
    {
        g_demo_input_off = 1u;
        g_demo_report_hz = 0u;
        g_demo_rate_update_pending = 0u;
        g_demo_ack_clock_armed = 0u;
        g_demo_ack_burst_left = 0u;
        g_demo_force_ack_burst = 0u;
        g_demo_ack_rx_active = 0u;
        g_demo_wait_ack_after_tx = 0u;
        g_demo_tx_busy = 0u;
        g_demo_hop_state = RF_AUTO_HOP_COMM;
        g_demo_link_state = RF_AUTO_TX_UNCONNECTED;
        g_demo_old_channel = g_demo_current_channel;
        g_demo_target_channel = g_demo_current_channel;
        demo_reconfigure_report_timer(0u);
        (void)RFRole_Stop();
        g_pending_event_state_code = RF_LINK_STATE_IDLE;
#if 0
        PRINT("[RF][CTRL] RF input off, radio stopped\r\n");
#endif
        return true;
    }

    g_demo_input_off = 0u;
    g_demo_report_hz = hz;
    g_demo_rate_code = rfh_rate_code_from_hz(hz);
    demo_reconfigure_report_timer(hz);
    if(was_off != 0u)
    {
        g_demo_reconnecting = 0u;
        demo_enter_tx_unconnected(now);
    }
    else if(g_demo_link_state == RF_AUTO_TX_COMM)
    {
        g_demo_rate_update_seq++;
        if(g_demo_rate_update_seq == 0u)
        {
            g_demo_rate_update_seq = 1u;
        }
        g_demo_rate_update_pending = 1u;
        g_demo_force_ack_burst = 1u;
        g_pending_event_state_code = RF_LINK_STATE_CONNECTED;
    }
    else
    {
        g_demo_rate_update_pending = 0u;
        g_demo_force_ack_burst = 1u;
        g_pending_event_state_code = (g_demo_has_bond != 0u) ?
                                     RF_LINK_STATE_CONNECTING :
                                     RF_LINK_STATE_IDLE;
    }
#if 0
    PRINT("[RF][CTRL] RF wake/rate applied hz:%u code:%u was_off:%u state:%u\r\n",
          (unsigned int)g_demo_report_hz,
          (unsigned int)g_demo_rate_code,
          (unsigned int)was_off,
          (unsigned int)g_demo_link_state);
#endif
    return true;
}

bool RF_SetReportRateHz(uint16_t hz)
{
    uint32_t irq_status;
    bool result;
    SYS_DisableAllIrq(&irq_status);
    result = demo_set_report_rate(hz);
    SYS_RecoverIrq(irq_status);
    return result;
}

bool RF_PrepareSleep(void)
{
    return RF_SetReportRateHz(0u);
}

uint16_t RF_GetReportRateHz(void)
{
    return g_demo_report_hz;
}

bool RF_StartPairing(void)
{
    uint32_t now = RF_LinkClockNow();

    if(g_demo_config_ret != SUCCESS)
    {
        return false;
    }
    if(demo_pair_is_active() != 0u)
    {
        g_pending_event_state_code = RF_LINK_STATE_PAIRING;
        return true;
    }
    if(g_demo_pair_commit_pending != 0u)
    {
        /* An explicit new pairing request supersedes an uncommitted candidate,
         * but never destroys the last committed bond. */
        if(demo_abort_prepared_bond() == 0u)
        {
            g_pending_event_state_code = RF_LINK_STATE_PAIR_FAILED;
            return false;
        }
    }
    if(g_demo_report_hz == 0u)
    {
#if (RFM_COLD_BOOT_WAIT_HOST_RATE != 0u)
        return false;
#else
        g_demo_report_hz = RF_AUTO_DEMO_REPORT_HZ;
        g_demo_rate_code = RF_AUTO_DEMO_RATE_CODE;
        demo_reconfigure_report_timer(g_demo_report_hz);
#endif
    }

    g_demo_input_off = 0u;
    g_demo_reconnecting = 0u;
    g_demo_pair_session = demo_make_pair_session();
    g_demo_pair_tx_id_hash = g_demo_local_id_hash;
    g_demo_pair_rx_id_hash = 0u;
    g_demo_pair_link_access_address = 0u;
    g_demo_pair_done_confirm32 = 0u;
    g_demo_pair_done_pending = 0u;
    g_demo_pair_wait_rx_after_tx = 0u;
    g_demo_pair_tx_ticks_remaining = 0u;
    g_demo_pair_started_clock = now;
    g_demo_pair_deadline_clock =
        now + MS1_TO_SYSTEM_TIME(RFH_PAIR_WINDOW_MS);
    g_demo_pair_confirm_deadline_clock = 0u;
    g_demo_pair_state = RF_AUTO_PAIR_OFFERING;
    g_demo_link_state = RF_AUTO_TX_UNCONNECTED;
    g_demo_hop_state = RF_AUTO_HOP_COMM;
    g_demo_ack_clock_armed = 0u;
    g_demo_ack_burst_left = 0u;
    g_demo_force_ack_burst = 0u;
    g_demo_ack_rx_active = 0u;
    g_demo_wait_ack_after_tx = 0u;
    g_demo_tx_busy = 0u;
    (void)demo_apply_access_address(RFH_PAIR_ACCESS_ADDRESS);
    demo_apply_channel(RFH_PAIR_CHANNEL_A);
    g_pending_event_state_code = RF_LINK_STATE_PAIRING;
    return true;
}

bool RF_StopPairing(void)
{
    if(demo_pair_is_active() != 0u)
    {
        demo_finish_pairing(RF_LinkClockNow(),
                            (g_demo_has_bond != 0u) ?
                            RF_LINK_STATE_CONNECTING :
                            RF_LINK_STATE_IDLE,
                            0u);
    }
    else
    {
        g_pending_event_state_code = RF_GetLinkStateCode();
    }
    return true;
}

bool RF_Unbind(void)
{
    uint32_t now = RF_LinkClockNow();

    if(demo_pair_is_active() != 0u)
    {
        demo_finish_pairing(now,
                            (g_demo_has_bond != 0u) ?
                            RF_LINK_STATE_CONNECTING :
                            RF_LINK_STATE_IDLE,
                            0u);
    }
    if(demo_clear_bond() == 0u)
    {
        g_pending_event_state_code = RF_LINK_STATE_PAIR_FAILED;
        return false;
    }
    g_demo_reconnecting = 0u;
    (void)demo_apply_access_address(g_demo_link_access_address);
    demo_enter_tx_unconnected(now);
    g_pending_event_state_code = RF_LINK_STATE_IDLE;
    return true;
}

uint8_t RF_GetLinkStateCode(void)
{
    if((g_demo_config_ret != SUCCESS) || (g_demo_input_off != 0u))
    {
        return RF_LINK_STATE_IDLE;
    }
    if(demo_pair_is_active() != 0u)
    {
        return RF_LINK_STATE_PAIRING;
    }
    if(g_demo_link_state == RF_AUTO_TX_UNCONNECTED)
    {
        if(g_demo_has_bond == 0u)
        {
            return RF_LINK_STATE_IDLE;
        }
        return (g_demo_reconnecting != 0u) ?
               RF_LINK_STATE_RECONNECTING :
               RF_LINK_STATE_CONNECTING;
    }
    if(g_demo_link_state == RF_AUTO_TX_PROVISIONAL)
    {
        return RF_LINK_STATE_CONNECTING;
    }
    if(!RF_IsConnected()) return RF_LINK_STATE_RECONNECTING;
    return (g_demo_hop_state == RF_AUTO_HOP_RECOVERY_DUAL) ?
           RF_LINK_STATE_RECONNECTING : RF_LINK_STATE_CONNECTED;
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
    return ((g_demo_config_ret == SUCCESS) &&
            (g_demo_input_off == 0u) &&
            (demo_pair_is_active() == 0u) &&
            (g_demo_link_state == RF_AUTO_TX_COMM) &&
            (g_demo_hop_state != RF_AUTO_HOP_RECOVERY_DUAL) &&
            ((uint32_t)(RF_LinkClockNow() - g_demo_health.last_ack) < MS1_TO_SYSTEM_TIME(500u))) ? 1u : 0u;
}

uint8_t RF_GetRecoveryReason(void) { return g_demo_recovery_reason; }

uint8_t RF_HasBond(void)
{
    return g_demo_has_bond;
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
    conf.TxPower = RF_AUTO_DEMO_TX_POWER;
    conf.rfProcessCB = RF_ProcessCallBack;
    conf.processMask = RF_STATE_RX | RF_STATE_RX_CRCERR |
                       RF_STATE_TX_FINISH | RF_STATE_TIMEOUT;
    g_demo_config_ret = (uint8_t)RFRole_BasicInit(&conf);

    g_demo_local_id_hash = demo_make_local_id_hash();
    demo_load_bond();

    memset(&gParm, 0, sizeof(gParm));
    gParm.accessAddress = g_demo_link_access_address;
    gParm.crcInit = RF_LINK_CRC_INIT;
    gParm.frequency = demo_discovery_channel(0u);
    gParm.properties = RF_AUTO_DEMO_PHY_PROPS | RF_AUTO_DEMO_ACK_BIT;
    gParm.rxMaxLen = RF_AUTO_DEMO_PACKET_LEN;
    gParm.sendTime = RFH_TX_SEND_TIME_UNITS;
    RFRole_SetParam(&gParm);

    memset(&gTxParam, 0, sizeof(gTxParam));
    gTxParam.accessAddress = gParm.accessAddress;
    gTxParam.crcInit = gParm.crcInit;
    gTxParam.frequency = gParm.frequency;
    gTxParam.properties = gParm.properties;
    gTxParam.whiteChannel = gParm.frequency;
    gTxParam.sendTime = RF_AUTO_DEMO_TX_SEND_TIME_UNITS;
    gTxParam.sendCount = 1u;
    gTxParam.txDMA = (uint32_t)TxBuf;

    memset(&gRxParam, 0, sizeof(gRxParam));
    gRxParam.accessAddress = gParm.accessAddress;
    gRxParam.crcInit = gParm.crcInit;
    gRxParam.frequency = gParm.frequency;
    gRxParam.properties = RF_AUTO_DEMO_PHY_PROPS;
    gRxParam.rxDMA = (uint32_t)RxBuf;
    gRxParam.whiteChannel = gParm.frequency;
    gRxParam.rxMaxLen = RF_AUTO_DEMO_PACKET_LEN;
    gRxParam.timeOut = RF_AUTO_DEMO_ACK_RX_TIMEOUT_UNITS;

    g_pending_event_state_code = (g_demo_has_bond != 0u) ?
                                 RF_LINK_STATE_CONNECTING :
                                 RF_LINK_STATE_IDLE;
#if (RF_AUTO_DEMO_TX_IN_ISR == 0u)
    g_demo_pending_reports = 0u;
#endif
    g_demo_last_log_clock = RF_LinkClockNow();

    /* 2M short DATA: 11 PHY overhead + 7 payload bytes = 72 us,
     * plus documented RX->TX settle (sendTime/2 + 24 us) and 12 us margin.
     * Full packets retain TX_FINISH ownership. Preserve a conservative
     * guard for an explicitly selected 1M PHY as well. */
    {
        uint32_t cycles_per_us = GetSysClock() / 1000000u;
        uint32_t byte_us = (RF_AUTO_DEMO_PHY_PROPS == LLE_MODE_PHY_2M) ? 4u : 8u;
        g_demo_short_guard_cycles = cycles_per_us *
            ((RFH_SHORT_LEN + 11u) * byte_us + 24u +
             (RF_AUTO_DEMO_TX_SEND_TIME_UNITS + 1u) / 2u + 12u);
        g_demo_aux_guard_cycles = cycles_per_us *
            ((RFH_AUX_LEN + 11u) * byte_us + 24u +
             (RF_AUTO_DEMO_TX_SEND_TIME_UNITS + 1u) / 2u + 12u);
        g_demo_tx_guard_cycles = cycles_per_us *
            ((RFH_INPUT_AIR_PACKET_LEN + 11u) * byte_us + 24u +
             (RF_AUTO_DEMO_TX_SEND_TIME_UNITS + 1u) / 2u + 12u);
        g_demo_tx_wait_limit = cycles_per_us * 64u;
        g_demo_tx_control_guard_cycles = cycles_per_us * 300u;
        g_demo_tx_launch_valid = 0u;
    }
    tick_per_evt = 0u;
    if(g_demo_report_hz != 0u)
    {
        tick_per_evt = GetSysClock() / g_demo_report_hz;
        if(tick_per_evt == 0u)
        {
            tick_per_evt = 1u;
        }
        g_demo_report_tmr_cycles = tick_per_evt;
        g_demo_tmr_epoch_cycles = 0u;
        g_demo_last_payload_tmr_valid = 0u;
        TMR0_TimerInit(tick_per_evt);
        TMR0_ClearITFlag(TMR0_3_IT_CYC_END);
        TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    }
    PFIC_SetPriority(TMR0_IRQn, 0x80);
    PFIC_EnableIRQ(TMR0_IRQn);

    g_demo_current_channel = gParm.frequency;
    g_demo_old_channel = demo_discovery_channel(0u);
    g_demo_target_channel = demo_discovery_channel(0u);
    memset(g_demo_last_payload, 0, sizeof(g_demo_last_payload));
    g_demo_have_payload = 0u;
    g_demo_last_payload_tmr = 0u;
    g_demo_last_payload_tmr_valid = 0u;
    g_trace_head = g_trace_tail = g_trace_last_valid = 0u;
    g_monitor_latency_pending = 0u;
    g_monitor_latency_input_seq = 0u;
    g_monitor_latency_key_mask = 0u;
    g_monitor_latency_sample_tick_us = 0u;
    g_monitor_sync_echo_pending = 0u;
    g_monitor_sync_echo_seq = 0u;
    g_monitor_sync_echo_rx_tick_us = 0u;
    g_monitor_sync_echo_tx_tick_us = 0u;
    g_demo_input_off = RFM_COLD_BOOT_INITIAL_INPUT_OFF;
    g_demo_last_avg_irq_us = 0u;
    g_demo_last_max_irq_us = 0u;
    g_demo_irq_bad_window_count = 0u;
    g_demo_link_state = RF_AUTO_TX_UNCONNECTED;
    g_demo_pair_state = RF_AUTO_PAIR_IDLE;
    g_demo_pair_done_pending = 0u;
    g_demo_pair_wait_rx_after_tx = 0u;
    g_demo_discovery_side = 0u;
    g_demo_discovery_switch_clock = RF_LinkClockNow();
    g_demo_force_ack_burst = 1u;
    demo_channel_scores_init();
    g_demo_channel_tried_mask = 0u;
    demo_channel_mark_tried(RF_AUTO_DEMO_INITIAL_CHANNEL);
    demo_arm_next_ack_clock(RF_LinkClockNow());
    g_demo_hop_cooldown_until = RF_LinkClockNow() +
                                MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_COOLDOWN_MS);
    g_demo_channel_enter_clock = RF_LinkClockNow();

}

__HIGH_CODE
static void short_note_input(const uint8_t *p) {
    uint8_t tag=g_short_measure ? p[4]>>2 : 0;
    if(tag==g_relative_tag)return;
    g_relative_tag=tag;if(!tag)return;
    relative_tx_t *r=&g_relative_tx[tag];
    if(r->tag && !r->sent)++g_relative_overflow;
    memset(r,0,sizeof(*r));r->tag=tag;r->spi=p[0];
    r->born=demo_tx_cycle_now();
    r->end_valid=rfm_spi_port_input_end(tag,p[0],&r->end);
    r->mask=(uint32_t)p[2]|((uint32_t)p[3]<<8)|((uint32_t)(p[4]&3u)<<16);
}
static void short_note_launch(uint8_t tag) {
    if(!g_short_measure || !tag || tag>=64u)return;
    relative_tx_t *r=&g_relative_tx[tag];
    if(r->tag!=tag || r->sent)return;
    if(r->count<6u) {
        r->seq[r->count]=g_short_wire_seq;r->launch[r->count]=g_demo_tx_launch_cycles;r->count++;
    }
}
static uint8_t short_prepare_trace(void) {
    /* Diagnostic work must not starve the main-loop SPI consumer. In
     * particular, an idle capture must not copy all 63 empty/sent records
     * with interrupts masked on every pass through the loop. */
    for(unsigned n=0;n<4;n++) {
        g_relative_scan=(g_relative_scan%63u)+1u;
        relative_tx_t snapshot;uint32_t lock,generation;
        SYS_DisableAllIrq(&lock);
        const relative_tx_t *candidate=&g_relative_tx[g_relative_scan];
        if(!candidate->tag || !candidate->source || candidate->sent || !candidate->count ||
           (candidate->count<6u && candidate->tag==g_relative_tag)) {
            SYS_RecoverIrq(lock);continue;
        }
        /* Parsing can beat the NSS rising edge. The IRQ only caches the
         * boundary; complete its matching record here, not inside the IRQ. */
        if(!candidate->end_valid) {
            relative_tx_t *pending=&g_relative_tx[g_relative_scan];
            pending->end_valid=rfm_spi_port_input_end(pending->tag,pending->spi,&pending->end);
        }
        snapshot=*candidate;generation=g_demo_radio_generation;SYS_RecoverIrq(lock);
        relative_tx_t *r=&snapshot;
        if(!r->tag || !r->source || r->sent || !r->count ||
           (r->count<6u && r->tag==g_relative_tag))continue;
        // Give the already pending NSS interrupt a bounded chance to freeze
        // its boundary before publishing a permanently incomplete record.
        if(!r->end_valid && (uint32_t)(demo_tx_cycle_now()-r->born)<GetSysClock()/1000u)continue;
        /* RX will no longer join a record older than one second. Discard it
         * here instead of spending the short-packet side channel on stale data. */
        if((uint32_t)(demo_tx_cycle_now()-r->born)>GetSysClock()) {
            SYS_DisableAllIrq(&lock);
            relative_tx_t *stale=&g_relative_tx[g_relative_scan];
            if(stale->event==r->event && stale->spi==r->spi && !stale->sent) {
                stale->sent=1;++g_relative_overflow;
            }
            SYS_RecoverIrq(lock);continue;
        }
        uint8_t p[54]={0};rfh_put_u16(p,r->event);p[52]=r->end_valid ? 0u : 1u;
        p[2]=(uint8_t)r->mask;p[3]=(uint8_t)(r->mask>>8);p[4]=(uint8_t)(r->mask>>16);p[5]=r->count;
        for(unsigned i=0;i<4;i++)rfh_put_u32(p+6+4*i,r->stage[i]);
        for(unsigned i=0;i<r->count;i++) {
            uint8_t *a=p+22+5*i;uint32_t us=(r->launch[i]-r->end)/(GetSysClock()/1000000u);
            /* A launch before the captured NSS end, or a stale record, is not a duration. */
            if(!r->end_valid || us>1000000u)us=0xffffffu;
            rfh_put_u16(a,r->seq[i]);a[2]=(uint8_t)us;a[3]=(uint8_t)(us>>8);a[4]=(uint8_t)(us>>16);
        }
        rfh_aux_tx_t next=g_aux_tx;rfh_aux_begin(&next,RFH_AUX_TRACE,p,54);
        SYS_DisableAllIrq(&lock);
        relative_tx_t *live=&g_relative_tx[g_relative_scan];
        if(generation==g_demo_radio_generation && g_short_measure && !g_aux_tx.active && live->event==r->event && live->spi==r->spi) {
            g_aux_tx=next;live->sent=1;g_sync_air_count|=32u;SYS_RecoverIrq(lock);return 1;
        }
        SYS_RecoverIrq(lock);return 0;
    }
    return 0;
}
