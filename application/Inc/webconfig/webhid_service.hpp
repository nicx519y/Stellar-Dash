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
#include "webhid_logical_assembler.hpp"
#include "webhid_telemetry_state.hpp"

class WebHidService
{
public:
    WebHidService(const WebHidService &) = delete;
    WebHidService &operator=(const WebHidService &) = delete;

    static WebHidService &getInstance();

    bool setup();
    void process();
    // Foreground feedback only: never receives or dispatches another RPC.
    void serviceConfigSaveFeedback();
    void shutdown();
    bool isAuthenticated() const { return sessionEstablished; }
    uint32_t scopes() const { return grantedScopes; }

    bool enqueueReport(const uint8_t report[WEBHID_REPORT_BYTES]);
    void enqueueJsonEvent(const char *json, size_t length);
    void enqueueBinaryEvent(const uint8_t *data, size_t length);
    void onAdcButtonTransition(uint8_t buttonIndex, bool pressed);

private:
    WebHidService() = default;

    static constexpr size_t kRxQueueDepth = WEBHID_REPORT_WINDOW;
    // CH585 can forward one full four-report USB OUT window before the next
    // WebHID service tick. Drain that complete window in the same tick so a
    // fragmented bootstrap request cannot leave two stale reports occupying
    // the queue when the following USB window is collected.
    static constexpr size_t kRxProcessBudget = kRxQueueDepth;
    /*
     * Legacy WebConfig accepted logical JSON messages up to 16 KiB. Keep the
     * same bounded capacity in static RAM so a large request never attempts
     * to reserve the application's 8 KiB newlib heap.
     */
    static constexpr size_t kMaximumLogicalBytes = WebHidLogicalAssembler::Capacity;
    static constexpr size_t kMaximumStreamBytes = 8u * 1024u;
    static constexpr size_t kMaximumEventBytes = 8u * 1024u;
    static constexpr size_t kMaximumQueuedEventBytes = 16u * 1024u;
    static constexpr size_t kMaximumEventQueue = 8u;
    static constexpr size_t kMaximumOutboundBytes = 16u * 1024u;
    static constexpr size_t kMaximumOutboundMessages = 8u;
    struct StreamState
    {
        bool active = false;
        uint8_t type = 0u;
        uint32_t transferId = 0u;
        uint32_t expectedLength = 0u;
        uint32_t received = 0u;
        uint8_t remainingCredit = 0u;
        std::array<uint8_t, 32> expectedHash = {};
        /* One extra byte is reserved for cJSON's NUL terminator. */
        std::array<uint8_t, kMaximumStreamBytes + 1u> bytes = {};
    };

    struct OutboundLogical
    {
        uint8_t type = 0u;
        bool secure = false;
        bool activateSession = false;
        bool endSession = false;
        size_t start = 0u;
        size_t length = 0u;
        size_t offset = 0u;
    };

    enum class OutboundFrameSource : uint8_t
    {
        None = 0u,
        Logical,
        Edge,
        ButtonState,
        Sample,
        Checkpoint,
        Benchmark,
    };

    bool validateBootContext();
    bool handleBenchmarkRpc(uint32_t transactionId, const std::string &command, void *params);
    bool processBenchmarkData(const uint8_t *payload, uint16_t length);
    void pumpBenchmark();
    bool processReport(const uint8_t report[WEBHID_REPORT_BYTES]);
    bool acceptLogicalFragment(uint8_t type,
                               bool secure,
                               uint8_t flags,
                               const uint8_t *payload,
                               uint16_t length);
    bool processBootstrap(const uint8_t *message, size_t length);
    bool processSecureRpc(const uint8_t *message, size_t length);
    bool processStreamFragment(const uint8_t *payload, uint16_t length);
    bool processImageData(uint8_t flags,
                          const uint8_t *payload,
                          uint16_t length);

    bool handleAttestationCreate(uint32_t transactionId, void *params);
    bool handleDirectOpen(uint32_t transactionId, void *params);
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
                   uint16_t length,
                   bool secure,
                   OutboundFrameSource source);
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

    void resetSession(bool keepBootIdentity,
                      bool resetPhysicalTransport = true,
                      bool clearQueuedReports = true);
    void clearStream();
    void clearBinaryCapture();
    void clearRxQueue();
    bool installSessionKeys(
        const uint8_t permitHash[32],
        const uint8_t peerPublicKey[65],
        const std::string &sessionId);
    bool verifyPermit(const uint8_t *permit,
                      size_t length,
                      const uint8_t sessionId[16],
                      uint32_t &scopes);
    bool firmwareAuthorizationValid(
        const char *sessionId = nullptr,
        const uint8_t *chunk = nullptr,
        size_t chunkLength = 0u) const;
    void clearFirmwareAuthorization(bool abortSession);
    bool hasScope(uint32_t scope) const;

    bool queueOneEvent();
    void clearButtonStateQueue();
    bool sendOneButtonState();
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
    bool firmwareActionAuthorized = false;
    std::array<char, 33> authorizedFirmwareSession = {};
    uint32_t permitDeadlineMs = 0u;
    uint32_t sessionEpoch = 0u;
    uint32_t lastRxSequence = 0u;
    uint32_t nextTxSequence = 1u;
    /*
     * A board-link call may have transferred only the first SPI fragment of a
     * 64-byte WebHID report. Keep its producer pinned until that physical
     * transfer finishes; the exact encrypted bytes are retained in the
     * response scratch workspace, which cannot be reused while RX is gated.
     */
    OutboundFrameSource pendingFrameSource = OutboundFrameSource::None;
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
    WebHidLogicalAssembler assembler;
    StreamState stream;
    std::deque<OutboundLogical> outboundQueue;
    /*
     * FIFO byte storage for logical responses. Keeping payloads in this
     * fixed ring avoids a near-16 KiB std::vector allocation while the cJSON
     * response tree is still alive on the 32 KiB newlib heap.
     */
    std::array<uint8_t, kMaximumOutboundBytes> outboundStorage = {};
    size_t outboundReadOffset = 0u;
    size_t outboundWriteOffset = 0u;
    size_t outboundQueuedBytes = 0u;
    std::deque<std::string> eventQueue;
    size_t eventQueuedBytes = 0u;
    uint32_t droppedEventCount = 0u;
    std::vector<uint8_t> capturedBinary;
    bool captureBinary = false;
    bool captureBinaryInvalid = false;

    WebHidTelemetryState telemetry;
    uint32_t lastDwtCycles = 0u;
    uint64_t accumulatedCycles = 0u;
};

#define WEBHID_SERVICE WebHidService::getInstance()

#endif
