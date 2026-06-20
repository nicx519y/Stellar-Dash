#include "rf_transport.hpp"

#include <string.h>

#include "board_cfg.h"
#include "micro_timer.hpp"
#include "monitor_telemetry.hpp"
#include "rf_bridge_port.hpp"
#include "system_logger.h"

extern "C" {
#include "app_log_control.h"
}

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
static constexpr uint8_t EVT_MONITOR_CONFIG = 0x86u;
static constexpr uint8_t EVT_TIME_SYNC = 0x87u;
static constexpr uint8_t RFMON_FLAG_STM32_LOG = 0x08u;
static constexpr uint8_t INPUT_PAYLOAD_LEN = 10u;
static constexpr uint8_t INPUT_FORMAT_VERSION = 1u;
static constexpr uint8_t INPUT_FLAG_PROCESSED = 0x01u;
static constexpr uint8_t INPUT_FLAG_SYNC_ECHO = 0x02u;
static constexpr uint8_t INPUT_FLAGS = static_cast<uint8_t>((INPUT_FORMAT_VERSION << 4) | INPUT_FLAG_PROCESSED);
static constexpr uint8_t INPUT_CRC_OFFSET = 9u;
static constexpr uint8_t STATUS_PAYLOAD_LEN = 20u;
static constexpr uint8_t STATUS_CMD_TAG_OFFSET = 16u;
static constexpr uint8_t STATUS_TXN_OFFSET = 17u;
static constexpr uint8_t STATUS_RESULT_OFFSET = 18u;
static constexpr uint8_t STATUS_REASON_OFFSET = 19u;
static constexpr uint16_t RX_BUF_LEN = 32u;
static constexpr uint8_t CONTROL_RETRY_COUNT = 3u;

struct PendingTimeSyncEcho {
    bool pending = false;
    uint8_t seq = 0u;
    uint32_t rxTickUs = 0u;
};

PendingTimeSyncEcho g_pendingTimeSyncEcho;

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
    case RFLinkState::PairTimeout: return "PAIR_TIMEOUT";
    case RFLinkState::PairFailed: return "PAIR_FAILED";
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
    case EVT_MONITOR_CONFIG: return "MONITOR_CONFIG";
    case EVT_TIME_SYNC: return "TIME_SYNC";
    default: return "UNKNOWN";
    }
}

}

bool RFTransport::parseStatusPayload(const uint8_t* payload, uint8_t len) {
    if (payload == nullptr || len < 17u) {
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
    status.lastCommandTag = payload[STATUS_CMD_TAG_OFFSET];
    status.lastTransactionId = (len > STATUS_TXN_OFFSET) ? payload[STATUS_TXN_OFFSET] : 0u;
    status.lastResult = (len > STATUS_RESULT_OFFSET) ? payload[STATUS_RESULT_OFFSET] : 0u;
    status.lastErrorReason = (len > STATUS_REASON_OFFSET) ? payload[STATUS_REASON_OFFSET] : status.lastResult;
    return true;
}

bool RFTransport::hasStatusChangedForLog() const {
    return (status.state != lastLoggedStatus.state) ||
           (status.connected != lastLoggedStatus.connected) ||
           (status.hasBond != lastLoggedStatus.hasBond) ||
           (status.rateHz != lastLoggedStatus.rateHz) ||
           (status.rxOk != lastLoggedStatus.rxOk) ||
           (status.rxFail != lastLoggedStatus.rxFail) ||
           (status.txFail != lastLoggedStatus.txFail) ||
           (status.lastCommandTag != lastLoggedStatus.lastCommandTag) ||
           (status.lastTransactionId != lastLoggedStatus.lastTransactionId) ||
           (status.lastResult != lastLoggedStatus.lastResult) ||
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
    status.lastEvent = evt;
    status.eventCounter++;

    if (evt == EVT_MONITOR_CONFIG) {
        if (payloadLen < 4u) {
            return false;
        }
        const uint8_t seq = frame[3];
        const uint8_t flags = frame[4];
        const uint8_t result = frame[6];
        const uint8_t enabled = (flags & RFMON_FLAG_STM32_LOG) != 0u ? 1u : 0u;
        AppLogControl_Set(enabled, enabled);
        status.lastCommandTag = EVT_MONITOR_CONFIG;
        status.lastTransactionId = seq;
        status.lastResult = result;
        status.lastErrorReason = 0u;
        state = RFTransportState::Connected;
        APP_DBG("[RF_BRIDGE] monitor config seq=%u stm32Log=%u result=%u",
                static_cast<unsigned int>(seq),
                static_cast<unsigned int>(enabled),
                static_cast<unsigned int>(result));
        return true;
    }

    if (evt == EVT_TIME_SYNC) {
        if (payloadLen < 1u) {
            return false;
        }
        g_pendingTimeSyncEcho.pending = true;
        g_pendingTimeSyncEcho.seq = frame[3];
        g_pendingTimeSyncEcho.rxTickUs = MICROS_TIMER.micros();
        status.lastCommandTag = EVT_TIME_SYNC;
        status.lastTransactionId = frame[3];
        status.lastResult = 0u;
        status.lastErrorReason = 0u;
        state = RFTransportState::Connected;
        return true;
    }

    bool statusOk = false;
    if (payloadLen >= 17u) {
        statusOk = parseStatusPayload(&frame[3], payloadLen);
        if (!statusOk) {
            return false;
        }
    } else {
        if (!(evt == EVT_STATUS && payloadLen == 1u && frame[3] == CMD_GET_STATUS)) {
            // APP_DBG("[RF_BRIDGE] short evt frame evt=0x%02X payload=%u tag=0x%02X",
            //         (unsigned int)evt,
            //         (unsigned int)payloadLen,
            //         (unsigned int)(payloadLen > 0u ? frame[3] : 0u));
        }
    }

    if (evt == EVT_ERROR) {
        status.errorCounter++;
        if (statusOk) {
            status.lastErrorCommand = status.lastCommandTag;
        } else {
            status.lastErrorCommand = (payloadLen >= 1u) ? frame[3] : 0u;
            status.lastErrorReason = (payloadLen >= 2u) ? frame[4] : 0u;
            status.lastCommandTag = status.lastErrorCommand;
            status.lastResult = status.lastErrorReason;
            status.lastTransactionId = (payloadLen >= 3u) ? frame[5] : 0u;
        }
        state = RFTransportState::Error;
    } else {
        if (statusOk) {
            status.lastErrorCommand = 0u;
            status.lastErrorReason = 0u;
        }
        state = RFTransportState::Connected;
    }

    const bool shouldLog = (evt != EVT_STATUS) || (statusOk && hasStatusChangedForLog());
    if (!shouldLog) {
        return true;
    }
    lastLoggedStatus = status;

    APP_DBG("[RF_BRIDGE] event=%s state=%s connected=%u hasBond=%u rate=%u rxOk=%u rxFail=%u txFail=%u cmd=0x%02X txn=%u result=%u reject=%lu",
            eventToString(evt), linkStateToString(status.state), status.connected ? 1u : 0u,
            status.hasBond ? 1u : 0u, status.rateHz,
            (unsigned int)status.rxOk,
            (unsigned int)status.rxFail,
            (unsigned int)status.txFail,
            (unsigned int)status.lastCommandTag,
            (unsigned int)status.lastTransactionId,
            (unsigned int)status.lastResult,
            status.rejectCount);

    return true;
}

uint8_t RFTransport::nextTransactionId() {
    static uint8_t s_nextTxn = 0u;
    s_nextTxn++;
    if (s_nextTxn == 0u) {
        s_nextTxn = 1u;
    }
    return s_nextTxn;
}

bool RFTransport::lastEventMatches(uint8_t cmd, uint8_t txn) const {
    return (status.lastCommandTag == cmd) &&
           (status.lastTransactionId == txn);
}

bool RFTransport::transferCommand(uint8_t cmd, const uint8_t* payload, uint8_t len, bool forceReadback) {
    (void)forceReadback;

    if (cmd == CMD_INPUT_DATA) {
        return sendInputFrame(payload, len);
    }

    if (len > 23u) {
        state = RFTransportState::Error;
        return false;
    }

    const uint8_t txn = nextTransactionId();
    const uint8_t controlPayloadLen = static_cast<uint8_t>(len + 1u);

    uint8_t frame[4u + 24u + 1u] = {0};
    frame[0] = RF_SYNC;
    frame[1] = cmd;
    frame[2] = controlPayloadLen;
    frame[3] = txn;
    if (len > 0u && payload != nullptr) {
        memcpy(&frame[4], payload, len);
    }

    uint8_t checksum = 0u;
    const uint8_t totalNoChecksum = static_cast<uint8_t>(3u + controlPayloadLen);
    for (uint8_t i = 0; i < totalNoChecksum; i++) {
        checksum = (uint8_t)(checksum + frame[i]);
    }
    frame[totalNoChecksum] = checksum;

    APP_DBG("[RF_TRANSPORT] ctrl cmd=0x%02X txn=%u payload=%u",
            (unsigned int)cmd,
            (unsigned int)txn,
            (unsigned int)len);

    for (uint8_t attempt = 0u; attempt < CONTROL_RETRY_COUNT; attempt++) {
        uint8_t rxBuf[RX_BUF_LEN] = {0};
        uint16_t rxLen = RX_BUF_LEN;
        bool ok = RFBridgePort_ControlTransfer(frame,
                                               static_cast<uint16_t>(totalNoChecksum + 1u),
                                               rxBuf,
                                               &rxLen);
        if (!ok) {
            APP_ERR("[RF_TRANSPORT] ctrl cmd=0x%02X txn=%u attempt=%u transfer failed",
                    (unsigned int)cmd,
                    (unsigned int)txn,
                    (unsigned int)(attempt + 1u));
            state = RFTransportState::Error;
            continue;
        }

        if (!parseEventFrame(rxBuf, rxLen)) {
            APP_ERR("[RF_TRANSPORT] ctrl cmd=0x%02X txn=%u attempt=%u parse failed rxLen:%u",
                    (unsigned int)cmd,
                    (unsigned int)txn,
                    (unsigned int)(attempt + 1u),
                    (unsigned int)rxLen);
            state = RFTransportState::Error;
            continue;
        }

        if (!lastEventMatches(cmd, txn)) {
            APP_ERR("[RF_TRANSPORT] ctrl cmd=0x%02X txn=%u attempt=%u stale evt:0x%02X tag:0x%02X evt_txn:%u",
                    (unsigned int)cmd,
                    (unsigned int)txn,
                    (unsigned int)(attempt + 1u),
                    (unsigned int)status.lastEvent,
                    (unsigned int)status.lastCommandTag,
                    (unsigned int)status.lastTransactionId);
            state = RFTransportState::Error;
            continue;
        }

        if (status.lastEvent == EVT_ERROR) {
            APP_ERR("[RF_TRANSPORT] ctrl cmd=0x%02X txn=%u error result:%u reason:0x%02X",
                    (unsigned int)cmd,
                    (unsigned int)txn,
                    (unsigned int)status.lastResult,
                    (unsigned int)status.lastErrorReason);
            state = RFTransportState::Error;
            return false;
        }

        if (status.lastResult != 0u) {
            APP_ERR("[RF_TRANSPORT] ctrl cmd=0x%02X txn=%u nonzero result:%u",
                    (unsigned int)cmd,
                    (unsigned int)txn,
                    (unsigned int)status.lastResult);
            state = RFTransportState::Error;
            return false;
        }

        APP_DBG("[RF_TRANSPORT] ctrl cmd=0x%02X txn=%u ok evt:0x%02X state:%u rate:%u",
                (unsigned int)cmd,
                (unsigned int)txn,
                (unsigned int)status.lastEvent,
                (unsigned int)status.state,
                (unsigned int)status.rateHz);
        state = RFTransportState::Connected;
        return true;
    }

    state = RFTransportState::Error;
    return false;
}

bool RFTransport::sendInputFrame(const uint8_t* payload, uint8_t len) {
    if (len > 24u) {
        state = RFTransportState::Error;
        return false;
    }

    uint8_t frame[4u + 24u + 1u] = {0};
    frame[0] = RF_SYNC;
    frame[1] = CMD_INPUT_DATA;
    frame[2] = len;
    if (len > 0u && payload != nullptr) {
        memcpy(&frame[3], payload, len);
    }

    uint8_t checksum = 0u;
    const uint8_t totalNoChecksum = static_cast<uint8_t>(3u + len);
    for (uint8_t i = 0; i < totalNoChecksum; i++) {
        checksum = static_cast<uint8_t>(checksum + frame[i]);
    }
    frame[totalNoChecksum] = checksum;

    const bool ok = RFBridgePort_SendInputLatest(frame,
                                                 static_cast<uint16_t>(totalNoChecksum + 1u));
    state = ok ? RFTransportState::Connected : RFTransportState::Error;
    return ok;
}

uint8_t RFTransport::inputCrc8(const uint8_t* data, uint8_t len) {
    uint8_t crc = 0u;
    for (uint8_t i = 0u; i < len; i++) {
        crc = static_cast<uint8_t>(crc ^ data[i]);
        for (uint8_t bit = 0u; bit < 8u; bit++) {
            if ((crc & 0x80u) != 0u) {
                crc = static_cast<uint8_t>((crc << 1) ^ 0x07u);
            } else {
                crc = static_cast<uint8_t>(crc << 1);
            }
        }
    }
    return crc;
}

uint32_t RFTransport::buildHitboxKeyMask(const GamepadState& gamepad) {
    uint32_t mask = 0u;

    mask |= ((gamepad.dpad & GAMEPAD_MASK_UP) != 0u) ? (1UL << 0) : 0u;
    mask |= ((gamepad.dpad & GAMEPAD_MASK_DOWN) != 0u) ? (1UL << 1) : 0u;
    mask |= ((gamepad.dpad & GAMEPAD_MASK_LEFT) != 0u) ? (1UL << 2) : 0u;
    mask |= ((gamepad.dpad & GAMEPAD_MASK_RIGHT) != 0u) ? (1UL << 3) : 0u;
    mask |= ((gamepad.buttons & GAMEPAD_MASK_B1) != 0u) ? (1UL << 4) : 0u;
    mask |= ((gamepad.buttons & GAMEPAD_MASK_B2) != 0u) ? (1UL << 5) : 0u;
    mask |= ((gamepad.buttons & GAMEPAD_MASK_B3) != 0u) ? (1UL << 6) : 0u;
    mask |= ((gamepad.buttons & GAMEPAD_MASK_B4) != 0u) ? (1UL << 7) : 0u;
    mask |= ((gamepad.buttons & GAMEPAD_MASK_L1) != 0u) ? (1UL << 8) : 0u;
    mask |= ((gamepad.buttons & GAMEPAD_MASK_R1) != 0u) ? (1UL << 9) : 0u;
    mask |= ((gamepad.buttons & GAMEPAD_MASK_L2) != 0u) ? (1UL << 10) : 0u;
    mask |= ((gamepad.buttons & GAMEPAD_MASK_R2) != 0u) ? (1UL << 11) : 0u;
    mask |= ((gamepad.buttons & GAMEPAD_MASK_S1) != 0u) ? (1UL << 12) : 0u;
    mask |= ((gamepad.buttons & GAMEPAD_MASK_S2) != 0u) ? (1UL << 13) : 0u;
    mask |= ((gamepad.buttons & GAMEPAD_MASK_L3) != 0u) ? (1UL << 14) : 0u;
    mask |= ((gamepad.buttons & GAMEPAD_MASK_R3) != 0u) ? (1UL << 15) : 0u;
    mask |= ((gamepad.buttons & GAMEPAD_MASK_A1) != 0u) ? (1UL << 16) : 0u;
    mask |= ((gamepad.buttons & GAMEPAD_MASK_A2) != 0u) ? (1UL << 17) : 0u;

    return mask;
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

uint8_t RFTransport::serviceEvents(uint8_t drainLimit) {
    if ((drainLimit == 0u) || !RFBridgePort_IsReady()) {
        return 0u;
    }

    uint8_t drained = 0u;
    while (drained < drainLimit) {
        if (!RFBridgePort_HasPendingEvent()) {
            break;
        }

        uint8_t rxBuf[RX_BUF_LEN] = {0};
        uint16_t rxLen = RX_BUF_LEN;
        if (!RFBridgePort_ReadEvent(rxBuf, &rxLen)) {
            state = RFTransportState::Error;
            break;
        }
        if (rxLen == 0u) {
            break;
        }
        if (!parseEventFrame(rxBuf, rxLen)) {
            state = RFTransportState::Error;
            break;
        }
        drained++;
    }
    return drained;
}

bool RFTransport::sendInput(const GamepadState& gamepad, uint32_t seq) {
    uint8_t payload[INPUT_PAYLOAD_LEN] = {0};
    const uint32_t keyMask = buildHitboxKeyMask(gamepad);

    payload[0] = static_cast<uint8_t>(seq & 0xFFu);
    payload[1] = INPUT_FLAGS;
    payload[2] = static_cast<uint8_t>(keyMask & 0xFFu);
    payload[3] = static_cast<uint8_t>((keyMask >> 8) & 0xFFu);
    payload[4] = static_cast<uint8_t>((keyMask >> 16) & 0xFFu);
    payload[5] = static_cast<uint8_t>((keyMask >> 24) & 0xFFu);
    payload[INPUT_CRC_OFFSET] = inputCrc8(payload, INPUT_CRC_OFFSET);

    bool ok = transferCommand(CMD_INPUT_DATA, payload, sizeof(payload), false);
    MonitorTelemetry_OnRfTransfer(seq, CMD_INPUT_DATA, sizeof(payload), ok);
    return ok;
}
