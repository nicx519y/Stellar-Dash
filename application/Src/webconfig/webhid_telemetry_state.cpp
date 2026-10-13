#include "webhid_telemetry_state.hpp"
#include "device_security_crypto.h"
#include <algorithm>

void WebHidTelemetryState::reset()
{
    clearButtons();
    buttonStateSequence = 0u;
    buttonStateDropped = 0u;
    performanceEnabled = false;
    samplePending = false;
    sampleTimestampUs = 0u;
    nextSampleAtMs = 0u;
    nextCheckpointAtMs = 0u;
    edgeSequence = 0u;
    droppedSamples = 0u;
    totalDroppedSamples = 0u;
    HBoxCrypto_Zeroize(edgeQueue.data(), sizeof(edgeQueue));
    edgeHead = 0u;
    edgeTail = 0u;
    edgeCount = 0u;
    telemetryOverflow = false;
    checkpointRequested = false;
    checkpointActive = false;
    checkpointChunk = 0u;
    HBoxCrypto_Zeroize(
        checkpointKeys.data(), sizeof(checkpointKeys));
}
void WebHidTelemetryState::start(uint32_t now)
{
    performanceEnabled = true;
    nextSampleAtMs = now;
    nextCheckpointAtMs = now;
    samplePending = false;
    droppedSamples = 0u;
    totalDroppedSamples = 0u;
    edgeHead = 0u;
    edgeTail = 0u;
    edgeCount = 0u;
    telemetryOverflow = false;
    HBoxCrypto_Zeroize(
        edgeQueue.data(), sizeof(edgeQueue));
    requestCheckpoint();
}
void WebHidTelemetryState::stop()
{
    performanceEnabled = false;
    samplePending = false;
    checkpointRequested = false;
    checkpointActive = false;
    edgeHead = 0u;
    edgeTail = 0u;
    edgeCount = 0u;
    HBoxCrypto_Zeroize(
        edgeQueue.data(), sizeof(edgeQueue));
}
void WebHidTelemetryState::clearButtons()
{
    HBoxCrypto_Zeroize(
        buttonStateQueue.data(), sizeof(buttonStateQueue));
    buttonStateHead = 0u;
    buttonStateTail = 0u;
    buttonStateCount = 0u;
}
void WebHidTelemetryState::enqueueButton(uint32_t mask, uint8_t totalButtons, bool active, bool pinned)
{
    ++buttonStateSequence;
    if (buttonStateSequence == 0u) {
        ++buttonStateSequence;
    }

    /* A partially transferred report owns the queue head.  Under the
     * extremely unlikely condition that all 32 snapshots fill while that
     * report is pinned, replace only the newest queued snapshot.  This keeps
     * completion bookkeeping exact while ensuring the final visible mask is
     * still the most recent one. */
    bool replaceNewest = false;
    if (buttonStateCount >= kButtonStateQueueDepth) {
        if (buttonStateDropped != UINT16_MAX) {
            ++buttonStateDropped;
        }
        if (pinned) {
            replaceNewest = true;
        } else {
            HBoxCrypto_Zeroize(
                &buttonStateQueue[buttonStateHead],
                sizeof(buttonStateQueue[buttonStateHead]));
            buttonStateHead =
                (buttonStateHead + 1u) % kButtonStateQueueDepth;
            --buttonStateCount;
        }
    }

    const size_t destination = replaceNewest
        ? (buttonStateTail + kButtonStateQueueDepth - 1u) %
              kButtonStateQueueDepth
        : buttonStateTail;
    webhid_button_state_v1_t &event =
        buttonStateQueue[destination];
    event = {};
    event.event_sequence_le = buttonStateSequence;
    event.trigger_mask_le = mask;
    event.total_dropped_snapshots_le = buttonStateDropped;
    event.total_buttons = totalButtons;
    event.flags = active
        ? WEBHID_BUTTON_STATE_FLAG_ACTIVE
        : 0u;
    if (!replaceNewest) {
        buttonStateTail =
            (buttonStateTail + 1u) % kButtonStateQueueDepth;
        ++buttonStateCount;
    }
}
const webhid_button_state_v1_t *WebHidTelemetryState::frontButton() const
{ return buttonStateCount == 0u ? nullptr : &buttonStateQueue[buttonStateHead]; }
void WebHidTelemetryState::commitButton()
{
    if (!buttonStateCount) return;
    HBoxCrypto_Zeroize(&buttonStateQueue[buttonStateHead], sizeof(buttonStateQueue[buttonStateHead]));
    buttonStateHead = (buttonStateHead + 1u) % kButtonStateQueueDepth;
    --buttonStateCount;
}
bool WebHidTelemetryState::admitEdge(bool pinned)
{
    if (edgeSequence == UINT32_MAX) {
        telemetryOverflow = true;
        return false;
    }
    if (edgeCount >= kEdgeQueueDepth) {
        /* Preserve the newest edge and report the discontinuity in the next
         * checkpoint instead of invalidating the WebHID session. */
        if (pinned) {
            /* The physical report still owns edgeQueue[edgeHead]. Dropping
             * that entry would make sendOneEdge() pop a different edge when
             * the retained report completes, so discard the newest arrival. */
            telemetryOverflow = true;
            if (totalDroppedSamples != UINT32_MAX) {
                ++totalDroppedSamples;
            }
            if (droppedSamples != UINT8_MAX) {
                ++droppedSamples;
            }
            return false;
        }
        HBoxCrypto_Zeroize(&edgeQueue[edgeHead], sizeof(PerfEdge));
        edgeHead = (edgeHead + 1u) % kEdgeQueueDepth;
        --edgeCount;
        telemetryOverflow = true;
        if (totalDroppedSamples != UINT32_MAX) {
            ++totalDroppedSamples;
        }
        if (droppedSamples != UINT8_MAX) {
            ++droppedSamples;
        }
    }

    return true;
}
void WebHidTelemetryState::enqueueAdmittedEdge(PerfEdge edge)
{
    edge.edgeSequence = ++edgeSequence;
    edgeQueue[edgeTail] = edge;
    edgeTail = (edgeTail + 1u) % kEdgeQueueDepth;
    ++edgeCount;
}
const WebHidTelemetryState::PerfEdge *WebHidTelemetryState::frontEdge() const
{ return edgeCount == 0u ? nullptr : &edgeQueue[edgeHead]; }
void WebHidTelemetryState::commitEdge()
{
    if (!edgeCount) return;
    HBoxCrypto_Zeroize(&edgeQueue[edgeHead], sizeof(PerfEdge));
    edgeHead = (edgeHead + 1u) % kEdgeQueueDepth;
    --edgeCount;
}
bool WebHidTelemetryState::schedule(uint32_t now, uint32_t timestampUs, uint32_t sampleInterval, bool pinned)
{
    if (telemetryOverflow) {
        telemetryOverflow = false;
        requestCheckpoint();
    }
    if (nextSampleAtMs == 0u) {
        nextSampleAtMs = now;
    }
    if (!pinned &&
        static_cast<int32_t>(now - nextSampleAtMs) >= 0) {
        const uint32_t due =
            ((now - nextSampleAtMs) / sampleInterval) + 1u;
        uint32_t dropped = due - 1u;
        if (samplePending) {
            ++dropped;
        }
        if (dropped != 0u) {
            const uint32_t room =
                UINT32_MAX - totalDroppedSamples;
            totalDroppedSamples +=
                dropped > room ? room : dropped;
            const uint32_t local =
                static_cast<uint32_t>(droppedSamples) + dropped;
            droppedSamples = static_cast<uint8_t>(
                std::min<uint32_t>(local, 255u));
        }
        nextSampleAtMs += due * sampleInterval;
        sampleTimestampUs = timestampUs;
        samplePending = true;
    }
    if (nextCheckpointAtMs == 0u) {
        nextCheckpointAtMs = now;
    }
    if (static_cast<int32_t>(
            now - nextCheckpointAtMs) >= 0) {
        nextCheckpointAtMs = now + 1000u;
        requestCheckpoint();
    }
    return checkpointRequested && !checkpointActive;
}
void WebHidTelemetryState::beginCheckpoint(uint32_t timestampUs, uint16_t maximumTravelUm)
{
    checkpointTimestampUs = timestampUs;
    checkpointEdgeSequence = edgeSequence;
    checkpointMaximumTravelUm =
        static_cast<uint16_t>(maximumTravelUm);
    checkpointDroppedSamples =
        static_cast<uint16_t>(
            std::min<uint32_t>(
                totalDroppedSamples, UINT16_MAX));
}
void WebHidTelemetryState::setCheckpointKey(uint8_t index, const webhid_perf_checkpoint_key_v1_t &key)
{ if (index < WEBHID_PERF_KEY_COUNT) checkpointKeys[index] = key; }
void WebHidTelemetryState::finishCheckpoint()
{
    ++checkpointId;
    if (checkpointId == 0u) {
        ++checkpointId;
    }
    checkpointChunk = 0u;
    checkpointActive = true;
    checkpointRequested = false;
}
bool WebHidTelemetryState::checkpointReport(webhid_perf_checkpoint_v1_t &checkpoint) const
{
    static constexpr uint8_t kChunkCount =
        WEBHID_PERF_KEY_COUNT /
        WEBHID_PERF_CHECKPOINT_KEYS;
    if (!checkpointActive ||
        checkpointChunk >= kChunkCount) {
        return false;
    }

    checkpoint = {};
    checkpoint.device_timestamp_us_le =
        checkpointTimestampUs;
    checkpoint.edge_sequence_le =
        checkpointEdgeSequence;
    checkpoint.max_travel_distance_um_le =
        checkpointMaximumTravelUm;
    checkpoint.total_dropped_samples_le =
        checkpointDroppedSamples;
    checkpoint.checkpoint_id = checkpointId;
    checkpoint.chunk_index = checkpointChunk;
    checkpoint.chunk_count = kChunkCount;
    checkpoint.first_button =
        static_cast<uint8_t>(
            checkpointChunk *
            WEBHID_PERF_CHECKPOINT_KEYS);
    for (uint8_t index = 0u;
         index < WEBHID_PERF_CHECKPOINT_KEYS;
         ++index) {
        checkpoint.keys[index] =
            checkpointKeys[
                checkpoint.first_button + index];
    }
    return true;
}
void WebHidTelemetryState::commitCheckpoint()
{
    constexpr uint8_t kChunkCount = WEBHID_PERF_KEY_COUNT / WEBHID_PERF_CHECKPOINT_KEYS;
    ++checkpointChunk;
    if (checkpointChunk >= kChunkCount) {
        checkpointActive = false;
        checkpointChunk = 0u;
        HBoxCrypto_Zeroize(
            checkpointKeys.data(), sizeof(checkpointKeys));
    }
}
