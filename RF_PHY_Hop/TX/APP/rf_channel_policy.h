#ifndef RF_CHANNEL_POLICY_H
#define RF_CHANNEL_POLICY_H
#include "rf_channel_protocol.h"
typedef struct {
    uint32_t received, expected, measured, quarantine;
    uint32_t probe_received, probe_expected, probe_first, probe_last;
    uint16_t loss, windows, probes;
} rfc_history_t;
typedef struct {
    rfc_history_t history[RFH_HOP_CHANNEL_COUNT];
    uint32_t window_at, received, expected, last_sample;
    uint32_t recent_rx[3],recent_expected[3];
    uint32_t cooldown, emergency_after, explore_after;
    uint32_t probation_until, probation_rx, probation_expected;
    uint16_t baseline, loss;
    uint8_t channel, bad, good, emergency, cursor, recent_index;
    uint8_t probation, probation_old, probation_windows, worse, rollback, reason;
    uint8_t probe_candidate, window_invalid;
} rfc_policy_t;
void rfc_policy_init(rfc_policy_t *p,uint8_t channel,uint32_t now);
void rfc_policy_channel(rfc_policy_t *p,uint8_t channel,uint32_t now);
void rfc_policy_sample(rfc_policy_t *p,uint8_t channel,uint16_t rx,uint16_t expected,uint32_t now);
void rfc_policy_poll(rfc_policy_t *p,uint32_t now);
uint8_t rfc_policy_choose(rfc_policy_t *p,uint32_t now,uint8_t *reason);
void rfc_policy_started(rfc_policy_t *p,uint8_t target,uint8_t reason,uint32_t now);
void rfc_policy_committed(rfc_policy_t *p,uint8_t old,uint8_t target,uint8_t mode,uint32_t now);
void rfc_policy_failed(rfc_policy_t *p,uint8_t target,uint32_t now);
void rfc_policy_probe(rfc_policy_t *p,uint8_t ch,uint16_t rx,uint16_t sent,uint32_t now);
#endif
