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
    PairTimeout = 6,
    PairFailed = 7,
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
    uint8_t lastEvent = 0;
    uint8_t lastCommandTag = 0;
    uint8_t lastTransactionId = 0;
    uint8_t lastResult = 0;
    uint8_t lastErrorCommand = 0;
    uint8_t lastErrorReason = 0;
    uint32_t eventCounter = 0;
    uint32_t errorCounter = 0;
};

class RFTransport {
public:
    RFTransport() = default;
    bool begin();
    bool startPair();
    bool stopPair();
    bool unbind();
    bool sleep();
    bool wake();
    bool setRate(uint16_t rateHz);
    bool sendInput(const GamepadState& state, uint32_t seq);
    bool pollStatus();
    uint8_t serviceEvents(uint8_t drainLimit = 4u);
    const RFModuleStatus& getStatus() const { return status; }
    RFTransportState getState() const { return state; }

private:
    bool transferCommand(uint8_t cmd, const uint8_t* payload, uint8_t len, bool forceReadback = false);
    bool sendInputFrame(const uint8_t* payload, uint8_t len);
    bool parseEventFrame(const uint8_t* frame, uint16_t len, bool* applied = nullptr);
    void processCompletedReliableEvents();
    bool parseStatusPayload(const uint8_t* payload, uint8_t len);
    bool lastEventMatches(uint8_t cmd, uint8_t txn) const;
    bool waitCommandResult(uint8_t cmd, uint8_t txn, uint32_t timeoutMs);
    bool waitWakeupComplete(uint32_t timeoutMs);
    static uint8_t inputCrc8(const uint8_t* data, uint8_t len);
    static uint32_t buildHitboxKeyMask(const GamepadState& state);

    RFTransportState state = RFTransportState::Disconnected;
    RFModuleStatus status = {};
};

#endif
