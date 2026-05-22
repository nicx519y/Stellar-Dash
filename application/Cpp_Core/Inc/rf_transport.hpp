#ifndef RF_TRANSPORT_HPP
#define RF_TRANSPORT_HPP

#include <stdint.h>
#include <stddef.h>

#include "gamepad/GamepadState.hpp"

enum class RFTransportState : uint8_t {
    Disconnected = 0,
    Connected = 1,
    Error = 2,
};

enum class RFLinkState : uint8_t {
    Idle = 0,
    Pairing = 1,
    PairOk = 2,
    Connecting = 3,
    Connected = 4,
    Reconnecting = 5,
};

struct RFModuleStatus {
    RFLinkState state = RFLinkState::Idle;
    bool connected = false;
    bool hasBond = false;
    uint16_t rateHz = 1000;
    uint8_t txPowerLevel = 0;
    uint16_t rxOk = 0;
    uint16_t rxFail = 0;
    uint16_t txFail = 0;
    uint32_t rejectCount = 0;
};

class RFTransport {
public:
    RFTransport() = default;
    bool begin();
    bool startPair();
    bool stopPair();
    bool unbind();
    bool setRate(uint16_t rateHz);
    bool sendInput(const GamepadState& state, uint32_t seq);
    bool pollStatus();
    const RFModuleStatus& getStatus() const { return status; }
    RFTransportState getState() const { return state; }

private:
    bool transferCommand(uint8_t cmd, const uint8_t* payload, uint8_t len, bool forceReadback = false);
    bool parseEventFrame(const uint8_t* frame, uint16_t len);
    bool parseStatusPayload(const uint8_t* payload, uint8_t len);
    bool hasStatusChangedForLog() const;
    static uint8_t inputCrc8(const uint8_t* data, uint8_t len);
    static uint32_t buildHitboxKeyMask(const GamepadState& state);

    RFTransportState state = RFTransportState::Disconnected;
    RFModuleStatus status = {};
    RFModuleStatus lastLoggedStatus = {};
};

#endif
