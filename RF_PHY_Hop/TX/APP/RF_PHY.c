/********************************** (C) COPYRIGHT *******************************
 * File Name          : RF_PHY.c
 * Description        : TX side for RF PHY DATA + 500ms ACK control protocol.
 *******************************************************************************/

#include "CONFIG.h"
#include "RF_PHY.h"
#include "HAL.h"
#include "wchrf.h"
#include "rfm_config.h"
#include "rfm_spi_bridge.h"
#include "rfm_spi_port_internal.h"
#include "rf_hop_protocol.h"
#include "rf_hop_bond.h"
#include "rf_hop_score.h"
#include "rf_monitor_control.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef RF_SERIAL_LOG
#define RF_SERIAL_LOG                  0
#endif

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
#define RF_AUTO_DEMO_WAIT_ACK_ENABLE   0u
#define RF_AUTO_DEMO_ACK_BIT           (RF_AUTO_DEMO_WAIT_ACK_ENABLE ? PROP_WAIT_ACK : 0u)
#define RF_AUTO_DEMO_REPORT_HZ         8000u
#define RF_AUTO_DEMO_RATE_CODE         RFH_RATE_8K
#define RF_AUTO_DEMO_LOG_PERIOD_MS     5000u
#define RF_AUTO_DEMO_PENDING_MAX       4u
#define RF_AUTO_DEMO_TX_STUCK_MS       10u
#define RF_AUTO_DEMO_TX_IN_ISR         1u
#define RF_AUTO_DEMO_ACK_INTERVAL_MS   500u
#define RF_AUTO_DEMO_ACK_REQUEST_BURST 3u
#define RF_AUTO_DEMO_CONNECT_REQUEST_BURST 16u
#ifndef RF_AUTO_DEMO_ACK_RX_TIMEOUT_US
#define RF_AUTO_DEMO_ACK_RX_TIMEOUT_US 1200u
#endif
#ifndef RF_AUTO_DEMO_CONNECT_ACK_RX_TIMEOUT_US
#define RF_AUTO_DEMO_CONNECT_ACK_RX_TIMEOUT_US 5000u
#endif
#define RF_AUTO_DEMO_ACK_RX_STUCK_MS   20u
#define RF_AUTO_DEMO_ACK_RX_TIMEOUT_UNITS (RF_AUTO_DEMO_ACK_RX_TIMEOUT_US * 2u)
#define RF_AUTO_DEMO_CONNECT_ACK_RX_TIMEOUT_UNITS (RF_AUTO_DEMO_CONNECT_ACK_RX_TIMEOUT_US * 2u)
#ifndef RF_AUTO_DEMO_TX_SEND_TIME_UNITS
#define RF_AUTO_DEMO_TX_SEND_TIME_UNITS (4u * 2u)
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
 * - ACK 丢失、丢包率、IRQ 延迟都会触发跳频；冷却和改善阈值决定是否换到候选频道。
 */
#ifndef RF_AUTO_DEMO_AUTO_HOP_ENABLE
#define RF_AUTO_DEMO_AUTO_HOP_ENABLE   1u    /* 1=启用自动跳频，0=只接受手动切频道 */
#endif
#define RF_AUTO_DEMO_HOP_LOSS_TRIGGER_PERMILLE 70u /* ACK 上报丢包率超过 7% 时立即跳频 */
#define RF_AUTO_DEMO_HOP_ACK_MISS_THRESHOLD 8u /* 连续丢 ACK 达到该次数后，按评分触发跳频 */
#define RF_AUTO_DEMO_LINK_ACK_MISS_LIMIT \
    (RF_AUTO_DEMO_HOP_ACK_MISS_THRESHOLD + RFH_ACK_MISS_LIMIT_DEFAULT) /* 超过该次数认为链路断开 */
#define RF_AUTO_DEMO_HOP_SCORE_THRESHOLD 180u /* ACK 超时/评分兜底跳频阈值 */
#define RF_AUTO_DEMO_HOP_IRQ_THRESHOLD_US 1500u /* 平均 IRQ 延迟达到该值后计入跳频窗口 */
#define RF_AUTO_DEMO_IRQ_HOP_WINDOWS    2u    /* IRQ 延迟连续异常窗口数，达到后触发跳频 */
#define RF_AUTO_DEMO_HOP_IRQ_GOOD_US   800u  /* IRQ 延迟恢复到该值以下时清除异常窗口 */
#define RF_AUTO_DEMO_HOP_IRQ_WARN_US    1000u /* IRQ 评分开始升高的延迟 */
#define RF_AUTO_DEMO_HOP_IRQ_BAD_US     2500u /* IRQ 评分达到满坏分的延迟 */
#define RF_AUTO_DEMO_HOP_FORCE_SCORE    600u  /* 当前频道坏分达到该值时，无视普通跳频冷却 */
#define RF_AUTO_DEMO_HOP_SCORE_IMPROVE_MIN 40u /* 候选频道至少低这么多分才优先切换 */
#define RF_AUTO_DEMO_HOP_COOLDOWN_MS   10000u /* 一次跳频完成后的普通冷却时间 */
#define RF_AUTO_DEMO_CHANNEL_COOLDOWN_MS 10000u /* 频道被打满坏分后，暂不选回的时间 */
#define RF_AUTO_DEMO_HOP_STABLE_LOSS_MAX_PERMILLE 50u /* 稳定保护：10 秒内丢包率一直低于 5% 则不跳频 */
#define RF_AUTO_DEMO_HOP_STABLE_IRQ_AVG_US 1200u /* 稳定保护：10 秒内按键/IRQ 平均延迟低于 1.2ms 则不跳频 */
#define RF_AUTO_DEMO_RANK_PROMOTE_MS   RF_AUTO_DEMO_HOP_COOLDOWN_MS /* 停留在排行榜后半区超过该时间则尝试上移 */
#define RF_AUTO_DEMO_FRONT_HALF_COUNT  ((RFH_HOP_CHANNEL_COUNT + 1u) / 2u) /* 排行榜前半区频道数量 */
#define RF_AUTO_DEMO_HOP_PREPARE_TIMEOUT_MS 1000u /* HOP_PREPARE 等 ACK 的超时 */
#define RF_AUTO_DEMO_HOP_CONFIRM_TIMEOUT_MS 2500u /* HOP_CONFIRM 等 ACK 的超时，覆盖多次 500ms ACK 机会 */
#define RF_AUTO_DEMO_HOP_RECOVERY_TIMEOUT_MS 3000u /* 跳频握手失败后的双频道恢复总时长 */
#define RF_AUTO_DEMO_HOP_RECOVERY_DWELL_MS 500u /* 恢复期在旧/新频道之间切换的停留时间 */
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
    RF_AUTO_TX_COMM
} rf_auto_tx_link_state_t;

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
static volatile uint32_t g_demo_ack_rx_start_clock = 0u;
static volatile uint8_t g_demo_pause_tx = 0u;
static volatile uint8_t g_demo_force_ack_burst = 0u;
static volatile uint8_t g_pending_event_state_code = 0u;
static volatile rf_auto_connect_phase_t g_demo_connect_phase = RF_AUTO_CONNECT_SYN_TX;
static volatile uint32_t g_demo_connect_phase_clock = 0u;
static volatile uint8_t g_demo_connect_packet_stage = RFH_CONNECT_STAGE_SYN;
#if (RF_AUTO_DEMO_TX_IN_ISR == 0u)
static volatile uint32_t g_demo_pending_reports = 0u;
#endif
static uint8_t g_demo_seq = 0u;
static uint8_t g_demo_ack_burst_left = 0u;
static uint8_t g_demo_ack_token = 0u;
static uint8_t g_demo_active_ack_token = 0u;
static uint16_t g_demo_report_hz = RF_AUTO_DEMO_REPORT_HZ;
static uint8_t g_demo_rate_code = RF_AUTO_DEMO_RATE_CODE;
static uint8_t g_demo_input_off = 0u;
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
static volatile uint8_t g_demo_current_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
static uint8_t g_demo_old_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
static uint8_t g_demo_target_channel = RF_AUTO_DEMO_INITIAL_CHANNEL;
static uint8_t g_demo_hop_seq = 0u;
static rf_auto_hop_state_t g_demo_hop_state = RF_AUTO_HOP_COMM;
static volatile rf_auto_tx_link_state_t g_demo_link_state = RF_AUTO_TX_UNCONNECTED;
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
static uint32_t g_demo_hop_cooldown_until = 0u;
static uint32_t g_demo_recovery_switch_clock = 0u;
static uint8_t g_demo_recovery_side = 0u;
static volatile rf_auto_pair_state_t g_demo_pair_state = RF_AUTO_PAIR_IDLE;
static volatile uint8_t g_demo_pair_wait_rx_after_tx = 0u;
static volatile uint8_t g_demo_pair_done_pending = 0u;
static uint16_t g_demo_pair_tx_ticks_remaining = 0u;
static uint32_t g_demo_pair_started_clock = 0u;
static uint32_t g_demo_pair_deadline_clock = 0u;
static uint32_t g_demo_pair_session = 0u;
static uint32_t g_demo_pair_tx_id_hash = 0u;
static uint32_t g_demo_pair_rx_id_hash = 0u;
static uint32_t g_demo_pair_link_access_address = 0u;
static uint32_t g_demo_pair_done_confirm32 = 0u;
#if (RF_AUTO_DEMO_TX_IN_ISR == 0u)
static uint32_t g_demo_tx_start_clock = 0u;
#endif
static uint32_t g_demo_last_log_clock = 0u;
static uint8_t g_demo_last_payload[RFM_RF_INPUT_PAYLOAD_LEN] = {0};
static volatile uint8_t g_demo_have_payload = 0u;
static volatile uint32_t g_demo_tmr_epoch_cycles = 0u;
static uint32_t g_demo_report_tmr_cycles = 1u;
static uint32_t g_demo_last_payload_tmr = 0u;
static uint8_t g_demo_last_payload_tmr_valid = 0u;
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
static uint8_t g_monitor_sync_echo_seq = 0u;
static uint32_t g_monitor_sync_echo_rx_tick_us = 0u;
static uint32_t g_monitor_sync_echo_tx_tick_us = 0u;

static uint16_t g_demo_channel_scores[RFH_HOP_CHANNEL_COUNT];
static uint32_t g_demo_channel_cooldown_until[RFH_HOP_CHANNEL_COUNT];
static rf_score_window_t g_demo_score_windows[RFH_HOP_CHANNEL_COUNT];
static uint8_t g_demo_channel_tried_mask = 0u;
static uint8_t g_demo_channel_score_known_mask = 0u;

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
        seed = rfh_fnv1a32_mix_u32(seed, TMOS_GetSystemClock());
        g_demo_link_access_address = rfh_access_address_from_seed(seed);
    }
}

static void demo_apply_loaded_bond(const rfh_bond_record_t *record)
{
    g_demo_bond = *record;
    g_demo_has_bond = 1u;
    g_demo_link_access_address = record->link_access_address;
    g_demo_bond_channel_a = record->channel_a;
    g_demo_bond_channel_b = record->channel_b;
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

static uint8_t demo_clear_bond(void)
{
    if(EEPROM_ERASE(RFH_BOND_EEPROM_ADDR_DEFAULT, RFH_BOND_EEPROM_ERASE_SIZE) != 0u)
    {
        return 0u;
    }
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

    g_demo_pause_tx = 1u;
    (void)RFRole_Stop();
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

static uint32_t demo_make_pair_session(void)
{
    static const uint8_t tag[] = "HBOX_PAIR_SESSION";
    uint32_t hash = rfh_fnv1a32_bytes(tag, (uint32_t)(sizeof(tag) - 1u));

    hash = rfh_fnv1a32_mix_u32(hash, g_demo_local_id_hash);
    hash = rfh_fnv1a32_mix_u32(hash, TMOS_GetSystemClock());
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
    hash = rfh_fnv1a32_mix_u32(hash, TMOS_GetSystemClock());
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
    memcpy(g_demo_last_payload, payload, RFM_RF_INPUT_PAYLOAD_LEN);
    g_demo_last_payload_tmr = now_cycles;
    g_demo_last_payload_tmr_valid = 1u;
    g_demo_have_payload = 1u;
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

static uint16_t demo_score_irq_sample(uint16_t avg_irq_us)
{
    uint16_t irq_permille = rfh_score_irq_metric(avg_irq_us,
                                                 RF_AUTO_DEMO_HOP_IRQ_GOOD_US,
                                                 RF_AUTO_DEMO_HOP_IRQ_BAD_US);

    return demo_score_from_metrics(0u, 0u, 0u, 0u, irq_permille);
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
    if(idx >= RFH_HOP_CHANNEL_COUNT)
    {
        return;
    }

    g_demo_channel_scores[idx] = rfh_score_clamp(sample);
    if(idx < 8u)
    {
        g_demo_channel_score_known_mask |= (uint8_t)(1u << idx);
    }
    if(g_demo_channel_scores[idx] >= RF_AUTO_DEMO_CHANNEL_SCORE_BAD)
    {
        g_demo_channel_cooldown_until[idx] =
            now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_CHANNEL_COOLDOWN_MS);
    }
}

static void demo_channel_score_mark_known_by_index(uint8_t idx)
{
    if(idx < 8u)
    {
        g_demo_channel_score_known_mask |= (uint8_t)(1u << idx);
    }
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

    now = TMOS_GetSystemClock();
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

static uint16_t demo_irq_latency_score(uint16_t avg_irq_us)
{
    return demo_score_irq_sample(avg_irq_us);
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

static void demo_channel_score_set(uint8_t channel, uint16_t score)
{
    uint8_t idx = demo_channel_index(channel);
    uint32_t now = TMOS_GetSystemClock();

    if(idx == 0xFFu)
    {
        return;
    }
    g_demo_channel_scores[idx] = (score > 1000u) ? 1000u : score;
    demo_score_window_reset_by_index(idx, now);
    demo_channel_score_mark_known_by_index(idx);
}

static void demo_channel_scores_init(void)
{
    uint8_t i;
    uint32_t now = TMOS_GetSystemClock();

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
    demo_channel_score_mark_known_by_index(i);
}

#if (RF_SERIAL_LOG == 1)
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
    uint8_t i;
    uint8_t best = current;
    uint8_t fallback = current;
    uint8_t current_idx = demo_channel_index(current);
    uint16_t best_score = 0xFFFFu;
    uint16_t fallback_score = 0xFFFFu;

    for(i = 0u; i < RFH_HOP_CHANNEL_COUNT; i++)
    {
        uint8_t channel = rfh_hop_channel_at(i);
        uint16_t score = demo_channel_score_effective_by_index(i);
        if(channel == current)
        {
            continue;
        }
        if((allow_best_available == 0u) &&
           ((g_demo_channel_tried_mask & (uint8_t)(1u << i)) != 0u))
        {
            continue;
        }
        if(score < fallback_score)
        {
            fallback_score = score;
            fallback = channel;
        }
        if((int32_t)(now - g_demo_channel_cooldown_until[i]) < 0)
        {
            continue;
        }
        if(score < best_score)
        {
            best_score = score;
            best = channel;
        }
    }

    if(best == current)
    {
        return current;
    }
    if(((uint32_t)best_score + RF_AUTO_DEMO_HOP_SCORE_IMPROVE_MIN) < current_risk_score)
    {
        return best;
    }
    if((allow_best_available != 0u) && (fallback != current))
    {
        if((current_idx != 0xFFu) &&
           (demo_channel_score_effective_by_index(current_idx) <= fallback_score))
        {
            return current;
        }
        return (best == current) ? fallback : best;
    }
    if((current_risk_score >= RF_AUTO_DEMO_HOP_FORCE_SCORE) &&
       (best_score < current_risk_score))
    {
        return best;
    }
    return current;
}

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
    g_demo_channel_enter_clock = TMOS_GetSystemClock();
    demo_score_window_reset_channel(channel, g_demo_channel_enter_clock);
    demo_channel_score_mark_known_by_index(demo_channel_index(channel));
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

static uint8_t demo_manual_fixed_channel(uint8_t *channel)
{
    if((g_monitor_auto_hop_enabled == 0u) &&
       (demo_channel_index(g_monitor_manual_channel) != 0xFFu))
    {
        if(channel != 0)
        {
            *channel = g_monitor_manual_channel;
        }
        return 1u;
    }
    return 0u;
}

static void demo_enter_tx_unconnected(uint32_t now)
{
    uint8_t anchor_channel = demo_discovery_channel(0u);

    (void)demo_manual_fixed_channel(&anchor_channel);
    g_demo_link_state = RF_AUTO_TX_UNCONNECTED;
    g_demo_hop_state = RF_AUTO_HOP_COMM;
    g_demo_ack_miss_count = 0u;
    g_demo_irq_bad_window_count = 0u;
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
    g_pending_event_state_code = (g_demo_has_bond != 0u) ?
                                 RF_LINK_STATE_CONNECTING :
                                 RF_LINK_STATE_IDLE;
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
    if(rfh_channel_valid(channel) == 0u)
    {
        channel = g_demo_current_channel;
    }
    if(channel != g_demo_current_channel)
    {
        demo_apply_channel(channel);
    }
    g_demo_link_state = RF_AUTO_TX_COMM;
    g_demo_hop_state = RF_AUTO_HOP_COMM;
    g_demo_old_channel = channel;
    g_demo_target_channel = channel;
    g_demo_ack_miss_count = 0u;
    g_demo_irq_bad_window_count = 0u;
    g_demo_force_ack_burst = 0u;
    g_demo_ack_burst_left = 0u;
    g_demo_wait_ack_after_tx = 0u;
    g_demo_ack_rx_active = 0u;
    g_demo_channel_enter_clock = now;
    g_demo_hop_cooldown_until = now;
    demo_arm_next_ack_clock(now);
    g_pending_event_state_code = RF_LINK_STATE_CONNECTED;
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
    if(demo_manual_fixed_channel(&anchor_channel) == 0u)
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

static uint8_t demo_active_hop_cmd(void)
{
    if(g_demo_link_state != RF_AUTO_TX_COMM)
    {
        return RFH_CMD_NONE;
    }
    if((g_monitor_status_pending != 0u) &&
       (g_demo_hop_state == RF_AUTO_HOP_COMM) &&
       (g_demo_rate_update_pending == 0u))
    {
        return RFH_CMD_MONITOR_CONFIG;
    }
    if((g_demo_rate_update_pending != 0u) &&
       (g_demo_hop_state == RF_AUTO_HOP_COMM))
    {
        return RFH_CMD_RATE_UPDATE;
    }
    if((g_monitor_sync_echo_pending != 0u) &&
       (g_demo_hop_state == RF_AUTO_HOP_COMM))
    {
        return RFH_CMD_TIME_SYNC_ECHO;
    }
    if((g_monitor_latency_pending != 0u) &&
       (g_demo_hop_state == RF_AUTO_HOP_COMM))
    {
        return RFH_CMD_LATENCY_INPUT;
    }
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

static uint8_t demo_begin_hop_prepare_common(uint32_t now,
                                             uint8_t target_channel,
                                             uint16_t reason_score,
                                             uint8_t bypass_stable_block)
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
    if(g_demo_hop_seq == 0u)
    {
        g_demo_hop_seq = 1u;
    }
    g_demo_hop_state = RF_AUTO_HOP_PREPARE_ACK_WAIT;
    g_demo_hop_deadline_clock = now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_PREPARE_TIMEOUT_MS);
    g_demo_force_ack_burst = 1u;
    g_demo_stat.hop_event++;
    return 1u;
}

static uint8_t demo_begin_hop_prepare(uint32_t now,
                                      uint8_t target_channel,
                                      uint16_t reason_score)
{
    return demo_begin_hop_prepare_common(now, target_channel, reason_score, 0u);
}

static uint8_t demo_begin_manual_hop_prepare(uint32_t now,
                                             uint8_t target_channel,
                                             uint16_t reason_score)
{
    return demo_begin_hop_prepare_common(now, target_channel, reason_score, 1u);
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
    if(demo_stable_window_blocks_hop() != 0u)
    {
        return;
    }
    if(((int32_t)(now - g_demo_hop_cooldown_until) < 0) &&
       (reason_score < RF_AUTO_DEMO_HOP_FORCE_SCORE))
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
        g_demo_hop_cooldown_until = now + MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_COOLDOWN_MS / 2u);
        return;
    }
    demo_begin_hop_prepare(now, g_demo_target_channel, reason_score);
#endif
}

static void demo_queue_manual_hop(uint8_t seq, uint8_t target_channel)
{
    if(demo_channel_index(target_channel) == 0xFFu)
    {
        return;
    }
    if(seq == g_monitor_manual_applied_seq)
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
    if(demo_begin_manual_hop_prepare(TMOS_GetSystemClock(),
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
    g_demo_hop_state = RF_AUTO_HOP_COMM;
    g_demo_old_channel = g_demo_current_channel;
    g_demo_target_channel = g_demo_current_channel;
    demo_channel_mark_tried(g_demo_current_channel);
    demo_channel_score_update(g_demo_current_channel, RF_AUTO_DEMO_CHANNEL_SCORE_GOOD, now);
    g_demo_ack_miss_count = 0u;
    g_demo_irq_bad_window_count = 0u;
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
    demo_channel_score_update(g_demo_target_channel, demo_score_timeout_sample(), now);
    demo_score_window_flush_by_index(demo_channel_index(g_demo_target_channel), now, 1u);
    demo_apply_channel(g_demo_old_channel);
    g_demo_force_ack_burst = 1u;
    g_demo_stat.hop_event++;
}

static void demo_handle_command_ack(uint8_t cmd, uint8_t seq, uint8_t channel)
{
    uint32_t now = TMOS_GetSystemClock();

    if((cmd == RFH_CMD_RATE_UPDATE) &&
       (g_demo_rate_update_pending != 0u) &&
       (seq == g_demo_rate_update_seq))
    {
        g_demo_rate_update_pending = 0u;
        return;
    }

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
    uint8_t ack_flags;
    uint8_t seq;
    uint8_t channel;
    uint16_t irq_score;
    uint16_t risk_score;
    uint8_t should_hop = 0u;
    uint32_t now = TMOS_GetSystemClock();

    if((RxBuf[1] != RF_AUTO_DEMO_PACKET_LEN) ||
       (rfh_packet_type(air[RFH_HDR0_OFFSET]) != RFH_PKT_ACK))
    {
        g_demo_stat.ack_type_err++;
        demo_channel_score_update(g_demo_current_channel,
                                  demo_score_type_sample(),
                                  now);
        if(g_demo_hop_state != RF_AUTO_HOP_COMM)
        {
            g_demo_force_ack_burst = 1u;
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

    g_demo_stat.ack_ok++;

    if(g_demo_link_state == RF_AUTO_TX_UNCONNECTED)
    {
        if((g_demo_connect_phase == RF_AUTO_CONNECT_SYN_ACK_RX) &&
           (cmd == RFH_CMD_CONNECT_REQ) &&
           ((ack_flags & RFH_FLAG_CMD_ACK) != 0u) &&
           (seq == RFH_ACK_STATUS_CONNECTED))
        {
            if(rfh_channel_valid(channel) != 0u)
            {
                demo_apply_channel(channel);
            }
            g_demo_connect_phase = RF_AUTO_CONNECT_FINAL_TX;
            g_demo_connect_phase_clock = now;
            g_demo_connect_packet_stage = RFH_CONNECT_STAGE_FINAL;
        }
        else
        {
            g_demo_stat.ack_type_err++;
            g_demo_force_ack_burst = 1u;
        }
        return;
    }

    demo_score_window_note_quality(g_demo_current_channel,
                                   g_demo_last_quality,
                                   g_demo_last_avg_irq_us,
                                   now);
    g_demo_ack_miss_count = 0u;
    risk_score = RF_AUTO_DEMO_CHANNEL_SCORE_GOOD;
    if(g_demo_last_quality > RF_AUTO_DEMO_HOP_LOSS_TRIGGER_PERMILLE)
    {
        risk_score = demo_score_loss_sample(g_demo_last_quality);
        if(risk_score < RF_AUTO_DEMO_HOP_FORCE_SCORE)
        {
            risk_score = RF_AUTO_DEMO_HOP_FORCE_SCORE;
        }
        should_hop = 1u;
    }
    else if(g_demo_last_quality >= RF_AUTO_DEMO_HOP_SCORE_THRESHOLD)
    {
        risk_score = demo_score_loss_sample(g_demo_last_quality);
        should_hop = 1u;
    }
    irq_score = demo_irq_latency_score(g_demo_last_avg_irq_us);
    if(irq_score > risk_score)
    {
        risk_score = irq_score;
    }
    if(g_demo_last_avg_irq_us >= RF_AUTO_DEMO_HOP_IRQ_THRESHOLD_US)
    {
        if(g_demo_irq_bad_window_count != 0xFFu)
        {
            g_demo_irq_bad_window_count++;
        }
        if(g_demo_irq_bad_window_count >= RF_AUTO_DEMO_IRQ_HOP_WINDOWS)
        {
            should_hop = 1u;
        }
    }
    else if(g_demo_last_avg_irq_us <= RF_AUTO_DEMO_HOP_IRQ_GOOD_US)
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
        (void)rfm_spi_bridge_emit_time_sync(seq);
        return;
    }

    if(cmd == RFH_CMD_SCORE_HINT)
    {
        demo_channel_score_set(channel, (uint16_t)seq * 4u);
        cmd = RFH_CMD_NONE;
    }

    if((cmd == RFH_CMD_HOP_PREPARE) ||
       (cmd == RFH_CMD_HOP_CONFIRM) ||
       (cmd == RFH_CMD_RATE_UPDATE))
    {
        demo_handle_command_ack(cmd, seq, channel);
        if(g_demo_hop_state != RF_AUTO_HOP_COMM)
        {
            g_demo_force_ack_burst = 1u;
        }
    }
    else if((g_demo_hop_state == RF_AUTO_HOP_COMM) &&
            (should_hop != 0u))
    {
        demo_start_hop_prepare(now, risk_score);
    }
    else if(g_demo_hop_state != RF_AUTO_HOP_COMM)
    {
        g_demo_force_ack_burst = 1u;
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
    uint16_t stm32_age_us;
    uint8_t stm32_age_q8;
    uint8_t tx_wait_q8 = 0u;

    if((dst == 0) || (src == 0))
    {
        return;
    }

    stm32_age_us = rfh_get_u16(&src[RFMON_INPUT_SAMPLE_TICK_OFFSET]);
    stm32_age_q8 = tx_latency_q8_encode(stm32_age_us);
    if((stm32_age_q8 != 0u) && (g_demo_last_payload_tmr_valid != 0u))
    {
        uint32_t tx_wait_us = tx_cycles_to_us_saturated(
            tx_now_cycles() - g_demo_last_payload_tmr);

        tx_wait_q8 = tx_latency_q8_encode(tx_wait_us);
    }

    dst[0] = src[RFMON_INPUT_KEY_MASK_OFFSET];
    dst[1] = src[RFMON_INPUT_KEY_MASK_OFFSET + 1u];
    dst[2] = src[RFMON_INPUT_KEY_MASK_OFFSET + 2u];
    dst[3] = stm32_age_q8;
    dst[4] = tx_wait_q8;
}

static void demo_fill_tx_packet(uint8_t request_ack, uint8_t ack_token, uint8_t ack_burst_left)
{
    uint8_t i;
    uint8_t *air = &TxBuf[2];
    uint8_t *data = &air[RFH_DATA_OFFSET];
    uint8_t connect_channel_a = RF_AUTO_DEMO_DISCOVERY_CHANNEL_A;
    uint8_t connect_channel_b = RF_AUTO_DEMO_DISCOVERY_CHANNEL_B;
    uint8_t flags = (g_demo_link_state == RF_AUTO_TX_COMM) ?
                    RFH_FLAG_LINK_OK : RFH_FLAG_DUAL_REDUNDANT;
    uint8_t hop_cmd = demo_active_hop_cmd();
    uint8_t has_input_payload = 0u;
    uint8_t use_short_input = 0u;
    uint8_t input_payload[RFM_RF_INPUT_PAYLOAD_LEN];

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
    if(g_demo_link_state == RF_AUTO_TX_UNCONNECTED)
    {
        (void)demo_manual_fixed_channel(&connect_channel_a);
        connect_channel_b = (connect_channel_a == RF_AUTO_DEMO_DISCOVERY_CHANNEL_B) ?
                            RF_AUTO_DEMO_DISCOVERY_CHANNEL_A :
                            RF_AUTO_DEMO_DISCOVERY_CHANNEL_B;
        air[0] = rfh_make_header0(RFH_PKT_CONNECT, g_demo_rate_code, flags);
        air[1] = g_demo_seq;
        rfh_put_u32(&data[RFH_CONNECT_SESSION0], RFH_CONNECT_SESSION_ID);
        data[RFH_CONNECT_RATE] = g_demo_rate_code;
        data[RFH_CONNECT_CH_A] = connect_channel_a;
        data[RFH_CONNECT_CH_B] = connect_channel_b;
        data[RFH_CONNECT_ACK_WINDOW_MS] = RFH_DEFAULT_ACK_WINDOW_MS;
        data[RFH_CONNECT_OPTIONS] = g_demo_connect_packet_stage;
        data[RFH_CONNECT_VERSION] = RFH_PROTOCOL_VERSION;
        (void)ack_token;
        (void)ack_burst_left;
        TxBuf[1] = RF_AUTO_DEMO_PACKET_LEN;
        return;
    }

    air[0] = rfh_make_header0(RFH_PKT_DATA, g_demo_rate_code, flags);
    air[1] = g_demo_seq;
    if(hop_cmd != RFH_CMD_NONE)
    {
        if(hop_cmd == RFH_CMD_RATE_UPDATE)
        {
            data[RFH_CMD_SLOT_ID] = RFH_CMD_RATE_UPDATE;
            data[RFH_CMD_SLOT_ARG0] = g_demo_rate_code;
            data[RFH_CMD_SLOT_ARG3] = g_demo_rate_update_seq;
        }
        else if(hop_cmd == RFH_CMD_MONITOR_CONFIG)
        {
            data[RFH_CMD_SLOT_ID] = RFH_CMD_MONITOR_CONFIG;
            data[RFH_CMD_SLOT_ARG0] = g_monitor_status_flags;
            data[RFH_CMD_SLOT_ARG1] = g_monitor_status_manual_channel;
            data[RFH_CMD_SLOT_ARG2] = g_monitor_status_result;
            data[RFH_CMD_SLOT_ARG3] = g_monitor_status_seq;
            g_monitor_status_pending = 0u;
        }
        else if(hop_cmd == RFH_CMD_TIME_SYNC_ECHO)
        {
            data[RFH_TIME_SYNC_ECHO_CMD_ID] = RFH_CMD_TIME_SYNC_ECHO;
            data[RFH_TIME_SYNC_ECHO_SEQ] = g_monitor_sync_echo_seq;
            rfh_put_u32(&data[RFH_TIME_SYNC_ECHO_RX_TICK],
                        g_monitor_sync_echo_rx_tick_us);
            rfh_put_u32(&data[RFH_TIME_SYNC_ECHO_TX_TICK],
                        g_monitor_sync_echo_tx_tick_us);
            g_monitor_sync_echo_pending = 0u;
        }
        else if(hop_cmd == RFH_CMD_LATENCY_INPUT)
        {
            data[RFH_LATENCY_CMD_ID] = RFH_CMD_LATENCY_INPUT;
            data[RFH_LATENCY_INPUT_SEQ] = g_monitor_latency_input_seq;
            rfh_put_u32(&data[RFH_LATENCY_KEY_MASK],
                        g_monitor_latency_key_mask);
            rfh_put_u32(&data[RFH_LATENCY_SAMPLE_TICK],
                        g_monitor_latency_sample_tick_us);
            g_monitor_latency_pending = 0u;
        }
        else
        {
            data[RFH_HOP_CMD_ID] = hop_cmd;
            data[RFH_HOP_CMD_CHANNEL] = g_demo_target_channel;
            data[RFH_HOP_CMD_DELAY_LO_MS] = 0u;
            data[RFH_HOP_CMD_DELAY_HI_MS] = 0u;
            data[RFH_HOP_CMD_SEQ] = g_demo_hop_seq;
            data[RFH_HOP_CONFIRM_OLD_CHANNEL] = g_demo_old_channel;
            rfh_put_u16(&data[RFH_HOP_CMD_SCORE_LO], g_demo_hop_reason_score);
        }
    }
    else
    {
        if(request_ack == 0u)
        {
#if (RF_TX_FORCE_INPUT_PAYLOAD_TEST != 0u)
            demo_fill_direct_input_payload(input_payload);
            demo_encode_short_input_payload(data, input_payload);
            has_input_payload = 1u;
            use_short_input = 1u;
#else
            if(rfm_spi_port_peek_latest_input(input_payload, RFM_RF_INPUT_PAYLOAD_LEN))
            {
                if((g_demo_have_payload == 0u) ||
                   (demo_input_payload_same_input(g_demo_last_payload, input_payload) == 0u))
                {
                    demo_store_last_payload(input_payload, tx_now_cycles());
                }
                demo_encode_short_input_payload(data, g_demo_last_payload);
                rfh_put_u16(&g_demo_last_payload[RFMON_INPUT_SAMPLE_TICK_OFFSET], 0u);
                use_short_input = 1u;
                has_input_payload = 1u;
            }
            else if(g_demo_have_payload != 0u)
            {
                demo_encode_short_input_payload(data, g_demo_last_payload);
                rfh_put_u16(&g_demo_last_payload[RFMON_INPUT_SAMPLE_TICK_OFFSET], 0u);
                use_short_input = 1u;
                has_input_payload = 1u;
            }
            else
            {
                data[0] = (uint8_t)(g_demo_stat.tx_start & 0xFFu);
                data[1] = (uint8_t)((g_demo_stat.tx_start >> 8) & 0xFFu);
            }
#endif
        }
    }
    for(i = 8u;
        (has_input_payload == 0u) &&
        (hop_cmd == RFH_CMD_NONE) &&
        (i < RF_AUTO_DEMO_ACK_TOKEN_OFFSET);
        i++)
    {
        air[i] = (uint8_t)(0xA0u + i);
    }
    if(request_ack != 0u)
    {
        air[RF_AUTO_DEMO_ACK_TOKEN_OFFSET] = ack_token;
        air[RF_AUTO_DEMO_ACK_REMAIN_OFFSET] = ack_burst_left;
    }
    TxBuf[1] = (use_short_input != 0u) ? RFH_INPUT_AIR_PACKET_LEN : RF_AUTO_DEMO_PACKET_LEN;
}

static void demo_fill_pair_packet(uint8_t cmd)
{
    uint8_t *air = &TxBuf[2];
    uint8_t *data = &air[RFH_DATA_OFFSET];
    uint32_t arg32 = 0u;
    uint8_t meta = demo_pair_meta(0u);

    memset(TxBuf, 0, sizeof(TxBuf));
    TxBuf[0] = RFH_WCH_PREAMBLE;
    TxBuf[1] = RF_AUTO_DEMO_PACKET_LEN;
    air[RFH_HDR0_OFFSET] = rfh_make_header0(RFH_PKT_PAIR, g_demo_rate_code, 0u);
    air[RFH_HDR1_OFFSET] = (uint8_t)g_demo_pair_session;
    data[RFH_PAIR_CMD_ID] = cmd;
    rfh_put_u32(&data[RFH_PAIR_SESSION0], g_demo_pair_session);

    if(cmd == RFH_CMD_PAIR_OFFER)
    {
        arg32 = g_demo_pair_tx_id_hash;
    }
    else if(cmd == RFH_CMD_PAIR_CONFIRM)
    {
        arg32 = g_demo_pair_link_access_address;
        meta = demo_pair_meta(1u);
    }
    else if(cmd == RFH_CMD_PAIR_REJECT)
    {
        arg32 = RFH_PAIR_REJECT_BAD_STATE;
    }

    rfh_put_u32(&data[RFH_PAIR_ARG0], arg32);
    data[RFH_PAIR_META] = meta;
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
        g_demo_ack_rx_start_clock = TMOS_GetSystemClock();
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

static uint8_t demo_pair_meta_valid(uint8_t meta)
{
    return (((meta & RFH_PAIR_META_VERSION_MASK) >> RFH_PAIR_META_VERSION_SHIFT) ==
            RFH_PAIR_PROTO_VERSION) ? 1u : 0u;
}

static void demo_handle_pair_packet(void)
{
    const uint8_t *air = &RxBuf[2];
    const uint8_t *data = &air[RFH_DATA_OFFSET];
    uint8_t cmd;
    uint32_t session;
    uint32_t arg32;

    if((demo_pair_is_active() == 0u) ||
       (RxBuf[1] != RF_AUTO_DEMO_PACKET_LEN) ||
       (rfh_packet_type(air[RFH_HDR0_OFFSET]) != RFH_PKT_PAIR))
    {
        g_demo_stat.ack_type_err++;
        return;
    }

    cmd = data[RFH_PAIR_CMD_ID];
    session = rfh_get_u32(&data[RFH_PAIR_SESSION0]);
    arg32 = rfh_get_u32(&data[RFH_PAIR_ARG0]);
    if((session != g_demo_pair_session) || (demo_pair_meta_valid(data[RFH_PAIR_META]) == 0u))
    {
        g_demo_stat.ack_type_err++;
        return;
    }

    if((g_demo_pair_state == RF_AUTO_PAIR_OFFERING) &&
       (cmd == RFH_CMD_PAIR_ACCEPT) &&
       (arg32 != 0u))
    {
        g_demo_pair_rx_id_hash = arg32;
        g_demo_pair_link_access_address = demo_make_pair_link_access_address(arg32);
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
        g_demo_pair_tx_ticks_remaining = 0u;
        g_demo_stat.hop_event++;
        return;
    }

    if((g_demo_pair_state == RF_AUTO_PAIR_CONFIRM_WAIT) &&
       (cmd == RFH_CMD_PAIR_DONE) &&
       (arg32 == g_demo_pair_done_confirm32))
    {
        g_demo_pair_done_pending = 1u;
        g_demo_pause_tx = 1u;
        g_demo_stat.hop_event++;
        return;
    }

    if(cmd == RFH_CMD_PAIR_REJECT)
    {
        g_pending_event_state_code = RF_LINK_STATE_PAIR_FAILED;
        g_demo_pair_done_pending = 1u;
        return;
    }

    g_demo_stat.ack_type_err++;
}

static void demo_finish_pairing(uint32_t now, uint8_t state_code)
{
    g_demo_pair_state = RF_AUTO_PAIR_IDLE;
    g_demo_pair_wait_rx_after_tx = 0u;
    g_demo_pair_done_pending = 0u;
    g_demo_pair_tx_ticks_remaining = 0u;
    g_demo_ack_rx_active = 0u;
    g_demo_wait_ack_after_tx = 0u;
    g_demo_tx_busy = 0u;
    g_demo_pause_tx = 0u;
    (void)demo_apply_access_address(g_demo_link_access_address);
    demo_enter_tx_unconnected(now);
    g_pending_event_state_code = state_code;
    rfm_spi_bridge_emit_state_changed(0x02u);
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
            demo_finish_pairing(now, RF_LINK_STATE_PAIR_FAILED);
            return;
        }
        if(demo_save_bond(g_demo_pair_link_access_address,
                          g_demo_pair_rx_id_hash,
                          g_demo_pair_done_confirm32) != 0u)
        {
            demo_finish_pairing(now, RF_LINK_STATE_PAIR_OK);
        }
        else
        {
            demo_finish_pairing(now, RF_LINK_STATE_PAIR_FAILED);
        }
        return;
    }

    if((int32_t)(now - g_demo_pair_deadline_clock) >= 0)
    {
        demo_finish_pairing(now, RF_LINK_STATE_PAIR_TIMEOUT);
    }
}

static void demo_log_stats(uint32_t now)
{
    uint32_t elapsed_ticks;
    unsigned long elapsed_ms;
#if (RF_SERIAL_LOG == 1)
    uint32_t ack_fail;
#endif

    if((uint32_t)(now - g_demo_last_log_clock) < MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_LOG_PERIOD_MS))
    {
        return;
    }
    elapsed_ticks = now - g_demo_last_log_clock;
    g_demo_last_log_clock = now;
    elapsed_ms = (unsigned long)(((elapsed_ticks * (uint32_t)SYSTEM_TIME_MICROSEN) + 999u) / 1000u);
#if (RF_SERIAL_LOG == 1)
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
        g_demo_ack_rx_start_clock = TMOS_GetSystemClock();
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
    uint32_t now = TMOS_GetSystemClock();
    uint16_t current_score;
    uint16_t timeout_score = demo_score_timeout_sample();

    if(g_demo_link_state == RF_AUTO_TX_UNCONNECTED)
    {
        g_demo_ack_miss_count = 0u;
        g_demo_force_ack_burst = 1u;
        return;
    }

    demo_channel_score_update(g_demo_current_channel,
                              timeout_score,
                              now);
    g_demo_ack_miss_count++;

    if(g_demo_hop_state == RF_AUTO_HOP_COMM)
    {
        current_score = demo_channel_score_get(g_demo_current_channel);
        if((g_demo_ack_miss_count >= RF_AUTO_DEMO_HOP_ACK_MISS_THRESHOLD) &&
           (current_score >= RF_AUTO_DEMO_HOP_SCORE_THRESHOLD))
        {
            demo_start_hop_prepare(now, current_score);
            if(g_demo_hop_state != RF_AUTO_HOP_COMM)
            {
                g_demo_force_ack_burst = 1u;
                return;
            }
        }
    }

    if((g_demo_link_state == RF_AUTO_TX_COMM) &&
       (g_demo_ack_miss_count >= RF_AUTO_DEMO_LINK_ACK_MISS_LIMIT))
    {
        demo_enter_tx_unconnected(now);
        return;
    }

    if(g_demo_hop_state == RF_AUTO_HOP_COMM)
    {
        return;
    }

    g_demo_force_ack_burst = 1u;
}

static void demo_check_ack_rx_stuck(uint32_t now)
{
    if(g_demo_ack_rx_active == 0u)
    {
        return;
    }
    if((uint32_t)(now - g_demo_ack_rx_start_clock) <
       MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_ACK_RX_STUCK_MS))
    {
        return;
    }

    (void)RFRole_Stop();
    g_demo_ack_rx_active = 0u;
    g_demo_tx_busy = 0u;
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
        (void)RFRole_Stop();
        g_demo_tx_busy = 0u;
        g_demo_wait_ack_after_tx = 0u;
        g_demo_ack_rx_active = 0u;
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
            (void)RFRole_Stop();
            g_demo_ack_rx_active = 0u;
            g_demo_tx_busy = 0u;
            g_demo_connect_phase = RF_AUTO_CONNECT_SYN_TX;
            g_demo_connect_phase_clock = now;
            g_demo_connect_packet_stage = RFH_CONNECT_STAGE_SYN;
            return;
        }

        if(demo_manual_fixed_channel(&listen_channel) == 0u)
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
        demo_enter_tx_comm(now, g_demo_current_channel);
    }
}

static void demo_service_rank_promotion(uint32_t now)
{
#if (RF_AUTO_DEMO_AUTO_HOP_ENABLE == 0u)
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
    g_demo_tmr_epoch_cycles += g_demo_report_tmr_cycles;
    if(demo_pair_is_active() != 0u)
    {
        g_demo_stat.report_due++;
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

    g_demo_stat.report_due++;
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
            g_demo_stat.report_drop++;
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

        demo_fill_tx_packet(request_ack, ack_token, ack_burst_left);
        g_demo_tx_busy = (TxBuf[1] == RFH_INPUT_AIR_PACKET_LEN) ? 0u : 1u;
        g_demo_stat.tx_start++;
        gTxParam.txDMA = (uint32_t)TxBuf;
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
    if(sta & RF_STATE_TX_IDLE)
    {
        g_demo_tx_busy = 0u;
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

void RF_TxMainLoopProcess(void)
{
    uint32_t now = TMOS_GetSystemClock();

    if(demo_pair_is_active() != 0u)
    {
        demo_service_pairing(now);
        demo_log_stats(now);
        return;
    }

#if (RF_AUTO_DEMO_TX_IN_ISR == 0u)
    demo_check_tx_stuck(now);
    demo_try_send();
#endif
    demo_check_ack_rx_stuck(now);
    demo_service_connect_phase(now);
    demo_score_windows_service(now);
    demo_service_link(now);
    demo_ack_control_service(now);
    demo_service_hop(now);
    demo_service_rank_promotion(now);
    demo_service_manual_hop();
    demo_log_stats(now);
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
    if((payload[RFMON_INPUT_FLAGS_OFFSET] & RFMON_INPUT_FLAG_SYNC_ECHO) != 0u)
    {
        g_monitor_sync_echo_seq = payload[RFMON_SPI_INPUT_SYNC_SEQ_OFFSET];
        g_monitor_sync_echo_rx_tick_us =
            rfh_get_u32(&payload[RFMON_SPI_INPUT_SYNC_RX_TICK_OFFSET]);
        g_monitor_sync_echo_tx_tick_us =
            rfh_get_u32(&payload[RFMON_SPI_INPUT_SYNC_TX_TICK_OFFSET]);
        g_monitor_sync_echo_pending = 1u;
    }
    SYS_RecoverIrq(irq_status);
    return true;
}

bool RF_SetReportRateHz(uint16_t hz)
{
    uint32_t now = TMOS_GetSystemClock();
    uint8_t was_off = g_demo_input_off;

    if(demo_rate_valid(hz) == 0u)
    {
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
        return true;
    }

    g_demo_input_off = 0u;
    g_demo_report_hz = hz;
    g_demo_rate_code = rfh_rate_code_from_hz(hz);
    demo_reconfigure_report_timer(hz);
    if(was_off != 0u)
    {
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
    return true;
}

uint16_t RF_GetReportRateHz(void)
{
    return g_demo_report_hz;
}

bool RF_StartPairing(void)
{
    uint32_t now = TMOS_GetSystemClock();

    if(g_demo_config_ret != SUCCESS)
    {
        return false;
    }
    if(demo_pair_is_active() != 0u)
    {
        g_pending_event_state_code = RF_LINK_STATE_PAIRING;
        return true;
    }
    if(g_demo_report_hz == 0u)
    {
        g_demo_report_hz = RF_AUTO_DEMO_REPORT_HZ;
        g_demo_rate_code = RF_AUTO_DEMO_RATE_CODE;
        demo_reconfigure_report_timer(g_demo_report_hz);
    }

    g_demo_input_off = 0u;
    g_demo_pair_session = demo_make_pair_session();
    g_demo_pair_tx_id_hash = g_demo_local_id_hash;
    g_demo_pair_rx_id_hash = 0u;
    g_demo_pair_link_access_address = 0u;
    g_demo_pair_done_confirm32 = 0u;
    g_demo_pair_done_pending = 0u;
    g_demo_pair_wait_rx_after_tx = 0u;
    g_demo_pair_tx_ticks_remaining = 0u;
    g_demo_pair_started_clock = now;
    g_demo_pair_deadline_clock = now + MS1_TO_SYSTEM_TIME(RFH_PAIR_WINDOW_MS);
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
        demo_finish_pairing(TMOS_GetSystemClock(),
                            (g_demo_has_bond != 0u) ?
                            RF_LINK_STATE_CONNECTING :
                            RF_LINK_STATE_IDLE);
    }
    else
    {
        g_pending_event_state_code = RF_GetLinkStateCode();
    }
    return true;
}

bool RF_Unbind(void)
{
    uint32_t now = TMOS_GetSystemClock();

    if(demo_pair_is_active() != 0u)
    {
        demo_finish_pairing(now,
                            (g_demo_has_bond != 0u) ?
                            RF_LINK_STATE_CONNECTING :
                            RF_LINK_STATE_IDLE);
    }
    if(demo_clear_bond() == 0u)
    {
        g_pending_event_state_code = RF_LINK_STATE_PAIR_FAILED;
        return false;
    }
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
        return (g_demo_has_bond != 0u) ?
               RF_LINK_STATE_CONNECTING :
               RF_LINK_STATE_IDLE;
    }
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
            (g_demo_link_state == RF_AUTO_TX_COMM)) ? 1u : 0u;
}

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
                       RF_STATE_TX_FINISH | RF_STATE_TIMEOUT | RF_STATE_TX_IDLE;
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
    g_demo_last_log_clock = TMOS_GetSystemClock();

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
    PFIC_SetPriority(TMR0_IRQn, 0x80);
    PFIC_EnableIRQ(TMR0_IRQn);

    g_demo_current_channel = gParm.frequency;
    g_demo_old_channel = demo_discovery_channel(0u);
    g_demo_target_channel = demo_discovery_channel(0u);
    memset(g_demo_last_payload, 0, sizeof(g_demo_last_payload));
    g_demo_have_payload = 0u;
    g_demo_last_payload_tmr = 0u;
    g_demo_last_payload_tmr_valid = 0u;
    g_monitor_latency_pending = 0u;
    g_monitor_latency_input_seq = 0u;
    g_monitor_latency_key_mask = 0u;
    g_monitor_latency_sample_tick_us = 0u;
    g_monitor_sync_echo_pending = 0u;
    g_monitor_sync_echo_seq = 0u;
    g_monitor_sync_echo_rx_tick_us = 0u;
    g_monitor_sync_echo_tx_tick_us = 0u;
    g_demo_input_off = 0u;
    g_demo_last_avg_irq_us = 0u;
    g_demo_last_max_irq_us = 0u;
    g_demo_irq_bad_window_count = 0u;
    g_demo_link_state = RF_AUTO_TX_UNCONNECTED;
    g_demo_pair_state = RF_AUTO_PAIR_IDLE;
    g_demo_pair_done_pending = 0u;
    g_demo_pair_wait_rx_after_tx = 0u;
    g_demo_discovery_side = 0u;
    g_demo_discovery_switch_clock = TMOS_GetSystemClock();
    g_demo_force_ack_burst = 1u;
    demo_channel_scores_init();
    g_demo_channel_tried_mask = 0u;
    demo_channel_mark_tried(RF_AUTO_DEMO_INITIAL_CHANNEL);
    demo_arm_next_ack_clock(TMOS_GetSystemClock());
    g_demo_hop_cooldown_until = TMOS_GetSystemClock() +
                                MS1_TO_SYSTEM_TIME(RF_AUTO_DEMO_HOP_COOLDOWN_MS);
    g_demo_channel_enter_clock = TMOS_GetSystemClock();

}
