#ifndef HBOX_WEBHID_SERVICE_HPP
#define HBOX_WEBHID_SERVICE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "device_security_boot_context.h"
#include "webhid_protocol.h"

class WebHidService
{
public:
    WebHidService(const WebHidService &) = delete;
    WebHidService &operator=(const WebHidService &) = delete;

    static WebHidService &getInstance();

    bool setup();
    void process();
    void shutdown();
    bool isAuthenticated() const { return sessionEstablished; }
    uint32_t scopes() const { return grantedScopes; }

    void enqueueReport(const uint8_t report[WEBHID_REPORT_BYTES]);
    void enqueueJsonEvent(const char *json, size_t length);
    void enqueueBinaryEvent(const uint8_t *data, size_t length);
    void onAdcButtonTransition(uint8_t buttonIndex, bool pressed);

private:
    WebHidService() = default;

    static constexpr size_t kRxQueueDepth = 4u;
    static constexpr size_t kRxProcessBudget = 2u;
    static constexpr size_t kMaximumLogicalBytes = 8u * 1024u;
    static constexpr size_t kMaximumStreamBytes = 8u * 1024u;
    static constexpr size_t kMaximumEventBytes = 8u * 1024u;
    static constexpr size_t kMaximumQueuedEventBytes = 16u * 1024u;
    static constexpr size_t kMaximumEventQueue = 8u;
    static constexpr size_t kMaximumOutboundBytes = 16u * 1024u;
    static constexpr size_t kMaximumOutboundMessages = 8u;
    static constexpr size_t kEdgeQueueDepth = 64u;
    static constexpr size_t kControlBurstBytes = 256u;
    static constexpr uint32_t kFramePacingMs = 2u;

    struct LogicalAssembler
    {
        bool active = false;
        uint8_t type = 0u;
        bool secure = false;
        std::vector<uint8_t> bytes;
    };

    struct StreamState
    {
        bool active = false;
        uint8_t type = 0u;
        uint32_t transferId = 0u;
        uint32_t expectedLength = 0u;
        uint32_t received = 0u;
        uint8_t remainingCredit = 0u;
        std::array<uint8_t, 32> expectedHash = {};
        std::vector<uint8_t> bytes;
    };

    struct OutboundLogical
    {
        uint8_t type = 0u;
        bool secure = false;
        bool activateSession = false;
        bool endSession = false;
        bool telemetryPreemptible = false;
        size_t offset = 0u;
        std::vector<uint8_t> bytes;
    };

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

    bool validateBootContext();
    bool processReport(const uint8_t report[WEBHID_REPORT_BYTES]);
    bool acceptLogicalFragment(uint8_t type,
                               bool secure,
                               uint8_t flags,
                               const uint8_t *payload,
                               uint8_t length);
    bool processBootstrap(const std::vector<uint8_t> &message);
    bool processSecureRpc(const std::vector<uint8_t> &message);
    bool processStreamFragment(const uint8_t *payload, uint8_t length);

    bool handleAttestationCreate(uint32_t transactionId, void *params);
    bool handleInstallPermit(uint32_t transactionId, void *params);
    bool handleSpecialRpc(void *root,
                          uint32_t transactionId,
                          const std::string &command,
                          void *params,
                          bool &handled);
    bool handleBinaryExchange(uint32_t transactionId, void *params);
    bool handleStreamRpc(uint32_t transactionId,
                         const std::string &command,
                         void *params);

    bool sendLogical(uint8_t type,
                     const uint8_t *data,
                     size_t length,
                     bool secure);
    bool sendJson(uint8_t type,
                  const std::string &json,
                  bool secure);
    bool sendFrame(uint8_t type,
                   uint8_t flags,
                   const uint8_t *payload,
                   uint8_t length,
                   bool secure);
    bool sendRpcResult(uint32_t transactionId,
                       int error,
                       void *data,
                       const char *message = nullptr);
    bool sendResponse(uint8_t type,
                      bool secure,
                      uint32_t transactionId,
                      int error,
                      void *data,
                      const char *message = nullptr);
    bool sendBootstrapError(uint32_t transactionId,
                            const char *message);
    bool pumpLogicalOutput();
    void pumpOutput();
    bool outputPacingReady() const;
    void markOutputSent();

    void resetSession(bool keepBootIdentity);
    void clearRxQueue();
    bool installSessionKeys(
        const uint8_t permitHash[32],
        const uint8_t peerPublicKey[65],
        const std::string &sessionId);
    bool verifyPermit(const uint8_t *permit,
                      size_t length,
                      const uint8_t sessionId[16],
                      uint32_t &scopes,
                      uint32_t &durationMs);
    bool firmwareAuthorizationValid(
        const char *sessionId = nullptr,
        const uint8_t *chunk = nullptr,
        size_t chunkLength = 0u) const;
    void clearFirmwareAuthorization(bool abortSession);
    bool hasScope(uint32_t scope) const;

    bool queueOneEvent();
    void updateTelemetry();
    bool sendLatestSample(uint32_t timestampUs);
    bool sendOneEdge();
    void requestCheckpoint();
    bool prepareCheckpoint(uint32_t timestampUs);
    bool sendCheckpointChunk();
    uint32_t monotonicMicros();

    hbox_boot_security_context_v1_t bootContext = {};
    bool bootContextValid = false;
    bool initialized = false;
    bool sessionEstablished = false;
    bool waitingForPermit = false;
    bool sessionActivationPending = false;
    uint32_t grantedScopes = 0u;
    uint32_t sessionExpiresAtMs = 0u;
    uint32_t dangerousActionAuthorizedUntilMs = 0u;
    std::array<char, 33> authorizedFirmwareSession = {};
    uint32_t permitDeadlineMs = 0u;
    uint32_t lastRxSequence = 0u;
    uint32_t nextTxSequence = 1u;
    std::array<uint8_t, 32> rxKey = {};
    std::array<uint8_t, 32> txKey = {};
    std::array<uint8_t, 8> rxNoncePrefix = {};
    std::array<uint8_t, 8> txNoncePrefix = {};
    std::array<uint8_t, 32> deviceEphemeralPrivate = {};
    std::array<uint8_t, 65> deviceEphemeralPublic = {};
    std::array<uint8_t, 65> browserEphemeralPublic = {};
    std::array<uint8_t, 16> pendingSessionId = {};
    uint32_t requestedScopes = 0u;

    std::array<std::array<uint8_t, WEBHID_REPORT_BYTES>,
               kRxQueueDepth> rxQueue = {};
    volatile uint8_t rxHead = 0u;
    volatile uint8_t rxTail = 0u;
    volatile uint8_t rxCount = 0u;
    LogicalAssembler assembler;
    StreamState stream;
    std::deque<OutboundLogical> outboundQueue;
    size_t outboundQueuedBytes = 0u;
    std::deque<std::string> eventQueue;
    size_t eventQueuedBytes = 0u;
    std::vector<uint8_t> capturedBinary;
    bool captureBinary = false;

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
    uint32_t nextOutputAtMs = 0u;
    uint32_t lastDwtCycles = 0u;
    uint64_t accumulatedCycles = 0u;
};

#define WEBHID_SERVICE WebHidService::getInstance()

#endif
