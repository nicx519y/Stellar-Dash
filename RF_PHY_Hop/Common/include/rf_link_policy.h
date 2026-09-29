#ifndef RF_LINK_POLICY_H
#define RF_LINK_POLICY_H
#include <stdint.h>

/* Pure policy shared by firmware and native fault-injection tests. All clocks
 * are unsigned monotonic ticks; intervals must remain below half the range. */
typedef struct {
    uint32_t window_start, last_ack, retry_at;
    uint8_t good, misses, retries, retry_pending;
} rfh_health_t;

static inline void rfh_health_start(rfh_health_t *h, uint32_t now)
{
    h->window_start = h->last_ack = now;
    h->retry_at = 0;
    h->good = h->misses = h->retries = h->retry_pending = 0;
}
static inline void rfh_health_ack(rfh_health_t *h, uint32_t now)
{
    h->last_ack = now;
    h->good = 1;
    h->misses = h->retry_pending = 0;
}
static inline void rfh_health_failed(rfh_health_t *h, uint32_t now, uint32_t retry)
{
    if(!h->good && !h->retry_pending && h->retries < 2) {
        h->retry_at = now + retry;
        h->retry_pending = 1;
    }
}
static inline uint8_t rfh_health_poll(rfh_health_t *h, uint32_t now,
                                      uint32_t window, uint32_t hard_timeout)
{
    if((uint32_t)(now - h->last_ack) >= hard_timeout) return 1;
    while((uint32_t)(now - h->window_start) >= window) {
        h->window_start += window;
        if(h->good) h->misses = 0;
        else if(h->misses < 255) ++h->misses;
        h->good = h->retries = h->retry_pending = 0;
        if(h->misses >= 3) return 1;
    }
    return 0;
}
static inline uint8_t rfh_health_retry(rfh_health_t *h, uint32_t now)
{
    if(!h->retry_pending || (int32_t)(now - h->retry_at) < 0) return 0;
    h->retry_pending = 0;
    ++h->retries;
    return 1;
}

typedef struct {
    uint32_t received_at;
    uint8_t valid, seq;
} rfh_sequence_t;

/* The existing RX interprets this byte in report-rate slots. Reserve at
 * least 250 us of silence, rather than predicting future TX completions.
 * At 1K the one-slot reservation is 1000 us, within the 1200 us RX window. */
static inline uint8_t rfh_ack_guard_slots(uint16_t report_hz)
{
    return report_hz > 4000u ? 2u : 1u;
}

static inline uint32_t rfh_tx_guard_remaining(uint32_t launched, uint32_t now,
                                              uint32_t guard)
{
    uint32_t elapsed = now - launched;
    return elapsed < guard ? guard - elapsed : 0u;
}
/* Zero means duplicate/old; 1 establishes a new baseline after ambiguity.
 * A sequence is never used to estimate loss across >= half a wrap. */
static inline uint8_t rfh_sequence_accept(rfh_sequence_t *s, uint8_t seq,
                                         uint32_t now, uint32_t half_wrap)
{
    uint8_t diff = (uint8_t)(seq - s->seq);
    if(!s->valid || (uint32_t)(now - s->received_at) >= half_wrap) diff = 1;
    else if(diff == 0 || diff >= 128) return 0;
    s->valid = 1;
    s->seq = seq;
    s->received_at = now;
    return diff;
}

/* Timer0 wraps at a hardware modulus (not UINT32_MAX). */
static inline uint32_t rfh_timer_elapsed(uint32_t start, uint32_t end, uint32_t wrap)
{
    return end >= start ? end - start : wrap - start + end;
}
static inline uint32_t rfh_ack_delay(uint32_t received, uint32_t now,
                                    uint32_t wrap, uint32_t offset)
{
    uint32_t elapsed = rfh_timer_elapsed(received, now, wrap);
    return elapsed < offset ? offset - elapsed : 0;
}

typedef struct {
    uint8_t valid, seq, old_channel, target;
    uint32_t deadline;
} rfh_hop_transaction_t;
/* Return 1 for a new transaction, 2 for a repeat, 0 for invalid/stale. */
static inline uint8_t rfh_hop_prepare(rfh_hop_transaction_t *h, uint8_t seq,
                                      uint8_t old, uint8_t target,
                                      uint32_t now, uint32_t timeout)
{
    if(old == target) return 0;
    if(h->valid) {
        uint8_t delta = (uint8_t)(seq - h->seq);
        if(delta == 0) return (h->old_channel == old && h->target == target) ? 2 : 0;
        if(delta >= 128) return 0;
    }
    h->valid = 1; h->seq = seq; h->old_channel = old; h->target = target;
    h->deadline = now + timeout;
    return 1;
}
static inline uint8_t rfh_hop_confirm(const rfh_hop_transaction_t *h,
                                      uint8_t seq, uint8_t target, uint8_t channel)
{
    return h->valid && h->seq == seq && h->target == target && target == channel;
}
#endif
