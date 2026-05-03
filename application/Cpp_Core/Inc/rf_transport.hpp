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

class RFTransport {
public:
    RFTransport() = default;
    bool begin();
    bool setRate(uint16_t rateHz);
    bool sendInput(const GamepadState& state);
    RFTransportState getState() const { return state; }

private:
    bool transferCommand(uint8_t cmd, const uint8_t* payload, uint8_t len);
    static uint8_t encodeDpad(uint8_t dpad);

    RFTransportState state = RFTransportState::Disconnected;
    uint8_t seq = 0;
};

#endif
