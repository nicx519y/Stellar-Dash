#include "usb_board_link.h"

#include <string.h>

#include "usb_auth.h"
#include "usb_device.h"
#include "usb_host.h"
#include "usb_management_control.h"
#include "usb_net_bridge.h"
#include "usb_profiles.h"
#include "webhid_protocol.h"

#define USB_LINK_INPUT_VERSION_MASK 0xF0u

typedef struct
{
    uint8_t active;
    uint8_t channel;
    uint8_t transaction;
    uint8_t fragment_index;
    uint16_t length;
    uint16_t offset;
    uint16_t crc;
    uint8_t data[USB_BOARD_BULK_MESSAGE_MAX_BYTES];
} usb_board_outbound_t;

static usb_board_link_parser_t s_parser;
static usb_board_role_t s_role;
static uint8_t s_ready;
static uint8_t s_last_fault;
static uint8_t s_state_dirty;
static uint8_t s_state_valid;
static uint8_t s_port_fault_pending;
/* Keep the application hand-off half-duplex until STM32 has explicitly
 * requested CAPS.  Publishing USB_STATE/credit/fault events immediately
 * after ROLE_SELECTED can switch SPI0 to TX while STM32 is issuing its first
 * GET_CAPS write, losing that command at the FIFO-to-DMA ownership boundary. */
static uint8_t s_caps_requested;
static usb_board_usb_state_v1_t s_last_state;
static uint8_t s_tx_credits[USB_BOARD_CHANNEL_SLOTS];
static uint8_t s_credit_dirty_mask;
static uint8_t s_webconfig_credit_paused;
static uint8_t s_next_transaction[USB_BOARD_CHANNEL_SLOTS];
static usb_board_outbound_t s_outbound;

static void clear_outbound(void)
{
    uint16_t used = (s_outbound.length <= USB_BOARD_BULK_MESSAGE_MAX_BYTES)
        ? s_outbound.length
        : USB_BOARD_BULK_MESSAGE_MAX_BYTES;

    if(used != 0u)
    {
        memset(s_outbound.data, 0, used);
    }
    memset(&s_outbound,
           0,
           sizeof(s_outbound) - sizeof(s_outbound.data));
}

static bool queue_event(uint8_t command, const void *payload, uint8_t length)
{
    uint8_t frame[USB_BOARD_LINK_MAX_FRAME_BYTES];
    uint8_t frame_length = 0u;

    return usb_board_link_encode(command,
                                 payload,
                                 length,
                                 frame,
                                 sizeof(frame),
                                 &frame_length) &&
           usb_board_link_port_queue_event(frame, frame_length);
}

static bool queue_fault(uint8_t fault, uint8_t command)
{
    uint8_t payload[2];
    payload[0] = fault;
    payload[1] = command;
    s_last_fault = fault;
    return queue_event(USB_BOARD_EVT_FAULT, payload, sizeof(payload));
}

static void build_state(usb_board_usb_state_v1_t *state)
{
    uint8_t fault = s_last_fault;
    if((fault == USB_BOARD_STATUS_OK) &&
       (usb_device_last_fault() != USB_BOARD_STATUS_OK))
    {
        fault = usb_device_last_fault();
    }
    if((fault == USB_BOARD_STATUS_OK) &&
       (usb_host_last_fault() != USB_BOARD_STATUS_OK))
    {
        fault = usb_host_last_fault();
    }
    if((fault == USB_BOARD_STATUS_OK) &&
       (usb_management_control_last_fault() != USB_BOARD_STATUS_OK))
    {
        fault = usb_management_control_last_fault();
    }

    state->device_mounted = usb_device_is_mounted() ? 1u : 0u;
    state->device_suspended = usb_device_is_suspended() ? 1u : 0u;
    state->host_ready = usb_host_is_ready() ? 1u : 0u;
    state->host_attached = usb_host_is_attached() ? 1u : 0u;
    state->profile = (uint8_t)usb_device_profile();
    state->last_fault = fault;
}

static void poll_state_change(void)
{
    usb_board_usb_state_v1_t current;
    build_state(&current);
    if((s_state_valid == 0u) ||
       (memcmp(&current, &s_last_state, sizeof(current)) != 0))
    {
        s_state_dirty = 1u;
    }
}

static void queue_state(void)
{
    usb_board_usb_state_v1_t state;
    build_state(&state);
    if(queue_event(USB_BOARD_EVT_USB_STATE, &state, sizeof(state)))
    {
        s_last_state = state;
        s_state_valid = 1u;
        s_state_dirty = 0u;
    }
}

static void mark_credit_dirty(usb_board_channel_t channel)
{
    const uint8_t index = (uint8_t)channel;
    /*
     * WebConfig uses the transaction-correlated pull-credit control RPC.
     * Never enqueue an uncorrelated absolute EVT_BULK_CREDIT grant for this
     * channel: a delayed duplicate could re-authorize a report that STM32 has
     * already consumed locally.
     */
    if(channel == USB_BOARD_CHANNEL_WEBCONFIG)
    {
        return;
    }
    if((index != 0u) && (index < USB_BOARD_CHANNEL_SLOTS))
    {
        s_credit_dirty_mask |= (uint8_t)(1u << index);
    }
}

static void queue_one_credit(void)
{
    uint8_t channel;
    for(channel = USB_BOARD_CHANNEL_USB_DEVICE;
        channel <= USB_BOARD_CHANNEL_LAST;
        ++channel)
    {
        const uint8_t mask = (uint8_t)(1u << channel);
        usb_board_bulk_credit_v1_t credit;
        if(channel == USB_BOARD_CHANNEL_WEBCONFIG)
        {
            /* Defensive cleanup for a pre-profile/reset dirty bit. */
            s_credit_dirty_mask &= (uint8_t)~mask;
            continue;
        }
        if((s_credit_dirty_mask & mask) == 0u)
        {
            continue;
        }
        credit.channel = channel;
        credit.credits =
            usb_net_bridge_credit((usb_board_channel_t)channel);
        if(queue_event(USB_BOARD_EVT_BULK_CREDIT,
                       &credit,
                       sizeof(credit)))
        {
            s_credit_dirty_mask &= (uint8_t)~mask;
        }
        return;
    }
}

static void handle_caps(void)
{
    usb_board_caps_v1_t caps;
    memset(&caps, 0, sizeof(caps));
    caps.protocol_version = USB_BOARD_LINK_VERSION;
    caps.role_flags = USB_BOARD_CAP_ROLE_RF |
                      USB_BOARD_CAP_ROLE_USB |
                      USB_BOARD_CAP_ROLE_MAINTENANCE;
    caps.profile_flags = usb_profiles_capability_flags();
    caps.max_frame_bytes = USB_BOARD_LINK_MAX_FRAME_BYTES;
    caps.input_state_bytes = USB_BOARD_INPUT_V1_BYTES;
    caps.firmware_major = 2u;
    caps.firmware_minor = 1u;
    caps.firmware_patch = 0u;
    caps.feature_flags = USB_BOARD_CAP_FEATURE_TELEMETRY_HID |
                         USB_BOARD_CAP_FEATURE_CONTROL_V1 |
                         USB_BOARD_CAP_FEATURE_LOCAL_AUTH |
                         USB_BOARD_CAP_FEATURE_WEBHID_V1 |
                         USB_BOARD_CAP_FEATURE_WEBCONFIG_PULL_CREDIT;
    if(s_role == USB_BOARD_ROLE_USB)
    {
        caps.feature_flags |= USB_BOARD_CAP_FEATURE_SPI_FAST_INPUT_V2;
    }
    (void)queue_event(USB_BOARD_EVT_CAPS, &caps, sizeof(caps));
}

static void handle_set_data_plane(const usb_board_link_frame_t *frame)
{
    usb_board_data_plane_set_v1_t response;
    response.mode = USB_BOARD_DATA_PLANE_COMPAT;
    response.status = USB_BOARD_STATUS_BAD_LENGTH;

    if(frame->length == sizeof(usb_board_set_data_plane_v1_t))
    {
        response.mode = frame->payload[0];
        if(response.mode == USB_BOARD_DATA_PLANE_COMPAT)
        {
            response.status = usb_board_link_port_set_fast_input(false)
                ? USB_BOARD_STATUS_OK
                : USB_BOARD_STATUS_BUSY;
        }
        else if(response.mode == USB_BOARD_DATA_PLANE_FAST_INPUT_V2)
        {
            if(s_role != USB_BOARD_ROLE_USB)
            {
                response.status = USB_BOARD_STATUS_BAD_ROLE;
            }
            else
            {
                response.status = usb_board_link_port_set_fast_input(true)
                    ? USB_BOARD_STATUS_OK
                    : USB_BOARD_STATUS_BUSY;
            }
        }
        else
        {
            response.status = USB_BOARD_STATUS_UNSUPPORTED;
        }
    }
    (void)queue_event(USB_BOARD_EVT_DATA_PLANE_SET,
                      &response,
                      sizeof(response));
}

static void handle_data_plane_probe(const usb_board_link_frame_t *frame)
{
    usb_board_data_plane_probe_v1_t request;
    usb_board_data_plane_probe_result_v1_t response;
    uint8_t index;

    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));
    response.mode = USB_BOARD_DATA_PLANE_FAST_INPUT_V2;
    response.status = USB_BOARD_STATUS_BAD_LENGTH;
    if(frame->length != sizeof(request))
    {
        (void)queue_event(USB_BOARD_EVT_DATA_PLANE_PROBE,
                          &response,
                          sizeof(response));
        return;
    }

    memcpy(&request, frame->payload, sizeof(request));
    response.mode = request.mode;
    response.nonce_le = request.nonce_le;
    response.crc16_le = request.crc16_le;
    if((s_role != USB_BOARD_ROLE_USB) ||
       !usb_board_link_port_is_fast_input())
    {
        response.status = USB_BOARD_STATUS_BAD_ROLE;
    }
    else if((request.mode != USB_BOARD_DATA_PLANE_FAST_INPUT_V2) ||
            (request.version != USB_BOARD_DATA_PLANE_PROBE_VERSION))
    {
        response.status = USB_BOARD_STATUS_UNSUPPORTED;
    }
    else if(usb_board_crc16_ccitt((const uint8_t *)&request,
                                  (uint16_t)(sizeof(request) -
                                             sizeof(request.crc16_le))) !=
            request.crc16_le)
    {
        response.status = USB_BOARD_STATUS_CRC_ERROR;
    }
    else
    {
        response.status = USB_BOARD_STATUS_OK;
        for(index = 0u; index < sizeof(request.pattern); ++index)
        {
            const uint8_t expected = (uint8_t)(
                USB_BOARD_DATA_PLANE_PROBE_PATTERN_SEED +
                (uint8_t)(index * USB_BOARD_DATA_PLANE_PROBE_PATTERN_STEP));
            if(request.pattern[index] != expected)
            {
                response.status = USB_BOARD_STATUS_BAD_FRAME;
                break;
            }
        }
    }
    (void)queue_event(USB_BOARD_EVT_DATA_PLANE_PROBE,
                      &response,
                      sizeof(response));
}

static void handle_set_profile(const usb_board_link_frame_t *frame)
{
    usb_board_profile_set_v1_t response;
    response.profile = (frame->length == sizeof(usb_board_set_profile_v1_t))
        ? frame->payload[0]
        : USB_BOARD_PROFILE_NONE;
    response.status = USB_BOARD_STATUS_BAD_LENGTH;

    if(frame->length == sizeof(usb_board_set_profile_v1_t))
    {
        const usb_board_profile_t profile =
            (usb_board_profile_t)frame->payload[0];
        if((profile == USB_BOARD_PROFILE_WEB_CONFIG) &&
           (s_role != USB_BOARD_ROLE_MAINTENANCE))
        {
            response.status = USB_BOARD_STATUS_BAD_ROLE;
        }
        else if(!usb_profiles_is_supported(profile))
        {
            response.status = USB_BOARD_STATUS_UNSUPPORTED;
        }
        else if(usb_device_set_profile(profile))
        {
            response.status = USB_BOARD_STATUS_OK;
            s_state_dirty = 1u;
        }
        else
        {
            response.status = USB_BOARD_STATUS_NOT_READY;
        }
    }
    (void)queue_event(USB_BOARD_EVT_PROFILE_SET,
                      &response,
                      sizeof(response));
}

static void handle_input(const usb_board_link_frame_t *frame)
{
    const usb_board_input_v1_t *input;
    if(frame->length != sizeof(usb_board_input_v1_t))
    {
        queue_fault(USB_BOARD_STATUS_BAD_LENGTH, frame->command);
        return;
    }
    input = (const usb_board_input_v1_t *)frame->payload;
    if(((input->flags & USB_LINK_INPUT_VERSION_MASK) !=
        (USB_BOARD_INPUT_FORMAT_VERSION << USB_BOARD_INPUT_VERSION_SHIFT)) ||
       (usb_board_input_crc8(frame->payload,
                             (uint8_t)(frame->length - 1u)) != input->crc8))
    {
        queue_fault(USB_BOARD_STATUS_CRC_ERROR, frame->command);
        return;
    }
    if(!usb_device_submit_input(input))
    {
        queue_fault(USB_BOARD_STATUS_NOT_READY, frame->command);
    }
}

static void handle_fragment(const usb_board_link_frame_t *frame)
{
    usb_board_fragment_header_v1_t header;
    usb_board_channel_t channel;
    bool webconfig_report;
    bool first;
    bool active_before;
    bool accepted;

    if(frame->length < USB_BOARD_FRAGMENT_HEADER_BYTES)
    {
        queue_fault(USB_BOARD_STATUS_BAD_LENGTH, frame->command);
        return;
    }
    memcpy(&header, frame->payload, sizeof(header));
    channel = (usb_board_channel_t)header.channel;
    webconfig_report = channel == USB_BOARD_CHANNEL_WEBCONFIG;
    first = (header.flags & USB_BOARD_FRAGMENT_FLAG_FIRST) != 0u;
    if(channel == USB_BOARD_CHANNEL_NETWORK)
    {
        queue_fault(USB_BOARD_STATUS_UNSUPPORTED, frame->command);
        return;
    }
    if(webconfig_report &&
       (s_role != USB_BOARD_ROLE_MAINTENANCE))
    {
        queue_fault(USB_BOARD_STATUS_BAD_ROLE, frame->command);
        return;
    }
    if(webconfig_report &&
       (header.total_length_le != WEBHID_REPORT_BYTES))
    {
        queue_fault(USB_BOARD_STATUS_BAD_LENGTH, frame->command);
        return;
    }

    active_before = usb_net_bridge_message_active(channel);
    if(webconfig_report && first && active_before)
    {
        /*
         * The previous complete report is retained while its sink is busy.
         * Reject a replacement FIRST without touching that reservation.
         */
        queue_fault(USB_BOARD_STATUS_BUSY, frame->command);
        return;
    }
    if(webconfig_report && !first && !active_before)
    {
        queue_fault(USB_BOARD_STATUS_CRC_ERROR, frame->command);
        return;
    }
    if((!webconfig_report || first) &&
       !usb_net_bridge_take_credit(channel))
    {
        queue_fault(USB_BOARD_STATUS_BUSY, frame->command);
        return;
    }

    accepted = usb_net_bridge_fragment(
        &header,
        &frame->payload[USB_BOARD_FRAGMENT_HEADER_BYTES],
        (uint8_t)(frame->length - USB_BOARD_FRAGMENT_HEADER_BYTES));
    if(!accepted)
    {
        /*
         * A malformed WebConfig continuation releases the single whole-report
         * reservation. Other channels retain their per-fragment behavior.
         */
        if(!webconfig_report ||
           first ||
           !usb_net_bridge_message_active(channel))
        {
            usb_net_bridge_return_credit(channel);
            mark_credit_dirty(channel);
        }
        queue_fault(USB_BOARD_STATUS_CRC_ERROR, frame->command);
        return;
    }
    if(!webconfig_report)
    {
        usb_net_bridge_return_credit(channel);
        mark_credit_dirty(channel);
    }
}

static void handle_credit(const usb_board_link_frame_t *frame)
{
    usb_board_bulk_credit_v1_t credit;
    uint8_t limit;
    if(frame->length != sizeof(credit))
    {
        queue_fault(USB_BOARD_STATUS_BAD_LENGTH, frame->command);
        return;
    }
    memcpy(&credit, frame->payload, sizeof(credit));
    if((credit.channel == 0u) ||
       (credit.channel >= USB_BOARD_CHANNEL_SLOTS))
    {
        queue_fault(USB_BOARD_STATUS_BAD_LENGTH, frame->command);
        return;
    }
    limit = (credit.channel == USB_BOARD_CHANNEL_WEBCONFIG)
        ? USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW
        : USB_BOARD_BULK_CREDIT_WINDOW;
    s_tx_credits[credit.channel] =
        (credit.credits > limit)
            ? limit
            : credit.credits;
}

static void handle_control(const usb_board_link_frame_t *frame)
{
    uint8_t response[USB_BOARD_LINK_MAX_PAYLOAD_BYTES];
    uint8_t response_length = 0u;

    if(usb_management_control_handle(frame->payload,
                                     frame->length,
                                     response,
                                     sizeof(response),
                                     &response_length))
    {
        (void)queue_event(USB_BOARD_EVT_USB_CONTROL,
                          response,
                          response_length);
    }
    else
    {
        usb_board_control_response_v1_t fallback;
        memset(&fallback, 0, sizeof(fallback));
        if(frame->length >= USB_BOARD_CONTROL_HEADER_BYTES)
        {
            const usb_board_control_header_v1_t *request =
                (const usb_board_control_header_v1_t *)frame->payload;
            fallback.header.opcode = request->opcode;
            fallback.header.transaction = request->transaction;
        }
        fallback.header.status = USB_BOARD_STATUS_BAD_LENGTH;
        (void)queue_event(USB_BOARD_EVT_USB_CONTROL,
                          &fallback,
                          USB_BOARD_CONTROL_HEADER_BYTES);
    }
}

static void pump_outbound(void)
{
    uint8_t packet[USB_BOARD_LINK_MAX_PAYLOAD_BYTES];
    usb_board_fragment_header_v1_t *header;
    uint16_t remaining;
    uint8_t data_length;
    uint8_t channel;

    if(s_outbound.active == 0u)
    {
        return;
    }
    channel = s_outbound.channel;
    if((channel == 0u) || (channel >= USB_BOARD_CHANNEL_SLOTS) ||
       (s_tx_credits[channel] == 0u))
    {
        return;
    }

    memset(packet, 0, sizeof(packet));
    header = (usb_board_fragment_header_v1_t *)packet;
    remaining = (uint16_t)(s_outbound.length - s_outbound.offset);
    data_length = (remaining > USB_BOARD_FRAGMENT_DATA_BYTES)
        ? USB_BOARD_FRAGMENT_DATA_BYTES
        : (uint8_t)remaining;

    header->channel = channel;
    header->transaction = s_outbound.transaction;
    header->fragment_index = s_outbound.fragment_index;
    header->flags =
        (uint8_t)(((s_outbound.offset == 0u)
                       ? USB_BOARD_FRAGMENT_FLAG_FIRST
                       : 0u) |
                  (((uint16_t)(s_outbound.offset + data_length) >=
                    s_outbound.length)
                       ? USB_BOARD_FRAGMENT_FLAG_LAST
                       : 0u));
    header->total_length_le = s_outbound.length;
    header->message_crc16_le = s_outbound.crc;
    if(data_length != 0u)
    {
        memcpy(&packet[USB_BOARD_FRAGMENT_HEADER_BYTES],
               &s_outbound.data[s_outbound.offset],
               data_length);
    }

    if(!queue_event(USB_BOARD_EVT_BULK_FRAGMENT,
                    packet,
                    (uint8_t)(USB_BOARD_FRAGMENT_HEADER_BYTES +
                              data_length)))
    {
        return;
    }

    --s_tx_credits[channel];
    s_outbound.offset =
        (uint16_t)(s_outbound.offset + data_length);
    ++s_outbound.fragment_index;
    if(s_outbound.offset >= s_outbound.length)
    {
        clear_outbound();
    }
}

static void dispatch(const usb_board_link_frame_t *frame)
{
    if((frame == 0) || (s_ready == 0u))
    {
        return;
    }

    switch(frame->command)
    {
    case USB_BOARD_CMD_GET_CAPS:
        if(frame->length == 0u)
        {
            handle_caps();
            s_caps_requested = 1u;
        }
        else
        {
            queue_fault(USB_BOARD_STATUS_BAD_LENGTH, frame->command);
        }
        break;

    case USB_BOARD_CMD_SET_PROFILE:
        handle_set_profile(frame);
        break;

    case USB_BOARD_CMD_INPUT_STATE:
        if(s_role == USB_BOARD_ROLE_USB)
        {
            handle_input(frame);
        }
        else
        {
            queue_fault(USB_BOARD_STATUS_BAD_ROLE, frame->command);
        }
        break;

    case USB_BOARD_CMD_USB_CONTROL:
        handle_control(frame);
        break;

    case USB_BOARD_CMD_BULK_FRAGMENT:
        handle_fragment(frame);
        break;

    case USB_BOARD_CMD_BULK_CREDIT:
        handle_credit(frame);
        break;

    case USB_BOARD_CMD_SET_DATA_PLANE:
        handle_set_data_plane(frame);
        break;

    case USB_BOARD_CMD_DATA_PLANE_PROBE:
        handle_data_plane_probe(frame);
        break;

    case USB_BOARD_CMD_SELECT_ROLE:
        /*
         * The cold-boot parser already committed the role.  Its final
         * ROLE_SELECTED frame can be lost while SPI ownership moves from the
         * polling FIFO to Application DMA, so make a retry of the same role
         * idempotent.  A request for a different role remains forbidden.
         */
        if(frame->length != sizeof(usb_board_role_select_v1_t))
        {
            queue_fault(USB_BOARD_STATUS_BAD_LENGTH, frame->command);
        }
        else if(frame->payload[0] == (uint8_t)s_role)
        {
            usb_board_role_selected_v1_t response;
            response.role = (uint8_t)s_role;
            response.status = USB_BOARD_STATUS_OK;
            (void)queue_event(USB_BOARD_EVT_ROLE_SELECTED,
                              &response,
                              sizeof(response));
        }
        else
        {
            queue_fault(USB_BOARD_STATUS_ROLE_LOCKED, frame->command);
        }
        break;

    default:
        queue_fault(USB_BOARD_STATUS_UNSUPPORTED, frame->command);
        break;
    }
}

void usb_board_link_init(usb_board_role_t locked_role)
{
    usb_board_link_parser_init(&s_parser);
    memset(&s_last_state, 0, sizeof(s_last_state));
    memset(s_tx_credits, 0, sizeof(s_tx_credits));
    memset(s_next_transaction, 0, sizeof(s_next_transaction));
    clear_outbound();
    s_role = locked_role;
    s_ready = usb_board_link_port_init() ? 1u : 0u;
    s_last_fault = (s_ready != 0u)
        ? USB_BOARD_STATUS_OK
        : USB_BOARD_STATUS_NOT_READY;
    s_state_dirty = 1u;
    s_state_valid = 0u;
    s_port_fault_pending = USB_BOARD_STATUS_OK;
    s_caps_requested = 0u;
    s_credit_dirty_mask = 0u;
    s_webconfig_credit_paused = 0u;
    {
        uint8_t channel;
        for(channel = USB_BOARD_CHANNEL_USB_DEVICE;
            channel <= USB_BOARD_CHANNEL_LAST;
            ++channel)
        {
            mark_credit_dirty((usb_board_channel_t)channel);
        }
    }
}

void usb_board_link_process(void)
{
    uint8_t byte;
    uint8_t port_fault;
    usb_board_link_frame_t completed;

    if(s_ready == 0u)
    {
        return;
    }
    usb_board_link_port_process();
    if((s_port_fault_pending == USB_BOARD_STATUS_OK) &&
       usb_board_link_port_take_fault(&port_fault))
    {
        s_port_fault_pending = port_fault;
        s_last_fault = port_fault;
        s_state_dirty = 1u;
    }
    while(usb_board_link_port_pop_rx(&byte))
    {
        if(usb_board_link_parser_feed(&s_parser, byte, &completed))
        {
            dispatch(&completed);
        }
    }
    if(s_caps_requested != 0u)
    {
        usb_net_bridge_process();
        poll_state_change();
        if(s_state_dirty != 0u)
        {
            queue_state();
        }
        if((s_port_fault_pending != USB_BOARD_STATUS_OK) &&
           queue_fault(s_port_fault_pending, 0u))
        {
            s_port_fault_pending = USB_BOARD_STATUS_OK;
        }
        queue_one_credit();
        pump_outbound();
    }
    usb_board_link_port_process();
}

bool usb_board_link_is_ready(void)
{
    return s_ready != 0u;
}

bool usb_board_link_caps_requested(void)
{
    return s_caps_requested != 0u;
}

uint8_t usb_board_link_last_fault(void)
{
    return s_last_fault;
}

bool usb_board_link_publish_bulk(usb_board_channel_t channel,
                                 const uint8_t *data,
                                 uint16_t length)
{
    const uint8_t index = (uint8_t)channel;
    if((s_ready == 0u) || (s_outbound.active != 0u) ||
       (index == 0u) || (index >= USB_BOARD_CHANNEL_SLOTS) ||
       (length > USB_BOARD_BULK_MESSAGE_MAX_BYTES) ||
       ((length != 0u) && (data == 0)) ||
       (channel == USB_BOARD_CHANNEL_NETWORK) ||
       ((channel == USB_BOARD_CHANNEL_WEBCONFIG) &&
        (s_role != USB_BOARD_ROLE_MAINTENANCE)))
    {
        return false;
    }

    clear_outbound();
    s_outbound.active = 1u;
    s_outbound.channel = index;
    s_outbound.transaction = s_next_transaction[index]++;
    s_outbound.length = length;
    s_outbound.crc = usb_board_crc16_ccitt(data, length);
    if(length != 0u)
    {
        memcpy(s_outbound.data, data, length);
    }
    return true;
}

void usb_board_link_reset_channel(usb_board_channel_t channel)
{
    const uint8_t index = (uint8_t)channel;

    if((index == 0u) || (index >= USB_BOARD_CHANNEL_SLOTS))
    {
        return;
    }
    if((s_outbound.active != 0u) &&
       (s_outbound.channel == index))
    {
        clear_outbound();
    }
    ++s_next_transaction[index];
    usb_net_bridge_reset_channel(channel);
    if(channel == USB_BOARD_CHANNEL_WEBCONFIG)
    {
        s_webconfig_credit_paused = 0u;
    }
    else
    {
        mark_credit_dirty(channel);
    }
}

void usb_board_link_webconfig_set_ready(bool ready,
                                        uint8_t available_reports)
{
    const uint8_t credits =
        (available_reports >
         USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW)
            ? USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW
            : available_reports;

    if(!ready)
    {
        s_webconfig_credit_paused = 0u;
        usb_net_bridge_reset_channel(
            USB_BOARD_CHANNEL_WEBCONFIG);
    }
    else
    {
        s_webconfig_credit_paused = 0u;
        usb_net_bridge_set_credit(
            USB_BOARD_CHANNEL_WEBCONFIG, credits);
    }
}

void usb_board_link_webconfig_pause(void)
{
    /*
     * USB suspend is not a generation boundary. Advertise zero for new work
     * while preserving grants already observed by STM32 and any partial
     * reassembly slot; resume publishes the exact capacity via set_ready().
     */
    s_webconfig_credit_paused = 1u;
}

void usb_board_link_webconfig_report_consumed(void)
{
    usb_net_bridge_return_credit(USB_BOARD_CHANNEL_WEBCONFIG);
}

bool usb_management_control_hw_get_webconfig_credit(
    usb_board_bulk_credit_v1_t *credit)
{
    if((credit == 0) || (s_ready == 0u) ||
       (s_role != USB_BOARD_ROLE_MAINTENANCE) ||
       (usb_device_profile() != USB_BOARD_PROFILE_WEB_CONFIG) ||
       !usb_device_webhid_credit_ready())
    {
        return false;
    }

    credit->channel = USB_BOARD_CHANNEL_WEBCONFIG;
    credit->credits = (s_webconfig_credit_paused != 0u)
        ? 0u
        : usb_net_bridge_credit(USB_BOARD_CHANNEL_WEBCONFIG);
    if(credit->credits > USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW)
    {
        credit->credits = USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW;
    }
    return true;
}
