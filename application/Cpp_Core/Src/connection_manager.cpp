#include "connection_manager.hpp"

#include "config.hpp"
#include "usbdriver.hpp"

void ConnectionManager::setup(ConnectionMode connMode, WirelessReportRate wirelessRate) {
    mode = connMode;
    appliedReportRateHz = 1000;
    linkState = ConnectionLinkState::Disconnected;

    if (mode == ConnectionMode::CONNECTION_MODE_USB) {
        appliedReportRateHz = 1000;
        linkState = get_usb_mounted() ? ConnectionLinkState::Connected : ConnectionLinkState::Disconnected;
        return;
    }

    appliedReportRateHz = ConfigUtils::getWirelessReportRateHz(wirelessRate);
    linkState = ConnectionLinkState::Connecting;
    bool ok = rfTransport.begin() && rfTransport.setRate(appliedReportRateHz);
    linkState = ok ? ConnectionLinkState::Connected : ConnectionLinkState::Error;
}

void ConnectionManager::loop() {
    if (mode == ConnectionMode::CONNECTION_MODE_USB) {
        linkState = get_usb_mounted() ? ConnectionLinkState::Connected : ConnectionLinkState::Disconnected;
    }
}

void ConnectionManager::onReportReady(const GamepadState& state) {
    if (mode != ConnectionMode::CONNECTION_MODE_RF24G) return;
    bool ok = rfTransport.sendInput(state);
    linkState = ok ? ConnectionLinkState::Connected : ConnectionLinkState::Error;
}
