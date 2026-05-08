#include "dongle_fsm.h"

#include "dongle_config.h"
#include "platform_port.h"
#include "rf_link.h"

/* Pair/connect timing policy */
#define PAIRING_TIMEOUT_US      (10000000u)
#define PAIRED_HOLD_US          (500000u)
#define CONNECTING_TIMEOUT_US   (3000000u)
#define AUTO_PAIR_DELAY_US      (500000u)
#define AUTO_PAIR_RETRY_US      (1500000u)

/* LED patterns */
#define LED_BLINK_FAST_US       (500000u)

static dongle_state_t s_state;
static uint32_t s_state_deadline_us;
static bool s_led_on;
static uint32_t s_led_deadline_us;
static bool s_pairing_req;
static bool s_unpair_req;
static uint32_t s_auto_pair_deadline_us;

static void led_apply(bool on)
{
    s_led_on = on;
    platform_led_set(on);
}

static void enter_state(dongle_state_t new_state, uint32_t now_us)
{
    s_state = new_state;

    switch (s_state) {
    case DONGLE_STATE_WAIT:
        rf_link_stop_pairing();
        rf_link_stop_connect();
        s_state_deadline_us = 0u;
        if (rf_link_has_bond()) {
            s_auto_pair_deadline_us = 0u;
        } else {
            s_auto_pair_deadline_us = now_us + AUTO_PAIR_RETRY_US;
        }
        s_led_deadline_us = now_us + DISCONNECTED_BLINK_INTERVAL_US;
        led_apply(true);
        break;
    case DONGLE_STATE_PAIRING:
        rf_link_start_pairing();
        s_state_deadline_us = now_us + PAIRING_TIMEOUT_US;
        s_led_deadline_us = now_us + LED_BLINK_FAST_US;
        led_apply(true);
        break;
    case DONGLE_STATE_PAIRED_OK:
        rf_link_stop_pairing();
        s_state_deadline_us = now_us + PAIRED_HOLD_US;
        s_led_deadline_us = 0u;
        led_apply(true);
        break;
    case DONGLE_STATE_CONNECTING:
        rf_link_start_connect();
        s_state_deadline_us = now_us + CONNECTING_TIMEOUT_US;
        s_led_deadline_us = now_us + LED_BLINK_FAST_US;
        led_apply(true);
        break;
    case DONGLE_STATE_CONNECTED:
        s_state_deadline_us = 0u;
        led_apply(true);
        break;
    default:
        break;
    }
}

void dongle_fsm_init(uint32_t now_us)
{
    s_pairing_req = false;
    s_unpair_req = false;
    s_auto_pair_deadline_us = now_us + AUTO_PAIR_DELAY_US;
    enter_state(DONGLE_STATE_WAIT, now_us);
}

void dongle_fsm_request_pairing(void)
{
    s_pairing_req = true;
}

void dongle_fsm_request_unpair(void)
{
    s_unpair_req = true;
}

static void handle_led_pattern(uint32_t now_us)
{
    if (s_state == DONGLE_STATE_WAIT) {
        bool on = (((now_us / DISCONNECTED_BLINK_INTERVAL_US) & 0x1u) == 0u);
        if (on != s_led_on) {
            led_apply(on);
        }
        return;
    }

    if ((s_state == DONGLE_STATE_PAIRING) || (s_state == DONGLE_STATE_CONNECTING)) {
        bool on = (((now_us / LED_BLINK_FAST_US) & 0x1u) == 0u);
        if (on != s_led_on) {
            led_apply(on);
        }
    }
}

void dongle_fsm_tick(uint32_t now_us)
{
    rf_link_event_t ev = rf_link_take_event();

    if (s_unpair_req) {
        s_unpair_req = false;
        s_pairing_req = false;
        rf_link_clear_bond();
        enter_state(DONGLE_STATE_WAIT, now_us);
        return;
    }

    switch (s_state) {
    case DONGLE_STATE_WAIT:
        if (s_pairing_req) {
            s_pairing_req = false;
            enter_state(DONGLE_STATE_PAIRING, now_us);
        } else if (rf_link_has_bond()) {
            enter_state(DONGLE_STATE_CONNECTING, now_us);
        } else if ((s_auto_pair_deadline_us != 0u) &&
                   ((int32_t)(now_us - s_auto_pair_deadline_us) >= 0)) {
            enter_state(DONGLE_STATE_PAIRING, now_us);
        }
        break;

    case DONGLE_STATE_PAIRING:
        if (ev == RF_LINK_EVENT_PAIRING_DONE) {
            enter_state(DONGLE_STATE_PAIRED_OK, now_us);
        } else if ((ev == RF_LINK_EVENT_PAIRING_TIMEOUT) ||
                   ((int32_t)(now_us - s_state_deadline_us) >= 0)) {
            enter_state(DONGLE_STATE_WAIT, now_us);
        }
        break;

    case DONGLE_STATE_PAIRED_OK:
        if ((int32_t)(now_us - s_state_deadline_us) >= 0) {
            enter_state(DONGLE_STATE_CONNECTING, now_us);
        }
        break;

    case DONGLE_STATE_CONNECTING:
        if (ev == RF_LINK_EVENT_CONNECT_DONE) {
            enter_state(DONGLE_STATE_CONNECTED, now_us);
        } else if ((ev == RF_LINK_EVENT_CONNECT_TIMEOUT) ||
                   ((int32_t)(now_us - s_state_deadline_us) >= 0)) {
            enter_state(DONGLE_STATE_WAIT, now_us);
        }
        break;

    case DONGLE_STATE_CONNECTED:
        if ((ev == RF_LINK_EVENT_LINK_LOST) || !rf_link_is_connected()) {
            enter_state(DONGLE_STATE_CONNECTING, now_us);
        }
        break;

    default:
        enter_state(DONGLE_STATE_WAIT, now_us);
        break;
    }

    handle_led_pattern(now_us);
}

bool dongle_fsm_allow_report(void)
{
    return (s_state == DONGLE_STATE_CONNECTED);
}

dongle_state_t dongle_fsm_get_state(void)
{
    return s_state;
}
