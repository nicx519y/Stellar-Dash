#include "connection_manager.hpp"

#include "config.hpp"
#include "monitor_telemetry.hpp"
#include "usbdriver.hpp"
#include "system_logger.h"
#include "stm32h7xx_hal.h"

bool ConnectionManager::tryRfBringup(bool isRetry) {
    bool ok = rfTransport.begin();
    APP_DBG("[RF_BRIDGE] rf begin %sresult: %d", isRetry ? "retry " : "", ok);

    if (ok) {
        const RFModuleStatus& st = rfTransport.getStatus();
        APP_DBG("[RF_BRIDGE] initial state=%u connected=%u hasBond=%u rate=%u",
                static_cast<uint8_t>(st.state), st.connected ? 1u : 0u, st.hasBond ? 1u : 0u, st.rateHz);
    }
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
        APP_DBG("[RF_BRIDGE] rf setup failed");
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

void ConnectionManager::setup(ConnectionMode connMode, WirelessReportRate wirelessRate) {
    mode = connMode;
    appliedReportRateHz = 1000;
    linkState = ConnectionLinkState::Disconnected;
    lastRfStatusPollMs = HAL_GetTick();

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

    appliedReportRateHz = ConfigUtils::getWirelessReportRateHz(wirelessRate);
    MonitorTelemetry_Init(mode, appliedReportRateHz);
    (void)tryRfBringup(false);
    lastRfBeginRetryMs = HAL_GetTick();
}

void ConnectionManager::loop() {
    if (mode == ConnectionMode::CONNECTION_MODE_USB) {
        ConnectionLinkState nextState = get_usb_mounted() ? ConnectionLinkState::Connected : ConnectionLinkState::Disconnected;
        if (linkState != nextState) {
            MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
        }
        linkState = nextState;
        return;
    }

    const uint32_t nowMs = HAL_GetTick();
    if (linkState == ConnectionLinkState::Error) {
        if ((nowMs - lastRfBeginRetryMs) >= 1000u) {
            lastRfBeginRetryMs = nowMs;
            (void)tryRfBringup(true);
        }
        return;
    }

    if ((nowMs - lastRfStatusPollMs) >= 200u) {
        lastRfStatusPollMs = nowMs;
        if (rfTransport.pollStatus()) {
            updateRfLinkStateFromStatus();
        } else {
            ConnectionLinkState nextState = ConnectionLinkState::Error;
            if (linkState != nextState) {
                MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
            }
            linkState = nextState;
            MonitorTelemetry_OnError("CONNECTION_MANAGER", 1003u, "rf pollStatus failed");
            APP_DBG("[RF_BRIDGE] rf poll status failed");
        }
    }
}

void ConnectionManager::onReportReady(const GamepadState& state, uint32_t seq) {
    if (mode != ConnectionMode::CONNECTION_MODE_RF24G) return;
    if (!rfTransport.getStatus().connected) return;

    bool ok = rfTransport.sendInput(state, seq);
    ConnectionLinkState nextState = ok ? ConnectionLinkState::Connected : ConnectionLinkState::Error;
    if (linkState != nextState) {
        MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(nextState));
    }
    linkState = nextState;
    if (!ok) {
        MonitorTelemetry_OnError("CONNECTION_MANAGER", 1002u, "rf sendInput failed");
    }
}
