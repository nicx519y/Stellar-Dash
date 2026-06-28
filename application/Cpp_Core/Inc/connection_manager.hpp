#ifndef CONNECTION_MANAGER_HPP
#define CONNECTION_MANAGER_HPP

#include <stdint.h>

#include "enums.hpp"
#include "gamepad/GamepadState.hpp"
#include "rf_transport.hpp"

enum class ConnectionLinkState : uint8_t {
    Disconnected = 0,
    Connecting = 1,
    Connected = 2,
    Error = 3,
};

enum class RfPairingState : uint8_t {
    Idle = 0,
    Starting = 1,
    PairModeOn = 2,
    TxError = 3,
    PairOk = 4,
    Timeout = 5,
};

enum class RfPowerReason : uint8_t {
    Boot = 0,
    UsbMode = 1,
    RfMode = 2,
    SystemSleep = 3,
    SystemWake = 4,
    Manual = 5,
};

enum class RfPowerState : uint8_t {
    Unknown = 0,
    Awake = 1,
    SleepPending = 2,
    Sleeping = 3,
    WakePending = 4,
    Error = 5,
};

class ConnectionManager {
public:
    ConnectionManager(ConnectionManager const&) = delete;
    void operator=(ConnectionManager const&) = delete;
    static ConnectionManager& getInstance() {
        static ConnectionManager instance;
        return instance;
    }

    void setup(ConnectionMode mode, WirelessReportRate wirelessRate);
    void loop();
    void onReportReady(const GamepadState& state, uint32_t seq);
    bool applyWirelessReportRate(WirelessReportRate wirelessRate, bool persist);
    bool startRfPairing();
    bool stopRfPairing();
    bool initializeRfPowerForMode(ConnectionMode mode, WirelessReportRate wirelessRate);
    bool ensureRfSleeping(RfPowerReason reason);
    bool ensureRfAwake(RfPowerReason reason);
    bool restoreRfRuntime(WirelessReportRate wirelessRate);
    bool sleepRfModule();
    bool wakeRfModule();

    ConnectionMode getMode() const { return mode; }
    ConnectionLinkState getLinkState() const { return linkState; }
    uint16_t getAppliedReportRateHz() const { return appliedReportRateHz; }
    bool isRfPairing() const { return rfPairingActive; }
    bool hasRfPairSucceeded() const { return rfPairSucceeded || rfPairingState == RfPairingState::PairOk; }
    bool isRfSleeping() const { return rfPowerState == RfPowerState::Sleeping; }
    bool isRfWakeInProgress() const { return rfPowerState == RfPowerState::WakePending; }
    RfPowerState getRfPowerState() const { return rfPowerState; }
    RfPairingState getRfPairingState() const { return rfPairingState; }
    const RFModuleStatus& getRfModuleStatus() const { return rfTransport.getStatus(); }
    uint8_t getRfPairingLastErrorCommand() const { return rfPairingLastErrorCommand; }
    uint8_t getRfPairingLastErrorReason() const { return rfPairingLastErrorReason; }
    uint32_t getRfPairingStartedAtMs() const { return rfPairingStartedAtMs; }

private:
    ConnectionManager() = default;
    void serviceRfEvents();
    void updatePairingStateFromStatus();
    void updateRfLinkStateFromStatus();
    void activateRfModeAfterPairSuccess();
    bool tryRfBringup(bool isRetry);
    bool tryRfSleepCommand();
    bool checkAndResleepAfterUnexpectedWake(RfPowerReason reason);
    void loadRfPowerStateHint();
    void setRfPowerState(RfPowerState state, bool persist);
    bool rfPowerStateBlocksSpi() const;
    bool rfPowerStateIsBootHint() const { return rfPowerStateFromPersistedHint; }

    ConnectionMode mode = ConnectionMode::CONNECTION_MODE_USB;
    ConnectionLinkState linkState = ConnectionLinkState::Disconnected;
    uint16_t appliedReportRateHz = 1000;
    uint16_t requestedReportRateHz = 1000;
    bool rateApplyPending = false;
    RfPowerState rfPowerState = RfPowerState::Unknown;
    bool rfPowerStateFromPersistedHint = false;
    uint32_t lastRfStatusPollMs = 0;
    uint32_t lastRfBeginRetryMs = 0;
    uint32_t lastRfSleepRetryMs = 0;
    uint32_t rfStatLastMs = 0;
    uint32_t rfSendWin = 0;
    uint32_t rfSendOkWin = 0;
    uint32_t rfSendFailWin = 0;
    uint32_t rfSendTotal = 0;
    uint32_t rfLastSeq = 0;
    bool rfEventServiceEnabled = false;
    bool rfPairingActive = false;
    bool rfPairSucceeded = false;
    RfPairingState rfPairingState = RfPairingState::Idle;
    uint32_t rfPairingLastEventCounter = 0;
    uint32_t rfPairingStartedAtMs = 0;
    uint8_t rfPairingLastErrorCommand = 0;
    uint8_t rfPairingLastErrorReason = 0;
    RFTransport rfTransport;
};

#define CONNECTION_MANAGER ConnectionManager::getInstance()

#endif
