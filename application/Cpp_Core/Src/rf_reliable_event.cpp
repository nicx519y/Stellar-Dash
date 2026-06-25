#include "rf_reliable_event.hpp"

#include <stdio.h>
#include <string.h>

#include "board_cfg.h"
#include "rf_bridge_port.hpp"
#include "stm32h7xx_hal.h"

namespace {
static constexpr uint8_t RF_SYNC = 0xA5u;
static constexpr uint8_t CMD_EVENT_ACK = 0x07u;
static constexpr uint8_t EVT_STATE_CHANGED = 0x82u;
static constexpr uint8_t STATUS_EVENT_SEQ_OFFSET = 20u;
static constexpr uint8_t MAX_PAYLOAD_LEN = 24u;
static constexpr uint32_t T1_MS = 50u;
static constexpr uint32_t ACK_DELAY_MS = T1_MS / 2u;
static constexpr uint32_t ACK_RETRY_DELAY_MS = 5u;

#ifndef RF_RELIABLE_EVENT_LOG
#define RF_RELIABLE_EVENT_LOG APPLICATION_SERIAL_PRINT
#endif

#if RF_RELIABLE_EVENT_LOG
#define RF_REL_EVT_LOG(fmt, ...) printf("[RF_REL_EVT] " fmt "\r\n", ##__VA_ARGS__)
#else
#define RF_REL_EVT_LOG(fmt, ...) ((void)0)
#endif

static uint8_t checksum8(const uint8_t* data, uint16_t len) {
    uint8_t sum = 0u;
    for (uint16_t i = 0u; i < len; ++i) {
        sum = static_cast<uint8_t>(sum + data[i]);
    }
    return sum;
}

static bool sendAck(uint8_t seq) {
    uint8_t frame[5] = {
        RF_SYNC,
        CMD_EVENT_ACK,
        1u,
        seq,
        0u,
    };

    frame[4] = checksum8(frame, 4u);
    return RFBridgePort_SendNoResponse(frame, static_cast<uint16_t>(sizeof(frame)));
}

static bool due(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
}

struct PendingEvent {
    bool active = false;
    bool ackPending = false;
    bool completeAfterAck = false;
    uint8_t evt = 0u;
    uint8_t seq = 0u;
    uint8_t payload[MAX_PAYLOAD_LEN] = {0};
    uint8_t payloadLen = 0u;
    uint32_t ackDueMs = 0u;
};

struct CompletedEvent {
    bool available = false;
    uint8_t evt = 0u;
    uint8_t seq = 0u;
    uint8_t payload[MAX_PAYLOAD_LEN] = {0};
    uint8_t payloadLen = 0u;
};

PendingEvent g_pending;
CompletedEvent g_completed;
bool g_haveLastCompleted = false;
uint8_t g_lastCompletedEvt = 0u;
uint8_t g_lastCompletedSeq = 0u;

static void storePending(uint8_t evt, const uint8_t* payload, uint8_t payloadLen) {
    const uint8_t seq = payload[STATUS_EVENT_SEQ_OFFSET];
    const uint32_t now = HAL_GetTick();
    const bool duplicate = g_pending.active &&
                           (g_pending.evt == evt) &&
                           (g_pending.seq == seq);
    const bool keepPendingAckDue = duplicate && g_pending.ackPending;
    const uint32_t pendingAckDueMs = g_pending.ackDueMs;
    const bool alreadyCompleted =
            (g_completed.available && (g_completed.evt == evt) && (g_completed.seq == seq)) ||
            (g_haveLastCompleted && (g_lastCompletedEvt == evt) && (g_lastCompletedSeq == seq));

    g_pending.active = true;
    g_pending.ackPending = true;
    g_pending.completeAfterAck = !alreadyCompleted;
    g_pending.evt = evt;
    g_pending.seq = seq;
    g_pending.payloadLen = payloadLen;
    memcpy(g_pending.payload, payload, payloadLen);
    g_pending.ackDueMs = keepPendingAckDue ? pendingAckDueMs : (now + ACK_DELAY_MS);

    RF_REL_EVT_LOG("%s evt=0x%02X seq=%u state=%u ack_due_in_ms=%ld complete_after_ack=%u",
            duplicate ? "RECV_DUP" : "RECV_EVT",
            (unsigned int)evt,
            (unsigned int)seq,
            (unsigned int)payload[0],
            (long)(g_pending.ackDueMs - now),
            (unsigned int)(g_pending.completeAfterAck ? 1u : 0u));
}

static bool publishPendingComplete() {
    if (!g_pending.completeAfterAck) {
        return true;
    }
    if (g_completed.available) {
        return false;
    }

    g_completed.available = true;
    g_completed.evt = g_pending.evt;
    g_completed.seq = g_pending.seq;
    g_completed.payloadLen = g_pending.payloadLen;
    memcpy(g_completed.payload, g_pending.payload, g_pending.payloadLen);
    g_haveLastCompleted = true;
    g_lastCompletedEvt = g_pending.evt;
    g_lastCompletedSeq = g_pending.seq;
    RF_REL_EVT_LOG("COMPLETE evt=0x%02X seq=%u state=%u",
            (unsigned int)g_pending.evt,
            (unsigned int)g_pending.seq,
            (unsigned int)g_pending.payload[0]);
    return true;
}

static void completePendingAfterAck() {
    g_pending.ackPending = false;
    if (!publishPendingComplete()) {
        return;
    }

    g_pending = PendingEvent{};
}
}

namespace RFReliableEvent {

bool completeFrameIfNeeded(const uint8_t* frame, uint16_t frameLen) {
    if ((frame == nullptr) || (frameLen < 4u) || (frame[0] != RF_SYNC)) {
        return true;
    }

    const uint8_t payloadLen = frame[2];
    const uint16_t expectedLen = static_cast<uint16_t>(3u + payloadLen + 1u);
    if ((expectedLen != frameLen) ||
        (checksum8(frame, static_cast<uint16_t>(frameLen - 1u)) != frame[frameLen - 1u])) {
        return true;
    }

    return completeIfNeeded(frame[1], &frame[3], payloadLen);
}

bool completeIfNeeded(uint8_t evt, const uint8_t* payload, uint8_t payloadLen) {
    if ((evt != EVT_STATE_CHANGED) ||
        (payload == nullptr) ||
        (payloadLen <= STATUS_EVENT_SEQ_OFFSET)) {
        return true;
    }

    const uint8_t seq = payload[STATUS_EVENT_SEQ_OFFSET];
    if (seq == 0u) {
        return true;
    }
    if (payloadLen > MAX_PAYLOAD_LEN) {
        return false;
    }

    storePending(evt, payload, payloadLen);
    poll();
    return true;
}

void poll() {
    if (!g_pending.active) {
        return;
    }

    const uint32_t now = HAL_GetTick();
    if (!g_pending.ackPending && publishPendingComplete()) {
        g_pending = PendingEvent{};
        return;
    }

    if (g_pending.ackPending && due(now, g_pending.ackDueMs)) {
        const bool ackOk = sendAck(g_pending.seq);
        RF_REL_EVT_LOG("SEND_ACK evt=0x%02X seq=%u ok=%u",
                (unsigned int)g_pending.evt,
                (unsigned int)g_pending.seq,
                (unsigned int)ackOk);
        if (ackOk) {
            completePendingAfterAck();
        } else {
            RF_REL_EVT_LOG("ACK_FAIL evt=0x%02X seq=%u",
                    (unsigned int)g_pending.evt,
                    (unsigned int)g_pending.seq);
            g_pending.ackDueMs = now + ACK_RETRY_DELAY_MS;
        }
        return;
    }
}

bool popCompleted(uint8_t* evt, uint8_t* payload, uint8_t* payloadLen, uint8_t payloadCapacity) {
    if (!g_completed.available) {
        return false;
    }
    if ((evt == nullptr) || (payload == nullptr) || (payloadLen == nullptr) ||
        (payloadCapacity < g_completed.payloadLen)) {
        return false;
    }

    *evt = g_completed.evt;
    *payloadLen = g_completed.payloadLen;
    memcpy(payload, g_completed.payload, g_completed.payloadLen);
    g_completed = CompletedEvent{};
    return true;
}

}
