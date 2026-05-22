#include "connection_manager.hpp"

#include "config.hpp"
#include "monitor_telemetry.hpp"
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

void ConnectionManager::setup(ConnectionMode connMode, WirelessReportRate wirelessRate) {
    mode = connMode;
    appliedReportRateHz = 1000;
    linkState = ConnectionLinkState::Disconnected;
    lastRfStatusPollMs = HAL_GetTick();
    rfStatLastMs = HAL_GetTick();
    rfSendWin = 0u;
    rfSendOkWin = 0u;
    rfSendFailWin = 0u;
    rfSendTotal = 0u;
    rfLastSeq = 0u;

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

    appliedReportRateHz = getRfReportRateHz(wirelessRate);
    MonitorTelemetry_Init(mode, appliedReportRateHz);
    /*
     * SPI bring-up path: stream INPUT_DATA as a one-way fast path.
     * Status readback depends on the CH584 IRQ response line and must not
     * gate the input cadence while the board link is being validated.
     */
    linkState = ConnectionLinkState::Connected;
    MonitorTelemetry_OnLinkStateChanged(mode, static_cast<uint8_t>(linkState));
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

    // RF24G 8K data streaming is intentionally independent from status readback.
}

void ConnectionManager::onReportReady(const GamepadState& state, uint32_t seq) {
    if (mode != ConnectionMode::CONNECTION_MODE_RF24G) return;

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
