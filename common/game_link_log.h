#ifndef XORA_GAME_LINK_LOG_H
#define XORA_GAME_LINK_LOG_H
#include <stdint.h>

/* Stored in the existing 96-byte journal record. Never change old event IDs. */
#define GL_EVENT_BEGIN 200u
#define GL_EVENT_UPDATE 201u
#define GL_EVENT_RECOVERED 202u
#define GL_EVENT_CONTEXT 203u
#define GL_EVENT_ACTION 204u
#define GL_VERSION 1u
#define GL_MODE_USB 1u
#define GL_MODE_RF 2u

#define GL_USB_LOST       (1u << 0)
#define GL_USB_RESET      (1u << 1)
#define GL_USB_SUSPEND    (1u << 2)
#define GL_OUTPUT_STALLED (1u << 3)
#define GL_INPUT_STALE    (1u << 4)
#define GL_CONGESTION     (1u << 5)
#define GL_RATE_FALLBACK  (1u << 6)
#define GL_BOARD_FAULT    (1u << 7)
#define GL_RF_LOST        (1u << 8)
#define GL_RADIO_FAULT    (1u << 9)
#define GL_NEUTRAL        (1u << 10)
#define GL_RECOVERY_FAILED (1u << 11)
#define GL_START_TIMEOUT  (1u << 12)
#define GL_FRAME_ERROR    (1u << 13)
#define GL_POWER_STOP     (1u << 14)
#define GL_NEUTRAL_DONE   (1u << 15)
#define GL_CONFIRMED_DISCONNECT (1u << 16)
#define GL_DISCONNECT_CONFIRM_MS 5000u

#define GL_ENABLED       (1u << 0)
#define GL_USB_READY     (1u << 1)
#define GL_SUSPENDED     (1u << 2)
#define GL_EXPECT_INPUT  (1u << 3)
#define GL_OUTPUT_PENDING (1u << 4)
#define GL_CHECK_USB     (1u << 5)
#define GL_RF_CONNECTED  (1u << 6)

typedef struct {
    uint32_t received, completed, dropped, notices;
    uint32_t status, detail;
    uint16_t rate;
    uint8_t mode, profile;
} gl_sample;

/* Main-loop owned. Hot paths only update the four counters below, under their
 * existing interrupt ownership. No clocks, CRCs, allocation or Flash in ISR. */
typedef struct {
    uint32_t received, completed, dropped, input_at, complete_at;
    uint32_t active, incident, since, good_at, status;
    uint32_t drop_at, drop_base, link_wait_at;
    uint8_t mode, seen_input, seen_output, save_requested;
    uint32_t disconnect_since;
    uint8_t connected, disconnect_tracking;
} gl_state;

static inline uint32_t gl_gap_limit(uint16_t rate) {
    uint32_t ms = rate ? (32000u + rate - 1u) / rate : 32u;
    return ms < 20u ? 20u : ms;
}
/* Returns newly observed symptoms. Missing packets and state coalescing are
 * not interchangeable: congestion requires drops AND delayed output. */
static inline uint32_t gl_observe(gl_state *s, const gl_sample *p, uint32_t now) {
    uint32_t faults = p->notices;
    if (!s->mode || s->mode != p->mode || !(s->status & GL_ENABLED)) {
        s->input_at = s->complete_at = s->drop_at = now;
        s->received = p->received; s->completed = p->completed;
        s->drop_base = p->dropped; s->seen_input = s->seen_output = 0;
        s->connected = s->disconnect_tracking = 0;
    }
    if (p->received != s->received) { s->seen_input = 1; s->input_at = now; }
    if (p->completed != s->completed) { s->seen_output = 1; s->complete_at = now; }
    if ((p->status & GL_EXPECT_INPUT) && !(s->status & GL_EXPECT_INPUT)) s->input_at = now;
    if ((p->status & GL_OUTPUT_PENDING) && !(s->status & GL_OUTPUT_PENDING)) s->complete_at = now;
    if (s->mode == p->mode && (s->status & p->status & GL_ENABLED)) {
        if ((s->status & GL_USB_READY) && !(p->status & GL_USB_READY)) faults |= GL_USB_LOST;
        if (!(s->status & GL_SUSPENDED) && (p->status & GL_SUSPENDED)) faults |= GL_USB_SUSPEND;
        if ((s->status & GL_RF_CONNECTED) && !(p->status & GL_RF_CONNECTED)) faults |= GL_RF_LOST;
    }
    if ((p->status & GL_ENABLED) && !(p->status & GL_SUSPENDED)) {
        if ((p->status & GL_EXPECT_INPUT) && now - s->input_at >= 50u)
            faults |= GL_INPUT_STALE;
        if ((p->status & (GL_CHECK_USB|GL_USB_READY|GL_OUTPUT_PENDING)) ==
                (GL_CHECK_USB|GL_USB_READY|GL_OUTPUT_PENDING) &&
            now - s->complete_at >= gl_gap_limit(p->rate)) faults |= GL_OUTPUT_STALLED;
        if (now - s->drop_at >= 100u) {
            if (p->dropped - s->drop_base >= 8u && now - s->complete_at >= 5u)
                faults |= GL_CONGESTION;
            s->drop_at = now; s->drop_base = p->dropped;
        }
    }
    /* A powered but unused receiver is not a failed game session. Only start
     * the RF/receiver-USB connection deadline once real input has arrived. */
    const uint8_t waiting = (p->status & GL_ENABLED) && !(p->status & GL_SUSPENDED) &&
        s->seen_input && p->mode==GL_MODE_RF &&
        ((p->status & GL_CHECK_USB) ? !(p->status & GL_USB_READY) : !(p->status & GL_RF_CONNECTED));
    if(waiting) {
        if(!s->link_wait_at) s->link_wait_at=now?now:1u;
        else if(now-s->link_wait_at>=5000u) faults|=GL_START_TIMEOUT;
    } else s->link_wait_at=0;
    /* A transient reset/loss flag, a suspended host, and a receiver that has
     * never connected do not authorize persistence. This observes existing
     * transport state only; it never changes reconnect timing or hardware. */
    if ((p->status & GL_ENABLED) && !(p->status & GL_SUSPENDED)) {
        s->connected |= (uint8_t)(p->status & (GL_USB_READY | GL_RF_CONNECTED));
        const uint8_t missing = s->connected & (uint8_t)~p->status;
        if (missing) {
            if (!s->disconnect_tracking) {
                s->disconnect_tracking = 1; s->disconnect_since = now;
            } else if (now - s->disconnect_since >= GL_DISCONNECT_CONFIRM_MS) {
                faults |= GL_CONFIRMED_DISCONNECT;
                if (missing & GL_USB_READY) faults |= GL_USB_LOST;
                if (missing & GL_RF_CONNECTED) faults |= GL_RF_LOST;
            }
        } else s->disconnect_tracking = 0;
    } else s->disconnect_tracking = 0;
    s->received = p->received; s->completed = p->completed; s->dropped = p->dropped;
    s->mode = p->mode; s->status = p->status;
    return faults;
}

#ifdef __cplusplus
extern "C" {
#endif
extern volatile uint32_t g_game_log_received, g_game_log_completed;
extern volatile uint32_t g_game_log_dropped, g_game_log_notices;
void XoraLog_GameSample(const gl_sample *sample);
void XoraLog_GameFault(uint8_t mode, uint32_t reasons, uint32_t detail);
void XoraLog_GameAction(uint8_t mode, uint32_t action, uint32_t result);
void XoraLog_GamePause(void);
void XoraLog_GameRecovered(uint8_t mode, uint32_t detail);
int XoraLog_GameSaveRequested(void);
void XoraLog_DeferFaultWindow(void);
/* Only at boot or an already stopped maintenance window. Never stop ADC,
 * RF or USB to create a logging window. Only confirmed disconnect/fatal
 * evidence is eligible; failures remain pending without busy-loop retries. */
void XoraLog_SaveFaultWindow(void);
#ifdef __cplusplus
}
#endif

#ifndef XORA_LOG_IMPLEMENTATION
#include "diagnostics_policy.h"
#define XoraLog_GameSample(...) XORA_LOG_CALL(XoraLog_GameSample(__VA_ARGS__))
#define XoraLog_GameFault(...) XORA_LOG_CALL(XoraLog_GameFault(__VA_ARGS__))
#define XoraLog_GameAction(...) XORA_LOG_CALL(XoraLog_GameAction(__VA_ARGS__))
#define XoraLog_GameRecovered(...) XORA_LOG_CALL(XoraLog_GameRecovered(__VA_ARGS__))
#define XoraLog_SaveFaultWindow(...) XORA_LOG_CALL(XoraLog_SaveFaultWindow(__VA_ARGS__))
#endif
#endif
