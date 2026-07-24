#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "monitor_telemetry.hpp"
#include "usb_board_link.hpp"
#include "usb_board_link_c_api.h"
#include "usb_board_link_codec.h"
#include "usb_board_link_port.hpp"
#include "webhid_protocol.h"

namespace {

struct QueuedEvent {
    uint8_t bytes[USB_BOARD_LINK_MAX_FRAME_BYTES];
    uint8_t length;
};

struct AcceptedFragment {
    usb_board_fragment_header_v1_t header;
    uint8_t data[USB_BOARD_FRAGMENT_DATA_BYTES];
    uint8_t length;
};

QueuedEvent events[16] = {};
uint8_t eventHead;
uint8_t eventTail;
AcceptedFragment accepted[16] = {};
uint8_t acceptedCount;
uint8_t secondFragmentFailures;
bool failSecondFragment;
bool suspendAfterFirstFragment;
uint8_t clearFaultCount;
uint32_t nowMs;

void queueEvent(uint8_t command, const void *payload, uint8_t length)
{
    assert(static_cast<uint8_t>(eventTail - eventHead) <
           static_cast<uint8_t>(sizeof(events) / sizeof(events[0])));
    QueuedEvent &event =
        events[eventTail % (sizeof(events) / sizeof(events[0]))];
    assert(usb_board_link_encode(
        command,
        payload,
        length,
        event.bytes,
        sizeof(event.bytes),
        &event.length));
    ++eventTail;
}

void grantWebConfigCredit(uint8_t credits)
{
    const usb_board_bulk_credit_v1_t update = {
        USB_BOARD_CHANNEL_WEBCONFIG,
        credits,
    };
    queueEvent(USB_BOARD_EVT_BULK_CREDIT, &update, sizeof(update));
    USB_BOARD_LINK.process();
}

void queueUsbState(bool mounted, bool suspended)
{
    const usb_board_usb_state_v1_t state = {
        static_cast<uint8_t>(mounted ? 1u : 0u),
        static_cast<uint8_t>(suspended ? 1u : 0u),
        1u,
        0u,
        USB_BOARD_PROFILE_WEB_CONFIG,
        USB_BOARD_STATUS_OK,
    };
    queueEvent(USB_BOARD_EVT_USB_STATE, &state, sizeof(state));
    USB_BOARD_LINK.process();
}

void initializeMaintenanceProfile()
{
    assert(USB_BOARD_LINK.selectRole(
        USB_BOARD_ROLE_MAINTENANCE, 20u));
    assert(USB_BOARD_LINK.getCapabilities());
    assert(USB_BOARD_LINK.setProfile(
        USB_BOARD_PROFILE_WEB_CONFIG));
    grantWebConfigCredit(1u);
}

void fillReport(uint8_t seed, uint8_t report[WEBHID_REPORT_BYTES])
{
    for (uint8_t index = 0u; index < WEBHID_REPORT_BYTES; ++index) {
        report[index] = static_cast<uint8_t>(seed ^ index);
    }
}

void testSecondFragmentFailureResumesWithoutNewCredit()
{
    uint8_t report[WEBHID_REPORT_BYTES];
    uint8_t original[WEBHID_REPORT_BYTES];
    uint8_t rebuilt[WEBHID_REPORT_BYTES] = {};
    fillReport(0xA6u, report);
    memcpy(original, report, sizeof(original));

    failSecondFragment = true;
    assert(!UsbBoardLink_WebConfigSendReport(report));
    assert(acceptedCount == 1u);
    assert(accepted[0].header.fragment_index == 0u);
    assert((accepted[0].header.flags &
            USB_BOARD_FRAGMENT_FLAG_FIRST) != 0u);
    assert(secondFragmentFailures == 2u);

    /*
     * sendLocked() exhausted both SPI attempts for fragment 1.  No credit is
     * returned because CH585 still owns the partial report; retry must resume
     * fragment 1 directly.
    */
    failSecondFragment = false;
    report[17] ^= 0xFFu;
    assert(UsbBoardLink_WebConfigSendReport(report));
    assert(acceptedCount == 2u);
    assert(accepted[1].header.fragment_index == 1u);
    assert((accepted[1].header.flags &
            USB_BOARD_FRAGMENT_FLAG_FIRST) == 0u);
    assert((accepted[1].header.flags &
            USB_BOARD_FRAGMENT_FLAG_LAST) != 0u);
    assert(accepted[0].header.transaction ==
           accepted[1].header.transaction);
    memcpy(rebuilt, accepted[0].data, accepted[0].length);
    memcpy(&rebuilt[accepted[0].length],
           accepted[1].data,
           accepted[1].length);
    assert(memcmp(rebuilt, original, sizeof(original)) == 0);

    /* A new report still requires the next complete-report credit. */
    fillReport(0x5Cu, report);
    assert(!UsbBoardLink_WebConfigSendReport(report));
    assert(acceptedCount == 2u);
    grantWebConfigCredit(1u);
    assert(UsbBoardLink_WebConfigSendReport(report));
    assert(acceptedCount == 4u);
    assert(accepted[2].header.transaction ==
           static_cast<uint8_t>(accepted[0].header.transaction + 1u));
}

void testExplicitSessionResetDiscardsPartialAndWaitsForCredit()
{
    uint8_t oldReport[WEBHID_REPORT_BYTES];
    uint8_t newReport[WEBHID_REPORT_BYTES];
    const uint8_t acceptedBefore = acceptedCount;

    grantWebConfigCredit(1u);
    fillReport(0x31u, oldReport);
    failSecondFragment = true;
    assert(!UsbBoardLink_WebConfigSendReport(oldReport));
    assert(acceptedCount == static_cast<uint8_t>(acceptedBefore + 1u));

    UsbBoardLink_WebConfigResetTransport();
    USB_BOARD_LINK.process();
    assert(clearFaultCount == 1u);

    fillReport(0xE2u, newReport);
    failSecondFragment = false;
    assert(!UsbBoardLink_WebConfigSendReport(newReport));
    assert(acceptedCount == static_cast<uint8_t>(acceptedBefore + 1u));

    grantWebConfigCredit(1u);
    assert(UsbBoardLink_WebConfigSendReport(newReport));
    assert(acceptedCount == static_cast<uint8_t>(acceptedBefore + 3u));
    assert(accepted[acceptedBefore + 1u].header.fragment_index == 0u);
    assert(memcmp(accepted[acceptedBefore + 1u].data,
                  newReport,
                  accepted[acceptedBefore + 1u].length) == 0);
}

void testSuspendBetweenFragmentsCancelsOldGeneration()
{
    uint8_t oldReport[WEBHID_REPORT_BYTES];
    uint8_t resumedReport[WEBHID_REPORT_BYTES];
    const uint8_t acceptedBefore = acceptedCount;

    grantWebConfigCredit(1u);
    fillReport(0x48u, oldReport);
    suspendAfterFirstFragment = true;
    assert(!UsbBoardLink_WebConfigSendReport(oldReport));
    assert(acceptedCount == static_cast<uint8_t>(acceptedBefore + 1u));
    assert(accepted[acceptedBefore].header.fragment_index == 0u);

    fillReport(0x94u, resumedReport);
    assert(!UsbBoardLink_WebConfigSendReport(resumedReport));
    queueUsbState(true, false);
    grantWebConfigCredit(1u);
    assert(UsbBoardLink_WebConfigSendReport(resumedReport));
    assert(acceptedCount == static_cast<uint8_t>(acceptedBefore + 3u));
    assert(accepted[acceptedBefore + 1u].header.fragment_index == 0u);
    assert(memcmp(accepted[acceptedBefore + 1u].data,
                  resumedReport,
                  accepted[acceptedBefore + 1u].length) == 0);
}

} // namespace

extern "C" uint32_t HAL_GetTick(void)
{
    return nowMs;
}

extern "C" void HAL_Delay(uint32_t delayMs)
{
    nowMs += delayMs;
}

void MonitorTelemetry_SetCh585Status(uint8_t,
                                     uint8_t,
                                     uint8_t,
                                     uint8_t)
{
}

bool MonitorTelemetry_FillPowerFrameV2(MonitorPowerFrameV2 *)
{
    return false;
}

bool USBBoardLinkPort_Init()
{
    return true;
}

void USBBoardLinkPort_Shutdown()
{
}

bool USBBoardLinkPort_Send(const uint8_t *frame, uint8_t frameLength)
{
    usb_board_link_frame_t decoded = {};
    assert(usb_board_link_decode(frame, frameLength, &decoded));

    switch (decoded.command) {
    case USB_BOARD_CMD_SELECT_ROLE: {
        const usb_board_role_selected_v1_t response = {
            decoded.payload[0],
            USB_BOARD_STATUS_OK,
        };
        queueEvent(
            USB_BOARD_EVT_ROLE_SELECTED, &response, sizeof(response));
        return true;
    }
    case USB_BOARD_CMD_GET_CAPS: {
        const usb_board_caps_v1_t response = {
            USB_BOARD_LINK_VERSION,
            static_cast<uint8_t>(USB_BOARD_CAP_ROLE_MAINTENANCE),
            static_cast<uint16_t>(USB_BOARD_CAP_PROFILE_WEB_CONFIG),
            USB_BOARD_LINK_MAX_FRAME_BYTES,
            USB_BOARD_INPUT_V1_BYTES,
            2u,
            0u,
            0u,
            static_cast<uint8_t>(
                USB_BOARD_CAP_FEATURE_WEBHID_V1 |
                USB_BOARD_CAP_FEATURE_LOCAL_AUTH |
                USB_BOARD_CAP_FEATURE_CONTROL_V1),
        };
        queueEvent(USB_BOARD_EVT_CAPS, &response, sizeof(response));
        return true;
    }
    case USB_BOARD_CMD_SET_PROFILE: {
        const usb_board_profile_set_v1_t response = {
            decoded.payload[0],
            USB_BOARD_STATUS_OK,
        };
        queueEvent(
            USB_BOARD_EVT_PROFILE_SET, &response, sizeof(response));
        return true;
    }
    case USB_BOARD_CMD_BULK_FRAGMENT: {
        usb_board_fragment_header_v1_t header = {};
        assert(decoded.length >= USB_BOARD_FRAGMENT_HEADER_BYTES);
        memcpy(&header, decoded.payload, sizeof(header));
        if (failSecondFragment && header.fragment_index == 1u) {
            ++secondFragmentFailures;
            return false;
        }
        assert(acceptedCount <
               static_cast<uint8_t>(
                   sizeof(accepted) / sizeof(accepted[0])));
        AcceptedFragment &fragment = accepted[acceptedCount++];
        fragment.header = header;
        fragment.length = static_cast<uint8_t>(
            decoded.length - USB_BOARD_FRAGMENT_HEADER_BYTES);
        memcpy(fragment.data,
               &decoded.payload[USB_BOARD_FRAGMENT_HEADER_BYTES],
               fragment.length);
        if (suspendAfterFirstFragment && header.fragment_index == 0u) {
            const usb_board_usb_state_v1_t state = {
                1u,
                1u,
                1u,
                0u,
                USB_BOARD_PROFILE_WEB_CONFIG,
                USB_BOARD_STATUS_OK,
            };
            suspendAfterFirstFragment = false;
            queueEvent(USB_BOARD_EVT_USB_STATE, &state, sizeof(state));
        }
        return true;
    }
    case USB_BOARD_CMD_USB_CONTROL: {
        usb_board_control_request_v1_t request = {};
        usb_board_control_response_v1_t response = {};
        assert(decoded.length >= USB_BOARD_CONTROL_HEADER_BYTES);
        memcpy(&request, decoded.payload, decoded.length);
        response.header = request.header;
        response.header.status = USB_BOARD_STATUS_OK;
        response.header.data_length = 0u;
        if (request.header.opcode == USB_BOARD_CONTROL_CLEAR_FAULT) {
            ++clearFaultCount;
        }
        queueEvent(
            USB_BOARD_EVT_USB_CONTROL,
            &response,
            USB_BOARD_CONTROL_HEADER_BYTES);
        return true;
    }
    default:
        return true;
    }
}

bool USBBoardLinkPort_Transact(const uint8_t *,
                               uint8_t,
                               uint8_t *,
                               uint8_t,
                               uint8_t *,
                               uint32_t)
{
    return false;
}

bool USBBoardLinkPort_HasEvent()
{
    return eventHead != eventTail;
}

bool USBBoardLinkPort_ReadEvent(uint8_t *response,
                                uint8_t responseCapacity,
                                uint8_t *responseLength)
{
    if (!USBBoardLinkPort_HasEvent()) {
        return false;
    }
    QueuedEvent &event =
        events[eventHead % (sizeof(events) / sizeof(events[0]))];
    if (event.length > responseCapacity) {
        return false;
    }
    memcpy(response, event.bytes, event.length);
    *responseLength = event.length;
    ++eventHead;
    return true;
}

int main()
{
    initializeMaintenanceProfile();
    testSecondFragmentFailureResumesWithoutNewCredit();
    testExplicitSessionResetDiscardsPartialAndWaitsForCredit();
    testSuspendBetweenFragmentsCancelsOldGeneration();
    USB_BOARD_LINK.shutdown();
    return 0;
}
