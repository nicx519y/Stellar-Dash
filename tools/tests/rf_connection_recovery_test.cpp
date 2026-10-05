#include <cassert>
#include <cstdint>
#define printf(...) ((void)0)

enum class ConnectionMode { CONNECTION_MODE_USB, CONNECTION_MODE_RF24G };
enum class BoardMode { Usb, Rf };
enum class RfPowerState { Awake, Sleeping };
enum class ConnectionLinkState { Connected, Connecting, Error };
enum class RfSleepState { Off, Ready, Failed };
struct GamepadState {};
static uint32_t nowMs = 0;
static bool releaseFault = false, pendingEvent = false, roleActive = true;
static bool portIdle = true;
static uint32_t HAL_GetTick() { return nowMs; }
static void HAL_Delay(uint32_t ms) { nowMs += ms; }
static bool RFBridgePort_HasReleaseFault() { return releaseFault; }
static bool RFBridgePort_HasPendingEvent() { return pendingEvent; }
static bool RFBridgePort_RecoveryIdle() { return portIdle && !pendingEvent && !releaseFault; }
static bool rfPhysicalRoleIsActive() { return roleActive; }
static void MonitorTelemetry_OnError(const char*, unsigned, const char*) {}
static uint16_t clampRfReportRateHz(uint16_t rate) { return rate; }
static constexpr uint32_t kRfRateAppliedTimeoutMs = 500u, kRfStatusPollMs = 500u;
static constexpr uint32_t kRfStatusReplyTimeoutMs = 20u;
static constexpr uint8_t kRfRuntimeRecoveryAttempts = 3u;
struct FakeBoardMode {
    bool stable = true;
    BoardMode mode = BoardMode::Rf;
    bool isStable() const { return stable; }
    BoardMode current() const { return mode; }
} BOARD_MODE;
struct FakeRecovery {
    unsigned begins = 0, services = 0;
    uint8_t budget = 0;
    RfSleepState phase = RfSleepState::Off;
    void begin(uint16_t, uint8_t attempts) { ++begins; budget = attempts; }
    void service(uint32_t) { ++services; }
    RfSleepState state() const { return phase; }
} RF_SLEEP_RECOVERY;
struct FakeTransport {
    uint32_t generation = 1u;
    uint16_t observedRate = 8000u, requestedRate = 8000u;
    unsigned queries = 0, inputs = 0;
    bool replyEnabled = true, replyPending = false, wrongRate = false;
    bool setRate(uint16_t rate) { requestedRate = rate; return true; }
    uint32_t receivedStatusGeneration() const { return generation; }
    bool receivedStatusMatchesRate(uint16_t rate) const { return observedRate == rate; }
    bool pollStatus() { ++queries; replyPending = true; return true; } // write alone succeeds
    void serviceEvents(uint8_t) {
        if (replyEnabled && replyPending) {
            replyPending = false;
            observedRate = wrongRate && requestedRate != 1000u ? 0u : requestedRate;
            ++generation;
        }
    }
    bool sendInput(const GamepadState&, uint32_t) { ++inputs; return true; }
};
class ConnectionManager {
public:
    bool confirmRfReportRate(uint16_t);
    void serviceRfStatusPoll();
    bool serviceRfRuntimeRecovery();
    bool onReportReady(const GamepadState&, uint32_t);
    bool rfPowerStateBlocksSpi() const { return rfSleepRecoveryOwned || rfPowerState != RfPowerState::Awake; }
    void setLinkState(ConnectionLinkState state) { linkState = state; }
    void updateRfLinkStateFromStatus() { linkState = ConnectionLinkState::Connecting; } // RX offline
    void serviceRfEvents() {}
    void recordRfRecoveryStart() {}
    ConnectionMode mode = ConnectionMode::CONNECTION_MODE_RF24G;
    RfPowerState rfPowerState = RfPowerState::Awake;
    ConnectionLinkState linkState = ConnectionLinkState::Connecting;
    bool rfPairingActive = false, rfSleepRecoveryOwned = false, rfRuntimeRecoveryActive = false;
    bool reportRateConfirmed = true, rateApplyPending = false, rfStatusPollPending = false;
    uint16_t requestedReportRateHz = 8000u, appliedReportRateHz = 8000u;
    uint8_t rfStatusPollFailures = 0u;
    uint32_t lastRfStatusPollMs = 0u, lastRfStatusGeneration = 1u;
    uint32_t rfSendWin = 0u, rfLastSeq = 0u, rfSendOkWin = 0u, rfSendTotal = 0u, rfSendFailWin = 0u;
    FakeTransport rfTransport;
};
#include "rf_manager_functions.inc"

int main() {
    // A cached matching rate and successful SET_RATE writes are insufficient.
    ConnectionManager stale;
    stale.rfTransport.replyEnabled = false;
    assert(!stale.confirmRfReportRate(8000));
    assert(!stale.reportRateConfirmed && stale.appliedReportRateHz == 0u);
    ConnectionManager fresh;
    assert(fresh.confirmRfReportRate(8000));
    assert(fresh.rfTransport.queries && fresh.reportRateConfirmed);
    ConnectionManager fallback;
    fallback.rfTransport.wrongRate = true;
    assert(fallback.confirmRfReportRate(8000));
    assert(fallback.appliedReportRateHz == 1000u);

    // The RX can remain offline while the TX answers correct status forever.
    ConnectionManager offline;
    nowMs = 500;
    for (unsigned i = 0; i < 10; ++i) {
        offline.serviceRfStatusPoll();
        offline.rfTransport.serviceEvents(8u);
        offline.serviceRfStatusPoll();
        assert(!offline.serviceRfRuntimeRecovery());
        nowMs += 500;
    }
    assert(!RF_SLEEP_RECOVERY.begins);

    // No normal input may consume the TX reply between its request and read.
    ConnectionManager exclusive;
    nowMs = 500;
    exclusive.serviceRfStatusPoll();
    assert(exclusive.rfStatusPollPending && exclusive.rfTransport.queries == 1u);
    assert(!exclusive.onReportReady({}, 1u) && !exclusive.rfTransport.inputs);
    nowMs += 1;
    exclusive.rfTransport.serviceEvents(8u);
    exclusive.serviceRfStatusPoll();
    assert(!exclusive.rfStatusPollPending);
    assert(exclusive.onReportReady({}, 2u) && exclusive.rfTransport.inputs == 1u);

    // Missing reply releases the input gate after 20 ms, not the 500-ms poll
    // period. Busy arbitration defers a query without consuming fault budget.
    ConnectionManager deadline;
    nowMs = 500;
    deadline.serviceRfStatusPoll();
    nowMs = 519; deadline.serviceRfStatusPoll();
    assert(!deadline.onReportReady({}, 3u));
    nowMs = 520; deadline.serviceRfStatusPoll();
    assert(!deadline.rfStatusPollPending && deadline.rfStatusPollFailures == 1u);
    assert(deadline.onReportReady({}, 4u));
    ConnectionManager busy;
    portIdle = false;
    for (nowMs = 500; nowMs <= 2500; nowMs += 500) busy.serviceRfStatusPoll();
    assert(!busy.rfTransport.queries && !busy.rfStatusPollFailures);
    assert(!busy.serviceRfRuntimeRecovery());
    portIdle = true;

    // A successful asynchronous poll write must time out without a reply.
    ConnectionManager missing;
    for (nowMs = 500; nowMs <= 2000; nowMs += 500) missing.serviceRfStatusPoll();
    assert(missing.rfStatusPollFailures == 3u);
    assert(missing.serviceRfRuntimeRecovery());
    assert(RF_SLEEP_RECOVERY.begins == 1u && RF_SLEEP_RECOVERY.budget == 3u);
    roleActive = false; // role unlock during restart must not stop its service
    assert(missing.serviceRfRuntimeRecovery() && RF_SLEEP_RECOVERY.services == 1u);
    BOARD_MODE.mode = BoardMode::Usb;
    assert(!missing.serviceRfRuntimeRecovery() && RF_SLEEP_RECOVERY.services == 1u);
    BOARD_MODE.mode = BoardMode::Rf;
    roleActive = true;

    ConnectionManager mismatch;
    ++mismatch.rfTransport.generation;
    mismatch.rfTransport.observedRate = 0;
    mismatch.serviceRfStatusPoll();
    assert(!mismatch.reportRateConfirmed);
    assert(!mismatch.onReportReady({}, 1u) && !mismatch.rfTransport.inputs);
    assert(mismatch.serviceRfRuntimeRecovery());

    ConnectionManager fault;
    releaseFault = true;
    assert(fault.serviceRfRuntimeRecovery());
    releaseFault = false;
    fault.rfSleepRecoveryOwned = true;
    assert(!fault.onReportReady({}, 2u) && !fault.rfTransport.inputs);
    return 0;
}
