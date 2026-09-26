#include "physical_confirmation.h"

#include <stddef.h>

#define HBOX_CONFIRM_RELEASE_DEBOUNCE_MS 50u
#define HBOX_CONFIRM_PRESS_DEBOUNCE_MS 30u
#define HBOX_CONFIRM_HOLD_MS 2000u
#define HBOX_CONFIRM_AUTHORIZATION_MS 10000u

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static void enter_state(
    hbox_physical_confirmation_t *confirmation,
    hbox_physical_confirmation_state_t state,
    uint32_t now_ms)
{
    confirmation->state = state;
    confirmation->state_since_ms = now_ms;
}

void HBoxPhysicalConfirmation_Init(
    hbox_physical_confirmation_t *confirmation,
    uint32_t now_ms)
{
    if (confirmation == NULL) {
        return;
    }
    confirmation->state = HBOX_CONFIRM_AWAIT_RELEASE;
    confirmation->state_since_ms = now_ms;
    confirmation->authorization_expires_at_ms = 0u;
}

void HBoxPhysicalConfirmation_Update(
    hbox_physical_confirmation_t *confirmation,
    uint32_t now_ms,
    bool maintenance_gate_open,
    bool confirmation_buttons_released,
    bool confirmation_buttons_pressed)
{
    if (confirmation == NULL) {
        return;
    }
    if (!maintenance_gate_open) {
        HBoxPhysicalConfirmation_Init(confirmation, now_ms);
        return;
    }

    switch (confirmation->state) {
    case HBOX_CONFIRM_AWAIT_RELEASE:
        if (confirmation_buttons_released) {
            enter_state(
                confirmation,
                HBOX_CONFIRM_RELEASE_DEBOUNCE,
                now_ms);
        }
        break;
    case HBOX_CONFIRM_RELEASE_DEBOUNCE:
        if (!confirmation_buttons_released) {
            enter_state(
                confirmation,
                HBOX_CONFIRM_AWAIT_RELEASE,
                now_ms);
        } else if ((uint32_t)(
                       now_ms - confirmation->state_since_ms) >=
                   HBOX_CONFIRM_RELEASE_DEBOUNCE_MS) {
            enter_state(confirmation, HBOX_CONFIRM_ARMED, now_ms);
        }
        break;
    case HBOX_CONFIRM_ARMED:
        if (confirmation_buttons_pressed) {
            enter_state(
                confirmation,
                HBOX_CONFIRM_PRESS_DEBOUNCE,
                now_ms);
        }
        break;
    case HBOX_CONFIRM_PRESS_DEBOUNCE:
        if (!confirmation_buttons_pressed) {
            enter_state(confirmation, HBOX_CONFIRM_ARMED, now_ms);
        } else if ((uint32_t)(
                       now_ms - confirmation->state_since_ms) >=
                   HBOX_CONFIRM_PRESS_DEBOUNCE_MS) {
            /*
             * Keep the original press timestamp.  The hold interval includes
             * the debounce time instead of silently extending the gesture.
             */
            confirmation->state = HBOX_CONFIRM_HOLDING;
        }
        break;
    case HBOX_CONFIRM_HOLDING:
        if (!confirmation_buttons_pressed) {
            enter_state(
                confirmation,
                HBOX_CONFIRM_AWAIT_RELEASE,
                now_ms);
        } else if ((uint32_t)(
                       now_ms - confirmation->state_since_ms) >=
                   HBOX_CONFIRM_HOLD_MS) {
            confirmation->authorization_expires_at_ms =
                now_ms + HBOX_CONFIRM_AUTHORIZATION_MS;
            enter_state(
                confirmation,
                HBOX_CONFIRM_AUTHORIZED,
                now_ms);
        }
        break;
    case HBOX_CONFIRM_AUTHORIZED:
        if (deadline_reached(
                now_ms,
                confirmation->authorization_expires_at_ms)) {
            HBoxPhysicalConfirmation_Init(confirmation, now_ms);
        }
        break;
    default:
        HBoxPhysicalConfirmation_Init(confirmation, now_ms);
        break;
    }
}

bool HBoxPhysicalConfirmation_Consume(
    hbox_physical_confirmation_t *confirmation,
    uint32_t now_ms,
    bool maintenance_gate_open)
{
    if (confirmation == NULL ||
        !maintenance_gate_open ||
        confirmation->state != HBOX_CONFIRM_AUTHORIZED ||
        deadline_reached(
            now_ms,
            confirmation->authorization_expires_at_ms)) {
        if (confirmation != NULL &&
            (!maintenance_gate_open ||
             confirmation->state == HBOX_CONFIRM_AUTHORIZED)) {
            HBoxPhysicalConfirmation_Init(confirmation, now_ms);
        }
        return false;
    }

    HBoxPhysicalConfirmation_Init(confirmation, now_ms);
    return true;
}
