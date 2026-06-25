#include "connection_manager.hpp"

#include "config.hpp"
#include "monitor_telemetry.hpp"
#include "rf_bridge_port.hpp"
#include "report_scheduler.hpp"
#include "storagemanager.hpp"
#include "usbdriver.hpp"
#include "system_logger.h"
#include "board_cfg.h"
#include "stm32h7xx_hal.h"

#include <stdio.h>

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
static constexpr uint8_t kRfCmdStartPair = 0x02u;
static constexpr uint8_t kRfCmdStopPair = 0x03u;
static constexpr uint8_t kRfCmdSleep = 0x08u;
static constexpr uint8_t kRfEvtError = 0x85u;
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
    if (!rfEventServiceEnabled && !rfPairingActive) {
        return;
    }

    const uint8_t drained = rfTransport.serviceEvents();
    if (drained == 0u) {
        return;
    }

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
    rfSleepPending = false;
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
        if (RFBridgePort_IsReady()) {
            (void)rfTransport.setRate(0u);
        }
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
    bool rateOk = rfTransport.setRate(requestedReportRateHz);
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
    if (REPORT_SCHEDULER.isStarted()) {
        REPORT_SCHEDULER.setRate(appliedReportRateHz);
    }
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
        rfSleepPending = false;
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

bool ConnectionManager::sleepRfModule() {
    rfEventServiceEnabled = true;
    rfSleepPending = true;
    lastRfSleepRetryMs = HAL_GetTick();
    const bool ok = tryRfSleepCommand();
    if (!ok) {
        MonitorTelemetry_OnError("CONNECTION_MANAGER", 1006u, "rf sleep failed");
    }
    return ok;
}

void ConnectionManager::loop() {
    serviceRfEvents();

    if (rfSleepPending && ((HAL_GetTick() - lastRfSleepRetryMs) >= kRfSleepRetryMs)) {
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
