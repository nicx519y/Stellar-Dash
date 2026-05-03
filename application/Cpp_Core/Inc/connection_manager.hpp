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
    void onReportReady(const GamepadState& state);

    ConnectionMode getMode() const { return mode; }
    ConnectionLinkState getLinkState() const { return linkState; }
    uint16_t getAppliedReportRateHz() const { return appliedReportRateHz; }

private:
    ConnectionManager() = default;

    ConnectionMode mode = ConnectionMode::CONNECTION_MODE_USB;
    ConnectionLinkState linkState = ConnectionLinkState::Disconnected;
    uint16_t appliedReportRateHz = 1000;
    RFTransport rfTransport;
};

#define CONNECTION_MANAGER ConnectionManager::getInstance()

#endif
