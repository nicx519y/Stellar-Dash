#include "usb_monitor_protocol.h"
#include "trace_clock.hpp"
#include "micro_timer.hpp"
#include "usb_board_link.hpp"
#include "usb_board_link_c_api.h"

#include <string.h>

#include "board_cfg.h"
#include "monitor_telemetry.hpp"
#include "system_logger.h"
#include "stm32h7xx_hal.h"
#include "usb_board_link_codec.h"
#include "usb_board_link_port.hpp"
#include "webhid_protocol.h"
#include "webhid_fast_link.h"
#include "states/webconfig_state.hpp"

/* First rejected SPI block only: header/counters, never report payloads or keys.
 * Publish whole dedicated cache lines so a running-core SWD read is reliable. */
extern "C" {
alignas(32) volatile uint32_t g_webhid_link_fault[32] = {};
/* First USB-input overflow metadata only; no input reports or session keys. */
alignas(32) volatile uint32_t g_usb_input_fault[8] = {};
}

namespace {
struct UsbSourceMonitor {
    TraceClock clock;
    uint32_t session=0, query=0, queryAt=0, lastQueryMs=0, lastReplyMs=0;
    uint32_t previous=0, event=0, trigger=0, complete=0, ready=0, sampleUs=0, drops=0;
    uint8_t enabled=0, baseline=0, sampleValid=0, clockPending=0, head=0, count=0;
    uint8_t clockFrame[24]={}, edges[8][40]={};
};
// CPU-only diagnostic state in the existing NOLOAD D2 area; initialized on role selection.
__attribute__((section(".DMA_Section"), aligned(32))) UsbSourceMonitor usbMon;
uint32_t usbMonitorNow() {
    return usbMon.clock.observe(DWT->CYCCNT,HAL_GetTick(),SystemCoreClock/1000000u);
}

static whf_link_t s_hsLink;
// CPU formatting buffer; port DMA uses its separate cache-managed copy. Each
// transmitted byte is initialized by whf_prepare, so NOLOAD D2 is safe here.
__attribute__((section(".DMA_Section"), aligned(32))) static uint8_t s_hsBlock[WHF_BLOCK_BYTES];
static bool s_hsReady;
static bool s_hsSessionInvalid;
static bool s_releaseFaultReported;
static uint32_t s_hsEpoch;
static bool s_txBulkCapable;
static uint32_t s_txBulkSequence;
struct TxBulkRead {
    uint32_t id=0, offset=0;
    uint16_t length=0;
    uint8_t *bytes=nullptr;
    bool active=false, done=false, success=false;
};
static TxBulkRead s_txBulkRead;
/* Use the TX-advertised 15 MHz application data rate. Bootstrap/IAP retain
 * their separate slow clock and PREPARE/probe/COMMIT ownership checks. */
static constexpr uint32_t kWebHidSpiHz = 15000000u;

static constexpr uint32_t kControlTimeoutMs = 20u;
static constexpr uint32_t kEventDrainTimeoutMs = 20u;
static constexpr uint32_t kBulkCreditWaitMs = 50u;
static constexpr uint32_t kTelemetryIntervalMs = 1000u;
static constexpr uint8_t kMaxEventsPerDrain = 64u;
static constexpr uint16_t kNetworkFrameBytes =
    USB_BOARD_BULK_MESSAGE_MAX_BYTES;
static_assert(sizeof(MonitorPowerFrameV2) == USB_BOARD_TELEMETRY_FRAME_BYTES,
              "MPW2 must remain one complete UsbBoardLink telemetry fragment");
static usb_board_link_network_rx_callback_t s_networkRxCallback = nullptr;
static uint8_t s_networkRx[kNetworkFrameBytes];
static uint16_t s_networkRxLength;
static uint16_t s_networkRxExpectedLength;
static uint16_t s_networkRxCrc;
static uint8_t s_networkRxTransaction;
static uint8_t s_networkRxExpectedFragment;
static bool s_networkRxActive;
static usb_board_link_webconfig_rx_callback_t s_webConfigRxCallback = nullptr;

// SPI backup replies never enter the browser RPC receiver. CRC, epoch,
// sequence and credit are already checked by the enclosing DMA link.
static void receiveFastReports()
{
    const uint8_t *report;
    while((report=whf_peek(&s_hsLink))!=nullptr) {
        if(txb_reserved(report)) {
            if(s_txBulkRead.active && !s_txBulkRead.done && txb_u32(report+8)==s_txBulkRead.id) {
                s_txBulkRead.success=txb_matches(report,s_txBulkRead.id,s_txBulkRead.offset,s_txBulkRead.length) &&
                    report[3]==USB_BOARD_STATUS_OK;
                if(s_txBulkRead.success)
                    memcpy(s_txBulkRead.bytes,report+XORA_TX_BULK_HEADER_BYTES,s_txBulkRead.length);
                s_txBulkRead.done=true;
            }
        } else if(!s_webConfigRxCallback || !s_webConfigRxCallback(report)) break;
        whf_release(&s_hsLink);
    }
}

static bool supportedRole(usb_board_role_t role)
{
    return (role == USB_BOARD_ROLE_RF) ||
           (role == USB_BOARD_ROLE_USB) ||
           (role == USB_BOARD_ROLE_MAINTENANCE);
}

static uint8_t bulkCreditLimit(uint8_t channel)
{
    return channel == USB_BOARD_CHANNEL_WEBCONFIG
        ? USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW
        : USB_BOARD_BULK_CREDIT_WINDOW;
}

static bool responseStatusOk(const uint8_t *payload, uint8_t length)
{
    return (payload != nullptr) && (length >= 2u) &&
           (payload[1] == USB_BOARD_STATUS_OK);
}

class LinkTransactionGuard
{
public:
    explicit LinkTransactionGuard(bool &active)
        : flag(active), acquired(!active)
    {
        if (acquired) {
            flag = true;
        }
    }

    ~LinkTransactionGuard()
    {
        if (acquired) {
            flag = false;
        }
    }

    explicit operator bool() const { return acquired; }

private:
    bool &flag;
    bool acquired;
};

} // namespace

bool UsbBoardLink::transact(uint8_t command,
                            const void *payload,
                            uint8_t payloadLength,
                            uint8_t expectedEvent,
                            void *responsePayload,
                            uint8_t responseCapacity,
                            uint8_t *responseLength,
                            uint32_t timeoutMs)
{
    uint8_t request[USB_BOARD_LINK_MAX_FRAME_BYTES] = {};
    uint8_t requestLength = 0u;
    const uint32_t startedAt = HAL_GetTick();
    const bool diagnoseCaps = command == USB_BOARD_CMD_GET_CAPS;
    const bool diagnoseRole = command == USB_BOARD_CMD_SELECT_ROLE;
    uint32_t observedEvents = 0u;
    uint32_t readFailures = 0u;
    uint32_t decodeFailures = 0u;
    uint8_t lastObservedEvent = 0u;
    LinkTransactionGuard transaction(transactionActive);

    if (responseLength != nullptr) {
        *responseLength = 0u;
    }
    if (!transaction) {
        return false;
    }

    /* USB control opcodes share one event number. A late reply to another
     * transaction must not complete this call (or suppress its request). */
    const auto matchesResponse = [&](const usb_board_link_frame_t &frame) {
        if (frame.command != expectedEvent) return false;
        if (command != USB_BOARD_CMD_USB_CONTROL) return true;
        const auto *header = static_cast<const usb_board_control_header_v1_t *>(payload);
        return header != nullptr && payloadLength >= USB_BOARD_CONTROL_HEADER_BYTES &&
            frame.length >= USB_BOARD_CONTROL_HEADER_BYTES &&
            frame.payload[0] == header->opcode && frame.payload[1] == header->transaction;
    };

    /*
     * A response can assert W_INT at the exact retry boundary. Drain it
     * before clocking a retry so a late ROLE_SELECTED frame is never shifted
     * out underneath a second SELECT_ROLE request.
     */
    // W_INT also carries a 100-ms boot-ready pulse. Before the first physical
    // SELECT_ROLE write it cannot be a role response. Leave it untouched and
    // let the bounded bootstrap retry observe its release before sending.
    if (diagnoseRole && !USBBoardLinkPort_RoleRequestSent() && USBBoardLinkPort_HasEvent()) {
        return false;
    }
    for (;;) {
        // A high-speed response is delivered before the peer releases W_INT.
        // In particular HS_COMMIT follows the probe immediately. Wait within
        // this transaction's budget, then drain any newly asserted event;
        // never mistake "still releasing" for an empty, writable bus.
        const uint32_t elapsed = HAL_GetTick() - startedAt;
        if (elapsed >= timeoutMs ||
            !USBBoardLinkPort_WaitEventRelease(timeoutMs - elapsed)) return false;
        if (!USBBoardLinkPort_HasEvent()) break;
        uint8_t pending[USB_BOARD_LINK_MAX_FRAME_BYTES] = {};
        uint8_t pendingLength = 0u;
        usb_board_link_frame_t decoded = {};
        ++observedEvents;
        if (!USBBoardLinkPort_ReadEvent(pending,
                                        sizeof(pending),
                                        &pendingLength)) {
            ++readFailures;
            return false;
        }
        if (!usb_board_link_decode(pending, pendingLength, &decoded)) {
            ++decodeFailures;
            return false;
        }
        lastObservedEvent = decoded.command;
        handleEvent(decoded.command, decoded.payload, decoded.length);
        if (!matchesResponse(decoded)) {
            continue;
        }
        if (decoded.length > responseCapacity) {
            return false;
        }
        if ((decoded.length != 0u) && (responsePayload != nullptr)) {
            memcpy(responsePayload, decoded.payload, decoded.length);
        }
        if (responseLength != nullptr) {
            *responseLength = decoded.length;
        }
        return true;
    }

    if (!usb_board_link_encode(command,
                               payload,
                               payloadLength,
                               request,
                               sizeof(request),
                               &requestLength) ||
        !USBBoardLinkPort_Send(request, requestLength)) {
        if (diagnoseCaps || diagnoseRole) {
            APP_STAGE_ERROR(diagnoseRole ? "R03D" : "M09D",
                            "%s send blocked: pending=%u observed=%lu read_fail=%lu decode_fail=%lu last=%02x",
                            diagnoseRole ? "SELECT_ROLE" : "GET_CAPS",
                            USBBoardLinkPort_HasEvent() ? 1u : 0u,
                            static_cast<unsigned long>(observedEvents),
                            static_cast<unsigned long>(readFailures),
                            static_cast<unsigned long>(decodeFailures),
                            static_cast<unsigned int>(lastObservedEvent));
        }
        return false;
    }

    do {
        uint8_t response[USB_BOARD_LINK_MAX_FRAME_BYTES] = {};
        uint8_t frameLength = 0u;
        usb_board_link_frame_t decoded = {};

        if (USBBoardLinkPort_HasEvent() &&
            USBBoardLinkPort_ReadEvent(response,
                                       sizeof(response),
                                       &frameLength) &&
            usb_board_link_decode(response, frameLength, &decoded)) {
            ++observedEvents;
            lastObservedEvent = decoded.command;
            handleEvent(decoded.command, decoded.payload, decoded.length);
            if (!matchesResponse(decoded)) {
                continue;
            }
            if (decoded.length > responseCapacity) {
                return false;
            }
            if ((decoded.length != 0u) && (responsePayload != nullptr)) {
                memcpy(responsePayload, decoded.payload, decoded.length);
            }
            if (responseLength != nullptr) {
                *responseLength = decoded.length;
            }
            return true;
        }
        HAL_Delay(1u);
    } while ((HAL_GetTick() - startedAt) < timeoutMs);
    if (diagnoseCaps || diagnoseRole) {
        APP_STAGE_ERROR(diagnoseRole ? "R03D" : "M09D",
                        "%s response timeout: observed=%lu read_fail=%lu decode_fail=%lu last=%02x pending=%u",
                        diagnoseRole ? "SELECT_ROLE" : "GET_CAPS",
                        static_cast<unsigned long>(observedEvents),
                        static_cast<unsigned long>(readFailures),
                        static_cast<unsigned long>(decodeFailures),
                        static_cast<unsigned int>(lastObservedEvent),
                        USBBoardLinkPort_HasEvent() ? 1u : 0u);
    }
    return false;
}

bool UsbBoardLink::drainEventsLocked(uint32_t timeoutMs)
{
    const uint32_t startedAt = HAL_GetTick();
    uint8_t count = 0u;

    while (USBBoardLinkPort_HasEvent()) {
        uint8_t raw[USB_BOARD_LINK_MAX_FRAME_BYTES] = {};
        uint8_t rawLength = 0u;
        usb_board_link_frame_t event = {};

        if ((count >= kMaxEventsPerDrain) ||
            ((HAL_GetTick() - startedAt) >= timeoutMs) ||
            !USBBoardLinkPort_ReadEvent(raw,
                                        sizeof(raw),
                                        &rawLength) ||
            !usb_board_link_decode(raw, rawLength, &event)) {
            return false;
        }
        handleEvent(event.command, event.payload, event.length);
        ++count;
    }
    return !USBBoardLinkPort_HasReleaseFault();
}

bool UsbBoardLink::sendLocked(uint8_t command,
                              const void *payload,
                              uint8_t payloadLength)
{
    uint8_t frame[USB_BOARD_LINK_MAX_FRAME_BYTES] = {};
    uint8_t frameLength = 0u;
    if (!usb_board_link_encode(command,
                               payload,
                               payloadLength,
                               frame,
                               sizeof(frame),
                               &frameLength)) {
        return false;
    }

    /*
     * W_INT low owns the next NSS assertion. Drain it before every write.
     * Retry once if an event reached the final GPIO boundary between the
     * drain and the write attempt.
     */
    for (uint8_t attempt = 0u; attempt < 2u; ++attempt) {
        if (!drainEventsLocked(kEventDrainTimeoutMs)) {
            return false;
        }
        if (!USBBoardLinkPort_WaitEventRelease(kEventDrainTimeoutMs)) return false;
        if (USBBoardLinkPort_HasEvent()) continue;
        if (USBBoardLinkPort_Send(frame, frameLength)) {
            return true;
        }
    }
    return false;
}

bool UsbBoardLink::send(uint8_t command,
                        const void *payload,
                        uint8_t payloadLength)
{
    LinkTransactionGuard transaction(transactionActive);
    if (!transaction) {
        return false;
    }
    return sendLocked(command,
                      payload,
                      payloadLength);
}

bool UsbBoardLink::selectRole(usb_board_role_t role, uint32_t timeoutMs)
{
    usb_board_role_select_v1_t request = {static_cast<uint8_t>(role)};
    usb_board_role_selected_v1_t response = {};
    uint8_t responseLength = 0u;

    if (!supportedRole(role) || roleLocked) {
        return roleLocked && (selectedRole == role);
    }
    usbMon=UsbSourceMonitor{};
    usbSubsystemEvidence = false;
    if (!USBBoardLinkPort_Init()) {
        return false;
    }

    const bool explicitSelection =
        transact(USB_BOARD_CMD_SELECT_ROLE,
                 &request,
                 sizeof(request),
                 USB_BOARD_EVT_ROLE_SELECTED,
                 &response,
                 sizeof(response),
                 &responseLength,
                 timeoutMs);
    const bool validExplicitSelection =
        explicitSelection &&
        (responseLength == sizeof(response)) &&
        (response.role == static_cast<uint8_t>(role)) &&
        (response.status == USB_BOARD_STATUS_OK);
    /*
     * ROLE_SELECTED is emitted by the cold-boot selector immediately before
     * it hands SPI ownership to the USB subsystem.  If that short frame is
     * lost at the hand-off boundary, a valid USB_STATE is stronger evidence:
     * CH585 can only generate it after the requested USB/maintenance role has
     * been accepted and usb_board_link_init() has completed.  RF selection
     * still requires the explicit acknowledgement because it never runs the
     * USB board-link subsystem.
     */
    const bool validUsbSubsystemSelection =
        !explicitSelection &&
        usbSubsystemEvidence &&
        ((role == USB_BOARD_ROLE_USB) ||
         (role == USB_BOARD_ROLE_MAINTENANCE));
    if (!validExplicitSelection && !validUsbSubsystemSelection) {
        return false;
    }

    if ((role == USB_BOARD_ROLE_USB) ||
        (role == USB_BOARD_ROLE_MAINTENANCE)) {
        /* Observe the Application-ready pulse only after a known ACK release.
         * A missed/ambiguous pulse keeps the legacy 150-ms settle fallback.
         * CAPS remains the definitive liveness check; no SPI probe is sent
         * during the ready pulse or the selector-to-Application handoff. */
        USBBoardLinkPort_WaitApplicationReady();
    }

    selectedRole = role;
    roleLocked = true;
    MonitorTelemetry_SetCh585Status(static_cast<uint8_t>(role),
                                    0u,
                                    0u,
                                    0u);
    if (role == USB_BOARD_ROLE_RF) {
        /*
         * The CH585 has disabled the 0x5A parser at this point. Release SPI4 so
         * the unchanged RF bridge can initialize and own it.
         */
        USBBoardLinkPort_Shutdown();
        return true;
    }
    if (!USBBoardLinkPort_InitApplication()) {
        roleLocked = false;
        selectedRole = USB_BOARD_ROLE_NONE;
        return false;
    }
    /*
     * Role ACK is the bootstrap boundary. The CH585 enters its USB loop only
     * after this transaction has completed, so capability discovery is
     * intentionally performed by USBDriver after bootstrap lock.
     */
    return true;
}

bool UsbBoardLink::getReleaseIdentity(xora_release_identity_t &identity)
{
    memset(&identity, 0, sizeof(identity));
    for (uint8_t offset = 0; offset < sizeof(identity);) {
        uint8_t count = sizeof(identity) - offset;
        if (count > 40) count = 40;
        uint8_t received = 0;
        if (!transact(USB_BOARD_CMD_RELEASE_IDENTITY, &offset, 1,
                      USB_BOARD_EVT_RELEASE_IDENTITY, reinterpret_cast<uint8_t*>(&identity) + offset,
                      count, &received, kControlTimeoutMs) || received != count) return false;
        offset += count;
    }
    return memcmp(identity.magic, XORA_RELEASE_IDENTITY_MAGIC, 8) == 0 &&
           identity.component == 2 && identity.protocol == XORA_INSTALL_PROTOCOL &&
           identity.version[31] == 0 && identity.build_id[64] == 0;
}

bool UsbBoardLink::getCapabilities()
{
    uint8_t responseLength = 0u;
    usb_board_caps_v1_t response = {};

    capsValid = false;
    if (!roleLocked || (selectedRole == USB_BOARD_ROLE_RF) ||
        !transact(USB_BOARD_CMD_GET_CAPS,
                  nullptr,
                  0u,
                  USB_BOARD_EVT_CAPS,
                  &response,
                  sizeof(response),
                  &responseLength,
                  kControlTimeoutMs) ||
        (responseLength != sizeof(response))) {
        return false;
    }

    caps = response;
    capsValid =
        (caps.protocol_version == USB_BOARD_LINK_VERSION) &&
        (caps.max_frame_bytes == USB_BOARD_LINK_MAX_FRAME_BYTES) &&
        (caps.input_state_bytes == USB_BOARD_INPUT_V1_BYTES);
    fastApplication = false;
    if (capsValid) {
        MonitorTelemetry_SetCh585Status(
            static_cast<uint8_t>(selectedRole),
            caps.firmware_major,
            caps.firmware_minor,
            caps.firmware_patch);
    }
    if (capsValid) {
        /*
         * CAPS is the application-liveness contract. Initial receive-credit
         * writes are a separate, retryable flow-control phase: immediately
         * after role hand-off CH585 can still have USB_STATE/credit events
         * queued, so a write may legitimately lose the W_INT ownership race.
         * Keep validated CAPS and leave unsent credits dirty; process() will
         * retry them on subsequent ticks.
         */
        (void)grantInitialReceiveCredits();
    }
    return capsValid;
}

bool UsbBoardLink::getUsbLinkState(usb_board_control_link_state_v1_t &state)
{
    uint8_t responseLength = 0u;
    memset(&state, 0, sizeof(state));
    return sendControl(USB_BOARD_CONTROL_GET_LINK_STATE,
                       nullptr,
                       0u,
                       reinterpret_cast<uint8_t *>(&state),
                       sizeof(state),
                       &responseLength) &&
           responseLength == sizeof(state);
}

bool UsbBoardLink::setDataPlane(usb_board_data_plane_t mode)
{
    const usb_board_set_data_plane_v1_t request = {
        static_cast<uint8_t>(mode)
    };
    usb_board_data_plane_set_v1_t response = {};
    uint8_t responseLength = 0u;

    return transact(USB_BOARD_CMD_SET_DATA_PLANE,
                    &request,
                    sizeof(request),
                    USB_BOARD_EVT_DATA_PLANE_SET,
                    &response,
                    sizeof(response),
                    &responseLength,
                    kControlTimeoutMs) &&
           responseLength == sizeof(response) &&
           response.mode == static_cast<uint8_t>(mode) &&
           response.status == USB_BOARD_STATUS_OK;
}

bool UsbBoardLink::enableWebHidDataPlane()
{
    WebConfig_RecordStartupStage(0x40u, 3u);
    s_hsReady = false;
    if (!capsValid || selectedRole != USB_BOARD_ROLE_MAINTENANCE ||
        selectedProfile != USB_BOARD_PROFILE_WEB_CONFIG) return false;
    uint8_t capability[12] = {}, size = 0u, request[8] = {};
    WebConfig_RecordStartupStage(0x41u, 3u);
    /* This read-only query may race the final profile/state event or time
     * out during startup. Retry only transport/not-ready failures; a valid
     * incompatible response must still fail closed without switching SPI. */
    bool discovered = false;
    for (unsigned attempt = 0u; attempt < 3u && !discovered; ++attempt) {
        uint8_t status = USB_BOARD_STATUS_NOT_READY;
        discovered = sendControl(USB_BOARD_CONTROL_HS_CAPS, nullptr, 0u, capability,
            sizeof(capability), &size, &status);
        if (!discovered) {
            if (status != USB_BOARD_STATUS_NOT_READY && status != USB_BOARD_STATUS_BUSY &&
                status != USB_BOARD_STATUS_OK) return false;
            if (attempt + 1u < 3u) HAL_Delay(1u);
        }
    }
    if (!discovered || size != sizeof(capability) ||
        whf_u32(capability) != WEBHID_CAPABILITY_MAGIC ||
        whf_u16(capability+8) != WEBHID_REPORT_BYTES ||
        capability[10] != WHF_CAPACITY || capability[11] != WEBHID_PROTOCOL_VERSION ||
        whf_u32(capability+4) < kWebHidSpiHz) return false;
    if (++s_hsEpoch == 0u) ++s_hsEpoch;
    whf_put32(request, s_hsEpoch); whf_put32(request+4, kWebHidSpiHz);
    WebConfig_RecordStartupStage(0x42u, 3u);
    /* No report has been submitted yet. Repeating PREPARE for this epoch is
     * idempotent if the RX backend changed but its acknowledgement was lost.
     * Do not switch the master's clock without a matching acknowledgement. */
    bool prepared = false;
    for (unsigned attempt = 0u; attempt < 3u && !prepared; ++attempt) {
        prepared = sendControl(USB_BOARD_CONTROL_HS_PREPARE, request, sizeof(request));
        if (!prepared) HAL_Delay(1u);
    }
    if (!prepared) return false;
    WebConfig_RecordStartupStage(0x43u, 3u);
    if (!USBBoardLinkPort_EnableWebHid(kWebHidSpiHz)) return false;
    whf_init(&s_hsLink, s_hsEpoch);
    const uint16_t length = whf_prepare(&s_hsLink, s_hsBlock);
    WebConfig_RecordStartupStage(0x44u, 3u);
    if (!USBBoardLinkPort_SendWebHidBlock(s_hsBlock, length)) return false;
    whf_commit(&s_hsLink, s_hsBlock);
    const uint32_t start = HAL_GetTick();
    while (s_hsLink.rx_block == 0u && HAL_GetTick()-start < 20u && !s_hsLink.failed) {
        LinkTransactionGuard guard(transactionActive);
        if (guard) (void)drainEventsLocked(kEventDrainTimeoutMs);
    }
    WebConfig_RecordStartupStage(s_hsLink.rx_block, 6u);
    WebConfig_RecordStartupStage(s_hsLink.failed, 7u);
    if (!s_hsLink.rx_block || s_hsLink.failed) return false;
    WebConfig_RecordStartupStage(0x45u, 3u);
    if (!sendControl(USB_BOARD_CONTROL_HS_COMMIT, request, 4u)) return false;
    WebConfig_RecordStartupStage(0x46u, 3u);
    s_hsReady = true;
    return true;
}

bool UsbBoardLink::enableFastInputDataPlane()
{
    const auto recordStage = [](uint32_t stage) {
        g_usb_input_fault[7] = stage;
        SCB_CleanDCache_by_Addr((uint32_t *)g_usb_input_fault,sizeof(g_usb_input_fault));
        __DSB();
    };
    if (!capsValid || selectedRole != USB_BOARD_ROLE_USB ||
        selectedProfile != USB_BOARD_PROFILE_XINPUT ||
        (caps.feature_flags &
         USB_BOARD_CAP_FEATURE_SPI_FAST_INPUT_V2) == 0u) {
        return false;
    }
    if (fastApplication && USBBoardLinkPort_IsFastApplication()) {
        return true;
    }

    fastDataPlaneFaultPending = false;
    fastDataPlaneFault = USB_BOARD_STATUS_OK;
    recordStage(1u);
    bool prepared = false;
    /* SET_DATA_PLANE is idempotent. A busy NSS boundary or a lost reply
     * must not permanently disable fast input for this connection. */
    for(unsigned attempt=0u; attempt<3u && !prepared; ++attempt) {
        prepared=setDataPlane(USB_BOARD_DATA_PLANE_FAST_INPUT_V2);
        if(!prepared && attempt+1u<3u) HAL_Delay(1u);
    }
    if (!prepared) {
        recordStage(0xE1u);
        APP_STAGE_ERROR("U05F", "FAST_INPUT_V2 handshake rejected");
        return false;
    }
    if (!USBBoardLinkPort_EnableFastApplication()) {
        recordStage(0xE2u);
        APP_STAGE_ERROR("U05F",
                        "FAST_INPUT_V2 local SPI switch failed: spi4_hz=%lu",
                        static_cast<unsigned long>(
                            USBBoardLinkPort_ClockHz()));
        (void)restoreCompatibleDataPlane();
        return false;
    }

    usb_board_data_plane_probe_v1_t probe = {};
    usb_board_data_plane_probe_result_v1_t response = {};
    uint8_t responseLength = 0u;
    probe.mode = USB_BOARD_DATA_PLANE_FAST_INPUT_V2;
    probe.version = USB_BOARD_DATA_PLANE_PROBE_VERSION;
    probe.nonce_le = (++dataPlaneNonce) ^ HAL_GetTick() ^ 0x58525632u;
    for (uint8_t index = 0u; index < sizeof(probe.pattern); ++index) {
        probe.pattern[index] = static_cast<uint8_t>(
            USB_BOARD_DATA_PLANE_PROBE_PATTERN_SEED +
            static_cast<uint8_t>(
                index * USB_BOARD_DATA_PLANE_PROBE_PATTERN_STEP));
    }
    probe.crc16_le = usb_board_crc16_ccitt(
        reinterpret_cast<const uint8_t *>(&probe),
        static_cast<uint16_t>(sizeof(probe) - sizeof(probe.crc16_le)));

    const bool probeOk =
        transact(USB_BOARD_CMD_DATA_PLANE_PROBE,
                 &probe,
                 sizeof(probe),
                 USB_BOARD_EVT_DATA_PLANE_PROBE,
                 &response,
                 sizeof(response),
                 &responseLength,
                 kControlTimeoutMs) &&
        responseLength == sizeof(response) &&
        response.mode == USB_BOARD_DATA_PLANE_FAST_INPUT_V2 &&
        response.status == USB_BOARD_STATUS_OK &&
        response.nonce_le == probe.nonce_le &&
        response.crc16_le == probe.crc16_le;
    if (!probeOk) {
        recordStage(0xE3u);
        APP_STAGE_ERROR("U05P", "FAST_INPUT_V2 maximum-frame probe failed");
        (void)restoreCompatibleDataPlane();
        return false;
    }

    fastApplication = true;
    recordStage(4u);
    APP_STAGE("U05P",
              "FAST_INPUT_V2 probe accepted: spi4_hz=%lu nonce=%08lx",
              static_cast<unsigned long>(USBBoardLinkPort_ClockHz()),
              static_cast<unsigned long>(probe.nonce_le));
    return true;
}

bool UsbBoardLink::restoreCompatibleDataPlane()
{
    const bool localCompatible = USBBoardLinkPort_DisableFastApplication();
    fastApplication = false;
    const bool remoteCompatible =
        capsValid && selectedRole != USB_BOARD_ROLE_RF
            ? setDataPlane(USB_BOARD_DATA_PLANE_COMPAT)
            : true;
    if (!localCompatible || !remoteCompatible) {
        APP_STAGE_ERROR("U05R",
                        "BoardLink compatibility recovery failed: local=%u remote=%u",
                        localCompatible ? 1u : 0u,
                        remoteCompatible ? 1u : 0u);
    }
    return localCompatible && remoteCompatible;
}

bool UsbBoardLink::takeFastDataPlaneFault(uint8_t &fault)
{
    if (!fastDataPlaneFaultPending) {
        return false;
    }
    fault = fastDataPlaneFault;
    fastDataPlaneFaultPending = false;
    fastDataPlaneFault = USB_BOARD_STATUS_OK;
    return true;
}

bool UsbBoardLink::grantInitialReceiveCredits()
{
    memset(receiveCredits, 0, sizeof(receiveCredits));
    memset(receiveCreditDirty, 0, sizeof(receiveCreditDirty));
    for (uint8_t channel = USB_BOARD_CHANNEL_USB_DEVICE;
         channel <= USB_BOARD_CHANNEL_LAST;
         ++channel) {
        receiveCredits[channel] =
            (channel == USB_BOARD_CHANNEL_WEBCONFIG &&
             s_webConfigRxCallback == nullptr)
                ? 0u
                : bulkCreditLimit(channel);
        receiveCreditDirty[channel] = channel == USB_BOARD_CHANNEL_WEBCONFIG ? 0u : 1u;
    }
    flushReceiveCredits();
    for (uint8_t channel = USB_BOARD_CHANNEL_USB_DEVICE;
         channel <= USB_BOARD_CHANNEL_LAST;
         ++channel) {
        if (receiveCreditDirty[channel] != 0u) {
            return false;
        }
    }
    return true;
}

void UsbBoardLink::returnReceiveCredit(usb_board_channel_t channel)
{
    const uint8_t index = static_cast<uint8_t>(channel);
    if ((index == 0u) || (index >= sizeof(receiveCredits))) {
        return;
    }
    if (receiveCredits[index] < bulkCreditLimit(index)) {
        ++receiveCredits[index];
    }
    receiveCreditDirty[index] = 1u;
}

void UsbBoardLink::flushReceiveCredits()
{
    if (transactionActive || !roleLocked ||
        (selectedRole == USB_BOARD_ROLE_RF)) {
        return;
    }
    for (uint8_t channel = USB_BOARD_CHANNEL_USB_DEVICE;
         channel <= USB_BOARD_CHANNEL_LAST;
         ++channel) {
        if (receiveCreditDirty[channel] == 0u) {
            continue;
        }
        const usb_board_bulk_credit_v1_t credit = {
            channel,
            receiveCredits[channel],
        };
        if (!send(USB_BOARD_CMD_BULK_CREDIT,
                  &credit,
                  sizeof(credit))) {
            return;
        }
        receiveCreditDirty[channel] = 0u;
    }
}

bool UsbBoardLink::setProfile(usb_board_profile_t profile)
{
    usb_board_set_profile_v1_t request = {static_cast<uint8_t>(profile)};
    usb_board_profile_set_v1_t response = {};
    uint8_t responseLength = 0u;

    const bool webConfigProfile =
        profile == USB_BOARD_PROFILE_WEB_CONFIG;
    if (!capsValid ||
        ((profile == USB_BOARD_PROFILE_XINPUT) &&
         ((caps.feature_flags &
           USB_BOARD_CAP_FEATURE_TELEMETRY_HID) == 0u)) ||
        (webConfigProfile &&
         (selectedRole != USB_BOARD_ROLE_MAINTENANCE ||
           (caps.profile_flags & USB_BOARD_CAP_PROFILE_WEB_CONFIG) == 0u ||
           (caps.feature_flags & USB_BOARD_CAP_FEATURE_WEBHID_V1) == 0u ||
           (caps.feature_flags &
            USB_BOARD_CAP_FEATURE_WEBCONFIG_PULL_CREDIT) == 0u)) ||
        ((selectedRole != USB_BOARD_ROLE_USB) &&
         (selectedRole != USB_BOARD_ROLE_MAINTENANCE)) ||
        !transact(USB_BOARD_CMD_SET_PROFILE,
                  &request,
                  sizeof(request),
                  USB_BOARD_EVT_PROFILE_SET,
                  &response,
                  sizeof(response),
                  &responseLength,
                  kControlTimeoutMs) ||
        (responseLength != sizeof(response)) ||
        !responseStatusOk(reinterpret_cast<const uint8_t *>(&response),
                          sizeof(response)) ||
        (response.profile != static_cast<uint8_t>(profile))) {
        return false;
    }
    selectedProfile = profile;
    credits[USB_BOARD_CHANNEL_WEBCONFIG] = 0u;
    webConfigTransportState = WebConfigTransportState::Ready;
    telemetryTransaction = 0u;
    nextTelemetryAtMs = HAL_GetTick() + kTelemetryIntervalMs;
    return true;
}

bool UsbBoardLink::submitInput(uint32_t processedActionMask,
                               uint16_t ageUs,
                               uint8_t batteryCode,
                               bool batteryValid,
                               uint16_t reportRateHz)
{
    usb_board_input_v1_t input = {};
    if (!capsValid || (selectedRole != USB_BOARD_ROLE_USB)) {
        return false;
    }
    input.seq = inputSequence++;
    input.flags =
        static_cast<uint8_t>((USB_BOARD_INPUT_FORMAT_VERSION <<
                              USB_BOARD_INPUT_VERSION_SHIFT) |
                             USB_BOARD_INPUT_FLAG_PROCESSED |
                             usb_board_input_rate_flags(reportRateHz) |
                             (batteryValid
                                  ? USB_BOARD_INPUT_FLAG_BATTERY_VALID
                                  : 0u));
    input.action_mask_le = processedActionMask;
    input.age_us_le = ageUs;
    input.battery_code = batteryCode;
    input.crc8 = usb_board_input_crc8(
        reinterpret_cast<const uint8_t *>(&input),
        static_cast<uint8_t>(sizeof(input) - 1u));
    const bool edge=(usbMon.enabled&UM_LATENCY) && usbMon.baseline && usbMon.previous!=processedActionMask;
    const uint32_t previous=usbMon.previous;
    usbMon.previous=processedActionMask;usbMon.baseline=1u;
    const bool ok=send(USB_BOARD_CMD_INPUT_STATE, &input, sizeof(input));
    if(edge && usbMon.sampleValid) {
        // Sparse metadata never changes the 10-byte input ABI or waits for credit.
        if(usbMon.count<8u) {
            uint8_t *p=usbMon.edges[(usbMon.head+usbMon.count)%8u];memset(p,0,40);
            p[0]=UM_BOARD_EDGE;p[1]=UM_VERSION;p[2]=ok?0u:UM_SEND_FAILED;p[3]=input.seq;
            um_put32(p+4,usbMon.session);um_put32(p+8,++usbMon.event);
            um_put32(p+12,previous);um_put32(p+16,processedActionMask);um_put32(p+20,usbMon.sampleUs);
            const uint32_t scale=SystemCoreClock/1000000u;
            um_put32(p+24,(usbMon.complete-usbMon.trigger)/scale);
            um_put32(p+28,(usbMon.ready-usbMon.complete)/scale);
            uint32_t start=0,end=0;
            const bool timing=ok && USBBoardLinkPort_LastMonitorTiming(&start,&end);
            um_put32(p+32,timing?(start-usbMon.ready)/scale:UM_UNKNOWN);
            um_put32(p+36,timing?(end-start)/scale:UM_UNKNOWN);
            usbMon.count++;
        } else usbMon.drops++;
        // A missing sidecar remains a partial CH585 edge, never a fabricated duration.
    }
    usbMon.sampleValid=0u;
    return ok;
}

bool UsbBoardLink::sendControl(usb_board_control_opcode_t opcode,
                               const uint8_t *payload,
                               uint8_t length,
                               uint8_t *responseData,
                               uint8_t responseCapacity,
                               uint8_t *responseDataLength,
                               uint8_t *remoteStatus)
{
    usb_board_control_request_v1_t request = {};
    usb_board_control_response_v1_t response = {};
    uint8_t responseLength = 0u;
    const uint8_t transaction = controlTransaction++;

    if (responseDataLength != nullptr) {
        *responseDataLength = 0u;
    }
    if (!capsValid || (selectedRole == USB_BOARD_ROLE_RF) ||
        ((caps.feature_flags & USB_BOARD_CAP_FEATURE_CONTROL_V1) == 0u) ||
        (length > USB_BOARD_CONTROL_DATA_BYTES) ||
        ((length != 0u) && (payload == nullptr))) {
        return false;
    }

    request.header.opcode = static_cast<uint8_t>(opcode);
    request.header.transaction = transaction;
    request.header.status = USB_BOARD_STATUS_OK;
    request.header.data_length = length;
    if (length != 0u) {
        memcpy(request.data, payload, length);
    }

    const bool received = transact(USB_BOARD_CMD_USB_CONTROL,
                  &request,
                  static_cast<uint8_t>(USB_BOARD_CONTROL_HEADER_BYTES +
                                       length),
                  USB_BOARD_EVT_USB_CONTROL,
                  &response,
                  sizeof(response),
                  &responseLength,
                  opcode == USB_BOARD_CONTROL_RF_BINDING ? 2000u : kControlTimeoutMs);
    if (opcode == USB_BOARD_CONTROL_CONNECT || opcode == USB_BOARD_CONTROL_HS_PREPARE ||
        opcode == USB_BOARD_CONTROL_HS_CAPS) {
        const uint32_t field = opcode == USB_BOARD_CONTROL_CONNECT ? 4u : 6u;
        WebConfig_RecordStartupStage((received ? 0x80000000u : 0u) |
            (uint32_t(responseLength) << 16u) | (uint32_t(response.header.opcode) << 8u) |
            response.header.status, field);
        WebConfig_RecordStartupStage((uint32_t(transaction) << 8u) | response.header.transaction, field + 1u);
    }
    if (remoteStatus != nullptr) {
        *remoteStatus = received && responseLength >= USB_BOARD_CONTROL_HEADER_BYTES &&
            response.header.opcode == static_cast<uint8_t>(opcode) &&
            response.header.transaction == transaction ? response.header.status :
            static_cast<uint8_t>(USB_BOARD_STATUS_NOT_READY);
    }
    if (!received ||
        (responseLength < USB_BOARD_CONTROL_HEADER_BYTES) ||
        (response.header.opcode != static_cast<uint8_t>(opcode)) ||
        (response.header.transaction != transaction) ||
        (response.header.status != USB_BOARD_STATUS_OK) ||
        (response.header.data_length !=
         static_cast<uint8_t>(responseLength -
                              USB_BOARD_CONTROL_HEADER_BYTES)) ||
        (response.header.data_length > responseCapacity) ||
        ((response.header.data_length != 0u) &&
         (responseData == nullptr))) {
        return false;
    }

    if (response.header.data_length != 0u) {
        memcpy(responseData, response.data, response.header.data_length);
    }
    if (responseDataLength != nullptr) {
        *responseDataLength = response.header.data_length;
    }
    return true;
}

bool UsbBoardLink::getTxImageInfo()
{
    uint32_t info[3]={};uint8_t size=0;
    s_txBulkCapable=false;
    if(!(sendControl(USB_BOARD_CONTROL_TX_IMAGE_INFO,nullptr,0,
        reinterpret_cast<uint8_t*>(info),sizeof(info),&size) && size==sizeof(info) &&
        info[0]==2u && info[1]==0x1000u && info[2]==XORA_TX_BACKUP_APP_BYTES)) return false;
    if(!s_hsReady) return true;
    uint8_t bulk[12]={},status=USB_BOARD_STATUS_NOT_READY;
    if(!sendControl(USB_BOARD_CONTROL_TX_IMAGE_BULK_INFO,nullptr,0,bulk,sizeof(bulk),&size,&status))
        return status==USB_BOARD_STATUS_UNSUPPORTED; // Old TX only; no timeout/corruption downgrade.
    if(size!=sizeof(bulk) || txb_u32(bulk)!=XORA_TX_BULK_MAGIC ||
       txb_u32(bulk+4)!=XORA_TX_BULK_VERSION || txb_u32(bulk+8)!=XORA_TX_BULK_READ_BYTES) return false;
    s_txBulkCapable=true;return true;
}

uint16_t UsbBoardLink::txImageReadBytes() const
{
    return s_txBulkCapable ? XORA_TX_BULK_READ_BYTES : XORA_TX_READ_BYTES;
}

bool UsbBoardLink::readTxImage(uint32_t offset,uint8_t* bytes,uint16_t length)
{
    if(s_txBulkCapable) {
        if(!bytes || !txb_range(offset,length) || !s_hsReady || s_txBulkRead.active ||
           selectedRole!=USB_BOARD_ROLE_MAINTENANCE || selectedProfile!=USB_BOARD_PROFILE_WEB_CONFIG ||
           s_txBulkSequence==UINT32_MAX) return false;
        LinkTransactionGuard transaction(transactionActive);
        if(!transaction) return false;
        uint8_t request[WEBHID_REPORT_BYTES];
        const uint32_t id=++s_txBulkSequence;
        if(!txb_request(request,id,offset,length)) return false;
        s_txBulkRead={id,offset,length,bytes,true,false,false};
        const uint32_t start=HAL_GetTick();
        bool queued=false;
        // Pump transport only, never RPC dispatch/QSPI users. Return to the
        // main loop after each chunk so status queries remain serviceable.
        while(s_hsReady && !s_txBulkRead.done && !USBBoardLinkPort_HasReleaseFault() && HAL_GetTick()-start<100u) {
            (void)drainEventsLocked(kEventDrainTimeoutMs);
            receiveFastReports();
            if(!s_hsReady || s_txBulkRead.done) break;
            if(!queued) queued=whf_enqueue(&s_hsLink,request);
            const uint16_t size=whf_prepare(&s_hsLink,s_hsBlock);
            if(size && USBBoardLinkPort_SendWebHidBlock(s_hsBlock,size)) whf_commit(&s_hsLink,s_hsBlock);
        }
        const bool ok=queued && s_txBulkRead.done && s_txBulkRead.success &&
            s_hsReady && !USBBoardLinkPort_HasReleaseFault();
        s_txBulkRead={};
        return ok;
    }
    if(!bytes || !xora_tx_read_range_valid(offset,length))return false;
    uint8_t request[6],response[6+XORA_TX_READ_BYTES],size=0;
    memcpy(request,&offset,4);memcpy(request+4,&length,2);
    if(!sendControl(USB_BOARD_CONTROL_TX_IMAGE_READ,request,sizeof(request),response,sizeof(response),&size) ||
       size!=6+length || memcmp(request,response,6))return false;
    memcpy(bytes,response+6,length);return true;
}

uint8_t UsbBoardLink::creditFor(usb_board_channel_t channel) const
{
    const uint8_t index = static_cast<uint8_t>(channel);
    return (index < sizeof(credits)) ? credits[index] : 0u;
}

void UsbBoardLink::consumeCredit(usb_board_channel_t channel)
{
    const uint8_t index = static_cast<uint8_t>(channel);
    if ((index < sizeof(credits)) && (credits[index] != 0u)) {
        --credits[index];
    }
}

void UsbBoardLink::requestWebConfigTransportReset()
{
    if (webConfigTransportState != WebConfigTransportState::ResetRequested) webConfigResetAttempts = 0u;
    credits[USB_BOARD_CHANNEL_WEBCONFIG] = 0u;
    if (capsValid &&
        selectedRole == USB_BOARD_ROLE_MAINTENANCE &&
        selectedProfile == USB_BOARD_PROFILE_WEB_CONFIG) {
        webConfigTransportState =
            WebConfigTransportState::ResetRequested;
    }
}

void UsbBoardLink::serviceWebConfigTransportReset()
{
    if (webConfigTransportState !=
        WebConfigTransportState::ResetRequested) {
        return;
    }
    if (!capsValid ||
        selectedRole != USB_BOARD_ROLE_MAINTENANCE ||
        selectedProfile != USB_BOARD_PROFILE_WEB_CONFIG) {
        return;
    }

    s_hsReady = false;
    if (USBBoardLinkPort_HasReleaseFault()) return;
    if (webConfigResetAttempts >= 3u) return;
    ++webConfigResetAttempts;
    if (sendControl(USB_BOARD_CONTROL_CLEAR_FAULT) && enableWebHidDataPlane()) {
        webConfigTransportState = WebConfigTransportState::Ready;
    }
}

bool UsbBoardLink::sendBulk(usb_board_channel_t channel,
                            uint8_t transaction,
                            const uint8_t *payload,
                            uint16_t length)
{
    return sendBulkInternal(
        channel, transaction, payload, length, true);
}

bool UsbBoardLink::trySendBulk(usb_board_channel_t channel,
                               uint8_t transaction,
                               const uint8_t *payload,
                               uint16_t length)
{
    return sendBulkInternal(
        channel, transaction, payload, length, false);
}

bool UsbBoardLink::sendBulkInternal(usb_board_channel_t channel,
                                    uint8_t transaction,
                                    const uint8_t *payload,
                                    uint16_t length,
                                    bool waitForCredit)
{
    uint16_t offset = 0u;
    uint8_t fragmentIndex = 0u;
    uint16_t messageCrc;
    const uint8_t channelIndex = static_cast<uint8_t>(channel);
    const bool webConfigReport =
        channel == USB_BOARD_CHANNEL_WEBCONFIG;

    if (!capsValid || (selectedRole == USB_BOARD_ROLE_RF) ||
        (channelIndex == 0u) || (channelIndex >= sizeof(credits)) ||
        (length > USB_BOARD_BULK_MESSAGE_MAX_BYTES) ||
        (webConfigReport && length != WEBHID_REPORT_BYTES) ||
        ((length != 0u) && (payload == nullptr))) {
        return false;
    }

    if (channel == USB_BOARD_CHANNEL_WEBCONFIG) return false;

    messageCrc = usb_board_crc16_ccitt(payload, length);

    const uint16_t requiredCredits =
        length == 0u
            ? 1u
            : static_cast<uint16_t>(
                  (length + USB_BOARD_FRAGMENT_DATA_BYTES - 1u) /
                  USB_BOARD_FRAGMENT_DATA_BYTES);
    if (!waitForCredit &&
        creditFor(channel) < requiredCredits) {
        return false;
    }

    do {
        uint8_t packet[USB_BOARD_LINK_MAX_PAYLOAD_BYTES] = {};
        auto *header =
            reinterpret_cast<usb_board_fragment_header_v1_t *>(packet);
        const uint16_t remaining = static_cast<uint16_t>(length - offset);
        const uint8_t dataLength = static_cast<uint8_t>(
            (remaining > USB_BOARD_FRAGMENT_DATA_BYTES)
                ? USB_BOARD_FRAGMENT_DATA_BYTES
                : remaining);

        if (waitForCredit) {
            const uint32_t creditWaitStarted = HAL_GetTick();
            while (creditFor(channel) == 0u) {
                process();
                if ((HAL_GetTick() - creditWaitStarted) >=
                    kBulkCreditWaitMs) {
                    return false;
                }
                HAL_Delay(1u);
            }
        } else if (creditFor(channel) == 0u) {
            return false;
        }

        header->channel = static_cast<uint8_t>(channel);
        header->transaction = transaction;
        header->fragment_index = fragmentIndex;
        header->flags = static_cast<uint8_t>(
            ((offset == 0u) ? USB_BOARD_FRAGMENT_FLAG_FIRST : 0u) |
            (((uint16_t)(offset + dataLength) >= length)
                 ? USB_BOARD_FRAGMENT_FLAG_LAST
                 : 0u));
        header->total_length_le = length;
        header->message_crc16_le = messageCrc;
        if (dataLength != 0u) {
            memcpy(&packet[USB_BOARD_FRAGMENT_HEADER_BYTES],
                   &payload[offset],
                   dataLength);
        }
        if (!send(USB_BOARD_CMD_BULK_FRAGMENT,
                  packet,
                  static_cast<uint8_t>(USB_BOARD_FRAGMENT_HEADER_BYTES +
                                       dataLength))) {
            return false;
        }
        consumeCredit(channel);
        offset = static_cast<uint16_t>(offset + dataLength);
        ++fragmentIndex;
    } while (offset < length);

    return true;
}

bool UsbBoardLink::trySendTelemetry(const uint8_t *payload, uint8_t length)
{
    uint8_t packet[USB_BOARD_FRAGMENT_HEADER_BYTES +
                   USB_BOARD_TELEMETRY_FRAME_BYTES] = {};
    auto *header =
        reinterpret_cast<usb_board_fragment_header_v1_t *>(packet);

    if ((payload == nullptr) ||
        (length != USB_BOARD_TELEMETRY_FRAME_BYTES) ||
        !capsValid ||
        (selectedRole != USB_BOARD_ROLE_USB) ||
        (selectedProfile != USB_BOARD_PROFILE_XINPUT) ||
        ((caps.feature_flags &
          USB_BOARD_CAP_FEATURE_TELEMETRY_HID) == 0u) ||
        (creditFor(USB_BOARD_CHANNEL_TELEMETRY) == 0u)) {
        return false;
    }

    header->channel = USB_BOARD_CHANNEL_TELEMETRY;
    header->transaction = telemetryTransaction;
    header->fragment_index = 0u;
    header->flags = USB_BOARD_FRAGMENT_FLAG_FIRST |
                    USB_BOARD_FRAGMENT_FLAG_LAST;
    header->total_length_le = length;
    header->message_crc16_le = usb_board_crc16_ccitt(payload, length);
    memcpy(&packet[USB_BOARD_FRAGMENT_HEADER_BYTES], payload, length);

    /*
     * A complete MPW2 frame fits one 0x5A fragment.  Unlike sendBulk(), this
     * path never waits for credit, so telemetry cannot add a 50 ms stall to
     * the 1 kHz input loop.
     */
    if (!send(USB_BOARD_CMD_BULK_FRAGMENT,
              packet,
              sizeof(packet))) {
        return false;
    }

    consumeCredit(USB_BOARD_CHANNEL_TELEMETRY);
    ++telemetryTransaction;
    return true;
}

void UsbBoardLink::pumpTelemetry()
{
    const uint32_t now = HAL_GetTick();
    if (nextTelemetryAtMs == 0u) {
        nextTelemetryAtMs = now + kTelemetryIntervalMs;
        return;
    }
    if (static_cast<int32_t>(now - nextTelemetryAtMs) < 0) {
        return;
    }
    nextTelemetryAtMs = now + kTelemetryIntervalMs;

    if (!capsValid ||
        (selectedRole != USB_BOARD_ROLE_USB) ||
        (selectedProfile != USB_BOARD_PROFILE_XINPUT) ||
        !isDeviceMounted() ||
        isDeviceSuspended() ||
        ((caps.feature_flags &
          USB_BOARD_CAP_FEATURE_TELEMETRY_HID) == 0u)) {
        return;
    }

    MonitorPowerFrameV2 frame = {};
    if (MonitorTelemetry_FillPowerFrameV2(&frame)) {
        (void)trySendTelemetry(
            reinterpret_cast<const uint8_t *>(&frame),
            sizeof(frame));
    }
}

void UsbBoardLink::handleEvent(uint8_t command,
                               const uint8_t *payload,
                               uint8_t length)
{
    if(command==UM_BOARD_EVENT && length==20u && payload[0]==UM_BOARD_QUERY &&
       payload[1]==UM_VERSION && um_u32(payload+8)==usbMon.query) {
        const uint32_t now=usbMonitorNow(), peer=um_u32(payload+4);
        if(now-usbMon.queryAt>500000u)return;
        if(peer!=usbMon.session || ((payload[2]^usbMon.enabled)&UM_LATENCY)) {
            usbMon.baseline=0;usbMon.head=usbMon.count=0;
        }
        usbMon.session=peer;usbMon.enabled=payload[2];usbMon.lastReplyMs=HAL_GetTick();
        uint8_t *p=usbMon.clockFrame;memset(p,0,24);
        p[0]=UM_BOARD_CLOCK;p[1]=UM_VERSION;um_put32(p+4,peer);um_put32(p+8,usbMon.query);
        // CH585-minus-STM32 interval, no symmetry assumption. Queue time widens it.
        um_put32(p+12,um_u32(payload+16)-now-4u);
        um_put32(p+16,um_u32(payload+12)-usbMon.queryAt+4u);
        um_put32(p+20,um_u32(payload+12));usbMon.clockPending=1u;
        return;
    }
    if ((command == USB_BOARD_EVT_USB_STATE) &&
        (length == sizeof(usbState))) {
        usb_board_usb_state_v1_t updated = {};
        memcpy(&updated, payload, sizeof(updated));
        usbSubsystemEvidence = true;
        if (updated.device_mounted == 0u && usbState.device_mounted != 0u) {
            requestWebConfigTransportReset();
            /*
             * A real unmount has discarded the CH585 endpoint generation.
             * Drop any saved second fragment and wait for a fresh credit from
             * the newly configured interface.
             */
                    credits[USB_BOARD_CHANNEL_WEBCONFIG] = 0u;
        } else if (updated.device_suspended != 0u) {
            /*
             * Suspend is only a pause. Withhold new complete reports but keep
             * a credit-owned partial transmit so it can finish with the same
             * transaction, fragment index and payload after resume.
             */
            credits[USB_BOARD_CHANNEL_WEBCONFIG] = 0u;
        }
        usbState = updated;
    } else if ((command == USB_BOARD_EVT_BULK_CREDIT) &&
               (length == sizeof(usb_board_bulk_credit_v1_t))) {
        usb_board_bulk_credit_v1_t update = {};
        memcpy(&update, payload, sizeof(update));
        if (update.channel == USB_BOARD_CHANNEL_WEBCONFIG) {
            /*
             * WebConfig is pull-only. Ignore any stale event left across a
             * profile/reset boundary or emitted by an older CH585 image.
             */
            return;
        }
        const uint8_t boundedCredits =
            update.channel < sizeof(credits)
                ? ((update.credits > bulkCreditLimit(update.channel))
                       ? bulkCreditLimit(update.channel)
                       : update.credits)
                : 0u;
        if (update.channel < sizeof(credits)) {
            credits[update.channel] = boundedCredits;
        }
    } else if ((command == USB_BOARD_EVT_FAULT) && (length != 0u)) {
        if(selectedRole == USB_BOARD_ROLE_USB && length == 14u && !g_usb_input_fault[0]) {
            g_usb_input_fault[1]=HAL_GetTick();
            g_usb_input_fault[2]=payload[0]; g_usb_input_fault[3]=payload[1];
            for(unsigned i=0;i<3u;++i) g_usb_input_fault[4u+i]=whf_u32(payload+2u+4u*i);
            g_usb_input_fault[0]=0x55494631u;
            SCB_CleanDCache_by_Addr((uint32_t *)g_usb_input_fault,sizeof(g_usb_input_fault));
            __DSB();
        }
        usbState.last_fault = payload[0];
        if (s_hsReady) USBBoardLink_HsTransportFault();
        if (fastApplication || USBBoardLinkPort_IsFastApplication()) {
            fastDataPlaneFaultPending = true;
            fastDataPlaneFault = payload[0];
        }
    } else if ((command == USB_BOARD_EVT_BULK_FRAGMENT) &&
               (length >= USB_BOARD_FRAGMENT_HEADER_BYTES)) {
        usb_board_fragment_header_v1_t header = {};
        memcpy(&header, payload, sizeof(header));
        const auto channel =
            static_cast<usb_board_channel_t>(header.channel);
        const uint8_t channelIndex = header.channel;
        const uint8_t dataLength =
            static_cast<uint8_t>(length - USB_BOARD_FRAGMENT_HEADER_BYTES);

        if ((channelIndex == 0u) ||
            (channelIndex >= sizeof(receiveCredits)) ||
            (receiveCredits[channelIndex] == 0u)) {
            usbState.last_fault = USB_BOARD_STATUS_QUEUE_FULL;
            return;
        }
        --receiveCredits[channelIndex];

        uint8_t *rxBuffer = nullptr;
        uint16_t rxCapacity = 0u;
        uint16_t *rxLength = nullptr;
        uint16_t *rxExpectedLength = nullptr;
        uint16_t *rxCrc = nullptr;
        uint8_t *rxTransaction = nullptr;
        uint8_t *rxExpectedFragment = nullptr;
        bool *rxActive = nullptr;

        if (header.channel == USB_BOARD_CHANNEL_NETWORK) {
            rxBuffer = s_networkRx;
            rxCapacity = sizeof(s_networkRx);
            rxLength = &s_networkRxLength;
            rxExpectedLength = &s_networkRxExpectedLength;
            rxCrc = &s_networkRxCrc;
            rxTransaction = &s_networkRxTransaction;
            rxExpectedFragment = &s_networkRxExpectedFragment;
            rxActive = &s_networkRxActive;
        } else {
            returnReceiveCredit(channel);
            return;
        }
        if (header.total_length_le > rxCapacity) {
            *rxActive = false;
            returnReceiveCredit(channel);
            return;
        }
        if ((header.flags & USB_BOARD_FRAGMENT_FLAG_FIRST) != 0u) {
            *rxLength = 0u;
            *rxExpectedLength = header.total_length_le;
            *rxCrc = header.message_crc16_le;
            *rxTransaction = header.transaction;
            *rxExpectedFragment = 0u;
            *rxActive = true;
        }
        if (!*rxActive ||
            header.transaction != *rxTransaction ||
            header.fragment_index != *rxExpectedFragment ||
            (static_cast<uint32_t>(*rxLength) + dataLength >
             *rxExpectedLength)) {
            *rxActive = false;
            returnReceiveCredit(channel);
            return;
        }
        memcpy(&rxBuffer[*rxLength],
               &payload[USB_BOARD_FRAGMENT_HEADER_BYTES],
               dataLength);
        *rxLength = static_cast<uint16_t>(*rxLength + dataLength);
        ++*rxExpectedFragment;
        if ((header.flags & USB_BOARD_FRAGMENT_FLAG_LAST) != 0u) {
            const bool complete =
                (*rxLength == *rxExpectedLength) &&
                (usb_board_crc16_ccitt(rxBuffer, *rxLength) == *rxCrc);
            if (complete &&
                header.channel == USB_BOARD_CHANNEL_NETWORK &&
                s_networkRxCallback != nullptr) {
                s_networkRxCallback(rxBuffer, *rxLength);

            }
            *rxActive = false;
        }
        returnReceiveCredit(channel);
    }
}

void UsbBoardLink::process()
{
    {
        LinkTransactionGuard transaction(transactionActive);
        if (!transaction) {
            return;
        }
        (void)drainEventsLocked(kEventDrainTimeoutMs);
    }
    if (USBBoardLinkPort_HasReleaseFault()) {
        if (!s_releaseFaultReported) {
            s_releaseFaultReported = true;
            usbState.last_fault = USB_BOARD_STATUS_INTERNAL_ERROR;
            fastDataPlaneFault = USB_BOARD_STATUS_INTERNAL_ERROR;
            fastDataPlaneFaultPending = fastApplication;
            USBBoardLink_HsTransportFault();
        }
        return;
    }
    serviceWebConfigTransportReset();
    if (s_hsReady) {
        receiveFastReports();
        const uint16_t size = whf_prepare(&s_hsLink, s_hsBlock);
        LinkTransactionGuard transaction(transactionActive);
        if (transaction && size && USBBoardLinkPort_SendWebHidBlock(s_hsBlock, size))
            whf_commit(&s_hsLink, s_hsBlock);
    }
    flushReceiveCredits();
    pumpMonitor();
    pumpTelemetry();
}

void UsbBoardLink::shutdown()
{
    s_releaseFaultReported = false;
    usbMon=UsbSourceMonitor{};
    s_hsReady = false; whf_init(&s_hsLink, 0u);
    s_txBulkCapable=false;s_txBulkRead={};
    USBBoardLinkPort_Shutdown();
    selectedRole = USB_BOARD_ROLE_NONE;
    selectedProfile = USB_BOARD_PROFILE_NONE;
    memset(&caps, 0, sizeof(caps));
    memset(&usbState, 0, sizeof(usbState));
    memset(credits, 0, sizeof(credits));
    memset(receiveCredits, 0, sizeof(receiveCredits));
    memset(receiveCreditDirty, 0, sizeof(receiveCreditDirty));
    webConfigTransportState = WebConfigTransportState::Ready;
    telemetryTransaction = 0u;
    controlTransaction = 0u;
    nextTelemetryAtMs = 0u;
    roleLocked = false;
    capsValid = false;
    fastApplication = false;
    fastDataPlaneFaultPending = false;
    fastDataPlaneFault = USB_BOARD_STATUS_OK;
    transactionActive = false;
    s_networkRxActive = false;
    s_networkRxLength = 0u;
    s_networkRxExpectedLength = 0u;



    MonitorTelemetry_SetCh585Status(0u, 0u, 0u, 0u);
}

bool UsbBoardLink_SelectRoleCallback(Ch585Role role)
{
    return USB_BOARD_LINK.selectRole(
        static_cast<usb_board_role_t>(static_cast<uint8_t>(role)),
        CH585_ROLE_RESPONSE_TIMEOUT_MS);
}

extern "C" bool UsbBoardLink_NetworkSend(const uint8_t *data,
                                          uint16_t length)
{
    static uint8_t transaction = 0u;
    if (!USB_BOARD_LINK.isRoleLocked() ||
        !USB_BOARD_LINK.isCompatible() ||
        (USB_BOARD_LINK.role() != USB_BOARD_ROLE_MAINTENANCE)) {
        return false;
    }
    return USB_BOARD_LINK.sendBulk(USB_BOARD_CHANNEL_NETWORK,
                                   transaction++,
                                   data,
                                   length);
}

extern "C" void UsbBoardLink_SetNetworkReceiveCallback(
    usb_board_link_network_rx_callback_t callback)
{
    s_networkRxCallback = callback;
}

void USBBoardLink_HsAcceptBlock(const uint8_t *data, uint16_t length)
{
    if (!whf_accept(&s_hsLink, data, length)) {
        if (g_webhid_link_fault[0] == 0u) {
            g_webhid_link_fault[1] = HAL_GetTick();
            g_webhid_link_fault[2] = length;
            g_webhid_link_fault[3] = kWebHidSpiHz;
            const uint32_t counters[] = {
                s_hsLink.epoch, s_hsLink.tx_block, s_hsLink.rx_block,
                s_hsLink.tx_produced, s_hsLink.tx_sent, s_hsLink.tx_acked,
                s_hsLink.peer_limit, s_hsLink.rx_accepted, s_hsLink.rx_released,
                s_hsLink.crc_errors, s_hsLink.protocol_errors, s_hsLink.failed,
            };
            for (unsigned i = 0; i < 12u; ++i) g_webhid_link_fault[4u+i] = counters[i];
            if (length >= WHF_HEADER_BYTES && length <= WHF_BLOCK_BYTES) {
                for (unsigned i = 0; i < 8u; ++i)
                    g_webhid_link_fault[16u+i] = whf_u32(data + i*4u);
                g_webhid_link_fault[24] = whf_crc(data, length);
            }
            __DMB();
            g_webhid_link_fault[0] = 0x57484632u;
            SCB_CleanDCache_by_Addr((uint32_t *)g_webhid_link_fault, sizeof(g_webhid_link_fault));
            __DSB();
        }
        USBBoardLink_HsTransportFault();
    }
}

void USBBoardLink_HsTransportFault()
{
    s_hsLink.failed = 1u;
    s_hsReady = false;
    s_hsSessionInvalid = true;
    USB_BOARD_LINK.requestWebConfigTransportReset();
}

extern "C" bool UsbBoardLink_WebConfigTakeFault(void)
{
    const bool invalid = s_hsSessionInvalid;
    s_hsSessionInvalid = false;
    return invalid;
}

extern "C" bool UsbBoardLink_WebConfigSendReport(const uint8_t report[WEBHID_REPORT_BYTES])
{
    return s_hsReady && USB_BOARD_LINK.isDeviceMounted() &&
           !USB_BOARD_LINK.isDeviceSuspended() && whf_enqueue(&s_hsLink, report);
}

extern "C" void UsbBoardLink_WebConfigResetTransport(void)
{
    USB_BOARD_LINK.requestWebConfigTransportReset();
}

extern "C" void UsbBoardLink_SetWebConfigReceiveCallback(
    usb_board_link_webconfig_rx_callback_t callback)
{
    s_webConfigRxCallback = callback;

}

extern "C" void UsbBoardLink_Process(void)
{
    USB_BOARD_LINK.process();
}

void UsbBoardLink::monitorSample(uint32_t trigger,uint32_t complete) {
    if(!(usbMon.enabled&UM_LATENCY)) {usbMon.sampleValid=0u;return;}
    usbMon.ready=DWT->CYCCNT;
    const uint32_t scale=SystemCoreClock/1000000u;
    usbMon.sampleUs=usbMon.clock.observe(usbMon.ready,HAL_GetTick(),scale)-(usbMon.ready-trigger)/scale;
    usbMon.trigger=trigger;usbMon.complete=complete;usbMon.sampleValid=1u;
}
bool UsbBoardLink::tryMonitorSend(const uint8_t *payload,uint8_t length) {
    LinkTransactionGuard transaction(transactionActive);
    if(!transaction || USBBoardLinkPort_HasEvent())return false;
    uint8_t frame[64],size=0;
    return usb_board_link_encode(UM_BOARD_COMMAND,payload,length,frame,sizeof(frame),&size) &&
           USBBoardLinkPort_Send(frame,size);
}
void UsbBoardLink::pumpMonitor() {
    if(!capsValid || selectedRole!=USB_BOARD_ROLE_USB || selectedProfile!=USB_BOARD_PROFILE_XINPUT ||
       !isDeviceMounted() || isDeviceSuspended()) { usbMon.enabled=usbMon.baseline=0u;return; }
    // 2.2 is the first USB-side monitor-capable firmware. Old firmware gets no new commands.
    if(caps.firmware_major<2u || (caps.firmware_major==2u && caps.firmware_minor<2u))return;
    const uint32_t now=HAL_GetTick();
    if(now-usbMon.lastReplyMs>3000u)usbMon.enabled=usbMon.baseline=0u;
    if(usbMon.count) {
        uint8_t *p=usbMon.edges[usbMon.head];
        // Avoid ambiguous matches after an 8-bit input sequence wrap.
        if(usbMonitorNow()-um_u32(p+20)>UM_MATCH_US) {
            p[2]|=UM_UNMATCHED;
        }
        if(tryMonitorSend(p,40)) { usbMon.head=(usbMon.head+1u)%8u;usbMon.count--; }
        return;
    }
    if(usbMon.clockPending) {
        if(!(usbMon.enabled&UM_LATENCY) || tryMonitorSend(usbMon.clockFrame,24))usbMon.clockPending=0u;
        return;
    }
    if(now-usbMon.lastQueryMs>=1000u) {
        uint8_t p[16]={UM_BOARD_QUERY,UM_VERSION};um_put32(p+8,++usbMon.query);um_put32(p+12,usbMon.drops);
        usbMon.queryAt=usbMonitorNow();
        if(tryMonitorSend(p,sizeof(p)))usbMon.lastQueryMs=now;
    }
}
