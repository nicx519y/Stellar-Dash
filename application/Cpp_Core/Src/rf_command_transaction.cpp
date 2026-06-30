#include "rf_command_transaction.hpp"

#include <stdio.h>
#include <string.h>

#include "board_cfg.h"
#include "rf_bridge_port.hpp"
#include "rf_reliable_event.hpp"
#include "stm32h7xx_hal.h"

namespace {
static constexpr uint8_t RF_SYNC = 0xA5u;
static constexpr uint8_t MAX_ARGS_LEN = 23u;
static constexpr uint8_t MAX_ATTEMPTS = 3u;
static constexpr uint8_t EVT_STATUS = 0x81u;
static constexpr uint8_t EVT_ERROR = 0x85u;
static constexpr uint8_t STATUS_CMD_TAG_OFFSET = 16u;
static constexpr uint8_t STATUS_TXN_OFFSET = 17u;
static constexpr uint32_t SCHEDULED_COMMAND_WINDOW_MS = 100u;
static constexpr uint8_t SCHEDULED_COMMAND_PACKET_COUNT = 20u;

#ifndef RF_COMMAND_TRANSACTION_ACK_TIMEOUT_MS
#define RF_COMMAND_TRANSACTION_ACK_TIMEOUT_MS 20u
#endif

#ifndef RF_COMMAND_TRANSACTION_LOG
#define RF_COMMAND_TRANSACTION_LOG 0
#endif

#if RF_COMMAND_TRANSACTION_LOG
#define RF_CMD_TXN_LOG(fmt, ...) printf("[RF_CMD_TXN] " fmt "\r\n", ##__VA_ARGS__)
#else
#define RF_CMD_TXN_LOG(fmt, ...) ((void)0)
#endif

static uint8_t checksum8(const uint8_t* data, uint16_t len) {
    uint8_t sum = 0u;
    for (uint16_t i = 0u; i < len; ++i) {
        sum = static_cast<uint8_t>(sum + data[i]);
    }
    return sum;
}

static bool buildCommandFrame(uint8_t cmd,
                              uint8_t txn,
                              const uint8_t* args,
                              uint8_t argsLen,
                              uint8_t* frame,
                              uint16_t& frameLen) {
    if ((frame == nullptr) || (argsLen > MAX_ARGS_LEN)) {
        frameLen = 0u;
        return false;
    }

    const uint8_t payloadLen = static_cast<uint8_t>(argsLen + 1u);
    const uint8_t totalNoChecksum = static_cast<uint8_t>(3u + payloadLen);

    frame[0] = RF_SYNC;
    frame[1] = cmd;
    frame[2] = payloadLen;
    frame[3] = txn;
    if ((argsLen != 0u) && (args != nullptr)) {
        memcpy(&frame[4], args, argsLen);
    }
    frame[totalNoChecksum] = checksum8(frame, totalNoChecksum);
    frameLen = static_cast<uint16_t>(totalNoChecksum + 1u);
    return true;
}

static uint16_t remainingMs(uint32_t start, uint32_t now) {
    const uint32_t elapsed = now - start;
    if (elapsed >= SCHEDULED_COMMAND_WINDOW_MS) {
        return 0u;
    }
    return static_cast<uint16_t>(SCHEDULED_COMMAND_WINDOW_MS - elapsed);
}

static bool buildScheduledCommandFrame(uint8_t cmd,
                                       uint8_t seq,
                                       uint16_t completeMs,
                                       const uint8_t* args,
                                       uint8_t argsLen,
                                       uint8_t* frame,
                                       uint16_t& frameLen) {
    if ((frame == nullptr) || (argsLen > MAX_ARGS_LEN)) {
        frameLen = 0u;
        return false;
    }

    const uint8_t payloadLen = static_cast<uint8_t>(argsLen + 3u);
    const uint8_t totalNoChecksum = static_cast<uint8_t>(3u + payloadLen);

    frame[0] = RF_SYNC;
    frame[1] = cmd;
    frame[2] = payloadLen;
    frame[3] = seq;
    frame[4] = static_cast<uint8_t>(completeMs & 0xFFu);
    frame[5] = static_cast<uint8_t>((completeMs >> 8) & 0xFFu);
    if ((argsLen != 0u) && (args != nullptr)) {
        memcpy(&frame[6], args, argsLen);
    }
    frame[totalNoChecksum] = checksum8(frame, totalNoChecksum);
    frameLen = static_cast<uint16_t>(totalNoChecksum + 1u);
    return true;
}

static bool ackMatches(uint8_t cmd, uint8_t txn, const uint8_t* frame, uint16_t frameLen) {
    if ((frame == nullptr) || (frameLen < 4u) || (frame[0] != RF_SYNC)) {
        return false;
    }

    const uint8_t payloadLen = frame[2];
    const uint16_t expectedLen = static_cast<uint16_t>(3u + payloadLen + 1u);
    if ((expectedLen != frameLen) ||
        (checksum8(frame, static_cast<uint16_t>(frameLen - 1u)) != frame[frameLen - 1u])) {
        return false;
    }

    if (payloadLen <= STATUS_TXN_OFFSET) {
        return false;
    }
    if ((frame[1] != EVT_STATUS) && (frame[1] != EVT_ERROR)) {
        return false;
    }
    const uint8_t* payload = &frame[3];
    return (payload[STATUS_CMD_TAG_OFFSET] == cmd) &&
           (payload[STATUS_TXN_OFFSET] == txn);
}

static uint8_t ackEvent(const uint8_t* frame, uint16_t frameLen) {
    return ((frame != nullptr) && (frameLen >= 2u)) ? frame[1] : 0u;
}
}

uint8_t RFCommandTransaction::nextTransactionId() {
    static uint8_t s_nextTxn = 0u;

    s_nextTxn++;
    if (s_nextTxn == 0u) {
        s_nextTxn = 1u;
    }
    return s_nextTxn;
}

bool RFCommandTransaction::send(uint8_t cmd,
                                const uint8_t* args,
                                uint8_t argsLen,
                                RFCommandTransactionResult& result) {
    result = {};
    if (argsLen > MAX_ARGS_LEN) {
        return false;
    }

    uint8_t frame[4u + MAX_ARGS_LEN + 1u] = {0};
    uint16_t frameLen = 0u;
    const uint8_t txn = nextTransactionId();
    if (!buildCommandFrame(cmd, txn, args, argsLen, frame, frameLen)) {
        return false;
    }

    result.txn = txn;
    for (uint8_t attempt = 1u; attempt <= MAX_ATTEMPTS; ++attempt) {
        uint8_t ack[sizeof(result.ackFrame)] = {0};
        uint16_t ackLen = static_cast<uint16_t>(sizeof(ack));
        result.attempts = attempt;

        RF_CMD_TXN_LOG("SEND_CMD cmd=0x%02X txn=%u attempt=%u args_len=%u t1_ms=%u",
                       (unsigned int)cmd,
                       (unsigned int)txn,
                       (unsigned int)attempt,
                       (unsigned int)argsLen,
                       (unsigned int)RF_COMMAND_TRANSACTION_ACK_TIMEOUT_MS);
        if (!RFBridgePort_ControlTransferWithTimeout(frame,
                                                     frameLen,
                                                     ack,
                                                     &ackLen,
                                                     RF_COMMAND_TRANSACTION_ACK_TIMEOUT_MS)) {
            RF_CMD_TXN_LOG("ACK_TIMEOUT cmd=0x%02X txn=%u attempt=%u",
                           (unsigned int)cmd,
                           (unsigned int)txn,
                           (unsigned int)attempt);
            continue;
        }
        RF_CMD_TXN_LOG("RECV_ACK cmd=0x%02X txn=%u attempt=%u evt=0x%02X len=%u",
                       (unsigned int)cmd,
                       (unsigned int)txn,
                       (unsigned int)attempt,
                       (unsigned int)ackEvent(ack, ackLen),
                       (unsigned int)ackLen);
        if (!ackMatches(cmd, txn, ack, ackLen)) {
            (void)RFReliableEvent::completeFrameIfNeeded(ack, ackLen);
            RFReliableEvent::poll();
            RF_CMD_TXN_LOG("ACK_MISMATCH cmd=0x%02X txn=%u attempt=%u evt=0x%02X len=%u",
                           (unsigned int)cmd,
                           (unsigned int)txn,
                           (unsigned int)attempt,
                           (unsigned int)ackEvent(ack, ackLen),
                           (unsigned int)ackLen);
            continue;
        }

        memcpy(result.ackFrame, ack, ackLen);
        result.ackLen = ackLen;
        RF_CMD_TXN_LOG("COMPLETE cmd=0x%02X txn=%u attempts=%u",
                       (unsigned int)cmd,
                       (unsigned int)txn,
                       (unsigned int)attempt);
        return true;
    }

    RF_CMD_TXN_LOG("FAILED cmd=0x%02X txn=%u attempts=%u",
                   (unsigned int)cmd,
                   (unsigned int)txn,
                   (unsigned int)MAX_ATTEMPTS);
    return false;
}

bool RFCommandTransaction::sendScheduled(uint8_t cmd,
                                         const uint8_t* args,
                                         uint8_t argsLen,
                                         RFCommandTransactionResult& result) {
    result = {};
    if (argsLen > MAX_ARGS_LEN) {
        return false;
    }

    const uint8_t seq = nextTransactionId();
    const uint32_t start = HAL_GetTick();
    const uint32_t packetIntervalMs =
            SCHEDULED_COMMAND_WINDOW_MS / SCHEDULED_COMMAND_PACKET_COUNT;
    bool anySent = false;

    result.txn = seq;
    for (uint8_t i = 0u; i < SCHEDULED_COMMAND_PACKET_COUNT; ++i) {
        uint8_t frame[4u + MAX_ARGS_LEN + 3u + 1u] = {0};
        uint16_t frameLen = 0u;
        const uint32_t now = HAL_GetTick();
        const uint16_t completeMs = remainingMs(start, now);

        if (!buildScheduledCommandFrame(cmd, seq, completeMs, args, argsLen, frame, frameLen)) {
            return false;
        }

        RF_CMD_TXN_LOG("SEND_SCHED cmd=0x%02X seq=%u pkt=%u complete_ms=%u",
                       (unsigned int)cmd,
                       (unsigned int)seq,
                       (unsigned int)(i + 1u),
                       (unsigned int)completeMs);
        if (RFBridgePort_SendNoResponse(frame, frameLen)) {
            anySent = true;
            result.attempts++;
        }

        const uint32_t nextDue = start + (packetIntervalMs * static_cast<uint32_t>(i + 1u));
        while (static_cast<int32_t>(HAL_GetTick() - nextDue) < 0) {
            HAL_Delay(1u);
        }
    }

    while (static_cast<int32_t>(HAL_GetTick() - (start + SCHEDULED_COMMAND_WINDOW_MS)) < 0) {
        HAL_Delay(1u);
    }

    if (!anySent) {
        RF_CMD_TXN_LOG("SCHED_FAILED cmd=0x%02X seq=%u", (unsigned int)cmd, (unsigned int)seq);
        return false;
    }

    RF_CMD_TXN_LOG("SCHED_COMPLETE cmd=0x%02X seq=%u sent=%u",
                   (unsigned int)cmd,
                   (unsigned int)seq,
                   (unsigned int)result.attempts);
    return true;
}
