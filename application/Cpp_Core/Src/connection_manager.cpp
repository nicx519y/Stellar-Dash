#include "connection_manager.hpp"

#include "config.hpp"
#include "monitor_telemetry.hpp"
#include "rf_boot_ready.hpp"
#include "rf_bridge_port.hpp"
#include "storagemanager.hpp"
#include "usbdriver.hpp"
#include "system_logger.h"
#include "board_cfg.h"
#include "stm32h7xx_hal.h"

#include <stdio.h>

#if !APP_LOG_VERBOSE
#define printf(...) ((void)0)
#endif

namespace {
static uint16_t clampRfReportRateHz(uint16_t rateHz) {
    switch (rateHz) {
    case 1000u:
    case 2000u:
    case 4000u:
    case 8000u:
        return rateHz;
    default:
        return 1000u;
    }
}

static uint16_t getRfReportRateHz(WirelessReportRate wirelessRate) {
#if RF24G_FORCE_REPORT_RATE_HZ != 0
    (void)wirelessRate;
    return clampRfReportRateHz(static_cast<uint16_t>(RF24G_FORCE_REPORT_RATE_HZ));
#else
    return clampRfReportRateHz(ConfigUtils::getWirelessReportRateHz(wirelessRate));
#endif
}

static constexpr uint32_t kRfSleepRetryMs = 500u;
static constexpr uint32_t kRfBootReadyTimeoutMs = 1500u;
static constexpr uint32_t kRfPostSleepSettleMs = 150u;
static constexpr uint8_t kRfCmdStartPair = 0x02u;
static constexpr uint8_t kRfCmdStopPair = 0x03u;
static constexpr uint8_t kRfCmdSleep = 0x08u;
static constexpr uint8_t kRfEvtWakeupComplete = 0x88u;
static constexpr uint8_t kRfEvtError = 0x85u;
static constexpr uint32_t kRfPostSleepWakeDetectMs = 150u;
static constexpr uint8_t kRfPowerHintUnknown = 0u;
static constexpr uint8_t kRfPowerHintAwake = 1u;
static constexpr uint8_t kRfPowerHintSleeping = 2u;

static const char* rfPowerReasonName(RfPowerReason reason) {
    switch (reason) {
    case RfPowerReason::Boot: return "boot";
    case RfPowerReason::UsbMode: return "usb_mode";
    case RfPowerReason::RfMode: return "rf_mode";
    case RfPowerReason::SystemSleep: return "system_sleep";
    case RfPowerReason::SystemWake: return "system_wake";
    case RfPowerReason::Manual: return "manual";
    default: return "unknown";
    }
}

static const char* rfPowerStateName(RfPowerState state) {
    switch (state) {
    case RfPowerState::Unknown: return "unknown";
    case RfPowerState::Awake: return "awake";
    case RfPowerState::SleepPending: return "sleep_pending";
    case RfPowerState::Sleeping: return "sleeping";
    case RfPowerState::WakePending: return "wake_pending";
    case RfPowerState::Error: return "error";
    default: return "invalid";
    }
}

static uint8_t rfPowerStateToHint(RfPowerState state) {
    switch (state) {
    case RfPowerState::Awake:
        return kRfPowerHintAwake;
    case RfPowerState::Sleeping:
        return kRfPowerHintSleeping;
    default:
        return kRfPowerHintUnknown;
    }
}

static RfPowerState rfPowerHintToState(uint8_t hint) {
    switch (hint) {
    case kRfPowerHintAwake:
        return RfPowerState::Awake;
    case kRfPowerHintSleeping:
        return RfPowerState::Sleeping;
    default:
        return RfPowerState::Unknown;
    }
}
}

void ConnectionManager::loadRfPowerStateHint() {
    rfPowerState = rfPowerHintToState(STORAGE_MANAGER.getRfPowerStateHint());
    rfPowerStateFromPersistedHint = (rfPowerState != RfPowerState::Unknown);
    printf("[RF_PWR][HINT] state=%s raw=%u\r\n",
           rfPowerStateName(rfPowerState),
           (unsigned int)STORAGE_MANAGER.getRfPowerStateHint());
}

void ConnectionManager::setRfPowerState(RfPowerState state, bool persist) {
    const RfPowerState oldState = rfPowerState;
    rfPowerState = state;
    rfPowerStateFromPersistedHint = false;
    printf("[RF_PWR][STATE] %s->%s persist=%u\r\n",
           rfPowerStateName(oldState),
           rfPowerStateName(state),
           (unsigned int)persist);

    if (!persist) {
        return;
    }

    const uint8_t hint = rfPowerStateToHint(state);
    if (STORAGE_MANAGER.getRfPowerStateHint() == hint) {
        return;
    }

    STORAGE_MANAGER.setRfPowerStateHint(hint);
    if (!STORAGE_MANAGER.saveConfig()) {
        printf("[RF_PWR][HINT_SAVE_FAIL] state=%s raw=%u\r\n",
               rfPowerStateName(state),
               (unsigned int)hint);
    }
}

bool ConnectionManager::rfPowerStateBlocksSpi() const {
    return (rfPowerState == RfPowerState::SleepPending) ||
           (rfPowerState == RfPowerState::Sleeping) ||
           (rfPowerState == RfPowerState::WakePending);
}

void ConnectionManager::activateRfModeAfterPairSuccess() {
    if (mode != ConnectionMode::CONNECTION_MODE_RF24G) {
        mode = ConnectionMode::CONNECTION_MODE_RF24G;
        rfEventServiceEnabled = true;
        STORAGE_MANAGER.setConnectionMode(ConnectionMode::CONNECTION_MODE_RF24G);
        STORAGE_MANAGER.setInputMode(InputMode::INPUT_MODE_XINPUT);
    }

    requestedReportRateHz = getRfReportRateHz(STORAGE_MANAGER.getWirelessReportRate());
    if (rfTransport.setRate(requestedReportRateHz)) {
        appliedReportRateHz = requestedReportRateHz;
    } else if (appliedReportRateHz == 0u) {
        appliedReportRateHz = requestedReportRateHz;
    }

    MonitorTelemetry_Init(mode, appliedReportRateHz);

    ConnectionLinkState nextState = ConnectionLinkState::Connected;
    if (linkState != nextState) {
        MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
    }
    linkState = nextState;
}

bool ConnectionManager::tryRfBringup(bool isRetry) {
    (void)isRetry;
    bool ok = rfTransport.begin();

    /*
     * SPI bring-up mode:
     * keep only GET_STATUS path to validate transport stability first.
     * Rate/pair commands are re-enabled after SPI link is proven stable.
     */

    if (ok) {
        updateRfLinkStateFromStatus();
    } else {
        ConnectionLinkState nextState = ConnectionLinkState::Error;
        if (linkState != nextState) {
            MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
        }
        linkState = nextState;
        MonitorTelemetry_OnError("CONNECTION_MANAGER", 1001u, "rf begin/setRate failed");
    }
    return ok;
}

void ConnectionManager::updateRfLinkStateFromStatus() {
    const RFModuleStatus& st = rfTransport.getStatus();
    ConnectionLinkState nextState = ConnectionLinkState::Disconnected;
    switch (st.state) {
    case RFLinkState::Pairing:
    case RFLinkState::PairOk:
    case RFLinkState::Connecting:
    case RFLinkState::Reconnecting:
        nextState = ConnectionLinkState::Connecting;
        break;
    case RFLinkState::Connected:
        nextState = st.connected ? ConnectionLinkState::Connected : ConnectionLinkState::Connecting;
        break;
    case RFLinkState::Idle:
    default:
        nextState = ConnectionLinkState::Disconnected;
        break;
    }
    if (linkState != nextState) {
        MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
    }
    linkState = nextState;
}

void ConnectionManager::updatePairingStateFromStatus() {
    const RFModuleStatus& st = rfTransport.getStatus();

    if (st.lastEvent == kRfEvtError) {
        rfPairingActive = false;
        rfPairingState = RfPairingState::TxError;
        rfPairingLastErrorCommand = st.lastErrorCommand;
        rfPairingLastErrorReason = st.lastErrorReason;
        return;
    }

    if (st.state == RFLinkState::PairOk) {
        rfPairingActive = false;
        rfPairSucceeded = true;
        rfPairingState = RfPairingState::PairOk;
        activateRfModeAfterPairSuccess();
        return;
    }

    if (!rfPairingActive &&
        rfPairingState != RfPairingState::Starting &&
        rfPairingState != RfPairingState::PairModeOn) {
        return;
    }

    switch (st.state) {
    case RFLinkState::Pairing:
        rfPairingActive = true;
        rfPairingState = RfPairingState::PairModeOn;
        break;
    case RFLinkState::PairTimeout:
        rfPairingActive = true;
        rfPairingState = RfPairingState::PairModeOn;
        break;
    case RFLinkState::PairFailed:
        rfPairingActive = false;
        rfPairingState = RfPairingState::TxError;
        rfPairingLastErrorCommand = kRfCmdStartPair;
        rfPairingLastErrorReason = 2u;
        break;
    case RFLinkState::Idle:
        break;
    case RFLinkState::Connecting:
        break;
    default:
        break;
    }

}

void ConnectionManager::serviceRfEvents() {
    if ((rfPowerState == RfPowerState::SleepPending) ||
        (rfPowerState == RfPowerState::Sleeping)) {
        return;
    }

    if (!rfEventServiceEnabled && !rfPairingActive) {
        return;
    }

    (void)rfTransport.serviceEvents();

    updateRfLinkStateFromStatus();
    if (rfTransport.getStatus().eventCounter != rfPairingLastEventCounter) {
        rfPairingLastEventCounter = rfTransport.getStatus().eventCounter;
        updatePairingStateFromStatus();
    }
}

void ConnectionManager::setup(ConnectionMode connMode, WirelessReportRate wirelessRate) {
    mode = connMode;
    appliedReportRateHz = 1000;
    requestedReportRateHz = 1000;
    rateApplyPending = false;
    loadRfPowerStateHint();
    linkState = ConnectionLinkState::Disconnected;
    lastRfStatusPollMs = HAL_GetTick();
    lastRfSleepRetryMs = HAL_GetTick();
    rfStatLastMs = HAL_GetTick();
    rfSendWin = 0u;
    rfSendOkWin = 0u;
    rfSendFailWin = 0u;
    rfSendTotal = 0u;
    rfLastSeq = 0u;
    rfEventServiceEnabled = (mode == ConnectionMode::CONNECTION_MODE_RF24G);
    rfPairingActive = false;
    rfPairSucceeded = false;
    rfPairingState = RfPairingState::Idle;
    rfPairingLastEventCounter = 0u;
    rfPairingStartedAtMs = 0u;
    rfPairingLastErrorCommand = 0u;
    rfPairingLastErrorReason = 0u;

    if (mode == ConnectionMode::CONNECTION_MODE_USB) {
        appliedReportRateHz = 1000;
        MonitorTelemetry_Init(mode, appliedReportRateHz);
        ConnectionLinkState nextState = get_usb_mounted() ? ConnectionLinkState::Connected : ConnectionLinkState::Disconnected;
        if (linkState != nextState) {
            MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
        }
        linkState = nextState;
        return;
    }

    requestedReportRateHz = getRfReportRateHz(wirelessRate);
    printf("[RF_RATE] setup mode=%u requested=%u applied=%u enum=%u\r\n",
            (unsigned int)mode,
            (unsigned int)requestedReportRateHz,
            (unsigned int)appliedReportRateHz,
            (unsigned int)wirelessRate);
    MonitorTelemetry_Init(mode, requestedReportRateHz);
#if RF24G_SPI_TEST_FORCE_RF24G
    rateApplyPending = false;
    appliedReportRateHz = requestedReportRateHz;
    linkState = ConnectionLinkState::Connected;
    MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(linkState));
    lastRfBeginRetryMs = HAL_GetTick();
    return;
#endif
    bool rateOk = false;
    if ((rfPowerState == RfPowerState::Sleeping) && rfPowerStateIsBootHint()) {
        printf("[RF_PWR][SETUP_WAKE_FROM_HINT] mode=%u rate=%u\r\n",
               (unsigned int)mode,
               (unsigned int)requestedReportRateHz);
        rateOk = wakeRfFromSleep(RfPowerReason::SystemWake) &&
                 restoreRfRuntime(wirelessRate);
    } else {
        rateOk = initializeRfPowerForMode(mode, wirelessRate);
    }
    rateApplyPending = false;
    printf("[RF_RATE] setup setRate result=%u requested=%u applied=%u\r\n",
            (unsigned int)rateOk,
            (unsigned int)requestedReportRateHz,
            (unsigned int)appliedReportRateHz);
    if (!rateOk) {
        ConnectionLinkState nextState = ConnectionLinkState::Error;
        if (linkState != nextState) {
            MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
        }
        linkState = nextState;
        MonitorTelemetry_OnError("CONNECTION_MANAGER", 1003u, "rf setRate failed");
    } else {
        appliedReportRateHz = requestedReportRateHz;
        linkState = ConnectionLinkState::Connected;
        MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(linkState));
    }
    /*
     * SPI bring-up path: stream INPUT_DATA as a one-way fast path.
     * Status readback depends on the CH584 IRQ response line and must not
     * gate the input cadence while the board link is being validated.
     */
    lastRfBeginRetryMs = HAL_GetTick();
}

bool ConnectionManager::applyWirelessReportRate(WirelessReportRate wirelessRate, bool persist) {
    const uint16_t nextRateHz = getRfReportRateHz(wirelessRate);
    const uint16_t previousRequestedHz = requestedReportRateHz;
    const uint16_t previousAppliedHz = appliedReportRateHz;
    requestedReportRateHz = nextRateHz;
    printf("[RF_RATE] apply begin mode=%u enum=%u target=%u prev_req=%u prev_applied=%u persist=%u\r\n",
            (unsigned int)mode,
            (unsigned int)wirelessRate,
            (unsigned int)nextRateHz,
            (unsigned int)previousRequestedHz,
            (unsigned int)previousAppliedHz,
            (unsigned int)persist);

    if (mode != ConnectionMode::CONNECTION_MODE_RF24G) {
        printf("[RF_RATE] apply skip not-rf mode=%u target=%u\r\n",
                (unsigned int)mode,
                (unsigned int)nextRateHz);
        if (persist) {
            STORAGE_MANAGER.setWirelessReportRate(wirelessRate);
            (void)STORAGE_MANAGER.saveConfig();
        }
        return false;
    }

    if (!requireRfCommandReady(RfPowerReason::RfMode)) {
        return false;
    }

    if (nextRateHz == appliedReportRateHz) {
        printf("[RF_RATE] apply re-send same target=%u applied=%u\r\n",
                (unsigned int)nextRateHz,
                (unsigned int)appliedReportRateHz);
        const bool rateOk = rfTransport.setRate(nextRateHz);
        printf("[RF_RATE] apply setRate result=%u target=%u same=1\r\n",
                (unsigned int)rateOk,
                (unsigned int)nextRateHz);
        if (!rateOk) {
            ConnectionLinkState nextState = ConnectionLinkState::Error;
            if (linkState != nextState) {
                MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
            }
            linkState = nextState;
            MonitorTelemetry_OnError("CONNECTION_MANAGER", 1004u, "runtime rf setRate failed");
            return false;
        }
        if (persist) {
            STORAGE_MANAGER.setWirelessReportRate(wirelessRate);
            return STORAGE_MANAGER.saveConfig();
        }
        return true;
    }

    const bool rateOk = rfTransport.setRate(nextRateHz);
    printf("[RF_RATE] apply setRate result=%u target=%u same=0 prev_applied=%u\r\n",
            (unsigned int)rateOk,
            (unsigned int)nextRateHz,
            (unsigned int)previousAppliedHz);
    if (!rateOk) {
        rateApplyPending = false;
        ConnectionLinkState nextState = ConnectionLinkState::Error;
        if (linkState != nextState) {
            MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
        }
        linkState = nextState;
        MonitorTelemetry_OnError("CONNECTION_MANAGER", 1004u, "runtime rf setRate failed");
        return false;
    }

    appliedReportRateHz = nextRateHz;
    rateApplyPending = false;
    printf("[RF_RATE] apply complete target=%u applied=%u\r\n",
            (unsigned int)nextRateHz,
            (unsigned int)appliedReportRateHz);
    if (persist) {
        STORAGE_MANAGER.setWirelessReportRate(wirelessRate);
        if (!STORAGE_MANAGER.saveConfig()) {
            return false;
        }
    }

    MonitorTelemetry_Init(mode, appliedReportRateHz);
    ConnectionLinkState nextState = ConnectionLinkState::Connected;
    if (linkState != nextState) {
        MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
    }
    linkState = nextState;
    return true;
}

bool ConnectionManager::startRfPairing() {
    if (!requireRfCommandReady(RfPowerReason::Manual)) {
        if (!wakeRfFromSleep(RfPowerReason::Manual)) {
            rfPairingActive = false;
            rfPairingState = RfPairingState::TxError;
            return false;
        }
    }

    if (!requireRfCommandReady(RfPowerReason::Manual)) {
        rfPairingActive = false;
        rfPairingState = RfPairingState::TxError;
        return false;
    }

    rfEventServiceEnabled = true;
    rfPairingActive = true;
    rfPairSucceeded = false;
    rfPairingState = RfPairingState::Starting;
    rfPairingStartedAtMs = HAL_GetTick();
    rfPairingLastErrorCommand = 0u;
    rfPairingLastErrorReason = 0u;

    (void)rfTransport.serviceEvents();
    const uint32_t errorsBeforeStart = rfTransport.getStatus().errorCounter;
    const bool ok = rfTransport.startPair();
    rfPairingLastEventCounter = rfTransport.getStatus().eventCounter;

    if (!ok) {
        const RFModuleStatus& st = rfTransport.getStatus();
        if (!(st.lastEvent == kRfEvtError && st.errorCounter != errorsBeforeStart)) {
            rfPairingActive = true;
            rfPairingState = RfPairingState::Starting;
            ConnectionLinkState nextState = ConnectionLinkState::Connecting;
            if (mode == ConnectionMode::CONNECTION_MODE_RF24G && linkState != nextState) {
                MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
            }
            if (mode == ConnectionMode::CONNECTION_MODE_RF24G) {
                linkState = nextState;
            }
            return true;
        }
        rfPairingActive = false;
        rfPairingState = RfPairingState::TxError;
        rfPairingLastErrorCommand = (st.lastErrorCommand != 0u) ?
                                    st.lastErrorCommand :
                                    kRfCmdStartPair;
        rfPairingLastErrorReason = st.lastErrorReason;
        ConnectionLinkState nextState = ConnectionLinkState::Error;
        if (mode == ConnectionMode::CONNECTION_MODE_RF24G && linkState != nextState) {
            MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
        }
        if (mode == ConnectionMode::CONNECTION_MODE_RF24G) {
            linkState = nextState;
        }
        MonitorTelemetry_OnError("CONNECTION_MANAGER", 1005u, "rf startPair failed");
        return false;
    }

    updateRfLinkStateFromStatus();
    updatePairingStateFromStatus();
    if (rfPairingState == RfPairingState::Starting) {
        rfPairingState = RfPairingState::PairModeOn;
    }
    return true;
}

bool ConnectionManager::stopRfPairing() {
    rfEventServiceEnabled = true;
    const bool ok = rfTransport.stopPair();
    rfPairingLastEventCounter = rfTransport.getStatus().eventCounter;
    if (ok) {
        rfPairingActive = false;
        if (rfPairingState == RfPairingState::PairModeOn ||
            rfPairingState == RfPairingState::Starting) {
            rfPairingState = RfPairingState::Idle;
        }
        updateRfLinkStateFromStatus();
    } else {
        const RFModuleStatus& st = rfTransport.getStatus();
        rfPairingActive = false;
        rfPairingState = RfPairingState::TxError;
        rfPairingLastErrorCommand = (st.lastErrorCommand != 0u) ? st.lastErrorCommand : kRfCmdStopPair;
        rfPairingLastErrorReason = st.lastErrorReason;
    }
    return ok;
}

bool ConnectionManager::tryRfSleepCommand() {
    const bool ok = rfTransport.sleep();
    if (ok) {
        setRfPowerState(RfPowerState::Sleeping, true);
        rfEventServiceEnabled = false;
        (void)RFBridgePort_PrepareWakeLineIdle();
        ConnectionLinkState nextState = ConnectionLinkState::Disconnected;
        if (mode == ConnectionMode::CONNECTION_MODE_RF24G && linkState != nextState) {
            MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
        }
        if (mode == ConnectionMode::CONNECTION_MODE_RF24G) {
            linkState = nextState;
        }
    } else {
        const RFModuleStatus& st = rfTransport.getStatus();
        rfPairingLastErrorCommand = (st.lastErrorCommand != 0u) ? st.lastErrorCommand : kRfCmdSleep;
        rfPairingLastErrorReason = st.lastErrorReason;
        ConnectionLinkState nextState = ConnectionLinkState::Error;
        if (mode == ConnectionMode::CONNECTION_MODE_RF24G && linkState != nextState) {
            MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
        }
        if (mode == ConnectionMode::CONNECTION_MODE_RF24G) {
            linkState = nextState;
        }
    }
    return ok;
}

bool ConnectionManager::checkAndResleepAfterUnexpectedWake(RfPowerReason reason) {
    const uint32_t start = HAL_GetTick();
    const uint32_t beforeEvents = rfTransport.getStatus().eventCounter;
    bool sawWakeComplete = false;

    while ((HAL_GetTick() - start) < kRfPostSleepWakeDetectMs) {
        if (!RFBridgePort_HasPendingEvent()) {
            HAL_Delay(1u);
            continue;
        }

        (void)rfTransport.serviceEvents(1u);
        const RFModuleStatus& st = rfTransport.getStatus();
        if ((st.eventCounter != beforeEvents) &&
            (st.lastEvent == kRfEvtWakeupComplete) &&
            (st.lastResult == 0u)) {
            sawWakeComplete = true;
            break;
        }
    }

    if (!sawWakeComplete) {
        return true;
    }

    printf("[RF_PWR][SLEEP_WAKE_EVT] reason=%s evt=0x%02X resleep=1\r\n",
           rfPowerReasonName(reason),
           (unsigned int)rfTransport.getStatus().lastEvent);

    rfEventServiceEnabled = true;
    setRfPowerState(RfPowerState::SleepPending, false);
    lastRfSleepRetryMs = HAL_GetTick();

    const bool ok = tryRfSleepCommand();
    if (!ok) {
        MonitorTelemetry_OnError("CONNECTION_MANAGER", 1009u, "rf resleep after wake failed");
        printf("[RF_PWR][SLEEP_RESLEEP_FAIL] reason=%s\r\n", rfPowerReasonName(reason));
        return false;
    }

    printf("[RF_PWR][SLEEP_RESLEEP_OK] reason=%s\r\n", rfPowerReasonName(reason));
    return true;
}

bool ConnectionManager::ensureRfSleeping(RfPowerReason reason) {
    if ((rfPowerState == RfPowerState::Sleeping) && !rfPowerStateIsBootHint()) {
        printf("[RF_PWR][SLEEP_SKIP] reason=%s state=%s\r\n",
               rfPowerReasonName(reason),
               rfPowerStateName(rfPowerState));
        return true;
    }

    if ((rfPowerState == RfPowerState::SleepPending) && !rfPowerStateIsBootHint()) {
        printf("[RF_PWR][SLEEP_PENDING] reason=%s\r\n", rfPowerReasonName(reason));
        return true;
    }

    printf("[RF_PWR][SLEEP_BEGIN] reason=%s mode=%u state=%s hint=%u\r\n",
           rfPowerReasonName(reason),
           (unsigned int)mode,
           rfPowerStateName(rfPowerState),
           (unsigned int)rfPowerStateIsBootHint());

    rfEventServiceEnabled = true;
    setRfPowerState(RfPowerState::SleepPending, false);
    lastRfSleepRetryMs = HAL_GetTick();
    const bool ok = tryRfSleepCommand();
    if (!ok) {
        MonitorTelemetry_OnError("CONNECTION_MANAGER", 1006u, "rf sleep failed");
        printf("[RF_PWR][SLEEP_FAIL] reason=%s\r\n", rfPowerReasonName(reason));
    } else {
        HAL_Delay(kRfPostSleepSettleMs);
        printf("[RF_PWR][SLEEP_OK] reason=%s\r\n", rfPowerReasonName(reason));
        if (!checkAndResleepAfterUnexpectedWake(reason)) {
            return false;
        }
    }
    return ok;
}

bool ConnectionManager::requireRfCommandReady(RfPowerReason reason) {
    if ((rfPowerState == RfPowerState::Awake) && !rfPowerStateIsBootHint()) {
        rfEventServiceEnabled = true;
        printf("[RF_PWR][CMD_READY] reason=%s state=%s\r\n",
               rfPowerReasonName(reason),
               rfPowerStateName(rfPowerState));
        return true;
    }

    printf("[RF_PWR][CMD_BLOCKED] reason=%s state=%s hint=%u\r\n",
           rfPowerReasonName(reason),
           rfPowerStateName(rfPowerState),
           (unsigned int)rfPowerStateIsBootHint());
    return false;
}

bool ConnectionManager::wakeRfFromSleep(RfPowerReason reason) {
    if ((rfPowerState == RfPowerState::Awake) && !rfPowerStateIsBootHint()) {
        rfEventServiceEnabled = true;
        printf("[RF_PWR][WAKE_SKIP] reason=%s state=%s\r\n",
               rfPowerReasonName(reason),
               rfPowerStateName(rfPowerState));
        return true;
    }

    if (rfPowerState != RfPowerState::Sleeping) {
        printf("[RF_PWR][WAKE_NOT_SLEEPING] reason=%s state=%s hint=%u\r\n",
               rfPowerReasonName(reason),
               rfPowerStateName(rfPowerState),
               (unsigned int)rfPowerStateIsBootHint());
        return false;
    }

    printf("[RF_PWR][WAKE_BEGIN] reason=%s mode=%u state=%s hint=%u\r\n",
           rfPowerReasonName(reason),
           (unsigned int)mode,
           rfPowerStateName(rfPowerState),
           (unsigned int)rfPowerStateIsBootHint());
    setRfPowerState(RfPowerState::WakePending, false);
    rfEventServiceEnabled = true;

    const bool wakeOk = rfTransport.wake();
    if (!wakeOk || rfTransport.getStatus().lastEvent != kRfEvtWakeupComplete) {
        setRfPowerState(RfPowerState::Error, false);
        const RFModuleStatus& st = rfTransport.getStatus();
        rfPairingLastErrorCommand = st.lastErrorCommand;
        rfPairingLastErrorReason = st.lastErrorReason;
        ConnectionLinkState nextState = ConnectionLinkState::Error;
        if (mode == ConnectionMode::CONNECTION_MODE_RF24G && linkState != nextState) {
            MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
        }
        if (mode == ConnectionMode::CONNECTION_MODE_RF24G) {
            linkState = nextState;
        }
        MonitorTelemetry_OnError("CONNECTION_MANAGER", 1007u, "rf wake failed");
        printf("[RF_PWR][WAKE_FAIL] reason=%s\r\n", rfPowerReasonName(reason));
        return false;
    }

    setRfPowerState(RfPowerState::Awake, true);
    printf("[RF_PWR][WAKE_OK] reason=%s\r\n", rfPowerReasonName(reason));
    return true;
}

bool ConnectionManager::enterRfModeAfterColdBoot(ConnectionMode connMode, WirelessReportRate wirelessRate) {
    rfEventServiceEnabled = true;
    requestedReportRateHz = getRfReportRateHz(wirelessRate);

    if (!RFBootReady::waitForModuleReady(kRfBootReadyTimeoutMs)) {
        MonitorTelemetry_OnError("CONNECTION_MANAGER", 1010u, "rf boot ready timeout");
        printf("[RF_BOOT][READY_FAIL] mode=%u rate=%u\r\n",
               (unsigned int)connMode,
               (unsigned int)requestedReportRateHz);
        return false;
    }

    setRfPowerState(RfPowerState::Awake, true);
    rfEventServiceEnabled = true;
    printf("[RF_BOOT][COMMAND_READY] mode=%u rate=%u\r\n",
           (unsigned int)connMode,
           (unsigned int)requestedReportRateHz);

    return restoreRfRuntime(wirelessRate);
}

bool ConnectionManager::restoreRfRuntime(WirelessReportRate wirelessRate) {
    requestedReportRateHz = getRfReportRateHz(wirelessRate);
    printf("[RF_PWR][RESTORE_BEGIN] mode=%u rate=%u\r\n",
           (unsigned int)mode,
           (unsigned int)requestedReportRateHz);

    bool restoreOk = true;
    if (mode == ConnectionMode::CONNECTION_MODE_RF24G) {
        restoreOk = rfTransport.setRate(requestedReportRateHz);
        if (restoreOk) {
            appliedReportRateHz = requestedReportRateHz;
            MonitorTelemetry_Init(mode, appliedReportRateHz);
        }
    } else {
        appliedReportRateHz = 1000u;
        restoreOk = true;
    }

    ConnectionLinkState nextState = restoreOk ? ConnectionLinkState::Connected : ConnectionLinkState::Error;
    if (mode == ConnectionMode::CONNECTION_MODE_RF24G && linkState != nextState) {
        MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
    }
    if (mode == ConnectionMode::CONNECTION_MODE_RF24G) {
        linkState = nextState;
    }
    if (!restoreOk) {
        MonitorTelemetry_OnError("CONNECTION_MANAGER", 1008u, "rf wake restore failed");
        printf("[RF_PWR][RESTORE_FAIL] mode=%u\r\n", (unsigned int)mode);
    } else {
        printf("[RF_PWR][RESTORE_OK] mode=%u rate=%u\r\n",
               (unsigned int)mode,
               (unsigned int)appliedReportRateHz);
    }
    return restoreOk;
}

bool ConnectionManager::initializeRfPowerForMode(ConnectionMode connMode, WirelessReportRate wirelessRate) {
    printf("[RF_PWR][INIT] mode=%u rate_enum=%u\r\n",
           (unsigned int)connMode,
           (unsigned int)wirelessRate);

    if (connMode == ConnectionMode::CONNECTION_MODE_USB) {
        appliedReportRateHz = 1000u;
        requestedReportRateHz = 1000u;
        if (!RFBootReady::waitForModuleReady(kRfBootReadyTimeoutMs)) {
            MonitorTelemetry_OnError("CONNECTION_MANAGER", 1011u, "rf boot ready timeout in usb mode");
            printf("[RF_BOOT][READY_FAIL_USB] mode=%u\r\n", (unsigned int)connMode);
            return false;
        }
        setRfPowerState(RfPowerState::Awake, true);
        rfEventServiceEnabled = false;
        printf("[RF_BOOT][USB_READY_IDLE] mode=%u\r\n", (unsigned int)connMode);
        return true;
    }

    return enterRfModeAfterColdBoot(connMode, wirelessRate);
}

bool ConnectionManager::switchOutputToRf(WirelessReportRate wirelessRate) {
    const uint16_t targetHz = getRfReportRateHz(wirelessRate);
    printf("[RF_MODE][SWITCH_TO_RF] begin mode=%u state=%s target=%u\r\n",
           (unsigned int)mode,
           rfPowerStateName(rfPowerState),
           (unsigned int)targetHz);

    if (mode == ConnectionMode::CONNECTION_MODE_RF24G) {
        return applyWirelessReportRate(wirelessRate, false);
    }

    if (!wakeRfFromSleep(RfPowerReason::RfMode)) {
        printf("[RF_MODE][SWITCH_TO_RF] wake_fail state=%s\r\n",
               rfPowerStateName(rfPowerState));
        return false;
    }

    const ConnectionMode previousMode = mode;
    mode = ConnectionMode::CONNECTION_MODE_RF24G;
    rfEventServiceEnabled = true;
    requestedReportRateHz = targetHz;

    if (!restoreRfRuntime(wirelessRate)) {
        mode = previousMode;
        printf("[RF_MODE][SWITCH_TO_RF] restore_fail target=%u\r\n",
               (unsigned int)targetHz);
        return false;
    }

    printf("[RF_MODE][SWITCH_TO_RF] ok target=%u\r\n", (unsigned int)targetHz);
    return true;
}

bool ConnectionManager::switchOutputToUsb() {
    printf("[RF_MODE][SWITCH_TO_USB] begin mode=%u state=%s\r\n",
           (unsigned int)mode,
           rfPowerStateName(rfPowerState));

    if (!ensureRfSleeping(RfPowerReason::UsbMode)) {
        printf("[RF_MODE][SWITCH_TO_USB] sleep_fail state=%s\r\n",
               rfPowerStateName(rfPowerState));
        return false;
    }

    mode = ConnectionMode::CONNECTION_MODE_USB;
    rfEventServiceEnabled = false;
    requestedReportRateHz = 1000u;
    appliedReportRateHz = 1000u;
    MonitorTelemetry_Init(mode, appliedReportRateHz);

    ConnectionLinkState nextState = get_usb_mounted() ? ConnectionLinkState::Connected : ConnectionLinkState::Disconnected;
    if (linkState != nextState) {
        MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
    }
    linkState = nextState;

    printf("[RF_MODE][SWITCH_TO_USB] ok\r\n");
    return true;
}

bool ConnectionManager::sleepRfModule() {
    return ensureRfSleeping(RfPowerReason::Manual);
}

bool ConnectionManager::wakeRfModule() {
    if (!wakeRfFromSleep(RfPowerReason::Manual)) {
        return false;
    }

    if (mode == ConnectionMode::CONNECTION_MODE_RF24G) {
        return restoreRfRuntime(STORAGE_MANAGER.getWirelessReportRate());
    }
    return true;
}

void ConnectionManager::loop() {
    serviceRfEvents();

    if ((rfPowerState == RfPowerState::SleepPending) &&
        ((HAL_GetTick() - lastRfSleepRetryMs) >= kRfSleepRetryMs)) {
        lastRfSleepRetryMs = HAL_GetTick();
        (void)tryRfSleepCommand();
    }

    if (mode == ConnectionMode::CONNECTION_MODE_USB) {
        ConnectionLinkState nextState = get_usb_mounted() ? ConnectionLinkState::Connected : ConnectionLinkState::Disconnected;
        if (linkState != nextState) {
            MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
        }
        linkState = nextState;
        return;
    }

    // RF24G 8K data streaming is intentionally independent from status readback.
}

void ConnectionManager::onReportReady(const GamepadState& state, uint32_t seq) {
    if (mode != ConnectionMode::CONNECTION_MODE_RF24G) return;
    if (rfPowerStateBlocksSpi()) return;

    if (rfPairingActive || RFBridgePort_HasPendingEvent()) {
        serviceRfEvents();
    }
    if (rfPairingActive) return;

    bool ok = rfTransport.sendInput(state, seq);
    rfSendWin++;
    rfLastSeq = seq;
    if (ok) {
        rfSendOkWin++;
        rfSendTotal++;
    } else {
        rfSendFailWin++;
    }

    ConnectionLinkState nextState = ok ? ConnectionLinkState::Connected : ConnectionLinkState::Error;
    if (linkState != nextState) {
        MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
    }
    linkState = nextState;
    if (!ok) {
        MonitorTelemetry_OnError("CONNECTION_MANAGER", 1002u, "rf sendInput failed");
    }
}
