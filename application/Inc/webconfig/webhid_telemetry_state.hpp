#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "webhid_protocol.h"

// Owns telemetry queues and scheduling. The service owns authorization,
// hardware snapshots, transport priority and in-flight frame ownership.
class WebHidTelemetryState {
public:
#pragma pack(push, 1)
    struct PerfEdge
    {
        uint32_t timestampUs;
        uint32_t edgeSequence;
        uint8_t buttonIndex;
        uint8_t pressed;
        uint16_t rawAdc;
        uint16_t currentDistanceUm;
        uint16_t pressTriggerDistanceUm;
        uint16_t pressStartDistanceUm;
        uint16_t releaseTriggerDistanceUm;
        uint16_t releaseStartDistanceUm;
    };
#pragma pack(pop)
    static_assert(sizeof(PerfEdge) == 22u,
                  "PERF_EDGE ABI must remain 22 bytes");

    bool enabled() const { return performanceEnabled; }
    bool hasEdges() const { return edgeCount != 0u; }
    bool hasButtons() const { return buttonStateCount != 0u; }
    bool hasSample() const { return samplePending; }
    bool hasCheckpoint() const { return checkpointActive; }
    uint32_t sampleTimestamp() const { return sampleTimestampUs; }
    uint8_t sampleDrops() const { return droppedSamples; }
    void reset();
    void start(uint32_t now);
    void stop();
    void clearButtons();
    void enqueueButton(uint32_t mask, uint8_t totalButtons, bool active, bool pinned);
    // Front views remain valid until commit/reset; pinned overflow preserves them.
    const webhid_button_state_v1_t *frontButton() const;
    void commitButton();
    // Admission handles overflow before the caller reads ADC distances/clock.
    // A successful admission must be followed synchronously by one enqueue.
    bool admitEdge(bool pinned);
    void enqueueAdmittedEdge(PerfEdge edge);
    const PerfEdge *frontEdge() const;
    void commitEdge();
    void commitSample() { droppedSamples = 0u; samplePending = false; }
    void requestCheckpoint() { checkpointRequested = true; }
    bool schedule(uint32_t now, uint32_t timestampUs, uint32_t sampleInterval, bool pinned);
    // Synchronous snapshot assembly; finish publishes all keys as one checkpoint.
    void beginCheckpoint(uint32_t timestampUs, uint16_t maximumTravelUm);
    void setCheckpointKey(uint8_t index, const webhid_perf_checkpoint_key_v1_t &key);
    void finishCheckpoint();
    bool checkpointReport(webhid_perf_checkpoint_v1_t &report) const;
    void commitCheckpoint();
private:
    static constexpr size_t kEdgeQueueDepth = 64u;
    static constexpr size_t kButtonStateQueueDepth = 32u;
    /* Ordinary button-state snapshots are native single-report events.  Keep
     * transitions in a fixed FIFO so a short press/release cannot disappear
     * merely because the previous physical report is still in flight. */
    std::array<webhid_button_state_v1_t,
               kButtonStateQueueDepth> buttonStateQueue = {};
    size_t buttonStateHead = 0u;
    size_t buttonStateTail = 0u;
    size_t buttonStateCount = 0u;
    uint32_t buttonStateSequence = 0u;
    uint16_t buttonStateDropped = 0u;

    bool performanceEnabled = false;
    bool samplePending = false;
    uint32_t sampleTimestampUs = 0u;
    uint32_t nextSampleAtMs = 0u;
    uint32_t nextCheckpointAtMs = 0u;
    uint32_t edgeSequence = 0u;
    uint8_t droppedSamples = 0u;
    uint32_t totalDroppedSamples = 0u;
    std::array<PerfEdge, kEdgeQueueDepth> edgeQueue = {};
    size_t edgeHead = 0u;
    size_t edgeTail = 0u;
    size_t edgeCount = 0u;
    bool telemetryOverflow = false;
    bool checkpointRequested = false;
    bool checkpointActive = false;
    uint8_t checkpointId = 0u;
    uint8_t checkpointChunk = 0u;
    uint32_t checkpointTimestampUs = 0u;
    uint32_t checkpointEdgeSequence = 0u;
    uint16_t checkpointMaximumTravelUm = 0u;
    uint16_t checkpointDroppedSamples = 0u;
    std::array<webhid_perf_checkpoint_key_v1_t,
               WEBHID_PERF_KEY_COUNT> checkpointKeys = {};
};
