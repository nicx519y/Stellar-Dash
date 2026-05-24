#include "connection_manager.hpp"

#include "config.hpp"
#include "monitor_telemetry.hpp"
#include "report_scheduler.hpp"
#include "storagemanager.hpp"
#include "usbdriver.hpp"
#include "system_logger.h"
#include "board_cfg.h"
#include "stm32h7xx_hal.h"

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

static constexpr uint32_t kRfRateApplyRetryMs = 500u;
static constexpr uint32_t kRfPairingLocalTimeoutMs = 65000u;
static constexpr uint8_t kRfCmdStartPair = 0x02u;
static constexpr uint8_t kRfCmdStopPair = 0x03u;
}

bool ConnectionManager::tryRfBringup(bool isRetry) {
    bool ok = rfTransport.begin();
    // APP_DBG("[RF_BRIDGE] rf begin %sresult: %d", isRetry ? "retry " : "", ok);

    // APP_DBG("[RF_BRIDGE] initial status read %s", ok ? "ok" : "failed");
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
        // APP_DBG("[RF_BRIDGE] rf setup failed");
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
    const RfPairingState prevState = rfPairingState;
    const bool prevActive = rfPairingActive;

    if (st.lastEvent == 0x85u) {
        rfPairingActive = false;
        rfPairingState = RfPairingState::TxError;
        rfPairingLastErrorCommand = st.lastErrorCommand;
        rfPairingLastErrorReason = st.lastErrorReason;
        APP_DBG("[RF_PAIR] state evt:0x%02X rf:%u active:%u->%u pair:%u->%u",
                (unsigned int)st.lastEvent,
                (unsigned int)st.state,
                (unsigned int)(prevActive ? 1u : 0u),
                (unsigned int)(rfPairingActive ? 1u : 0u),
                (unsigned int)prevState,
                (unsigned int)rfPairingState);
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
    case RFLinkState::PairOk:
        rfPairingActive = false;
        rfPairSucceeded = true;
        rfPairingState = RfPairingState::PairOk;
        break;
    case RFLinkState::Idle:
        if (rfPairingState == RfPairingState::PairModeOn || rfPairingActive) {
            rfPairingActive = false;
            rfPairingState = RfPairingState::Timeout;
        }
        break;
    default:
        break;
    }

    if (prevState != rfPairingState || prevActive != rfPairingActive) {
        APP_DBG("[RF_PAIR] state evt:0x%02X rf:%u active:%u->%u pair:%u->%u",
                (unsigned int)st.lastEvent,
                (unsigned int)st.state,
                (unsigned int)(prevActive ? 1u : 0u),
                (unsigned int)(rfPairingActive ? 1u : 0u),
                (unsigned int)prevState,
                (unsigned int)rfPairingState);
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
        APP_DBG("[RF_PAIR] event evt:0x%02X rf:%u events:%lu",
                (unsigned int)rfTransport.getStatus().lastEvent,
                (unsigned int)rfTransport.getStatus().state,
                (unsigned long)rfTransport.getStatus().eventCounter);
        updatePairingStateFromStatus();
    }
}

void ConnectionManager::setup(ConnectionMode connMode, WirelessReportRate wirelessRate) {
    mode = connMode;
    appliedReportRateHz = 1000;
    requestedReportRateHz = 1000;
    rateApplyPending = false;
    linkState = ConnectionLinkState::Disconnected;
    lastRfStatusPollMs = HAL_GetTick();
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
    appliedReportRateHz = requestedReportRateHz;
    MonitorTelemetry_Init(mode, appliedReportRateHz);
    bool rateOk = rfTransport.setRate(requestedReportRateHz);
    rateApplyPending = !rateOk;
    if (!rateOk) {
        ConnectionLinkState nextState = ConnectionLinkState::Error;
        if (linkState != nextState) {
            MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
        }
        linkState = nextState;
        MonitorTelemetry_OnError("CONNECTION_MANAGER", 1003u, "rf setRate failed");
        APP_ERR("[RF_BRIDGE] setRate failed, requested:%u", appliedReportRateHz);
    } else {
        linkState = ConnectionLinkState::Connected;
        MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(linkState));
        APP_DBG("[RF_BRIDGE] rate applied:%u", appliedReportRateHz);
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
    requestedReportRateHz = nextRateHz;

    if (mode != ConnectionMode::CONNECTION_MODE_RF24G) {
        if (persist) {
            STORAGE_MANAGER.setWirelessReportRate(wirelessRate);
            (void)STORAGE_MANAGER.saveConfig();
        }
        return false;
    }

    if (nextRateHz == appliedReportRateHz) {
        if (persist) {
            STORAGE_MANAGER.setWirelessReportRate(wirelessRate);
            return STORAGE_MANAGER.saveConfig();
        }
        return true;
    }

    if (!rfTransport.setRate(nextRateHz)) {
        rateApplyPending = true;
        ConnectionLinkState nextState = ConnectionLinkState::Error;
        if (linkState != nextState) {
            MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
        }
        linkState = nextState;
        MonitorTelemetry_OnError("CONNECTION_MANAGER", 1004u, "runtime rf setRate failed");
        APP_ERR("[RF_BRIDGE] runtime setRate failed, requested:%u", nextRateHz);
        return false;
    }

    appliedReportRateHz = nextRateHz;
    rateApplyPending = false;
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
    APP_DBG("[RF_BRIDGE] runtime rate applied:%u", appliedReportRateHz);
    return true;
}

bool ConnectionManager::startRfPairing() {
    APP_DBG("[RF_PAIR] start request mode:%u link:%u",
            (unsigned int)mode,
            (unsigned int)linkState);
    rfEventServiceEnabled = true;
    rfPairingActive = true;
    rfPairSucceeded = false;
    rfPairingState = RfPairingState::Starting;
    rfPairingStartedAtMs = HAL_GetTick();
    rfPairingLastErrorCommand = 0u;
    rfPairingLastErrorReason = 0u;

    (void)rfTransport.serviceEvents();
    const bool ok = rfTransport.startPair();
    rfPairingLastEventCounter = rfTransport.getStatus().eventCounter;

    if (!ok) {
        const RFModuleStatus& st = rfTransport.getStatus();
        APP_ERR("[RF_PAIR] start failed last_evt:0x%02X err_cmd:0x%02X reason:0x%02X events:%lu",
                (unsigned int)st.lastEvent,
                (unsigned int)st.lastErrorCommand,
                (unsigned int)st.lastErrorReason,
                (unsigned long)st.eventCounter);
        rfPairingActive = false;
        rfPairingState = RfPairingState::TxError;
        rfPairingLastErrorCommand = (st.lastErrorCommand != 0u) ? st.lastErrorCommand : kRfCmdStartPair;
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

    APP_DBG("[RF_PAIR] start ok events:%lu state:%u",
            (unsigned long)rfTransport.getStatus().eventCounter,
            (unsigned int)rfTransport.getStatus().state);
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

void ConnectionManager::loop() {
    serviceRfEvents();

    if (rfPairingActive &&
        rfPairingStartedAtMs != 0u &&
        (HAL_GetTick() - rfPairingStartedAtMs) >= kRfPairingLocalTimeoutMs) {
        APP_DBG("[RF_PAIR] local timeout active:1 pair:%u", (unsigned int)rfPairingState);
        rfPairingActive = false;
        rfPairingState = RfPairingState::Timeout;
    }

    if (mode == ConnectionMode::CONNECTION_MODE_USB) {
        ConnectionLinkState nextState = get_usb_mounted() ? ConnectionLinkState::Connected : ConnectionLinkState::Disconnected;
        if (linkState != nextState) {
            MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
        }
        linkState = nextState;
        return;
    }

    if (rateApplyPending && ((HAL_GetTick() - lastRfBeginRetryMs) >= kRfRateApplyRetryMs)) {
        lastRfBeginRetryMs = HAL_GetTick();
        if (rfTransport.setRate(requestedReportRateHz)) {
            appliedReportRateHz = requestedReportRateHz;
            rateApplyPending = false;
            if (REPORT_SCHEDULER.isStarted()) {
                REPORT_SCHEDULER.setRate(appliedReportRateHz);
            }
            MonitorTelemetry_Init(mode, appliedReportRateHz);
            ConnectionLinkState nextState = ConnectionLinkState::Connected;
            if (linkState != nextState) {
                MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
            }
            linkState = nextState;
            APP_DBG("[RF_BRIDGE] retry rate applied:%u", appliedReportRateHz);
        }
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

    const uint32_t nowMs = HAL_GetTick();
    const uint32_t elapsed = nowMs - rfStatLastMs;
    if (elapsed >= 5000u) {
        const uint32_t hz = (elapsed != 0u) ? ((rfSendOkWin * 1000u) / elapsed) : 0u;
        APP_DBG("[RF_SEND][5s] calls:%lu ok:%lu fail:%lu hz:%lu total:%lu last_seq:%lu rate:%u",
                rfSendWin,
                rfSendOkWin,
                rfSendFailWin,
                hz,
                rfSendTotal,
                rfLastSeq,
                appliedReportRateHz);
        rfStatLastMs = nowMs;
        rfSendWin = 0u;
        rfSendOkWin = 0u;
        rfSendFailWin = 0u;
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
