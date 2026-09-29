#include "rf_sleep_recovery.hpp"
#include <cstring>

// Production RF recovery state machine + fake electrical/transport endpoints.
// This suite is compiled only until the user resumes RF automatic regression.
static void step(unsigned ms) {
    for (unsigned i = 0; i < ms; ++i) {
        ++testNow;
        ++localIterations; // main-loop work remains independently serviceable
        RF_SLEEP_RECOVERY.service(testNow);
    }
}
int main(int argc, char** argv) {
    assert(argc == 2);
    const char* scenario = argv[1];
    assert(RF_SLEEP_RECOVERY.suspend());
    // No radio power until the input/display owner explicitly starts recovery.
    step(1100);
    assert(powerOns == 0);
    if (!std::strcmp(scenario, "offline")) receiverOnline = false;
    if (!std::strcmp(scenario, "fallback")) wrongRate = true;
    if (!std::strcmp(scenario, "retry")) roleOk = false;
    if (!std::strcmp(scenario, "fresh-status")) autoReply = false;
    if (!std::strcmp(scenario, "unsafe-park")) parkOk = false;
    if (!std::strcmp(scenario, "handoff-wrap")) testNow = UINT32_MAX - 800u;
    RF_SLEEP_RECOVERY.begin(8000);
    if (!std::strcmp(scenario, "handoff") || !std::strcmp(scenario, "cancel-handoff")) {
        for (unsigned i = 0; i < 1000 && RF_SLEEP_RECOVERY.state() != RfSleepState::ApplicationWait; ++i) step(1);
        assert(RF_SLEEP_RECOVERY.state() == RfSleepState::ApplicationWait);
        assert(portBegins == 0 && portReads == 0 && queryCount == 0);
        const auto localBefore = localIterations;
        step(RfSleepRecovery::applicationSettleMs - 1u);
        assert(portBegins == 0 && portReads == 0 && queryCount == 0);
        assert(localIterations > localBefore);
        if (!std::strcmp(scenario, "cancel-handoff")) {
            RF_SLEEP_RECOVERY.cancel();
            step(30000);
            assert(portBegins == 0 && portReads == 0 && !published);
        } else {
            step(1);
            assert(portBegins == 1 && portReads == 0);
            step(300); assert(published);
        }
    } else if (!std::strcmp(scenario, "unsafe-park")) {
        const auto off = powerOffs;
        step(9999);
        assert(powerOffs == off && powerOns == 0 && !published);
        parkOk = true; step(1200);
        assert(published);
    } else if (!std::strcmp(scenario, "retry")) {
        step(741);
        assert(RF_SLEEP_RECOVERY.state() == RfSleepState::RetryWait);
        const auto attempts = g_sleepDiagnostics.radioAttempts;
        step(9998); assert(g_sleepDiagnostics.radioAttempts == attempts);
        step(1000); assert(g_sleepDiagnostics.radioAttempts == attempts + 1);
        assert(localIterations > 10000 && !published);
        roleOk = true; step(11000); assert(published);
    } else if (!std::strcmp(scenario, "fresh-status")) {
        step(1050); assert(queryCount && !published);
        // A synthesized RATE_APPLIED and matching cached rate are insufficient.
        replies.push_back({8000, false}); step(1); assert(!published);
        replies.push_back({8000, true}); step(2); assert(published);
    } else if (!std::strcmp(scenario, "cancel")) {
        step(30); RF_SLEEP_RECOVERY.cancel();
        const auto on = powerOns; const auto queries = queryCount;
        step(30000); assert(powerOns == on && queryCount == queries && !published);
    } else if (!std::strcmp(scenario, "old-session")) {
        replies.push_back({8000, true});
        assert(RF_SLEEP_RECOVERY.suspend());
        assert(replies.empty());
        autoReply = false; RF_SLEEP_RECOVERY.begin(8000); step(1050);
        assert(!published);
    } else {
        step(1600); assert(published);
        assert(publishedRate == (wrongRate ? 1000 : 8000));
        if (!std::strcmp(scenario, "cycles")) {
            for (unsigned i = 0; i < 20; ++i) {
                assert(RF_SLEEP_RECOVERY.suspend()); RF_SLEEP_RECOVERY.begin(8000);
                step(1600); assert(published);
            }
            assert(neutralCount == 21);
        }
        const auto on = powerOns;
        step(30000); assert(powerOns == on); // Ready/offline does not power-cycle
    }
}
