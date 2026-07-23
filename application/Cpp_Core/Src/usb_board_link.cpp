#include "usb_board_link.hpp"
#include "usb_board_link_c_api.h"

#include <string.h>

#include "monitor_telemetry.hpp"
#include "system_logger.h"
#include "stm32h7xx_hal.h"
#include "usb_board_link_codec.h"
#include "usb_board_link_port.hpp"

namespace {

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

static bool supportedRole(usb_board_role_t role)
{
    return (role == USB_BOARD_ROLE_RF) ||
           (role == USB_BOARD_ROLE_USB) ||
           (role == USB_BOARD_ROLE_MAINTENANCE);
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
    LinkTransactionGuard transaction(transactionActive);

    if (responseLength != nullptr) {
        *responseLength = 0u;
    }
    if (!transaction) {
        return false;
    }

    /*
     * A response can assert W_INT at the exact retry boundary. Drain it
     * before clocking a retry so a late ROLE_SELECTED frame is never shifted
     * out underneath a second SELECT_ROLE request.
     */
    while (USBBoardLinkPort_HasEvent()) {
        uint8_t pending[USB_BOARD_LINK_MAX_FRAME_BYTES] = {};
        uint8_t pendingLength = 0u;
        usb_board_link_frame_t decoded = {};
        if (!USBBoardLinkPort_ReadEvent(pending,
                                        sizeof(pending),
                                        &pendingLength) ||
            !usb_board_link_decode(pending, pendingLength, &decoded)) {
            break;
        }
        handleEvent(decoded.command, decoded.payload, decoded.length);
        if (decoded.command != expectedEvent) {
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
            handleEvent(decoded.command, decoded.payload, decoded.length);
            if (decoded.command != expectedEvent) {
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
    return true;
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
    return sendLocked(command, payload, payloadLength);
}

bool UsbBoardLink::selectRole(usb_board_role_t role, uint32_t timeoutMs)
{
    usb_board_role_select_v1_t request = {static_cast<uint8_t>(role)};
    usb_board_role_selected_v1_t response = {};
    uint8_t responseLength = 0u;

    if (!supportedRole(role) || roleLocked) {
        return roleLocked && (selectedRole == role);
    }
    if (!USBBoardLinkPort_Init() ||
        !transact(USB_BOARD_CMD_SELECT_ROLE,
                  &request,
                  sizeof(request),
                  USB_BOARD_EVT_ROLE_SELECTED,
                  &response,
                  sizeof(response),
                  &responseLength,
                  timeoutMs) ||
        (responseLength != sizeof(response)) ||
        (response.role != static_cast<uint8_t>(role)) ||
        (response.status != USB_BOARD_STATUS_OK)) {
        return false;
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
    /*
     * Role ACK is the bootstrap boundary. The CH585 enters its USB loop only
     * after this transaction has completed, so capability discovery is
     * intentionally performed by USBDriver after bootstrap lock.
     */
    return true;
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
    if (capsValid) {
        MonitorTelemetry_SetCh585Status(
            static_cast<uint8_t>(selectedRole),
            caps.firmware_major,
            caps.firmware_minor,
            caps.firmware_patch);
    }
    if (capsValid && !grantInitialReceiveCredits()) {
        capsValid = false;
    }
    return capsValid;
}

bool UsbBoardLink::grantInitialReceiveCredits()
{
    memset(receiveCredits, 0, sizeof(receiveCredits));
    memset(receiveCreditDirty, 0, sizeof(receiveCreditDirty));
    for (uint8_t channel = USB_BOARD_CHANNEL_USB_DEVICE;
         channel <= USB_BOARD_CHANNEL_AUTH;
         ++channel) {
        receiveCredits[channel] = USB_BOARD_BULK_CREDIT_WINDOW;
        receiveCreditDirty[channel] = 1u;
    }
    flushReceiveCredits();
    for (uint8_t channel = USB_BOARD_CHANNEL_USB_DEVICE;
         channel <= USB_BOARD_CHANNEL_AUTH;
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
    if (receiveCredits[index] < USB_BOARD_BULK_CREDIT_WINDOW) {
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
         channel <= USB_BOARD_CHANNEL_AUTH;
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

    if (!capsValid ||
        ((profile == USB_BOARD_PROFILE_XINPUT) &&
         ((caps.feature_flags &
           USB_BOARD_CAP_FEATURE_TELEMETRY_HID) == 0u)) ||
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
    telemetryTransaction = 0u;
    nextTelemetryAtMs = HAL_GetTick() + kTelemetryIntervalMs;
    return true;
}

bool UsbBoardLink::submitInput(uint32_t processedActionMask,
                               uint16_t ageUs,
                               uint8_t batteryCode,
                               bool batteryValid)
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
                             (batteryValid
                                  ? USB_BOARD_INPUT_FLAG_BATTERY_VALID
                                  : 0u));
    input.action_mask_le = processedActionMask;
    input.age_us_le = ageUs;
    input.battery_code = batteryCode;
    input.crc8 = usb_board_input_crc8(
        reinterpret_cast<const uint8_t *>(&input),
        static_cast<uint8_t>(sizeof(input) - 1u));
    return send(USB_BOARD_CMD_INPUT_STATE, &input, sizeof(input));
}

bool UsbBoardLink::sendControl(usb_board_control_opcode_t opcode,
                               const uint8_t *payload,
                               uint8_t length,
                               uint8_t *responseData,
                               uint8_t responseCapacity,
                               uint8_t *responseDataLength)
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

    if (!transact(USB_BOARD_CMD_USB_CONTROL,
                  &request,
                  static_cast<uint8_t>(USB_BOARD_CONTROL_HEADER_BYTES +
                                       length),
                  USB_BOARD_EVT_USB_CONTROL,
                  &response,
                  sizeof(response),
                  &responseLength,
                  kControlTimeoutMs) ||
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

bool UsbBoardLink::sendBulk(usb_board_channel_t channel,
                            uint8_t transaction,
                            const uint8_t *payload,
                            uint16_t length)
{
    uint16_t offset = 0u;
    uint8_t fragmentIndex = 0u;
    const uint16_t messageCrc = usb_board_crc16_ccitt(payload, length);
    const uint8_t channelIndex = static_cast<uint8_t>(channel);

    if (!capsValid || (selectedRole == USB_BOARD_ROLE_RF) ||
        (channelIndex == 0u) || (channelIndex >= sizeof(credits)) ||
        (length > USB_BOARD_BULK_MESSAGE_MAX_BYTES) ||
        ((length != 0u) && (payload == nullptr))) {
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

        const uint32_t creditWaitStarted = HAL_GetTick();
        while (creditFor(channel) == 0u) {
            process();
            if ((HAL_GetTick() - creditWaitStarted) >=
                kBulkCreditWaitMs) {
                return false;
            }
            HAL_Delay(1u);
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
    if ((command == USB_BOARD_EVT_USB_STATE) &&
        (length == sizeof(usbState))) {
        memcpy(&usbState, payload, sizeof(usbState));
    } else if ((command == USB_BOARD_EVT_BULK_CREDIT) &&
               (length == sizeof(usb_board_bulk_credit_v1_t))) {
        usb_board_bulk_credit_v1_t update = {};
        memcpy(&update, payload, sizeof(update));
        if (update.channel < sizeof(credits)) {
            credits[update.channel] = update.credits;
        }
    } else if ((command == USB_BOARD_EVT_FAULT) && (length != 0u)) {
        usbState.last_fault = payload[0];
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

        if (header.channel != USB_BOARD_CHANNEL_NETWORK ||
            header.total_length_le > sizeof(s_networkRx)) {
            s_networkRxActive = false;
            returnReceiveCredit(channel);
            return;
        }
        if ((header.flags & USB_BOARD_FRAGMENT_FLAG_FIRST) != 0u) {
            s_networkRxLength = 0u;
            s_networkRxExpectedLength = header.total_length_le;
            s_networkRxCrc = header.message_crc16_le;
            s_networkRxTransaction = header.transaction;
            s_networkRxExpectedFragment = 0u;
            s_networkRxActive = true;
        }
        if (!s_networkRxActive ||
            header.transaction != s_networkRxTransaction ||
            header.fragment_index != s_networkRxExpectedFragment ||
            (static_cast<uint32_t>(s_networkRxLength) + dataLength >
             s_networkRxExpectedLength)) {
            s_networkRxActive = false;
            returnReceiveCredit(channel);
            return;
        }
        memcpy(&s_networkRx[s_networkRxLength],
               &payload[USB_BOARD_FRAGMENT_HEADER_BYTES],
               dataLength);
        s_networkRxLength =
            static_cast<uint16_t>(s_networkRxLength + dataLength);
        ++s_networkRxExpectedFragment;
        if ((header.flags & USB_BOARD_FRAGMENT_FLAG_LAST) != 0u) {
            if ((s_networkRxLength == s_networkRxExpectedLength) &&
                (usb_board_crc16_ccitt(s_networkRx, s_networkRxLength) ==
                 s_networkRxCrc) &&
                (s_networkRxCallback != nullptr)) {
                s_networkRxCallback(s_networkRx, s_networkRxLength);
            }
            s_networkRxActive = false;
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
    flushReceiveCredits();
    pumpTelemetry();
}

void UsbBoardLink::shutdown()
{
    USBBoardLinkPort_Shutdown();
    selectedRole = USB_BOARD_ROLE_NONE;
    selectedProfile = USB_BOARD_PROFILE_NONE;
    memset(&caps, 0, sizeof(caps));
    memset(&usbState, 0, sizeof(usbState));
    memset(credits, 0, sizeof(credits));
    memset(receiveCredits, 0, sizeof(receiveCredits));
    memset(receiveCreditDirty, 0, sizeof(receiveCreditDirty));
    telemetryTransaction = 0u;
    controlTransaction = 0u;
    nextTelemetryAtMs = 0u;
    roleLocked = false;
    capsValid = false;
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
        5u);
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

extern "C" void UsbBoardLink_Process(void)
{
    USB_BOARD_LINK.process();
}
