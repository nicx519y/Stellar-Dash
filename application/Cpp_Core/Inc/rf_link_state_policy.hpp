#ifndef RF_LINK_STATE_POLICY_HPP
#define RF_LINK_STATE_POLICY_HPP

#include <stdint.h>

#include "rf_transport.hpp"

enum class ConnectionLinkState : uint8_t {
    Disconnected = 0,
    Connecting = 1,
    Connected = 2,
    Error = 3,
};

constexpr ConnectionLinkState connectionLinkStateFromRfStatus(
    RFLinkState state,
    bool connected)
{
    switch (state) {
        case RFLinkState::Pairing:
        case RFLinkState::PairOk:
        case RFLinkState::Connecting:
        case RFLinkState::Reconnecting:
            return ConnectionLinkState::Connecting;
        case RFLinkState::Connected:
            return connected
                ? ConnectionLinkState::Connected
                : ConnectionLinkState::Connecting;
        case RFLinkState::Idle:
        case RFLinkState::PairTimeout:
        case RFLinkState::PairFailed:
        default:
            return ConnectionLinkState::Disconnected;
    }
}

#endif
