#ifndef HBOX_PHYSICAL_CONFIRMATION_H
#define HBOX_PHYSICAL_CONFIRMATION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    HBOX_CONFIRM_AWAIT_RELEASE = 0,
    HBOX_CONFIRM_RELEASE_DEBOUNCE,
    HBOX_CONFIRM_ARMED,
    HBOX_CONFIRM_PRESS_DEBOUNCE,
    HBOX_CONFIRM_HOLDING,
    HBOX_CONFIRM_AUTHORIZED
} hbox_physical_confirmation_state_t;

typedef struct
{
    hbox_physical_confirmation_state_t state;
    uint32_t state_since_ms;
    uint32_t authorization_expires_at_ms;
} hbox_physical_confirmation_t;

void HBoxPhysicalConfirmation_Init(
    hbox_physical_confirmation_t *confirmation,
    uint32_t now_ms);

void HBoxPhysicalConfirmation_Update(
    hbox_physical_confirmation_t *confirmation,
    uint32_t now_ms,
    bool maintenance_gate_open,
    bool confirmation_buttons_released,
    bool confirmation_buttons_pressed);

bool HBoxPhysicalConfirmation_Consume(
    hbox_physical_confirmation_t *confirmation,
    uint32_t now_ms,
    bool maintenance_gate_open);

#ifdef __cplusplus
}
#endif

#endif /* HBOX_PHYSICAL_CONFIRMATION_H */
