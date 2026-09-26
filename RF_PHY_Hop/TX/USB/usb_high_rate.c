#include "usb_high_rate.h"

#include <stddef.h>
#include <string.h>

typedef enum
{
    USB_HIGH_RATE_NATIVE = 0,
    USB_HIGH_RATE_ARMING,
    USB_HIGH_RATE_DETACHED_TO_TURBO,
    USB_HIGH_RATE_TURBO_WAIT,
    USB_HIGH_RATE_TURBO_ACTIVE,
    USB_HIGH_RATE_FALLBACK_PENDING,
    USB_HIGH_RATE_DETACHED_TO_NATIVE
} usb_high_rate_state_t;

static usb_high_rate_state_t s_state;
static uint8_t s_token[16];
static uint32_t s_deadline_ms;
static uint32_t s_last_heartbeat_ms;
static uint32_t s_stream_sequence;
static uint16_t s_effective_rate_hz;
static uint16_t s_overwrite_count;
static uint16_t s_board_link_fault_count;
static uint8_t s_packet_pending;
static uint8_t s_neutral_pending;
static uint8_t s_usb_high_speed;
static hbox_client_input_v1_t s_latest_packet;

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static bool token_matches(const uint8_t token[16])
{
    return memcmp(token, s_token, sizeof(s_token)) == 0;
}

static uint8_t runtime_flags(void)
{
    uint8_t flags = 0u;
    if(s_state != USB_HIGH_RATE_NATIVE)
    {
        flags |= HBOX_CLIENT_FLAG_ENABLED;
    }
    if(s_state == USB_HIGH_RATE_TURBO_ACTIVE)
    {
        flags |= HBOX_CLIENT_FLAG_STREAMING;
    }
    if(s_usb_high_speed != 0u)
    {
        flags |= HBOX_CLIENT_FLAG_USB_HS;
    }
    if((s_state == USB_HIGH_RATE_FALLBACK_PENDING) ||
       (s_state == USB_HIGH_RATE_DETACHED_TO_NATIVE))
    {
        flags |= HBOX_CLIENT_FLAG_FALLBACK;
    }
    return flags;
}

static void finish_control(hbox_client_control_v1_t *response)
{
    response->magic_le = HBOX_CLIENT_CONTROL_MAGIC;
    response->version = HBOX_CLIENT_PROTOCOL_VERSION;
    response->flags = runtime_flags();
    response->effective_rate_hz_le = s_effective_rate_hz;
    response->crc16_le = hbox_client_crc16_ccitt(
        (const uint8_t *)response,
        offsetof(hbox_client_control_v1_t, crc16_le));
}

static bool request_is_valid(const hbox_client_control_v1_t *request)
{
    return (request != NULL) &&
           (request->magic_le == HBOX_CLIENT_CONTROL_MAGIC) &&
           (request->crc16_le == hbox_client_crc16_ccitt(
               (const uint8_t *)request,
               offsetof(hbox_client_control_v1_t, crc16_le)));
}

void usb_high_rate_init(void)
{
    memset(s_token, 0, sizeof(s_token));
    memset(&s_latest_packet, 0, sizeof(s_latest_packet));
    s_state = USB_HIGH_RATE_NATIVE;
    s_deadline_ms = 0u;
    s_last_heartbeat_ms = 0u;
    s_stream_sequence = 0u;
    s_effective_rate_hz = 1000u;
    s_overwrite_count = 0u;
    s_board_link_fault_count = 0u;
    s_packet_pending = 0u;
    s_neutral_pending = 0u;
    s_usb_high_speed = 0u;
}

void usb_high_rate_reset(void)
{
    usb_high_rate_init();
}

bool usb_high_rate_is_turbo_presentation(void)
{
    return (s_state == USB_HIGH_RATE_DETACHED_TO_TURBO) ||
           (s_state == USB_HIGH_RATE_TURBO_WAIT) ||
           (s_state == USB_HIGH_RATE_TURBO_ACTIVE) ||
           (s_state == USB_HIGH_RATE_FALLBACK_PENDING);
}

bool usb_high_rate_is_streaming(void)
{
    return s_state == USB_HIGH_RATE_TURBO_ACTIVE;
}

uint16_t usb_high_rate_effective_rate_hz(void)
{
    return s_effective_rate_hz;
}

void usb_high_rate_get_status(hbox_client_control_v1_t *response,
                              uint32_t transaction,
                              bool usb_high_speed)
{
    if(response == NULL)
    {
        return;
    }
    s_usb_high_speed = usb_high_speed ? 1u : 0u;
    memset(response, 0, sizeof(*response));
    response->opcode = HBOX_CLIENT_CONTROL_QUERY;
    response->status = HBOX_CLIENT_STATUS_OK;
    response->transaction_le = transaction;
    memcpy(response->lease_token, s_token, sizeof(s_token));
    finish_control(response);
}

bool usb_high_rate_handle_control(const hbox_client_control_v1_t *request,
                                  hbox_client_control_v1_t *response,
                                  uint32_t now_ms,
                                  bool usb_high_speed,
                                  bool from_turbo_transport)
{
    uint8_t status = HBOX_CLIENT_STATUS_OK;

    if((request == NULL) || (response == NULL))
    {
        return false;
    }
    s_usb_high_speed = usb_high_speed ? 1u : 0u;
    memset(response, 0, sizeof(*response));
    response->opcode = request->opcode;
    response->transaction_le = request->transaction_le;

    if(!request_is_valid(request))
    {
        status = HBOX_CLIENT_STATUS_BAD_CRC;
    }
    else if(request->version != HBOX_CLIENT_PROTOCOL_VERSION)
    {
        status = HBOX_CLIENT_STATUS_BAD_VERSION;
    }
    else
    {
        switch(request->opcode)
        {
        case HBOX_CLIENT_CONTROL_QUERY:
            break;

        case HBOX_CLIENT_CONTROL_ACQUIRE:
            if(from_turbo_transport || (s_state != USB_HIGH_RATE_NATIVE))
            {
                status = HBOX_CLIENT_STATUS_BUSY;
            }
            else if(!usb_high_speed || (s_effective_rate_hz <= 1000u) ||
                    hbox_client_token_is_zero(request->lease_token))
            {
                status = HBOX_CLIENT_STATUS_BAD_STATE;
            }
            else
            {
                memcpy(s_token, request->lease_token, sizeof(s_token));
                s_state = USB_HIGH_RATE_ARMING;
                s_deadline_ms = 0u;
                s_neutral_pending = 1u;
                s_packet_pending = 0u;
                s_overwrite_count = 0u;
                s_board_link_fault_count = 0u;
                s_stream_sequence = 0u;
            }
            break;

        case HBOX_CLIENT_CONTROL_HEARTBEAT:
            if(!from_turbo_transport ||
               ((s_state != USB_HIGH_RATE_TURBO_WAIT) &&
                (s_state != USB_HIGH_RATE_TURBO_ACTIVE)))
            {
                status = HBOX_CLIENT_STATUS_BAD_STATE;
            }
            else if(!token_matches(request->lease_token))
            {
                status = HBOX_CLIENT_STATUS_BAD_TOKEN;
            }
            else
            {
                s_state = USB_HIGH_RATE_TURBO_ACTIVE;
                s_last_heartbeat_ms = now_ms;
            }
            break;

        case HBOX_CLIENT_CONTROL_RELEASE:
            if(s_state == USB_HIGH_RATE_NATIVE)
            {
                status = HBOX_CLIENT_STATUS_BAD_STATE;
            }
            else if(!token_matches(request->lease_token))
            {
                status = HBOX_CLIENT_STATUS_BAD_TOKEN;
            }
            else if((s_state == USB_HIGH_RATE_ARMING) &&
                    !from_turbo_transport)
            {
                usb_high_rate_init();
            }
            else
            {
                s_state = USB_HIGH_RATE_FALLBACK_PENDING;
                s_packet_pending = 0u;
            }
            break;

        default:
            status = HBOX_CLIENT_STATUS_BAD_STATE;
            break;
        }
    }

    response->status = status;
    memcpy(response->lease_token, s_token, sizeof(s_token));
    finish_control(response);
    return status == HBOX_CLIENT_STATUS_OK;
}

usb_high_rate_event_t usb_high_rate_process(uint32_t now_ms)
{
    if(s_neutral_pending != 0u)
    {
        return USB_HIGH_RATE_EVENT_SEND_NEUTRAL;
    }

    switch(s_state)
    {
    case USB_HIGH_RATE_ARMING:
        if(deadline_reached(now_ms, s_deadline_ms))
        {
            s_state = USB_HIGH_RATE_DETACHED_TO_TURBO;
            s_deadline_ms = now_ms + HBOX_CLIENT_REENUMERATE_DELAY_MS;
            return USB_HIGH_RATE_EVENT_DETACH_FOR_TURBO;
        }
        break;

    case USB_HIGH_RATE_DETACHED_TO_TURBO:
        if(deadline_reached(now_ms, s_deadline_ms))
        {
            s_state = USB_HIGH_RATE_TURBO_WAIT;
            s_last_heartbeat_ms = now_ms;
            return USB_HIGH_RATE_EVENT_ATTACH_TURBO;
        }
        break;

    case USB_HIGH_RATE_TURBO_WAIT:
    case USB_HIGH_RATE_TURBO_ACTIVE:
        if((uint32_t)(now_ms - s_last_heartbeat_ms) >=
           HBOX_CLIENT_LEASE_TIMEOUT_MS)
        {
            s_state = USB_HIGH_RATE_DETACHED_TO_NATIVE;
            s_deadline_ms = now_ms + HBOX_CLIENT_REENUMERATE_DELAY_MS;
            s_packet_pending = 0u;
            return USB_HIGH_RATE_EVENT_DETACH_FOR_NATIVE;
        }
        break;

    case USB_HIGH_RATE_FALLBACK_PENDING:
        s_state = USB_HIGH_RATE_DETACHED_TO_NATIVE;
        s_deadline_ms = now_ms + HBOX_CLIENT_REENUMERATE_DELAY_MS;
        s_packet_pending = 0u;
        return USB_HIGH_RATE_EVENT_DETACH_FOR_NATIVE;

    case USB_HIGH_RATE_DETACHED_TO_NATIVE:
        if(deadline_reached(now_ms, s_deadline_ms))
        {
            usb_high_rate_init();
            return USB_HIGH_RATE_EVENT_ATTACH_NATIVE;
        }
        break;

    default:
        break;
    }
    return USB_HIGH_RATE_EVENT_NONE;
}

void usb_high_rate_neutral_sent(uint32_t now_ms)
{
    if((s_state == USB_HIGH_RATE_ARMING) && (s_neutral_pending != 0u))
    {
        s_neutral_pending = 0u;
        s_deadline_ms = now_ms + HBOX_CLIENT_NEUTRAL_DELAY_MS;
    }
}

void usb_high_rate_note_board_link_fault(void)
{
    if(s_board_link_fault_count != UINT16_MAX)
    {
        ++s_board_link_fault_count;
    }
}

void usb_high_rate_submit_input(const usb_board_input_v1_t *input,
                                uint16_t xinput_buttons,
                                uint32_t producer_time_us)
{
    uint16_t flags = HBOX_CLIENT_INPUT_FLAG_VALID;

    if(input == NULL)
    {
        return;
    }
    s_effective_rate_hz = usb_board_input_rate_hz(input->flags);
    if(!usb_high_rate_is_streaming())
    {
        return;
    }
    if(s_packet_pending != 0u)
    {
        if(s_overwrite_count != UINT16_MAX)
        {
            ++s_overwrite_count;
        }
        flags |= HBOX_CLIENT_INPUT_FLAG_SOURCE_OVERWRITE;
    }
    if((input->flags & USB_BOARD_INPUT_FLAG_BATTERY_VALID) != 0u)
    {
        flags |= HBOX_CLIENT_INPUT_FLAG_BATTERY_VALID;
    }
    if(input->action_mask_le == 0u)
    {
        flags |= HBOX_CLIENT_INPUT_FLAG_NEUTRAL;
    }

    memset(&s_latest_packet, 0, sizeof(s_latest_packet));
    s_latest_packet.magic_le = HBOX_CLIENT_INPUT_MAGIC;
    s_latest_packet.version = HBOX_CLIENT_PROTOCOL_VERSION;
    s_latest_packet.length_le = HBOX_CLIENT_INPUT_BYTES;
    s_latest_packet.stream_sequence_le = s_stream_sequence++;
    s_latest_packet.producer_time_us_le = producer_time_us;
    memcpy(s_latest_packet.lease_token, s_token, sizeof(s_token));
    s_latest_packet.action_mask_le = input->action_mask_le;
    s_latest_packet.sample_age_us_le = input->age_us_le;
    s_latest_packet.effective_rate_hz_le = s_effective_rate_hz;
    s_latest_packet.xinput_buttons_le = xinput_buttons;
    s_latest_packet.left_trigger =
        (input->action_mask_le & (1ul << 10)) != 0u ? 0xFFu : 0u;
    s_latest_packet.right_trigger =
        (input->action_mask_le & (1ul << 11)) != 0u ? 0xFFu : 0u;
    s_latest_packet.flags_le = flags;
    s_latest_packet.battery_code = input->battery_code;
    s_latest_packet.device_overwrite_count_le = s_overwrite_count;
    s_latest_packet.board_link_fault_count_le = s_board_link_fault_count;
    s_latest_packet.crc32_le = hbox_client_crc32(
        (const uint8_t *)&s_latest_packet,
        offsetof(hbox_client_input_v1_t, crc32_le));
    s_packet_pending = 1u;
}

bool usb_high_rate_peek_input(hbox_client_input_v1_t *packet)
{
    if((packet == NULL) || (s_packet_pending == 0u) ||
       !usb_high_rate_is_streaming())
    {
        return false;
    }
    memcpy(packet, &s_latest_packet, sizeof(*packet));
    return true;
}

void usb_high_rate_commit_input(void)
{
    s_packet_pending = 0u;
}
