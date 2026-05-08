#include "rf_transport.hpp"

#include <string.h>

#include "board_cfg.h"
#include "monitor_telemetry.hpp"
#include "rf_bridge_port.hpp"
#include "system_logger.h"

namespace {
static constexpr uint8_t RF_SYNC = 0xA5u;
static constexpr uint8_t CMD_GET_STATUS = 0x01u;
static constexpr uint8_t CMD_START_PAIR = 0x02u;
static constexpr uint8_t CMD_STOP_PAIR = 0x03u;
static constexpr uint8_t CMD_UNBIND = 0x04u;
static constexpr uint8_t CMD_SET_RATE = 0x05u;
static constexpr uint8_t CMD_INPUT_DATA = 0x06u;
static constexpr uint8_t EVT_STATUS = 0x81u;
static constexpr uint8_t EVT_STATE_CHANGED = 0x82u;
static constexpr uint8_t EVT_RATE_APPLIED = 0x83u;
static constexpr uint8_t EVT_LINK_WARN = 0x84u;
static constexpr uint8_t EVT_ERROR = 0x85u;
static constexpr uint8_t INPUT_PAYLOAD_LEN = 15u;
static constexpr uint8_t STATUS_PAYLOAD_LEN = 17u;
static constexpr uint16_t RX_BUF_LEN = 32u;

static uint8_t frameChecksum(const uint8_t* buf, uint16_t len) {
    uint8_t s = 0u;
    for (uint16_t i = 0; i < len; i++) {
        s = static_cast<uint8_t>(s + buf[i]);
    }
    return s;
}

static const char* linkStateToString(RFLinkState st) {
    switch (st) {
    case RFLinkState::Idle: return "IDLE";
    case RFLinkState::Pairing: return "PAIRING";
    case RFLinkState::PairOk: return "PAIR_OK";
    case RFLinkState::Connecting: return "CONNECTING";
    case RFLinkState::Connected: return "CONNECTED";
    case RFLinkState::Reconnecting: return "RECONNECTING";
    default: return "UNKNOWN";
    }
}

static const char* eventToString(uint8_t evt) {
    switch (evt) {
    case EVT_STATUS: return "STATUS";
    case EVT_STATE_CHANGED: return "STATE_CHANGED";
    case EVT_RATE_APPLIED: return "RATE_APPLIED";
    case EVT_LINK_WARN: return "LINK_WARN";
    case EVT_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}
}

bool RFTransport::parseStatusPayload(const uint8_t* payload, uint8_t len) {
    if (payload == nullptr || len < STATUS_PAYLOAD_LEN) {
        return false;
    }

    status.state = static_cast<RFLinkState>(payload[0]);
    status.connected = payload[1] != 0u;
    status.hasBond = payload[2] != 0u;
    status.rateHz = static_cast<uint16_t>(payload[3] | (payload[4] << 8));
    status.txPowerLevel = payload[5];
    status.rxOk = static_cast<uint16_t>(payload[6] | (payload[7] << 8));
    status.rxFail = static_cast<uint16_t>(payload[8] | (payload[9] << 8));
    status.txFail = static_cast<uint16_t>(payload[10] | (payload[11] << 8));
    status.rejectCount = static_cast<uint32_t>(payload[12]) |
                         (static_cast<uint32_t>(payload[13]) << 8) |
                         (static_cast<uint32_t>(payload[14]) << 16) |
                         (static_cast<uint32_t>(payload[15]) << 24);
    return true;
}

bool RFTransport::hasStatusChangedForLog() const {
    return (status.state != lastLoggedStatus.state) ||
           (status.connected != lastLoggedStatus.connected) ||
           (status.hasBond != lastLoggedStatus.hasBond) ||
           (status.rateHz != lastLoggedStatus.rateHz) ||
           (status.rejectCount != lastLoggedStatus.rejectCount);
}

bool RFTransport::parseEventFrame(const uint8_t* frame, uint16_t len) {
    if (frame == nullptr || len < 4u) {
        return false;
    }
    if (frame[0] != RF_SYNC) {
        return false;
    }
    const uint8_t payloadLen = frame[2];
    const uint16_t expectLen = static_cast<uint16_t>(3u + payloadLen + 1u);
    if (len != expectLen) {
        return false;
    }
    if (frameChecksum(frame, static_cast<uint16_t>(len - 1u)) != frame[len - 1u]) {
        return false;
    }

    const uint8_t evt = frame[1];
    bool statusOk = false;
    if (payloadLen >= STATUS_PAYLOAD_LEN) {
        statusOk = parseStatusPayload(&frame[3], payloadLen);
        if (!statusOk) {
            return false;
        }
    } else {
        if (!(evt == EVT_STATUS && payloadLen == 1u && frame[3] == CMD_GET_STATUS)) {
            APP_DBG("[RF_BRIDGE] short evt frame evt=0x%02X payload=%u tag=0x%02X",
                    (unsigned int)evt,
                    (unsigned int)payloadLen,
                    (unsigned int)(payloadLen > 0u ? frame[3] : 0u));
        }
    }

    const bool shouldLog = (evt != EVT_STATUS) || (statusOk && hasStatusChangedForLog());
    if (!shouldLog) {
        return true;
    }
    lastLoggedStatus = status;

    APP_DBG("[RF_BRIDGE] event=%s state=%s connected=%u hasBond=%u rate=%u reject=%lu",
            eventToString(evt), linkStateToString(status.state), status.connected ? 1u : 0u,
            status.hasBond ? 1u : 0u, status.rateHz, status.rejectCount);

    return true;
}

bool RFTransport::transferCommand(uint8_t cmd, const uint8_t* payload, uint8_t len, bool forceReadback) {
    if (len > 24u) {
        state = RFTransportState::Error;
        return false;
    }

    uint8_t frame[4u + 24u + 1u] = {0};
    frame[0] = RF_SYNC;
    frame[1] = cmd;
    frame[2] = len;
    if (len > 0u && payload != nullptr) {
        memcpy(&frame[3], payload, len);
    }

    uint8_t checksum = 0u;
    const uint8_t totalNoChecksum = (uint8_t)(3u + len);
    for (uint8_t i = 0; i < totalNoChecksum; i++) {
        checksum = (uint8_t)(checksum + frame[i]);
    }
    frame[totalNoChecksum] = checksum;

    const bool wantReadback = forceReadback ||
                              (cmd == CMD_GET_STATUS) ||
                              (cmd == CMD_START_PAIR) ||
                              (cmd == CMD_STOP_PAIR) ||
                              (cmd == CMD_UNBIND) ||
                              (cmd == CMD_SET_RATE);
    uint8_t rxBuf[RX_BUF_LEN] = {0};
    uint16_t rxLen = wantReadback ? RX_BUF_LEN : 0u;
    bool ok = RFBridgePort_Transfer(frame, (uint16_t)(totalNoChecksum + 1u), rxBuf, &rxLen);
    if (!ok) {
        state = RFTransportState::Error;
        return false;
    }

    if (rxLen > 0u) {
        if (!parseEventFrame(rxBuf, rxLen)) {
            state = RFTransportState::Error;
            return false;
        }
    }

    state = RFTransportState::Connected;
    return true;
}

uint8_t RFTransport::encodeDpad(uint8_t dpad) {
    const bool up = (dpad & GAMEPAD_MASK_UP) != 0u;
    const bool down = (dpad & GAMEPAD_MASK_DOWN) != 0u;
    const bool left = (dpad & GAMEPAD_MASK_LEFT) != 0u;
    const bool right = (dpad & GAMEPAD_MASK_RIGHT) != 0u;

    if (up && right) return 2u;
    if (right && down) return 4u;
    if (down && left) return 6u;
    if (left && up) return 8u;
    if (up) return 1u;
    if (right) return 3u;
    if (down) return 5u;
    if (left) return 7u;
    return 0u;
}

bool RFTransport::begin() {
    return transferCommand(CMD_GET_STATUS, nullptr, 0u, true);
}

bool RFTransport::startPair() {
    return transferCommand(CMD_START_PAIR, nullptr, 0u, true);
}

bool RFTransport::stopPair() {
    return transferCommand(CMD_STOP_PAIR, nullptr, 0u, true);
}

bool RFTransport::unbind() {
    return transferCommand(CMD_UNBIND, nullptr, 0u, true);
}

bool RFTransport::setRate(uint16_t rateHz) {
    uint8_t payload[2] = {
        (uint8_t)(rateHz & 0xFFu),
        (uint8_t)((rateHz >> 8) & 0xFFu),
    };
    return transferCommand(CMD_SET_RATE, payload, sizeof(payload), true);
}

bool RFTransport::pollStatus() {
    return transferCommand(CMD_GET_STATUS, nullptr, 0u, true);
}

bool RFTransport::sendInput(const GamepadState& gamepad, uint32_t seq) {
    uint8_t payload[INPUT_PAYLOAD_LEN] = {0};
    payload[0] = static_cast<uint8_t>(seq & 0xFFu);
    payload[1] = 0u;

    uint16_t buttons = (uint16_t)(gamepad.buttons & 0xFFFFu);
    payload[2] = (uint8_t)(buttons & 0xFFu);
    payload[3] = (uint8_t)((buttons >> 8) & 0xFFu);
    payload[4] = encodeDpad(gamepad.dpad);
    payload[5] = gamepad.lt;
    payload[6] = gamepad.rt;

    int16_t lx = (int16_t)((int32_t)gamepad.lx - 32768);
    int16_t ly = (int16_t)((int32_t)gamepad.ly - 32768);
    int16_t rx = (int16_t)((int32_t)gamepad.rx - 32768);
    int16_t ry = (int16_t)((int32_t)gamepad.ry - 32768);

    payload[7] = (uint8_t)(lx & 0xFF);
    payload[8] = (uint8_t)((lx >> 8) & 0xFF);
    payload[9] = (uint8_t)(ly & 0xFF);
    payload[10] = (uint8_t)((ly >> 8) & 0xFF);
    payload[11] = (uint8_t)(rx & 0xFF);
    payload[12] = (uint8_t)((rx >> 8) & 0xFF);
    payload[13] = (uint8_t)(ry & 0xFF);
    payload[14] = (uint8_t)((ry >> 8) & 0xFF);

    bool ok = transferCommand(CMD_INPUT_DATA, payload, sizeof(payload), false);
    MonitorTelemetry_OnRfTransfer(seq, CMD_INPUT_DATA, sizeof(payload), ok);
    return ok;
}
