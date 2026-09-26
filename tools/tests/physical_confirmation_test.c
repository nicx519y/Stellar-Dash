#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "physical_confirmation.h"

static void release_and_arm(
    hbox_physical_confirmation_t *confirmation,
    uint32_t start)
{
    HBoxPhysicalConfirmation_Update(
        confirmation, start, true, true, false);
    HBoxPhysicalConfirmation_Update(
        confirmation, start + 50u, true, true, false);
    assert(confirmation->state == HBOX_CONFIRM_ARMED);
}

static void hold_to_authorize(
    hbox_physical_confirmation_t *confirmation,
    uint32_t start)
{
    HBoxPhysicalConfirmation_Update(
        confirmation, start, true, false, true);
    HBoxPhysicalConfirmation_Update(
        confirmation, start + 30u, true, false, true);
    HBoxPhysicalConfirmation_Update(
        confirmation, start + 2000u, true, false, true);
    assert(confirmation->state == HBOX_CONFIRM_AUTHORIZED);
}

static void test_power_on_hold_never_arms(void)
{
    hbox_physical_confirmation_t confirmation;
    HBoxPhysicalConfirmation_Init(&confirmation, 0u);
    HBoxPhysicalConfirmation_Update(
        &confirmation, 10000u, true, false, true);
    assert(confirmation.state == HBOX_CONFIRM_AWAIT_RELEASE);
    assert(!HBoxPhysicalConfirmation_Consume(
        &confirmation, 10001u, true));
}

static void test_single_stuck_button_never_counts_as_release(void)
{
    hbox_physical_confirmation_t confirmation;
    HBoxPhysicalConfirmation_Init(&confirmation, 0u);

    /*
     * One active-low input remains asserted.  "Not both pressed" is not the
     * same as "both released", so even pressing the second key later must not
     * bypass the release-to-arm boundary.
     */
    HBoxPhysicalConfirmation_Update(
        &confirmation, 100u, true, false, false);
    HBoxPhysicalConfirmation_Update(
        &confirmation, 10000u, true, false, false);
    HBoxPhysicalConfirmation_Update(
        &confirmation, 10001u, true, false, true);
    assert(confirmation.state == HBOX_CONFIRM_AWAIT_RELEASE);
    assert(!HBoxPhysicalConfirmation_Consume(
        &confirmation, 12002u, true));

    release_and_arm(&confirmation, 13000u);
    assert(confirmation.state == HBOX_CONFIRM_ARMED);
}

static void test_release_hold_and_one_shot_consume(void)
{
    hbox_physical_confirmation_t confirmation;
    HBoxPhysicalConfirmation_Init(&confirmation, 0u);
    release_and_arm(&confirmation, 10u);
    hold_to_authorize(&confirmation, 100u);
    assert(HBoxPhysicalConfirmation_Consume(
        &confirmation, 2101u, true));
    assert(!HBoxPhysicalConfirmation_Consume(
        &confirmation, 2102u, true));
}

static void test_bounce_and_short_hold_fail(void)
{
    hbox_physical_confirmation_t confirmation;
    HBoxPhysicalConfirmation_Init(&confirmation, 0u);
    release_and_arm(&confirmation, 0u);

    HBoxPhysicalConfirmation_Update(
        &confirmation, 100u, true, false, true);
    HBoxPhysicalConfirmation_Update(
        &confirmation, 120u, true, true, false);
    assert(confirmation.state == HBOX_CONFIRM_ARMED);

    HBoxPhysicalConfirmation_Update(
        &confirmation, 200u, true, false, true);
    HBoxPhysicalConfirmation_Update(
        &confirmation, 230u, true, false, true);
    HBoxPhysicalConfirmation_Update(
        &confirmation, 2199u, true, true, false);
    assert(!HBoxPhysicalConfirmation_Consume(
        &confirmation, 2200u, true));
}

static void test_gate_loss_and_expiry_fail_closed(void)
{
    hbox_physical_confirmation_t confirmation;
    HBoxPhysicalConfirmation_Init(&confirmation, 0u);
    release_and_arm(&confirmation, 0u);
    hold_to_authorize(&confirmation, 100u);
    HBoxPhysicalConfirmation_Update(
        &confirmation, 2200u, false, false, true);
    assert(!HBoxPhysicalConfirmation_Consume(
        &confirmation, 2201u, true));

    release_and_arm(&confirmation, 3000u);
    hold_to_authorize(&confirmation, 3100u);
    assert(!HBoxPhysicalConfirmation_Consume(
        &confirmation, 15100u, true));
}

int main(void)
{
    test_power_on_hold_never_arms();
    test_single_stuck_button_never_counts_as_release();
    test_release_hold_and_one_shot_consume();
    test_bounce_and_short_hold_fail();
    test_gate_loss_and_expiry_fail_closed();
    puts("physical_confirmation_test: PASS");
    return 0;
}
