#include <cassert>

#include "fn_layer_policy.hpp"
#include "input_runtime_policy.hpp"
#include "rf_link_state_policy.hpp"

static void test_fn_layer_policy()
{
    FnLayerPolicy policy;

    FnLayerDecision decision = policy.update(false, false);
    assert(!decision.processHotkeys);
    assert(!decision.submitNeutral);
    assert(decision.submitNormal);

    decision = policy.update(true, false);
    assert(decision.processHotkeys);
    assert(decision.submitNeutral);
    assert(!decision.submitNormal);

    policy.onNeutralSubmitted(false);
    decision = policy.update(true, true);
    assert(decision.submitNeutral);

    policy.onNeutralSubmitted(true);
    decision = policy.update(true, true);
    assert(decision.processHotkeys);
    assert(!decision.submitNeutral);
    assert(!decision.submitNormal);

    decision = policy.update(false, true);
    assert(decision.processHotkeys);
    assert(!decision.submitNeutral);
    assert(decision.submitNormal);
}

static void test_rf_input_mode_policy()
{
    const InputMode nonRfModes[] = {
        INPUT_MODE_XINPUT,
        INPUT_MODE_PS4,
        INPUT_MODE_PS5,
        INPUT_MODE_SWITCH,
        INPUT_MODE_XBOX,
    };

    for (InputMode mode : nonRfModes) {
        assert(effectiveInputModeForConnection(CONNECTION_MODE_RF24G, mode) ==
               INPUT_MODE_XINPUT);
        assert(requiresRfXInputPersistence(CONNECTION_MODE_RF24G, mode) ==
               (mode != INPUT_MODE_XINPUT));
        assert(effectiveInputModeForConnection(CONNECTION_MODE_USB, mode) ==
               mode);
    }
}

static void test_socd_policy()
{
    assert(requiresNeutralSocdForBypass(INPUT_MODE_SWITCH));
    assert(requiresNeutralSocdForBypass(INPUT_MODE_PS4));
    assert(requiresNeutralSocdForBypass(INPUT_MODE_PS5));
    assert(!requiresNeutralSocdForBypass(INPUT_MODE_XINPUT));
    assert(!requiresNeutralSocdForBypass(INPUT_MODE_XBOX));
}

static void test_rf_link_state_policy()
{
    assert(connectionLinkStateFromRfStatus(RFLinkState::Idle, false) ==
           ConnectionLinkState::Disconnected);
    assert(connectionLinkStateFromRfStatus(RFLinkState::Connecting, false) ==
           ConnectionLinkState::Connecting);
    assert(connectionLinkStateFromRfStatus(RFLinkState::Reconnecting, false) ==
           ConnectionLinkState::Connecting);
    assert(connectionLinkStateFromRfStatus(RFLinkState::PairOk, false) ==
           ConnectionLinkState::Connecting);
    assert(connectionLinkStateFromRfStatus(RFLinkState::Connected, false) ==
           ConnectionLinkState::Connecting);
    assert(connectionLinkStateFromRfStatus(RFLinkState::Connected, true) ==
           ConnectionLinkState::Connected);
    assert(connectionLinkStateFromRfStatus(RFLinkState::PairTimeout, false) ==
           ConnectionLinkState::Disconnected);
    assert(connectionLinkStateFromRfStatus(RFLinkState::PairFailed, false) ==
           ConnectionLinkState::Disconnected);
}

int main()
{
    test_fn_layer_policy();
    test_rf_input_mode_policy();
    test_socd_policy();
    test_rf_link_state_policy();
    return 0;
}
