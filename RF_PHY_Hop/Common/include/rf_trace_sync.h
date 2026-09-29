#ifndef RF_TRACE_SYNC_H
#define RF_TRACE_SYNC_H
#include <stdint.h>
typedef struct { uint32_t requested, wait_us; uint8_t seq, valid, sent; } rfh_trace_sync_t;
static inline void rfh_trace_sync_request(volatile rfh_trace_sync_t *s, uint8_t seq, uint32_t now) {
    s->valid=0; s->sent=0; s->seq=seq; s->requested=now; s->wait_us=0; s->valid=1;
}
/* Save the FIRST ACK preparation, before the radio transmits. Retries must not
 * inflate residence time: an echo may belong to the first transmission. */
static inline void rfh_trace_sync_ack(volatile rfh_trace_sync_t *s, uint8_t seq,
                                    uint32_t now, uint32_t wrap, uint32_t cycles_per_us) {
    uint32_t delta;
    if(!s->valid || s->seq!=seq || s->sent || !cycles_per_us) return;
    delta=now>=s->requested ? now-s->requested : wrap-s->requested+now;
    s->wait_us=delta/cycles_per_us; s->sent=1;
}
#endif
