#include "rf_transport.hpp"

#include <stdio.h>
#include <string.h>

#include "board_cfg.h"
#include "micro_timer.hpp"
#include "monitor_telemetry.hpp"
#include "power_manager.hpp"
#include "rf_command_transaction.hpp"
#include "rf_bridge_port.hpp"
#include "rf_reliable_event.hpp"
#include "system_logger.h"
#include "stm32h7xx_hal.h"

namespace {
static constexpr uint8_t RF_SYNC = 0xA5u;
static constexpr uint8_t CMD_GET_STATUS = 0x01u;
static constexpr uint8_t CMD_START_PAIR = 0x02u;
static constexpr uint8_t CMD_STOP_PAIR = 0x03u;
static constexpr uint8_t CMD_UNBIND = 0x04u;
static constexpr uint8_t CMD_SET_RATE = 0x05u;
static constexpr uint8_t CMD_INPUT_DATA = 0x06u;
static constexpr uint8_t CMD_SLEEP = 0x08u;
static constexpr uint8_t EVT_STATUS = 0x81u;
static constexpr uint8_t EVT_STATE_CHANGED = 0x82u;
static constexpr uint8_t EVT_RATE_APPLIED = 0x83u;
static constexpr uint8_t EVT_LINK_WARN = 0x84u;
static constexpr uint8_t EVT_ERROR = 0x85u;
static constexpr uint8_t EVT_MONITOR_CONFIG = 0x86u;
static constexpr uint8_t EVT_TIME_SYNC = 0x87u;
static constexpr uint8_t EVT_WAKEUP_COMPLETE = 0x88u;
static constexpr uint8_t EVT_SLEEP_ENTERING = 0x89u;
static constexpr uint8_t INPUT_PAYLOAD_LEN = 10u;
static constexpr uint8_t INPUT_FORMAT_VERSION = 1u;
static constexpr uint8_t INPUT_FLAG_PROCESSED = 0x01u;
static constexpr uint8_t INPUT_FLAG_SYNC_ECHO = 0x02u;
static constexpr uint8_t INPUT_FLAG_BATTERY_CODE = 0x04u;
static constexpr uint8_t INPUT_FLAG_BATTERY_H2 = 0x08u;
static constexpr uint8_t INPUT_BASE_FLAGS = static_cast<uint8_t>((INPUT_FORMAT_VERSION << 4) | INPUT_FLAG_PROCESSED);
static constexpr uint8_t INPUT_AGE_US_OFFSET = 6u;
static constexpr uint8_t INPUT_BATTERY_CODE_OFFSET = 8u;
static constexpr uint8_t INPUT_CRC_OFFSET = 9u;
static constexpr uint16_t INPUT_BATTERY_BASE_MV = 3000u;
static constexpr uint16_t INPUT_BATTERY_STEP_MV = 10u;
static constexpr uint8_t INPUT_BATTERY_MAX_CODE = 0x7Fu;
static constexpr uint8_t STATUS_PAYLOAD_LEN = 23u;
static constexpr uint8_t STATUS_CMD_TAG_OFFSET = 16u;
static constexpr uint8_t STATUS_TXN_OFFSET = 17u;
static constexpr uint8_t STATUS_RESULT_OFFSET = 18u;
static constexpr uint8_t STATUS_REASON_OFFSET = 19u;
static constexpr uint8_t STATUS_EVENT_SEQ_OFFSET = 20u;
static constexpr uint16_t RX_BUF_LEN = 32u;
static constexpr uint32_t COMMAND_RESULT_TIMEOUT_MS = 200u;
#ifndef RF_SPI_PROTOCOL_LOG
#define RF_SPI_PROTOCOL_LOG 0
#endif

#if RF_SPI_PROTOCOL_LOG
#define RF_SPI_LOG(fmt, ...) printf(fmt "\r\n", ##__VA_ARGS__)
#else
#define RF_SPI_LOG(fmt, ...) ((void)0)
#endif

struct PendingTimeSyncEcho {
    bool pending = false;
    uint8_t seq = 0u;
    uint32_t rxTickUs = 0u;
};

PendingTimeSyncEcho g_pendingTimeSyncEcho;

bool g_haveLastInputKeyMask = false;
uint32_t g_lastInputKeyMask = 0u;

static uint8_t frameChecksum(const uint8_t* buf, uint16_t len) {
    uint8_t s = 0u;
    for (uint16_t i = 0; i < len; i++) {
        s = static_cast<uint8_t>(s + buf[i]);
    }
    return s;
}

static bool isScheduledControlCommand(uint8_t cmd) {
    return (cmd == CMD_START_PAIR) ||
           (cmd == CMD_STOP_PAIR) ||
           (cmd == CMD_UNBIND) ||
           (cmd == CMD_SET_RATE) ||
           (cmd == CMD_SLEEP);
}

static void putU16(uint8_t* dst, uint16_t value) {
    dst[0] = static_cast<uint8_t>(value & 0xFFu);
    dst[1] = static_cast<uint8_t>(value >> 8);
}

static uint16_t saturateAgeUs(uint32_t value) {
    return value > 0xFFFFu ? 0xFFFFu : static_cast<uint16_t>(value);
}

static uint8_t encodeBatteryMv(uint32_t mv) {
    if (mv < INPUT_BATTERY_BASE_MV) {
        return 1u;
    }

    const uint32_t code = ((mv - INPUT_BATTERY_BASE_MV) + (INPUT_BATTERY_STEP_MV / 2u)) /
                          INPUT_BATTERY_STEP_MV + 1u;
    return code > INPUT_BATTERY_MAX_CODE ? INPUT_BATTERY_MAX_CODE : static_cast<uint8_t>(code);
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

bool RFTransport::parseEventFrame(const uint8_t* frame, uint16_t len, bool* applied) {
    if (applied != nullptr) {
        *applied = false;
    }
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

    if (evt == EVT_MONITOR_CONFIG) {
        if (payloadLen < 4u) {
            return false;
        }
        status.lastEvent = evt;
        status.eventCounter++;
        const uint8_t seq = frame[3];
        const uint8_t result = frame[6];
        status.lastCommandTag = EVT_MONITOR_CONFIG;
        status.lastTransactionId = seq;
        status.lastResult = result;
        status.lastErrorReason = 0u;
        state = RFTransportState::Connected;
        if (applied != nullptr) {
            *applied = true;
        }
        return true;
    }

    if (evt == EVT_TIME_SYNC) {
        if (payloadLen < 1u) {
            return false;
        }
        status.lastEvent = evt;
        status.eventCounter++;
        g_pendingTimeSyncEcho.pending = true;
        g_pendingTimeSyncEcho.seq = frame[3];
        g_pendingTimeSyncEcho.rxTickUs = MICROS_TIMER.micros();
        status.lastCommandTag = EVT_TIME_SYNC;
        status.lastTransactionId = frame[3];
        status.lastResult = 0u;
        status.lastErrorReason = 0u;
        state = RFTransportState::Connected;
        if (applied != nullptr) {
            *applied = true;
        }
        return true;
    }

    if ((evt == EVT_STATE_CHANGED) &&
        (payloadLen > STATUS_EVENT_SEQ_OFFSET) &&
        (frame[3u + STATUS_EVENT_SEQ_OFFSET] != 0u)) {
        return RFReliableEvent::completeIfNeeded(evt, &frame[3], payloadLen);
    }

    bool statusOk = false;
    if (payloadLen >= 17u) {
        statusOk = parseStatusPayload(&frame[3], payloadLen);
        if (!statusOk) {
            return false;
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

    status.lastEvent = evt;
    status.eventCounter++;
    if (applied != nullptr) {
        *applied = true;
    }
    return true;
}

void RFTransport::processCompletedReliableEvents() {
    uint8_t evt = 0u;
    uint8_t payload[STATUS_PAYLOAD_LEN] = {0};
    uint8_t payloadLen = 0u;

    while (RFReliableEvent::popCompleted(&evt, payload, &payloadLen, sizeof(payload))) {
        bool statusOk = false;
        if (payloadLen >= 17u) {
            statusOk = parseStatusPayload(payload, payloadLen);
            if (!statusOk) {
                state = RFTransportState::Error;
                return;
            }
        }

        status.lastEvent = evt;
        status.eventCounter++;
        if (evt == EVT_ERROR) {
            status.errorCounter++;
            status.lastErrorCommand = statusOk ? status.lastCommandTag :
                                      ((payloadLen >= 1u) ? payload[0] : 0u);
            state = RFTransportState::Error;
        } else {
            if (statusOk) {
                status.lastErrorCommand = 0u;
                status.lastErrorReason = 0u;
            }
            state = RFTransportState::Connected;
        }

        RF_SPI_LOG("[RF_SPI][REL_EVT_APPLY] evt=0x%02X state=%u counter=%lu",
                (unsigned int)evt,
                (unsigned int)((payloadLen > 0u) ? payload[0] : 0u),
                (unsigned long)status.eventCounter);
    }
}

bool RFTransport::lastEventMatches(uint8_t cmd, uint8_t txn) const {
    return (status.lastCommandTag == cmd) &&
           (status.lastTransactionId == txn);
}

bool RFTransport::waitCommandResult(uint8_t cmd, uint8_t txn, uint32_t timeoutMs) {
    const uint32_t start = HAL_GetTick();

    RF_SPI_LOG("[RF_SPI][RESULT_WAIT] cmd=0x%02X txn=%u timeout_ms=%lu",
            (unsigned int)cmd,
            (unsigned int)txn,
            (unsigned long)timeoutMs);
    while ((HAL_GetTick() - start) < timeoutMs) {
        RFReliableEvent::poll();
        processCompletedReliableEvents();
        if (!RFBridgePort_HasPendingEvent()) {
            continue;
        }

        uint8_t rxBuf[RX_BUF_LEN] = {0};
        uint16_t rxLen = RX_BUF_LEN;
        if (!RFBridgePort_ReadEvent(rxBuf, &rxLen)) {
            state = RFTransportState::Error;
            return false;
        }
        if (rxLen == 0u) {
            continue;
        }
        bool applied = false;
        if (!parseEventFrame(rxBuf, rxLen, &applied)) {
            state = RFTransportState::Error;
            return false;
        }
        RFReliableEvent::poll();
        processCompletedReliableEvents();
        if (!applied) {
            continue;
        }

        RF_SPI_LOG("[RF_SPI][RESULT_RECV] cmd=0x%02X txn=%u evt=0x%02X result=%u reason=%u",
                (unsigned int)cmd,
                (unsigned int)txn,
                (unsigned int)status.lastEvent,
                (unsigned int)status.lastResult,
                (unsigned int)status.lastErrorReason);
        if (!lastEventMatches(cmd, txn)) {
            continue;
        }
        if ((status.lastEvent == EVT_ERROR) || (status.lastResult != 0u)) {
            state = RFTransportState::Error;
            return false;
        }
        state = RFTransportState::Connected;
        return true;
    }

    RF_SPI_LOG("[RF_SPI][RESULT_TIMEOUT] cmd=0x%02X txn=%u",
            (unsigned int)cmd,
            (unsigned int)txn);
    state = RFTransportState::Error;
    return false;
}

bool RFTransport::waitWakeupComplete(uint32_t timeoutMs) {
    const uint32_t start = HAL_GetTick();

    RF_SPI_LOG("[RF_SPI][WAKE_WAIT] timeout_ms=%lu", (unsigned long)timeoutMs);
    while ((HAL_GetTick() - start) < timeoutMs) {
        RFReliableEvent::poll();
        processCompletedReliableEvents();

        if (!RFBridgePort_HasPendingEvent()) {
            HAL_Delay(1u);
            continue;
        }

        uint8_t rxBuf[RX_BUF_LEN] = {0};
        uint16_t rxLen = RX_BUF_LEN;
        if (!RFBridgePort_ReadEvent(rxBuf, &rxLen)) {
            HAL_Delay(1u);
            continue;
        }
        if (rxLen == 0u) {
            HAL_Delay(1u);
            continue;
        }

        bool applied = false;
        if (!parseEventFrame(rxBuf, rxLen, &applied)) {
            HAL_Delay(1u);
            continue;
        }
        if (!applied) {
            continue;
        }

        RF_SPI_LOG("[RF_SPI][WAKE_EVT] evt=0x%02X result=%u",
                (unsigned int)status.lastEvent,
                (unsigned int)status.lastResult);
        if (status.lastEvent == EVT_WAKEUP_COMPLETE && status.lastResult == 0u) {
            state = RFTransportState::Connected;
            return true;
        }
        if (status.lastEvent == EVT_ERROR) {
            state = RFTransportState::Error;
            return false;
        }
    }

    state = RFTransportState::Error;
    return false;
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

    const uint16_t logRateHz =
        ((cmd == CMD_SET_RATE) && (len == 2u) && (payload != nullptr)) ?
        static_cast<uint16_t>(payload[0] | (payload[1] << 8)) :
        0u;

    RFCommandTransactionResult txnResult = {};
    RF_SPI_LOG("[RF_SPI][TX_CMD] cmd=0x%02X payload_len=%u rate=%u",
            (unsigned int)cmd,
            (unsigned int)len,
            (unsigned int)logRateHz);
    if (isScheduledControlCommand(cmd)) {
        if (!RFCommandTransaction::sendScheduled(cmd, payload, len, txnResult)) {
            state = RFTransportState::Error;
            return false;
        }
        status.lastCommandTag = cmd;
        status.lastTransactionId = txnResult.txn;
        status.lastResult = 0u;
        status.lastErrorReason = 0u;
        state = RFTransportState::Connected;
        return true;
    }

    if (!RFCommandTransaction::send(cmd, payload, len, txnResult)) {
        state = RFTransportState::Error;
        return false;
    }

    bool ackApplied = false;
    if (!parseEventFrame(txnResult.ackFrame, txnResult.ackLen, &ackApplied) ||
        !ackApplied ||
        !lastEventMatches(cmd, txnResult.txn)) {
        state = RFTransportState::Error;
        return false;
    }

    if ((status.lastEvent == EVT_ERROR) || (status.lastResult != 0u)) {
        state = RFTransportState::Error;
        return false;
    }

    RF_SPI_LOG("[RF_SPI][ACK] cmd=0x%02X txn=%u attempt=%u evt=0x%02X result=%u rate=%u",
            (unsigned int)cmd,
            (unsigned int)txnResult.txn,
            (unsigned int)txnResult.attempts,
            (unsigned int)status.lastEvent,
            (unsigned int)status.lastResult,
            (unsigned int)status.rateHz);
    if (cmd == CMD_GET_STATUS) {
        state = RFTransportState::Connected;
        return true;
    }
    return waitCommandResult(cmd, txnResult.txn, COMMAND_RESULT_TIMEOUT_MS);
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

bool RFTransport::sleep() {
    return transferCommand(CMD_SLEEP, nullptr, 0u, true);
}

bool RFTransport::wake() {
    if (!RFBridgePort_WakePulse()) {
        state = RFTransportState::Error;
        return false;
    }
    return waitWakeupComplete(2000u);
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
    RFReliableEvent::poll();
    processCompletedReliableEvents();

    if (drainLimit == 0u) {
        return 0u;
    }
    if (!RFBridgePort_IsReady()) {
        RF_SPI_LOG("[RF_SPI][EVT_SERVICE] not_ready");
        return 0u;
    }

    uint8_t drained = 0u;
    while (drained < drainLimit) {
        RFReliableEvent::poll();
        processCompletedReliableEvents();

        if (!RFBridgePort_HasPendingEvent()) {
            break;
        }

        uint8_t rxBuf[RX_BUF_LEN] = {0};
        uint16_t rxLen = RX_BUF_LEN;
        if (!RFBridgePort_ReadEvent(rxBuf, &rxLen)) {
            RF_SPI_LOG("[RF_SPI][EVT_READ_FAIL] drained=%u", (unsigned int)drained);
            state = RFTransportState::Error;
            break;
        }
        if (rxLen == 0u) {
            break;
        }
        RF_SPI_LOG("[RF_SPI][EVT_READ] evt=0x%02X len=%u",
                (unsigned int)((rxLen >= 2u) ? rxBuf[1] : 0u),
                (unsigned int)rxLen);
        bool applied = false;
        if (!parseEventFrame(rxBuf, rxLen, &applied)) {
            RF_SPI_LOG("[RF_SPI][EVT_PARSE_FAIL] evt=0x%02X len=%u",
                    (unsigned int)((rxLen >= 2u) ? rxBuf[1] : 0u),
                    (unsigned int)rxLen);
            state = RFTransportState::Error;
            break;
        }
        RFReliableEvent::poll();
        processCompletedReliableEvents();
        drained++;
    }
    RFReliableEvent::poll();
    processCompletedReliableEvents();
    return drained;
}

bool RFTransport::sendInput(const GamepadState& gamepad, uint32_t seq) {
    uint8_t payload[INPUT_PAYLOAD_LEN] = {0};
    const uint32_t keyMask = buildHitboxKeyMask(gamepad);
    uint16_t ageUs = 0u;
    uint8_t flags = INPUT_BASE_FLAGS;

    payload[0] = static_cast<uint8_t>(seq & 0xFFu);
    payload[2] = static_cast<uint8_t>(keyMask & 0xFFu);
    payload[3] = static_cast<uint8_t>((keyMask >> 8) & 0xFFu);
    payload[4] = static_cast<uint8_t>((keyMask >> 16) & 0xFFu);
    payload[5] = static_cast<uint8_t>((keyMask >> 24) & 0xFFu);
    if (!g_haveLastInputKeyMask || keyMask != g_lastInputKeyMask) {
        uint32_t readyUs = 0u;
        if (MonitorTelemetry_GetReportReadyUs(seq, &readyUs)) {
            ageUs = saturateAgeUs(MICROS_TIMER.micros() - readyUs);
            if (ageUs == 0u) {
                ageUs = 1u;
            }
        }
        g_haveLastInputKeyMask = true;
        g_lastInputKeyMask = keyMask;
    }
    putU16(&payload[INPUT_AGE_US_OFFSET], ageUs);
    if (POWER_MANAGER.isVoltageValid()) {
        const PowerBatteryVoltages voltages = POWER_MANAGER.getVoltages();
        const PowerBatteryId activeBattery = POWER_MANAGER.getActiveDischargeBattery();
        const uint32_t activeMv = (activeBattery == PowerBatteryId::H2) ? voltages.h2_mv : voltages.h1_mv;
        if (activeMv > 0u) {
            payload[INPUT_BATTERY_CODE_OFFSET] = encodeBatteryMv(activeMv);
            flags = static_cast<uint8_t>(flags | INPUT_FLAG_BATTERY_CODE);
            if (activeBattery == PowerBatteryId::H2) {
                flags = static_cast<uint8_t>(flags | INPUT_FLAG_BATTERY_H2);
            }
        }
    }
    payload[1] = flags;
    payload[INPUT_CRC_OFFSET] = inputCrc8(payload, INPUT_CRC_OFFSET);

    bool ok = transferCommand(CMD_INPUT_DATA, payload, sizeof(payload), false);
    MonitorTelemetry_OnRfTransfer(seq, CMD_INPUT_DATA, sizeof(payload), ok);
    return ok;
}
