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
#include "rf_channel_policy.h"
#include "rf_channel_manager.h"
#include "rf_channel_radio.h"
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

/* Quality/transaction parameters live in rf_channel_policy and protocol. */
#define RF_AUTO_DEMO_HOP_IRQ_GOOD_US 800u
#define RF_AUTO_DEMO_PAIR_RX_TIMEOUT_US 30000u
#define RF_AUTO_DEMO_PAIR_RX_TIMEOUT_UNITS (RF_AUTO_DEMO_PAIR_RX_TIMEOUT_US * 2u)
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
    RF_AUTO_HOP_RECOVERY_DUAL = RFC_RECOVER
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

uint8_t taskID;

static rfRoleParam_t gParm;
static rfipTx_t gTxParam;
static rfipRx_t gRxParam;
__attribute__((__aligned__(4))) static uint8_t TxBuf[RF_AUTO_DEMO_DMA_LEN];
/* TxBuf is a CPU packet template. Timer-paced DATA uses alternating DMA
 * storage so assembling the next packet never rewrites the previous one. */
__attribute__((__aligned__(4))) static uint8_t TxDmaBuf[2][16];
static uint8_t g_demo_tx_dma_slot;
static uint32_t g_demo_tx_launch_cycles, g_demo_tx_guard_cycles, g_demo_tx_wait_limit, g_channel_control_air_guard;
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
static rfc_manager_t g_channel;
static rfc_policy_t g_channel_policy;
#define g_demo_hop_state g_channel.state
#define g_demo_old_channel g_channel.old
#define g_demo_target_channel g_channel.target
static uint8_t g_channel_control_seq, g_channel_wire_cmd, g_channel_diag_pending;
static uint32_t g_channel_ack_generation, g_channel_ack_deadline;
static uint8_t g_channel_ack_valid;
typedef struct { uint16_t rx,expected;uint8_t channel;uint32_t at; } channel_quality_t;
static channel_quality_t g_channel_quality[8];
static volatile uint8_t g_channel_quality_head,g_channel_quality_tail;
static uint32_t g_channel_quality_overflow;
static volatile uint8_t g_channel_quality_invalid;
static volatile uint8_t g_channel_policy_reset, g_channel_policy_reset_ch;

static volatile rf_auto_tx_link_state_t g_demo_link_state = RF_AUTO_TX_UNCONNECTED;
static uint8_t g_demo_reconnecting = 0u;
static uint32_t g_demo_channel_enter_clock = 0u;
static uint32_t g_demo_discovery_switch_clock = 0u;
static uint8_t g_demo_discovery_side = 0u;
static uint16_t g_demo_last_quality = 0u;
static uint16_t g_demo_last_avg_irq_us = 0u;
static uint16_t g_demo_last_max_irq_us = 0u;
static uint8_t g_demo_ack_miss_count = 0u;
static uint8_t g_demo_irq_bad_window_count = 0u;
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


static void demo_link_quality_reset(uint32_t now,
                                    rf_auto_link_quality_state_t state);
static uint8_t demo_abort_radio(void);

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
    g_demo_next_ack_clock = now + MS1_TO_SYSTEM_TIME(rfc_ack_interval(g_demo_report_hz,g_channel.peer_caps)/1000u);
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

static void demo_channel_scores_init(void) { rfc_policy_init(&g_channel_policy,g_demo_current_channel,RF_LinkClockUs()); }
static void demo_link_quality_reset(uint32_t now,rf_auto_link_quality_state_t state) {
    (void)now;(void)state;
    g_channel_policy_reset_ch=g_channel.connected?g_channel.primary:g_demo_current_channel;
    g_channel_policy_reset=1;
}

/* Callers serialize state transitions against TMR0 and BLE callbacks. */
static uint8_t demo_abort_radio(void)
{
    ++g_demo_radio_generation;
    g_channel_ack_valid=0;
    TMR1_Disable();TMR1_ITCfg(DISABLE,TMR0_3_IT_CYC_END);TMR1_ClearITFlag(TMR0_3_IT_CYC_END);
    g_short_ack_wait=0;
    g_demo_pause_tx = 1u;
    uint8_t stopped=(RFRole_Stop()==SUCCESS);
    g_demo_tx_busy = 0u;
    g_demo_wait_ack_after_tx = 0u;
    g_demo_ack_rx_active = 0u;
    g_demo_ack_burst_left = 0u;
    g_demo_force_ack_burst = 0u;
    return stopped;
}

static uint8_t channel_radio_drained(void) {
    g_demo_pause_tx=1;
    return !g_demo_tx_launch_valid || !rfh_tx_guard_remaining(g_demo_tx_launch_cycles,demo_tx_cycle_now(),
        g_demo_tx_guard_cycles>g_demo_tx_control_guard_cycles?g_demo_tx_guard_cycles:g_demo_tx_control_guard_cycles);
}
static uint8_t channel_radio_apply(uint8_t ch) {
    if(!demo_abort_radio())return 0;
    g_channel_ack_valid=0;
    gParm.frequency=ch;
    if(RFRole_SetParam(&gParm)!=SUCCESS)return 0;
    gTxParam.frequency=gTxParam.whiteChannel=ch;gRxParam.frequency=gRxParam.whiteChannel=ch;
    g_demo_current_channel=ch;g_demo_channel_enter_clock=RF_LinkClockNow();
    g_demo_tx_launch_valid=0;return 1;
}
static void channel_radio_ready(uint8_t ch,uint8_t ok,uint32_t generation) {
    if(!ok){g_channel.reason=RFC_REASON_RADIO;g_channel.radio_failures++;g_channel.discovery=1;g_demo_pause_tx=1;return;}
    rfc_manager_radio_ready(&g_channel,ch,generation,RF_LinkClockUs());g_demo_pause_tx=0;
    if(g_channel.mode&RFC_MODE_PROBE) {
        if(ch==g_channel.target && g_channel.state==RFC_PROBE)rfc_radio_schedule(g_channel.old,g_channel.return_at);
        else if(ch==g_channel.old && g_channel.state==RFC_PROBE){g_channel.probe_count++;g_channel.state=RFC_RETURN;g_channel.deadline=RF_LinkClockUs()+RFC_CONFIRM_US;}
    }
}
static void demo_apply_channel(uint8_t ch) { rfc_radio_schedule(ch,RF_LinkClockUs()); }


static uint8_t demo_discovery_channel(uint8_t side)
{
    return ((side & 1u) == 0u) ?
           g_demo_bond_channel_b :
           g_demo_bond_channel_a;
}

static void demo_enter_tx_unconnected(uint32_t now)
{
    rfc_radio_cancel();rfc_manager_cancel(&g_channel);
    ++g_channel.wire_session;
    g_channel_quality_tail=g_channel_quality_head;
    g_channel_policy.probation=g_channel_policy.rollback=0;
    memset(&g_aux_tx,0,sizeof(g_aux_tx));memset(g_relative_tx,0,sizeof(g_relative_tx));
    g_relative_tag=0;g_short_measure=0;g_short_wire_seq=0;g_demo_seq=0;g_short_anchor_serial=0;
    (void)rfm_spi_bridge_emit_time_sync(0);
    rfm_spi_port_measure_enable(0);
    uint8_t anchor_channel = demo_discovery_channel(0u);

    demo_abort_radio();


    rfh_health_start(&g_demo_health, now);

    g_demo_link_state = RF_AUTO_TX_UNCONNECTED;
    g_demo_hop_state = RF_AUTO_HOP_COMM;

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
    rfc_manager_connect(&g_channel,channel,RF_LinkClockUs());
    if(!g_monitor_auto_hop_enabled && demo_channel_index(g_monitor_manual_channel) != 0xFFu)
        g_monitor_manual_pending = 1u;
    g_demo_reconnecting = 0u;
    g_demo_hop_state = RF_AUTO_HOP_COMM;

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
    rfc_manager_connect(&g_channel,channel,RF_LinkClockUs());
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
    uint8_t p[54]={0},type=0,len=0;uint32_t lock,generation;
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
    if(!type && !g_channel_diag_pending && !g_demo_diag_pending && g_short_measure && short_prepare_trace())return;
    SYS_DisableAllIrq(&lock);
    if(!type && g_channel_diag_pending) {
        p[0]=3;p[1]=g_channel.state;p[2]=g_channel.primary;p[3]=g_channel.target;
        p[4]=rfc_backup(g_channel.primary,0);p[5]=rfc_backup(g_channel.primary,1);
        rfc_manager_probe_window(&g_channel);p[6]=g_channel.probe_disable;p[7]=(g_channel.state==RFC_IDLE && g_channel_policy.reason)?g_channel_policy.reason:g_channel.reason;
        rfh_put_u16(p+8,(uint16_t)g_channel.budget_used);rfh_put_u16(p+10,g_channel_policy.baseline);
        rfh_put_u16(p+12,g_channel_policy.loss);p[14]=rfc_local_caps(g_demo_report_hz);p[15]=g_channel.peer_caps;
        rfh_put_u32(p+16,g_channel.switches);rfh_put_u32(p+20,g_channel.probe_count);
        rfh_put_u32(p+24,g_channel.failures);rfh_put_u32(p+28,g_channel.probe_failures);
        uint8_t ci=rfc_index(g_channel.target);p[36]=g_channel_policy.probation;
        if(ci!=0xff){rfc_history_t *h=&g_channel_policy.history[ci];
            p[37]=h->windows?1u:2u;rfh_put_u32(p+32,h->windows?h->expected:h->probe_expected);
            uint32_t age=(RF_LinkClockUs()-(h->windows?h->measured:h->probe_last))/1000u;
            rfh_put_u16(p+52,(h->windows||h->probes)?(age>65534u?65534u:age):65535u);
        } else rfh_put_u16(p+52,65535u);
        for(unsigned k=0;k<7;k++){uint16_t loss=g_channel_policy.history[k].loss;
            if(!g_channel_policy.history[k].windows || RF_LinkClockUs()-g_channel_policy.history[k].measured>=RFC_HISTORY_US)loss=RFC_UNKNOWN;
            rfh_put_u16(p+38+k*2,loss);}
        type=RFC_AUX_STATUS;len=54;g_channel_diag_pending=0;
    } else if(!type && g_monitor_battery_pending) {
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
static uint8_t demo_active_hop_cmd(void) {
    if(g_demo_link_state!=RF_AUTO_TX_COMM || !rfc_due(RF_LinkClockUs(),g_channel.retry_at))return 0;
    return g_channel.state==RFC_PREPARE?RFH_CMD_HOP_PREPARE:g_channel.state==RFC_CONFIRM?RFH_CMD_HOP_CONFIRM:
        g_channel.state==RFC_RECOVER?RFC_CMD_RECOVER:0;
}
static void demo_queue_manual_hop(uint8_t seq,uint8_t ch) {
    if(rfc_index(ch)==0xff)return;
    g_monitor_manual_seq=seq;g_monitor_manual_channel=ch;g_monitor_manual_pending=1;
}
static void demo_service_manual_hop(void) {
    if(!g_monitor_manual_pending || g_monitor_auto_hop_enabled || g_demo_link_state!=RF_AUTO_TX_COMM)return;
    if(g_demo_current_channel==g_monitor_manual_channel ||
       rfc_manager_begin(&g_channel,g_monitor_manual_channel,RFC_MODE_MANUAL,g_channel_policy.loss,RF_LinkClockUs())) {
        g_monitor_manual_pending=0;g_monitor_manual_applied_seq=g_monitor_manual_seq;
    }
}
static void demo_handle_command_ack(uint8_t cmd,uint8_t seq,uint8_t ch) {
    rfc_manager_ack(&g_channel,cmd,seq,ch,RF_LinkClockUs());
    if(cmd==RFH_CMD_RATE_UPDATE && seq==g_demo_rate_update_seq)g_demo_rate_update_pending=0;
}
static void channel_service(uint32_t now) {
    g_channel.hz=g_demo_report_hz;g_channel.auto_enabled=g_monitor_auto_hop_enabled;
    rfc_manager_poll(&g_channel,now);
    uint8_t ch;uint32_t at;
    if(rfc_manager_switch(&g_channel,&ch,&at))rfc_radio_schedule(ch,at);
    if(g_channel.want_ack && !g_demo_ack_rx_active && !g_demo_wait_ack_after_tx)g_demo_force_ack_burst=1;
    if(g_channel.discovery) {
        g_demo_recovery_reason=g_channel.reason;g_demo_reconnecting=1;
        demo_enter_tx_unconnected(RF_LinkClockNow());
    }
}
static void channel_policy_service(uint32_t now) {
    uint32_t quality_lock;
    SYS_DisableAllIrq(&quality_lock);
    uint8_t invalid=g_channel_quality_invalid;g_channel_quality_invalid=0;
    uint8_t reset=g_channel_policy_reset,reset_ch=g_channel_policy_reset_ch;
    g_channel_policy_reset=0;
    if(reset)g_channel_quality_tail=g_channel_quality_head;
    SYS_RecoverIrq(quality_lock);
    if(reset)rfc_policy_channel(&g_channel_policy,reset_ch,now);
    if(invalid)g_channel_policy.window_invalid=1;
    /* Drain small ISR-produced quality records; all ranking stays outside IRQ masking. */
    while(g_channel_quality_tail!=g_channel_quality_head) {
        channel_quality_t q;uint32_t lock;SYS_DisableAllIrq(&lock);
        q=g_channel_quality[g_channel_quality_tail];g_channel_quality_tail=(g_channel_quality_tail+1u)%8u;SYS_RecoverIrq(lock);
        rfc_policy_sample(&g_channel_policy,q.channel,q.rx,q.expected,q.at);
    }
    uint32_t lock;uint8_t committed,failed,old,target,mode,probe;
    SYS_DisableAllIrq(&lock);
    committed=g_channel.committed;failed=g_channel.failed;old=g_channel.old;target=g_channel.target;mode=g_channel.mode;
    probe=g_channel.probe_result;g_channel.committed=g_channel.failed=g_channel.probe_result=0;
    uint16_t probe_rx=g_channel.probe_received,probe_sent=g_channel.probe_sent;
    SYS_RecoverIrq(lock);
    if(committed)rfc_policy_committed(&g_channel_policy,old,target,mode,now);
    if(failed) {
        rfc_policy_failed(&g_channel_policy,target,now);
        if(mode&RFC_MODE_ROLLBACK) {
            SYS_DisableAllIrq(&lock);g_channel.discovery=1;SYS_RecoverIrq(lock);
        }
    }
    if(probe==1)rfc_policy_probe(&g_channel_policy,target,probe_rx,probe_sent,now);
    if(probe==2)g_channel_policy.probe_candidate=0xff;
    rfc_policy_poll(&g_channel_policy,now);
    if(!g_monitor_auto_hop_enabled || g_demo_link_state!=RF_AUTO_TX_COMM)return;
    uint8_t reason=0;target=rfc_policy_choose(&g_channel_policy,now,&reason);
    if(target==g_channel_policy.channel)return;
    mode=reason==RFC_REASON_ROLLBACK?RFC_MODE_ROLLBACK:reason==RFC_REASON_EMERGENCY?RFC_MODE_EMERGENCY:0;
    uint8_t i=rfc_index(target);
    if(!mode && i!=0xff && (!g_channel_policy.history[i].windows ||
       now-g_channel_policy.history[i].measured>=RFC_HISTORY_US) &&
       !(g_channel_policy.history[i].probes>=8 && g_channel_policy.history[i].probe_expected>=64 &&
         g_channel_policy.history[i].probe_last-g_channel_policy.history[i].probe_first>=2000000u) &&
       rfc_manager_probe_window(&g_channel))mode|=RFC_MODE_PROBE;
    SYS_DisableAllIrq(&lock);
    uint8_t accepted=rfc_manager_begin(&g_channel,target,mode,g_channel_policy.loss,now);
    SYS_RecoverIrq(lock);
    if(accepted){rfc_policy_started(&g_channel_policy,target,reason,now);
        if(mode&RFC_MODE_PROBE)g_channel_policy.probe_candidate=target;}
}

static void demo_handle_ack_packet(void)
{
    const uint8_t *air = &RxBuf[2];
    const uint8_t *data = &air[RFH_DATA_OFFSET];
    uint8_t cmd;
    uint8_t ack_flags;
    uint8_t seq;
    uint8_t channel;
    uint32_t now = RF_LinkClockNow();

    uint16_t received=0,expected=0;
    uint8_t valid=(rfh_flags(air[0])&RFC_ACK_QUALITY_VALID)!=0;
    if(rfh_packet_type(air[0])!=RFH_PKT_ACK || !(rfh_flags(air[0])&RFH_FLAG_LINK_OK) ||
       rfh_rate_code(air[0])!=g_demo_rate_code)return;
    if(g_demo_link_state!=RF_AUTO_TX_UNCONNECTED && (!g_channel_ack_valid ||
       g_channel_ack_generation!=g_demo_radio_generation || rfc_due(RF_LinkClockUs(),g_channel_ack_deadline) ||
       air[1]!=g_demo_active_ack_token))return;
    if(RxBuf[1]==RFH_SHORT_ACK_LEN) {
        received=data[0];expected=rfh_get_u16(data+1);
        memset(RxBuf+4,0,10);RxBuf[4+RFH_ACK_CHANNEL]=g_demo_current_channel;
        RxBuf[4+RFH_ACK_FLAGS]=RFH_SHORT_ACK_VERSION;
    } else if(RxBuf[1]==RFH_AIR_PACKET_LEN) {
        if(!(data[RFH_ACK_FLAGS]&RFH_SHORT_ACK_VERSION))return;
        received=rfh_get_u16(data);expected=rfh_get_u16(data+2);
    } else return;
    cmd=data[RFH_ACK_CMD_ID];ack_flags=data[RFH_ACK_FLAGS];channel=data[RFH_ACK_CHANNEL];seq=data[RFH_ACK_STATUS];
    if(cmd==RFH_CMD_MONITOR_CONFIG)channel=g_demo_current_channel;
    if(g_demo_link_state==RF_AUTO_TX_UNCONNECTED) {
        if(RxBuf[1]!=RFH_AIR_PACKET_LEN || data[0]!=g_channel.wire_session ||
           data[4]!=RFH_PROTOCOL_VERSION || (data[5]&15u)!=RFC_PROFILE_VERSION)return;
        g_channel.peer_caps=data[5]&0x70u;
    } else if(channel!=g_demo_current_channel)return;
    if((cmd==RFH_CMD_HOP_PREPARE || cmd==RFH_CMD_HOP_CONFIRM || cmd==RFC_CMD_RECOVER || cmd==RFC_CMD_PROBE_RESULT) &&
       data[5]!=g_channel.wire_session)return;
    g_channel_ack_valid=0;
    g_demo_last_quality=valid?rfc_loss(received,expected):RFC_UNKNOWN;
    g_demo_last_avg_irq_us=g_demo_last_max_irq_us=0;
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

    rfh_health_ack(&g_demo_health,now);g_demo_ack_miss_count=0;
    if(cmd==RFC_CMD_PROBE_RESULT && g_channel.state==RFC_RETURN && seq==g_channel.seq) {
        g_channel.probe_received=data[4];g_channel.probe_result=1;g_channel.state=RFC_IDLE;
    }
    rfc_manager_ack(&g_channel,cmd,seq,g_demo_current_channel,RF_LinkClockUs());
    if(valid && expected && received<=expected && g_channel.state==RFC_IDLE) {
        uint8_t next=(g_channel_quality_head+1u)%8u;
        if(next!=g_channel_quality_tail) {
            channel_quality_t *q=&g_channel_quality[g_channel_quality_head];
            q->rx=received;q->expected=expected;q->channel=g_demo_current_channel;q->at=RF_LinkClockUs();
            g_channel_quality_head=next;
        } else {g_channel_quality_overflow++;g_channel_quality_invalid=1;}
    }
    if(cmd == RFH_CMD_MONITOR_CONFIG)
    {
        /* The shared wire byte also carries the ACK format marker, which
         * must never become a monitor setting or configuration receipt. */
        uint8_t flags = data[RFH_ACK_MON_FLAGS] &
            (RFMON_FLAG_AUTO_HOP | RFH_MEASUREMENT_FLAG);
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
            (uint8_t)(RFH_FLAG_LINK_OK | (g_channel.state==RFC_FAST ? RFC_DATA_RECOVERY:0u) | (request_ack ? RFH_FLAG_CMD_ACK : 0u) |
                      (g_short_aux_sent==2u ? RFH_FLAG_CMD_PRESENT : 0u)));
        air[1]=g_demo_seq;
        TxBuf[1]=g_short_aux_sent ? RFH_AUX_LEN : RFH_SHORT_LEN;
        if(request_ack) g_demo_active_ack_token=g_demo_seq;

}

static void demo_fill_tx_packet(uint8_t request_ack, uint8_t ack_token, uint8_t ack_burst_left)
{
    uint8_t *air=TxBuf+2,*data=air+RFH_DATA_OFFSET;
    uint8_t hop_cmd=demo_active_hop_cmd();
    g_channel_wire_cmd=0;
    memset(TxBuf,0,sizeof(TxBuf));TxBuf[0]=RFH_WCH_PREAMBLE;
    TxBuf[1]=RFH_AIR_PACKET_LEN;
    if(g_demo_link_state==RF_AUTO_TX_UNCONNECTED) {
        air[0]=rfh_make_header0(RFH_PKT_CONNECT,g_demo_rate_code,RFH_FLAG_DUAL_REDUNDANT|RFH_FLAG_CMD_ACK);
        air[1]=g_demo_seq;
        rfh_put_u32(data+RFH_CONNECT_SESSION0,RFH_CONNECT_SESSION_ID);
        data[RFH_CONNECT_RATE]=g_demo_rate_code;
        data[RFH_CONNECT_CH_A]=RF_AUTO_DEMO_DISCOVERY_CHANNEL_A|((RFC_PROFILE_VERSION&3u)<<6);
        data[RFH_CONNECT_CH_B]=RF_AUTO_DEMO_DISCOVERY_CHANNEL_B;
        data[RFH_CONNECT_ACK_WINDOW_MS]=g_channel.wire_session;
        data[RFH_CONNECT_OPTIONS]=g_demo_connect_packet_stage|rfc_local_caps(g_demo_report_hz);
        data[RFH_CONNECT_VERSION]=RFH_PROTOCOL_VERSION;
        return;
    }
    if(!request_ack || hop_cmd==RFH_CMD_NONE) {g_channel_wire_cmd=0;demo_fill_short_packet(request_ack);return;}
    air[0]=rfh_make_header0(RFH_PKT_DATA,g_demo_rate_code,RFH_FLAG_LINK_OK|RFH_FLAG_CMD_PRESENT|RFH_FLAG_CMD_ACK);
    air[1]=++g_channel_control_seq;g_demo_active_ack_token=air[1];
    g_channel_wire_cmd=rfc_manager_control(&g_channel,data,RF_LinkClockUs());
    if(!g_channel_wire_cmd)demo_fill_short_packet(request_ack);

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
          (unsigned int)g_channel_policy.loss,
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
                       ((rfc_local_caps(g_demo_report_hz) & g_channel.peer_caps & RFC_CAP_ACK) ? 650u : RF_AUTO_DEMO_ACK_RX_TIMEOUT_UNITS);
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

/* Connected DATA/control completions have no callback ownership. Open RX
 * after the immutable packet's guard using a dedicated timer at every rate. */
__INTERRUPT
__HIGH_CODE
void TMR1_IRQHandler(void)
{
    if(!TMR1_GetITFlag(TMR0_3_IT_CYC_END))return;
    TMR1_ClearITFlag(TMR0_3_IT_CYC_END);TMR1_Disable();
    if(!g_short_ack_wait || g_channel_ack_generation!=g_demo_radio_generation || g_demo_pause_tx)return;
    uint32_t left=rfh_tx_guard_remaining(g_short_ack_launch,demo_tx_cycle_now(),g_demo_tx_guard_cycles);
    if(left){TMR1_TimerInit(left);return;}
    uint32_t lock;SYS_DisableAllIrq(&lock);
    g_short_ack_wait=0;g_demo_tx_busy=0;g_demo_wait_ack_after_tx=0;demo_arm_ack_rx();
    SYS_RecoverIrq(lock);
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
        g_demo_next_ack_clock += MS1_TO_SYSTEM_TIME(rfc_ack_interval(g_demo_report_hz,g_channel.peer_caps)/1000u);
    } while((int32_t)(now - g_demo_next_ack_clock) >= 0);

    g_demo_force_ack_burst = 1u;
}

static void demo_note_ack_timeout(void)
{
    uint32_t now = RF_LinkClockNow();
    if(g_demo_link_state == RF_AUTO_TX_UNCONNECTED) return;
    g_channel_ack_valid=0;
    rfc_manager_timeout(&g_channel,RF_LinkClockUs());
    if(!rfc_fast_enabled(g_demo_report_hz,g_channel.peer_caps))rfh_health_failed(&g_demo_health, now, MS1_TO_SYSTEM_TIME(10u));
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
        g_short_ack_wait=0;g_demo_tx_busy=0;g_demo_wait_ack_after_tx=0;demo_arm_ack_rx();
    }
    if(g_demo_link_state!=RF_AUTO_TX_UNCONNECTED && (rfc_local_caps(g_demo_report_hz) & g_channel.peer_caps & RFC_CAP_ACK) &&
       g_channel_ack_deadline && !rfc_due(RF_LinkClockUs(),g_channel_ack_deadline)) {
        g_demo_stat.report_due++;g_demo_diag_due++;g_demo_stat.report_drop++;g_demo_diag_dropped++;g_short_ack_slots++;return;
    }
    g_demo_stat.report_due++; g_demo_diag_due++;
#if (RF_AUTO_DEMO_TX_IN_ISR != 0u)
    if(g_demo_config_ret == SUCCESS)
    {
        bStatus_t ret;
        uint8_t request_ack = 0u;
        uint8_t ack_token = 0u;
        uint8_t ack_burst_left = 0u;
        uint8_t maintenance_allowed = 1u;
        uint32_t maintenance_cost = 0u;

        if((g_demo_pause_tx != 0u) ||
           (g_demo_tx_busy != 0u) ||
           (g_demo_ack_rx_active != 0u) ||
           (g_demo_wait_ack_after_tx != 0u))
        {
            if(g_demo_ack_rx_active || g_demo_wait_ack_after_tx)++g_short_ack_slots;
            g_demo_stat.report_drop++; g_demo_diag_dropped++;
            return;
        }

        /* Check admission BEFORE pausing input for a long control packet.
         * A deferred transaction must keep sending ordinary DATA; otherwise
         * each DATA launch restarts the 300 us drain and drops two 8K slots
         * even though the maintenance budget cannot admit the transaction.
         * This check only ages the budget; charge once after the guards pass. */
        if(g_demo_link_state != RF_AUTO_TX_UNCONNECTED && g_demo_force_ack_burst &&
           g_channel.state != RFC_FAST && g_channel.state != RFC_RECOVER) {
            maintenance_cost = rfc_ack_window(g_demo_report_hz,g_channel.peer_caps) +
                1000000u/g_demo_report_hz + RFC_FIRST_PACKET_US +
                (demo_active_hop_cmd() ? 600u : 0u);
            maintenance_allowed = rfc_manager_budget_available(&g_channel,
                RF_LinkClockUs(),maintenance_cost);
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
            (g_demo_force_ack_burst && maintenance_allowed && demo_active_hop_cmd() != RFH_CMD_NONE)) &&
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
        else if(g_demo_force_ack_burst && g_channel.state!=RFC_FAST && g_channel.state!=RFC_RECOVER &&
                (!maintenance_allowed ||
                 !rfc_manager_budget(&g_channel,RF_LinkClockUs(),maintenance_cost))) {
            request_ack=0;
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
        else g_demo_tx_guard_cycles=g_channel_control_air_guard;
        uint8_t first_after_channel=rfc_radio_first();
        gTxParam.sendTime=first_after_channel?RFC_FIRST_SEND_UNITS:RF_AUTO_DEMO_TX_SEND_TIME_UNITS;
        if(first_after_channel)g_demo_tx_guard_cycles+=(RFC_FIRST_SEND_UNITS-RF_AUTO_DEMO_TX_SEND_TIME_UNITS)/2u*(GetSysClock()/1000000u);
        /* Refresh countdown at actual launch, after any DMA guard wait. */
        if(g_channel_wire_cmd==RFH_CMD_HOP_CONFIRM) {
            uint32_t left=g_channel.activate_at-RF_LinkClockUs();
            if(left>RFC_ACTIVATE_LEAD_US){g_demo_tx_busy=g_demo_wait_ack_after_tx=0;return;}
            rfh_put_u16(TxDmaBuf[g_demo_tx_dma_slot]+6,(uint16_t)left);
        }
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
        if(g_demo_tx_start_ret==SUCCESS && ret==SUCCESS) {
            rfc_radio_sent();
            if(rfh_is_short(TxBuf[1])){g_demo_seq++;g_short_wire_seq++;if(g_channel.state==RFC_PROBE && g_demo_current_channel==g_channel.target)g_channel.probe_sent++;}
            rfc_manager_sent(&g_channel,g_channel_wire_cmd,RF_LinkClockUs());
            if(request_ack && g_demo_link_state!=RF_AUTO_TX_UNCONNECTED) {
                g_channel_ack_generation=g_demo_radio_generation;g_channel_ack_valid=1;
                uint32_t air_guard=g_demo_tx_guard_cycles/(GetSysClock()/1000000u);
                g_channel.request_at=RF_LinkClockUs();
                g_channel_ack_deadline=g_channel.request_at+air_guard+rfc_ack_window(g_demo_report_hz,g_channel.peer_caps);
                {
                    g_short_ack_launch=g_demo_tx_launch_cycles;g_short_ack_wait=1;
                    uint32_t left=rfh_tx_guard_remaining(g_short_ack_launch,demo_tx_cycle_now(),g_demo_tx_guard_cycles);
                    TMR1_Disable();TMR1_ClearITFlag(TMR0_3_IT_CYC_END);
                    TMR1_TimerInit(left?left:2u*(GetSysClock()/1000000u));
                    TMR1_ITCfg(ENABLE,TMR0_3_IT_CYC_END);
                }
            }
        }
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
    if(!g_demo_tx_busy || (g_demo_link_state!=RF_AUTO_TX_UNCONNECTED && !demo_pair_is_active()))sta &= ~RF_STATE_TX_FINISH;
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
    g_demo_diag_pending = 1u;g_channel_diag_pending=1;
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
        if(rfh_health_poll(&g_demo_health, now, MS1_TO_SYSTEM_TIME(100u), MS1_TO_SYSTEM_TIME(500u))) {
            g_demo_recovery_reason = 2u;
            g_demo_reconnecting = (g_demo_link_state == RF_AUTO_TX_COMM);
            demo_enter_tx_unconnected(now);
            rfm_spi_bridge_emit_state_changed(0x02u);
        } else {
            g_demo_ack_miss_count = g_demo_health.misses;

            if(rfh_health_retry(&g_demo_health, now)) g_demo_force_ack_burst = 1u;
        }
    }
#if (RF_AUTO_DEMO_TX_IN_ISR == 0u)
    demo_try_send();
#endif
    demo_check_ack_rx_stuck(now);
    demo_service_connect_phase(now);

    demo_service_link(now);
    demo_ack_control_service(now);
    channel_service(RF_LinkClockUs());
    demo_service_manual_hop();
    SYS_RecoverIrq(irq_status);
    channel_policy_service(RF_LinkClockUs());
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
        rfc_radio_cancel();rfc_manager_cancel(&g_channel);
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
    rfc_radio_cancel();rfc_manager_cancel(&g_channel);
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
            (g_demo_hop_state != RFC_RECOVER) && (g_demo_hop_state != RFC_FAST) && (g_demo_hop_state != RFC_VERIFY) &&
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
     * Connected control packets also use timed ownership. Preserve a conservative
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
        g_channel_control_air_guard = cycles_per_us *
            ((RFH_AIR_PACKET_LEN + 11u) * byte_us + 24u +
             (RF_AUTO_DEMO_TX_SEND_TIME_UNITS + 1u) / 2u + 12u);
        g_demo_tx_guard_cycles=g_channel_control_air_guard;
        g_demo_tx_wait_limit = cycles_per_us * 64u;
        g_demo_tx_control_guard_cycles = cycles_per_us * RFC_CALLBACK_DRAIN_US;
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
    rfc_manager_init(&g_channel,1,g_demo_current_channel,g_demo_report_hz,RF_LinkClockUs());
    const rfc_radio_ops_t radio_ops={channel_radio_drained,channel_radio_apply,channel_radio_ready};
    rfc_radio_init(&radio_ops);
    PFIC_SetPriority(TMR1_IRQn,0x80);PFIC_EnableIRQ(TMR1_IRQn);
    demo_channel_scores_init();

    demo_arm_next_ack_clock(RF_LinkClockNow());

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
