#ifndef RF_CHANNEL_PROTOCOL_H
#define RF_CHANNEL_PROTOCOL_H
#include <stdint.h>
#include "rf_hop_protocol.h"

/* v3: timing capabilities are opt-in acceptance results, never inferred from
 * a successful build. Both ends must carry the same accepted profile. */
#define RFC_PROFILE_VERSION 2u
#ifndef RFC_TIMING_ACCEPTED
#define RFC_TIMING_ACCEPTED 0
#endif
#ifndef RFC_ACK_TIMING_ACCEPTED
#define RFC_ACK_TIMING_ACCEPTED 0
#endif
#ifndef RFC_PROBE_TIMING_ACCEPTED
#define RFC_PROBE_TIMING_ACCEPTED 0
#endif
#define RFC_CAP_SWITCH 0x10u
#define RFC_CAP_ACK 0x20u
#define RFC_CAP_PROBE 0x40u
#define RFC_CAPS ((RFC_TIMING_ACCEPTED ? RFC_CAP_SWITCH : 0u) | \
 (RFC_ACK_TIMING_ACCEPTED ? RFC_CAP_ACK : 0u) | \
 (RFC_PROBE_TIMING_ACCEPTED ? RFC_CAP_PROBE : 0u))
/* Acceptance is per rate as well as per matching firmware/profile. Bit 0..3
 * means 1K/2K/4K/8K. Leave zero until a recorded hardware acceptance exists. */
#ifndef RFC_SWITCH_ACCEPTED_RATES
#define RFC_SWITCH_ACCEPTED_RATES 0u
#endif
#ifndef RFC_ACK_ACCEPTED_RATES
#define RFC_ACK_ACCEPTED_RATES 0u
#endif
#ifndef RFC_PROBE_ACCEPTED_RATES
#define RFC_PROBE_ACCEPTED_RATES 0u
#endif
/* Engineering starting guards, NOT a measured hardware bound. */
#define RFC_SETTLE_US 100u
#define RFC_FIRST_SEND_UNITS 120u
#define RFC_DRAIN_US 1000u
#define RFC_CALLBACK_DRAIN_US 300u
#define RFC_SKEW_US 125u
#define RFC_FIRST_PACKET_US 180u
#define RFC_ACTIVATE_LEAD_US 5000u
#define RFC_PREPARE_US 100000u
#define RFC_CONFIRM_US 100000u
#define RFC_RECOVER_US 200000u
#define RFC_RETRY_US 10000u
#define RFC_HISTORY_US 60000000u
#define RFC_COOLDOWN_US 30000000u
#define RFC_QUARANTINE_US 60000000u
#define RFC_PROBATION_US 3000000u
#define RFC_BUDGET_US 30000u
#define RFC_UNKNOWN 0xffffu
#define RFC_CMD_RECOVER 0x13u
#define RFC_CMD_PROBE_RESULT 0x14u
#define RFC_AUX_STATUS 6u
#define RFC_DATA_RECOVERY RFH_FLAG_DUAL_REDUNDANT
#define RFC_ACK_QUALITY_VALID RFH_FLAG_CMD_ACK
#define RFC_MODE_PROBE 1u
#define RFC_MODE_MANUAL 2u
#define RFC_MODE_ROLLBACK 4u
#define RFC_MODE_EMERGENCY 8u
#define RFC_MODE_RECOVERY 16u /* Local commit classification; never sent as a transaction mode. */
/* 12-byte transaction: header/type + request token, then cmd, target,
 * delay-us LE16, transaction id, old, dwell-us/quality LE16,
 * session-high-nibble|profile, session-low-nibble<<4|mode.
 * CONNECT byte 7 negotiates the 8-bit session; CONNECT ACK byte 0 echoes it.
 * Transaction ACK byte 5 echoes it; probe result count fits in byte 4
 * (at most eight opportunities in a sub-millisecond excursion).
 * Delay is anchored to RFIP_SetTxStart, RX subtracts modelled on-air latency.
 * The difference remains inside the explicit, unaccepted skew allowance. */
enum { RFC_IDLE, RFC_PREPARE, RFC_CONFIRM, RFC_ARMED, RFC_VERIFY,
       RFC_RECOVER, RFC_PROBE, RFC_RETURN, RFC_FAST };
enum { RFC_REASON_NONE, RFC_REASON_BAD, RFC_REASON_EMERGENCY,
       RFC_REASON_MANUAL, RFC_REASON_ROLLBACK, RFC_REASON_ACK,
       RFC_REASON_PREPARE_TIMEOUT, RFC_REASON_CONFIRM_TIMEOUT,
       RFC_REASON_RADIO, RFC_REASON_PROBE_FAILED, RFC_REASON_INSUFFICIENT,
       RFC_REASON_ACCEPTED, RFC_REASON_NO_IMPROVEMENT };
enum { RFC_PROBE_OK, RFC_PROBE_UNACCEPTED, RFC_PROBE_PEER,
       RFC_PROBE_RATE, RFC_PROBE_WINDOW, RFC_PROBE_BUDGET };
static inline uint8_t rfc_due(uint32_t now, uint32_t at) { return (int32_t)(now-at)>=0; }
static inline uint8_t rfc_index(uint8_t ch) {
    for(uint8_t i=0;i<RFH_HOP_CHANNEL_COUNT;i++) if(rfh_hop_channel_at(i)==ch)return i;
    return 0xffu;
}
static inline uint8_t rfc_backup(uint8_t ch,uint8_t n) {
    uint8_t i=rfc_index(ch);if(i==0xffu)i=0;
    return rfh_hop_channel_at((i+(n ? 3u : 1u))%RFH_HOP_CHANNEL_COUNT);
}
static inline uint16_t rfc_loss(uint32_t received,uint32_t expected) {
    if(!expected || received>expected)return RFC_UNKNOWN;
    return (uint16_t)(((uint64_t)(expected-received)*1000u)/expected);
}
static inline uint8_t rfc_improved(uint16_t before,uint16_t after) {
    return before!=RFC_UNKNOWN && after!=RFC_UNKNOWN && before>=after+20u &&
           (uint32_t)after*4u <= (uint32_t)before*3u;
}
typedef struct {
    uint16_t hz,period_us,ack_window_us;
    uint32_t ack_interval_us;
} rfc_rate_profile_t;
static inline rfc_rate_profile_t rfc_rate_profile(uint16_t hz) {
    static const rfc_rate_profile_t profiles[4]={
        {1000,1000,1000,40000}, {2000,500,500,20000},
        {4000,250,500,20000}, {8000,125,500,20000}
    };
    return profiles[rfh_rate_code_from_hz(hz)];
}
static inline uint8_t rfc_local_caps(uint16_t hz) {
    if(hz!=1000 && hz!=2000 && hz!=4000 && hz!=8000)return 0;
    uint8_t bit=1u<<rfh_rate_code_from_hz(hz),caps=0;
    if(RFC_TIMING_ACCEPTED && (RFC_SWITCH_ACCEPTED_RATES&bit))caps|=RFC_CAP_SWITCH;
    if(RFC_ACK_TIMING_ACCEPTED && (RFC_ACK_ACCEPTED_RATES&bit))caps|=RFC_CAP_ACK;
    if(RFC_PROBE_TIMING_ACCEPTED && (RFC_PROBE_ACCEPTED_RATES&bit))caps|=RFC_CAP_PROBE;
    return caps;
}
static inline uint8_t rfc_fast_enabled(uint16_t hz,uint8_t peer) {
    return (rfc_local_caps(hz) & peer & (RFC_CAP_SWITCH|RFC_CAP_ACK))==(RFC_CAP_SWITCH|RFC_CAP_ACK);
}
static inline uint32_t rfc_ack_interval(uint16_t hz,uint8_t peer) {
    return (rfc_local_caps(hz) & peer & RFC_CAP_ACK) ? rfc_rate_profile(hz).ack_interval_us:100000u;
}
static inline uint32_t rfc_ack_window(uint16_t hz,uint8_t peer) {
    return (rfc_local_caps(hz) & peer & RFC_CAP_ACK) ? rfc_rate_profile(hz).ack_window_us:1750u;
}
#endif
