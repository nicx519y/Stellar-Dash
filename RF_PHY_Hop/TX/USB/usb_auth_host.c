#include "usb_auth_host.h"

#include <string.h>

#include "CH58x_common.h"
#include "usb_gip_protocol.h"
#include "usb_host.h"

#define AUTH_HOST_WAIT_TIMEOUT_MS        10000u
#define AUTH_TRANSFER_RETRY_LIMIT            3u
#define AUTH_POLL_INTERVAL_MS              100u
#define AUTH_POLL_LIMIT                     60u
#define AUTH_GIP_QUEUE_INTERVAL_MS          15u
#define AUTH_GIP_QUEUE_SLOTS                24u

#define AUTH_HID_GET_REPORT               0x01u
#define AUTH_HID_SET_REPORT               0x09u
#define HID_REPORT_TYPE_FEATURE           0x03u

#define PS4_REPORT_SET_AUTH               0xF0u
#define PS4_REPORT_GET_SIGNATURE          0xF1u
#define PS4_REPORT_GET_STATE              0xF2u
#define PS4_REPORT_RESET                  0xF3u
#define PS4_REPORT_GET_DEFINITION         0x03u
#define PS4_NONCE_BYTES                    256u
#define PS4_SIGNATURE_BYTES               1064u
#define PS4_PAGE_BYTES                      56u
#define PS4_NONCE_PAGES                      5u
#define PS4_SIGNATURE_CHUNKS                19u

#define X360_GET_SERIAL                   0x81u
#define X360_INIT_AUTH                    0x82u
#define X360_RESPOND_CHALLENGE            0x83u
#define X360_AUTH_KEEPALIVE               0x84u
#define X360_REQUEST_STATE                0x86u
#define X360_VERIFY_AUTH                  0x87u
#define X360_CONSOLE_INIT_BYTES             34u
#define X360_SERIAL_BYTES                   29u
#define X360_INIT_REPLY_BYTES               46u
#define X360_VERIFY_BYTES                   22u
#define X360_WVALUE_CONSOLE_DATA        0x0003u
#define X360_WVALUE_CONTROLLER_ID       0x5B17u
#define X360_WVALUE_INIT_REPLY          0x5C28u
#define X360_WVALUE_VERIFY_REPLY        0x5C10u
#define X360_WINDEX_SECURITY            0x0103u

#define GIP_ACK_RESPONSE                  0x01u
#define GIP_ANNOUNCE                      0x02u
#define GIP_DEVICE_DESCRIPTOR             0x04u
#define GIP_POWER_MODE                    0x05u
#define GIP_AUTH                          0x06u
#define GIP_RUMBLE                        0x09u
#define GIP_LED                           0x0Au
#define GIP_FINAL_AUTH                    0x1Eu

typedef enum
{
    ENGINE_PHASE_IDLE = 0,
    ENGINE_PHASE_BIND,
    ENGINE_PHASE_PS4_GET_DEFINITION,
    ENGINE_PHASE_PS4_WAIT_NONCE,
    ENGINE_PHASE_PS4_RESET,
    ENGINE_PHASE_PS4_SEND_PAGE,
    ENGINE_PHASE_PS4_WAIT_SIGNATURE,
    ENGINE_PHASE_PS4_READ_SIGNATURE,
    ENGINE_PHASE_X360_GET_SECURITY_STRING,
    ENGINE_PHASE_X360_GET_SERIAL,
    ENGINE_PHASE_X360_IDLE,
    ENGINE_PHASE_X360_SEND_REQUEST,
    ENGINE_PHASE_X360_WAIT_STATE,
    ENGINE_PHASE_X360_GET_REPLY,
    ENGINE_PHASE_X360_KEEPALIVE,
    ENGINE_PHASE_GIP_RUNNING
} auth_engine_phase_t;

typedef struct
{
    uint8_t data[USB_GIP_PACKET_MAX_BYTES];
    uint8_t length;
} auth_gip_frame_t;

typedef struct
{
    auth_gip_frame_t frames[AUTH_GIP_QUEUE_SLOTS];
    volatile uint8_t head;
    volatile uint8_t tail;
} auth_gip_queue_t;

static usb_auth_engine_state_t s_engine_state;
static usb_auth_error_t s_engine_error;
static usb_auth_scheme_t s_scheme;
static auth_engine_phase_t s_phase;
static usb_host_interface_t s_host_interface;
static uint8_t s_host_bound;
static uint8_t s_host_device_ready;
static uint8_t s_transfer_failures;
static uint8_t s_poll_count;
static uint32_t s_wait_started_cycles;

static uint8_t s_control_buffer[USB_HOST_DESCRIPTOR_MAX_BYTES];

static uint8_t s_ps4_nonce[PS4_NONCE_BYTES];
static uint8_t s_ps4_signature[PS4_SIGNATURE_BYTES];
static uint8_t s_ps4_nonce_id;
static uint8_t s_ps4_nonce_pages;
static uint8_t s_ps4_host_page;
static uint8_t s_ps4_host_chunk;
static uint8_t s_ps4_device_chunk;
static volatile uint8_t s_ps4_nonce_ready;
static volatile uint8_t s_ps4_signature_ready;

static uint8_t s_x360_console_initial[X360_CONSOLE_INIT_BYTES];
static uint8_t s_x360_serial[X360_SERIAL_BYTES];
static uint8_t s_x360_request[X360_INIT_REPLY_BYTES];
static uint8_t s_x360_reply[X360_INIT_REPLY_BYTES];
static uint8_t s_x360_request_id;
static uint8_t s_x360_request_length;
static uint8_t s_x360_reply_length;
static uint8_t s_x360_has_initial;
static volatile uint8_t s_x360_request_pending;
static volatile uint8_t s_x360_reply_ready;
static uint8_t s_x360_serial_ready;

static auth_gip_queue_t s_gip_device_rx;
static auth_gip_queue_t s_gip_device_tx;
static auth_gip_queue_t s_gip_host_tx;
static usb_gip_rx_t s_gip_device_parser;
static usb_gip_rx_t s_gip_host_parser;
static usb_gip_tx_t s_gip_tx_scratch;
static uint32_t s_gip_last_host_tx_cycles;
static uint8_t s_gip_host_tx_failures;

static const uint8_t s_gip_power_on[] = {
    0x06u,0x62u,0x45u,0xB8u,0x77u,0x26u,0x2Cu,0x55u,
    0x53u,0x00u,0x00u,0x00u,0x00u,0x00u,0x1Fu
};
static const uint8_t s_gip_power_on_single[] = {0x00u};
static const uint8_t s_gip_led_on[] = {0x00u,0x01u,0x14u};
static const uint8_t s_gip_rumble_on[] = {
    0x00u,0x0Fu,0x00u,0x00u,0x00u,0x00u,0xFFu,0x00u,0xEBu
};
static const uint8_t s_gip_auth_ready[] = {0x01u,0x00u};

static uint32_t auth_now_cycles(void)
{
    return SysTick->CNTL;
}

static bool auth_elapsed_ms(uint32_t started_cycles, uint32_t milliseconds)
{
    uint32_t cycles_per_ms = GetSysClock() / 1000u;
    if(cycles_per_ms == 0u)
    {
        cycles_per_ms = 1u;
    }
    return (uint32_t)(auth_now_cycles() - started_cycles) >=
           (cycles_per_ms * milliseconds);
}

static uint32_t auth_crc32(const uint8_t *data, uint16_t length)
{
    uint32_t crc = 0xFFFFFFFFul;
    uint16_t index;
    uint8_t bit;

    for(index = 0u; index < length; ++index)
    {
        crc ^= data[index];
        for(bit = 0u; bit < 8u; ++bit)
        {
            crc = (crc & 1u)
                ? ((crc >> 1) ^ 0xEDB88320ul)
                : (crc >> 1);
        }
    }
    return ~crc;
}

static void auth_store_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

static uint32_t auth_load_u32_le(const uint8_t *source)
{
    return (uint32_t)source[0] |
           ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) |
           ((uint32_t)source[3] << 24);
}

static void auth_setup_packet(uint8_t setup[8],
                              uint8_t request_type,
                              uint8_t request,
                              uint16_t value,
                              uint16_t index,
                              uint16_t length)
{
    setup[0] = request_type;
    setup[1] = request;
    setup[2] = (uint8_t)value;
    setup[3] = (uint8_t)(value >> 8);
    setup[4] = (uint8_t)index;
    setup[5] = (uint8_t)(index >> 8);
    setup[6] = (uint8_t)length;
    setup[7] = (uint8_t)(length >> 8);
}

static void auth_engine_fail(usb_auth_error_t error)
{
    s_engine_error = error;
    s_engine_state = USB_AUTH_ENGINE_FAILED;
    s_host_device_ready = 0u;
}

static bool auth_control_transfer(const uint8_t setup[8],
                                  uint8_t *data,
                                  uint8_t capacity,
                                  uint8_t *actual)
{
    uint8_t status = usb_host_control_transfer(setup,
                                               data,
                                               capacity,
                                               actual);
    if(status == ERR_SUCCESS)
    {
        s_transfer_failures = 0u;
        return true;
    }
    if(!usb_host_is_enumerated())
    {
        s_host_bound = 0u;
        s_host_device_ready = 0u;
        s_engine_state = USB_AUTH_ENGINE_WAIT_DEVICE;
        s_phase = ENGINE_PHASE_BIND;
        s_wait_started_cycles = auth_now_cycles();
        return false;
    }
    if(++s_transfer_failures >= AUTH_TRANSFER_RETRY_LIMIT)
    {
        auth_engine_fail(USB_AUTH_ERROR_HOST_TRANSFER);
    }
    return false;
}

static bool auth_gip_queue_push(auth_gip_queue_t *queue,
                                const uint8_t *data,
                                uint8_t length)
{
    uint8_t head;
    uint8_t next;
    if((queue == 0) || (data == 0) || (length == 0u) ||
       (length > USB_GIP_PACKET_MAX_BYTES))
    {
        return false;
    }
    head = queue->head;
    next = (uint8_t)((head + 1u) % AUTH_GIP_QUEUE_SLOTS);
    if(next == queue->tail)
    {
        return false;
    }
    memcpy(queue->frames[head].data, data, length);
    queue->frames[head].length = length;
    /*
     * Publish only after the frame is complete. Each queue is SPSC:
     * USB ISR -> process, process -> USB ISR, or process -> process.
     */
    queue->head = next;
    return true;
}

static bool auth_gip_queue_peek(auth_gip_queue_t *queue,
                                const uint8_t **data,
                                uint8_t *length)
{
    if((queue == 0) || (data == 0) || (length == 0) ||
       (queue->tail == queue->head))
    {
        return false;
    }
    *data = queue->frames[queue->tail].data;
    *length = queue->frames[queue->tail].length;
    return true;
}

static void auth_gip_queue_pop(auth_gip_queue_t *queue)
{
    if((queue != 0) && (queue->tail != queue->head))
    {
        queue->tail =
            (uint8_t)((queue->tail + 1u) % AUTH_GIP_QUEUE_SLOTS);
    }
}

static bool auth_gip_queue_message(auth_gip_queue_t *queue,
                                   uint8_t command,
                                   uint8_t sequence,
                                   bool internal,
                                   bool needs_ack,
                                   const uint8_t *data,
                                   uint16_t length)
{
    uint8_t packet[USB_GIP_PACKET_MAX_BYTES];
    uint8_t packet_length;
    uint8_t required_frames;
    uint8_t available_frames;

    required_frames = (length <= USB_GIP_CHUNK_DATA_BYTES)
        ? 1u
        : (uint8_t)(((length + USB_GIP_CHUNK_DATA_BYTES - 1u) /
                     USB_GIP_CHUNK_DATA_BYTES) + 1u);
    available_frames =
        (uint8_t)((queue->tail + AUTH_GIP_QUEUE_SLOTS -
                   queue->head - 1u) % AUTH_GIP_QUEUE_SLOTS);
    if(required_frames > available_frames)
    {
        return false;
    }

    if(!usb_gip_tx_begin(&s_gip_tx_scratch,
                         command,
                         sequence,
                         internal,
                         needs_ack,
                         data,
                         length))
    {
        return false;
    }
    while(!usb_gip_tx_complete(&s_gip_tx_scratch))
    {
        if(!usb_gip_tx_next(&s_gip_tx_scratch,
                            packet,
                            &packet_length) ||
           !auth_gip_queue_push(queue, packet, packet_length))
        {
            return false;
        }
    }
    return true;
}

static void auth_reset_runtime(void)
{
    memset(&s_host_interface, 0, sizeof(s_host_interface));
    s_host_bound = 0u;
    s_host_device_ready = 0u;
    s_transfer_failures = 0u;
    s_poll_count = 0u;
    s_wait_started_cycles = auth_now_cycles();

    memset(s_ps4_nonce, 0, sizeof(s_ps4_nonce));
    memset(s_ps4_signature, 0, sizeof(s_ps4_signature));
    s_ps4_nonce_id = 0u;
    s_ps4_nonce_pages = 0u;
    s_ps4_host_page = 0u;
    s_ps4_host_chunk = 0u;
    s_ps4_device_chunk = 0u;
    s_ps4_nonce_ready = 0u;
    s_ps4_signature_ready = 0u;

    memset(s_x360_console_initial, 0, sizeof(s_x360_console_initial));
    memset(s_x360_serial, 0, sizeof(s_x360_serial));
    memset(s_x360_request, 0, sizeof(s_x360_request));
    memset(s_x360_reply, 0, sizeof(s_x360_reply));
    s_x360_request_id = 0u;
    s_x360_request_length = 0u;
    s_x360_reply_length = 0u;
    s_x360_has_initial = 0u;
    s_x360_request_pending = 0u;
    s_x360_reply_ready = 0u;
    s_x360_serial_ready = 0u;

    memset(&s_gip_device_rx, 0, sizeof(s_gip_device_rx));
    memset(&s_gip_device_tx, 0, sizeof(s_gip_device_tx));
    memset(&s_gip_host_tx, 0, sizeof(s_gip_host_tx));
    usb_gip_rx_reset(&s_gip_device_parser);
    usb_gip_rx_reset(&s_gip_host_parser);
    usb_gip_tx_reset(&s_gip_tx_scratch);
    s_gip_last_host_tx_cycles = auth_now_cycles();
    s_gip_host_tx_failures = 0u;
}

static bool auth_report_descriptor_is_ps4(const uint8_t *descriptor,
                                          uint16_t length)
{
    uint16_t index;
    uint8_t usage_page_found = 0u;
    uint8_t report_f3_found = 0u;

    for(index = 0u; index + 2u < length; ++index)
    {
        if((descriptor[index] == 0x06u) &&
           (descriptor[index + 1u] == 0xF0u) &&
           (descriptor[index + 2u] == 0xFFu))
        {
            usage_page_found = 1u;
        }
        if((descriptor[index] == 0x85u) &&
           (descriptor[index + 1u] == PS4_REPORT_RESET))
        {
            report_f3_found = 1u;
        }
    }
    return (usage_page_found != 0u) && (report_f3_found != 0u);
}

static bool auth_bind_ps4(void)
{
    uint8_t index;
    uint8_t setup[8];

    for(index = 0u; index < usb_host_interface_count(); ++index)
    {
        usb_host_interface_t candidate;
        uint16_t report_length;
        uint16_t actual = 0u;
        uint8_t status;

        if(!usb_host_get_interface(index, &candidate) ||
           (candidate.class_code != USB_DEV_CLASS_HID) ||
           (candidate.hid_report_descriptor_length == 0u))
        {
            continue;
        }
        report_length = candidate.hid_report_descriptor_length;
        if(report_length > USB_HOST_DESCRIPTOR_MAX_BYTES)
        {
            continue;
        }
        auth_setup_packet(setup, 0x81u, USB_GET_DESCRIPTOR,
                          (uint16_t)(USB_DESCR_TYP_REPORT << 8),
                          candidate.number, report_length);
        status = usb_host_control_transfer_descriptor(
            setup, s_control_buffer, sizeof(s_control_buffer), &actual);
        if(status != ERR_SUCCESS)
        {
            continue;
        }
        if(auth_report_descriptor_is_ps4(s_control_buffer, actual))
        {
            s_host_interface = candidate;
            return true;
        }
    }
    auth_engine_fail(USB_AUTH_ERROR_WRONG_DEVICE);
    return false;
}

static bool auth_bind_host(void)
{
    bool found = false;

    switch(s_scheme)
    {
    case USB_AUTH_SCHEME_PS4:
        found = auth_bind_ps4();
        if(found)
        {
            s_phase = ENGINE_PHASE_PS4_GET_DEFINITION;
        }
        break;
    case USB_AUTH_SCHEME_XINPUT:
        found = usb_host_find_auth_interface(
            USB_HOST_AUTH_INTERFACE_XINPUT, &s_host_interface);
        if(found)
        {
            s_phase = ENGINE_PHASE_X360_GET_SECURITY_STRING;
        }
        break;
    case USB_AUTH_SCHEME_XBOX_GIP:
        found = usb_host_find_auth_interface(
            USB_HOST_AUTH_INTERFACE_XBOX_GIP, &s_host_interface);
        if(found &&
           ((s_host_interface.interrupt_in_endpoint == 0u) ||
            (s_host_interface.interrupt_out_endpoint == 0u)))
        {
            found = false;
        }
        if(found)
        {
            s_phase = ENGINE_PHASE_GIP_RUNNING;
        }
        break;
    default:
        break;
    }

    if(!found)
    {
        if(s_engine_state != USB_AUTH_ENGINE_FAILED)
        {
            auth_engine_fail(USB_AUTH_ERROR_WRONG_DEVICE);
        }
        return false;
    }
    s_host_bound = 1u;
    s_engine_state = USB_AUTH_ENGINE_RUNNING;
    s_engine_error = USB_AUTH_ERROR_NONE;
    if(s_scheme == USB_AUTH_SCHEME_XBOX_GIP)
    {
        /* GIP host readiness follows its announce/descriptor handshake. */
        s_host_device_ready = 0u;
    }
    return true;
}

static bool auth_ps4_feature_transfer(bool input,
                                      uint8_t report_id,
                                      uint8_t *data,
                                      uint8_t length)
{
    uint8_t setup[8];
    uint8_t actual = 0u;

    auth_setup_packet(setup,
                      input ? 0xA1u : 0x21u,
                      input ? AUTH_HID_GET_REPORT : AUTH_HID_SET_REPORT,
                      (uint16_t)((HID_REPORT_TYPE_FEATURE << 8) | report_id),
                      s_host_interface.number,
                      length);
    if(!auth_control_transfer(setup, data, length, &actual))
    {
        return false;
    }
    return input ? (actual == length) : true;
}

static void auth_process_ps4(void)
{
    switch(s_phase)
    {
    case ENGINE_PHASE_PS4_GET_DEFINITION:
        /*
         * Preserve the existing STM32 listener's mount-time probe.  The
         * 48-byte control transfer contains report ID 0x03 followed by the
         * 47-byte controller definition payload.
         */
        memset(s_control_buffer, 0, 48u);
        s_control_buffer[0] = PS4_REPORT_GET_DEFINITION;
        if(auth_ps4_feature_transfer(true,
                                     PS4_REPORT_GET_DEFINITION,
                                     s_control_buffer,
                                     48u))
        {
            s_host_device_ready = 1u;
            s_phase = ENGINE_PHASE_PS4_WAIT_NONCE;
        }
        break;

    case ENGINE_PHASE_PS4_WAIT_NONCE:
        if(s_ps4_nonce_ready != 0u)
        {
            s_ps4_nonce_ready = 0u;
            s_ps4_signature_ready = 0u;
            s_ps4_host_page = 0u;
            s_ps4_host_chunk = 0u;
            s_ps4_device_chunk = 0u;
            s_phase = ENGINE_PHASE_PS4_RESET;
        }
        break;

    case ENGINE_PHASE_PS4_RESET:
        memset(s_control_buffer, 0, 7u);
        s_control_buffer[1] = 0x38u;
        s_control_buffer[2] = 0x38u;
        if(auth_ps4_feature_transfer(true, PS4_REPORT_RESET,
                                     s_control_buffer, 7u))
        {
            s_phase = ENGINE_PHASE_PS4_SEND_PAGE;
        }
        break;

    case ENGINE_PHASE_PS4_SEND_PAGE:
    {
        uint8_t copy_length =
            (s_ps4_host_page == (PS4_NONCE_PAGES - 1u)) ? 32u : 56u;
        uint32_t crc;

        memset(s_control_buffer, 0, 64u);
        s_control_buffer[0] = PS4_REPORT_SET_AUTH;
        s_control_buffer[1] = s_ps4_nonce_id;
        s_control_buffer[2] = s_ps4_host_page;
        memcpy(&s_control_buffer[4],
               &s_ps4_nonce[(uint16_t)s_ps4_host_page * PS4_PAGE_BYTES],
               copy_length);
        crc = auth_crc32(s_control_buffer, 60u);
        auth_store_u32_le(&s_control_buffer[60], crc);
        if(auth_ps4_feature_transfer(false, PS4_REPORT_SET_AUTH,
                                     s_control_buffer, 64u))
        {
            ++s_ps4_host_page;
            if(s_ps4_host_page == PS4_NONCE_PAGES)
            {
                s_poll_count = 0u;
                s_wait_started_cycles = auth_now_cycles();
                s_phase = ENGINE_PHASE_PS4_WAIT_SIGNATURE;
            }
        }
        break;
    }

    case ENGINE_PHASE_PS4_WAIT_SIGNATURE:
        if(!auth_elapsed_ms(s_wait_started_cycles, AUTH_POLL_INTERVAL_MS))
        {
            break;
        }
        memset(s_control_buffer, 0, 16u);
        s_control_buffer[0] = PS4_REPORT_GET_STATE;
        s_control_buffer[1] = s_ps4_nonce_id;
        if(auth_ps4_feature_transfer(true, PS4_REPORT_GET_STATE,
                                     s_control_buffer, 16u))
        {
            if(s_control_buffer[2] == 0u)
            {
                s_ps4_host_chunk = 0u;
                s_phase = ENGINE_PHASE_PS4_READ_SIGNATURE;
            }
            else if(++s_poll_count >= AUTH_POLL_LIMIT)
            {
                auth_engine_fail(USB_AUTH_ERROR_TIMEOUT);
            }
            else
            {
                s_wait_started_cycles = auth_now_cycles();
            }
        }
        break;

    case ENGINE_PHASE_PS4_READ_SIGNATURE:
        memset(s_control_buffer, 0, 64u);
        s_control_buffer[0] = PS4_REPORT_GET_SIGNATURE;
        s_control_buffer[1] = s_ps4_nonce_id;
        s_control_buffer[2] = s_ps4_host_chunk;
        if(auth_ps4_feature_transfer(true, PS4_REPORT_GET_SIGNATURE,
                                     s_control_buffer, 64u))
        {
            memcpy(&s_ps4_signature[
                       (uint16_t)s_ps4_host_chunk * PS4_PAGE_BYTES],
                   &s_control_buffer[4],
                   PS4_PAGE_BYTES);
            ++s_ps4_host_chunk;
            if(s_ps4_host_chunk == PS4_SIGNATURE_CHUNKS)
            {
                s_ps4_signature_ready = 1u;
                s_ps4_device_chunk = 0u;
                s_phase = ENGINE_PHASE_PS4_WAIT_NONCE;
            }
        }
        break;

    default:
        auth_engine_fail(USB_AUTH_ERROR_PROTOCOL);
        break;
    }
}

static bool auth_x360_vendor_transfer(bool input,
                                      uint8_t request,
                                      uint16_t value,
                                      uint8_t *data,
                                      uint8_t length)
{
    uint8_t setup[8];
    uint8_t actual = 0u;

    auth_setup_packet(setup, input ? 0xC1u : 0x41u,
                      request, value, X360_WINDEX_SECURITY, length);
    if(!auth_control_transfer(setup, data, length, &actual))
    {
        return false;
    }
    return input ? (actual == length) : true;
}

static void auth_process_x360(void)
{
    switch(s_phase)
    {
    case ENGINE_PHASE_X360_GET_SECURITY_STRING:
    {
        uint8_t setup[8];
        uint8_t actual = 0u;
        auth_setup_packet(setup, 0x80u, USB_GET_DESCRIPTOR,
                          0x0304u, 0x0409u, 0x00B2u);
        /*
         * The legacy implementation deliberately ignored the XSM3 string
         * result. Some adapters only need the request as a wake-up probe.
         */
        (void)usb_host_control_transfer(setup, s_control_buffer,
                                        0xB2u, &actual);
        s_transfer_failures = 0u;
        s_phase = ENGINE_PHASE_X360_GET_SERIAL;
        break;
    }

    case ENGINE_PHASE_X360_GET_SERIAL:
        memset(s_control_buffer, 0, X360_SERIAL_BYTES);
        if(auth_x360_vendor_transfer(true, X360_GET_SERIAL,
                                     X360_WVALUE_CONTROLLER_ID,
                                     s_control_buffer,
                                     X360_SERIAL_BYTES))
        {
            memcpy(s_x360_serial, s_control_buffer, X360_SERIAL_BYTES);
            s_x360_serial_ready = 1u;
            s_host_device_ready = 1u;
            if(s_x360_has_initial != 0u)
            {
                memcpy(s_x360_request, s_x360_console_initial,
                       X360_CONSOLE_INIT_BYTES);
                s_x360_request_id = X360_INIT_AUTH;
                s_x360_request_length = X360_CONSOLE_INIT_BYTES;
                s_x360_request_pending = 1u;
            }
            s_phase = ENGINE_PHASE_X360_IDLE;
        }
        break;

    case ENGINE_PHASE_X360_IDLE:
        if(s_x360_request_pending != 0u)
        {
            s_x360_request_pending = 0u;
            s_x360_reply_ready = 0u;
            s_phase = ENGINE_PHASE_X360_SEND_REQUEST;
        }
        break;

    case ENGINE_PHASE_X360_SEND_REQUEST:
        if(auth_x360_vendor_transfer(false,
                                     s_x360_request_id,
                                     X360_WVALUE_CONSOLE_DATA,
                                     s_x360_request,
                                     s_x360_request_length))
        {
            s_poll_count = 0u;
            s_wait_started_cycles = auth_now_cycles();
            s_phase = ENGINE_PHASE_X360_WAIT_STATE;
        }
        break;

    case ENGINE_PHASE_X360_WAIT_STATE:
        if(!auth_elapsed_ms(s_wait_started_cycles, AUTH_POLL_INTERVAL_MS))
        {
            break;
        }
        memset(s_control_buffer, 0, 2u);
        if(auth_x360_vendor_transfer(true, X360_REQUEST_STATE,
                                     0u, s_control_buffer, 2u))
        {
            if(s_control_buffer[0] == 2u)
            {
                s_phase = ENGINE_PHASE_X360_GET_REPLY;
            }
            else if(++s_poll_count >= AUTH_POLL_LIMIT)
            {
                auth_engine_fail(USB_AUTH_ERROR_TIMEOUT);
            }
            else
            {
                s_wait_started_cycles = auth_now_cycles();
            }
        }
        break;

    case ENGINE_PHASE_X360_GET_REPLY:
    {
        const uint8_t reply_length =
            (s_x360_request_id == X360_INIT_AUTH)
                ? X360_INIT_REPLY_BYTES
                : X360_VERIFY_BYTES;
        const uint16_t value =
            (s_x360_request_id == X360_INIT_AUTH)
                ? X360_WVALUE_INIT_REPLY
                : X360_WVALUE_VERIFY_REPLY;
        memset(s_control_buffer, 0, reply_length);
        if(auth_x360_vendor_transfer(true, X360_RESPOND_CHALLENGE,
                                     value, s_control_buffer,
                                     reply_length))
        {
            memcpy(s_x360_reply, s_control_buffer, reply_length);
            s_x360_reply_length = reply_length;
            if(s_x360_request_id == X360_INIT_AUTH)
            {
                s_phase = ENGINE_PHASE_X360_KEEPALIVE;
            }
            else
            {
                s_x360_reply_ready = 1u;
                s_phase = ENGINE_PHASE_X360_IDLE;
            }
        }
        break;
    }

    case ENGINE_PHASE_X360_KEEPALIVE:
    {
        uint8_t setup[8];
        uint8_t actual = 0u;
        auth_setup_packet(setup, 0xC1u, X360_AUTH_KEEPALIVE,
                          X360_WVALUE_CONSOLE_DATA,
                          X360_WINDEX_SECURITY, 0u);
        /*
         * Some authenticators STALL this advisory request. Match the legacy
         * behavior: issue it, then expose the already valid challenge reply.
         */
        (void)usb_host_control_transfer(setup, 0, 0u, &actual);
        s_transfer_failures = 0u;
        s_x360_reply_ready = 1u;
        s_phase = ENGINE_PHASE_X360_IDLE;
        break;
    }

    default:
        auth_engine_fail(USB_AUTH_ERROR_PROTOCOL);
        break;
    }
}

static void auth_gip_queue_ack(auth_gip_queue_t *queue,
                               const usb_gip_rx_t *parser)
{
    uint8_t packet[USB_GIP_PACKET_MAX_BYTES];
    uint8_t length;
    if(usb_gip_rx_make_ack(parser, packet, &length) &&
       !auth_gip_queue_push(queue, packet, length))
    {
        auth_engine_fail(USB_AUTH_ERROR_QUEUE_FULL);
    }
}

static void auth_gip_process_device_frame(void)
{
    const uint8_t *packet;
    uint8_t length;

    if(!auth_gip_queue_peek(&s_gip_device_rx, &packet, &length))
    {
        return;
    }
    if(!usb_gip_rx_consume(&s_gip_device_parser, packet, length))
    {
        auth_gip_queue_pop(&s_gip_device_rx);
        auth_engine_fail(USB_AUTH_ERROR_PROTOCOL);
        return;
    }
    auth_gip_queue_pop(&s_gip_device_rx);

    if(usb_gip_rx_ack_required(&s_gip_device_parser))
    {
        auth_gip_queue_ack(&s_gip_device_tx, &s_gip_device_parser);
    }
    if((s_gip_device_parser.complete != 0u) &&
       ((s_gip_device_parser.command == GIP_AUTH) ||
        (s_gip_device_parser.command == GIP_FINAL_AUTH)))
    {
        if((s_gip_device_parser.data_length ==
            sizeof(s_gip_auth_ready)) &&
           (memcmp(s_gip_device_parser.data, s_gip_auth_ready,
                   sizeof(s_gip_auth_ready)) == 0))
        {
            s_engine_state = USB_AUTH_ENGINE_AUTHENTICATED;
        }
        if(!auth_gip_queue_message(
               &s_gip_host_tx,
               s_gip_device_parser.command,
               s_gip_device_parser.sequence,
               true,
               s_gip_device_parser.data_length > 2u,
               s_gip_device_parser.data,
               s_gip_device_parser.data_length))
        {
            auth_engine_fail(USB_AUTH_ERROR_QUEUE_FULL);
        }
        usb_gip_rx_reset(&s_gip_device_parser);
    }
}

static void auth_gip_host_handshake_complete(void)
{
    if(!auth_gip_queue_message(&s_gip_host_tx, GIP_POWER_MODE, 2u,
                               true, false,
                               s_gip_power_on, sizeof(s_gip_power_on)) ||
       !auth_gip_queue_message(&s_gip_host_tx, GIP_POWER_MODE, 3u,
                               true, false,
                               s_gip_power_on_single,
                               sizeof(s_gip_power_on_single)) ||
       !auth_gip_queue_message(&s_gip_host_tx, GIP_LED, 1u,
                               false, false,
                               s_gip_led_on, sizeof(s_gip_led_on)) ||
       !auth_gip_queue_message(&s_gip_host_tx, GIP_RUMBLE, 1u,
                               false, false,
                               s_gip_rumble_on,
                               sizeof(s_gip_rumble_on)))
    {
        auth_engine_fail(USB_AUTH_ERROR_QUEUE_FULL);
        return;
    }
    s_host_device_ready = 1u;
}

static void auth_gip_process_host_frame(const uint8_t *packet,
                                        uint8_t length)
{
    if(!usb_gip_rx_consume(&s_gip_host_parser, packet, length))
    {
        auth_engine_fail(USB_AUTH_ERROR_PROTOCOL);
        return;
    }
    if(usb_gip_rx_ack_required(&s_gip_host_parser))
    {
        auth_gip_queue_ack(&s_gip_host_tx, &s_gip_host_parser);
    }

    switch(s_gip_host_parser.command)
    {
    case GIP_ANNOUNCE:
        if(!auth_gip_queue_message(&s_gip_host_tx,
                                   GIP_DEVICE_DESCRIPTOR, 1u,
                                   true, false, 0, 0u))
        {
            auth_engine_fail(USB_AUTH_ERROR_QUEUE_FULL);
        }
        usb_gip_rx_reset(&s_gip_host_parser);
        break;
    case GIP_DEVICE_DESCRIPTOR:
        if((s_gip_host_parser.complete != 0u) &&
           ((s_gip_host_parser.chunked == 0u) ||
            (s_gip_host_parser.chunk_ended != 0u)))
        {
            auth_gip_host_handshake_complete();
            usb_gip_rx_reset(&s_gip_host_parser);
        }
        break;
    case GIP_AUTH:
    case GIP_FINAL_AUTH:
        if(s_gip_host_parser.complete != 0u)
        {
            if(!auth_gip_queue_message(
                   &s_gip_device_tx,
                   s_gip_host_parser.command,
                   s_gip_host_parser.sequence,
                   true,
                   s_gip_host_parser.data_length > 2u,
                   s_gip_host_parser.data,
                   s_gip_host_parser.data_length))
            {
                auth_engine_fail(USB_AUTH_ERROR_QUEUE_FULL);
            }
            usb_gip_rx_reset(&s_gip_host_parser);
        }
        break;
    case GIP_ACK_RESPONSE:
        usb_gip_rx_reset(&s_gip_host_parser);
        break;
    default:
        if(s_gip_host_parser.complete != 0u)
        {
            usb_gip_rx_reset(&s_gip_host_parser);
        }
        break;
    }
}

static void auth_process_gip(void)
{
    const uint8_t *packet;
    uint8_t packet_length;
    uint8_t status;

    auth_gip_process_device_frame();
    if(s_engine_state == USB_AUTH_ENGINE_FAILED)
    {
        return;
    }

    if(auth_gip_queue_peek(&s_gip_host_tx, &packet, &packet_length) &&
       auth_elapsed_ms(s_gip_last_host_tx_cycles,
                       AUTH_GIP_QUEUE_INTERVAL_MS))
    {
        status = usb_host_interrupt_out(
            s_host_interface.interrupt_out_endpoint,
            packet, packet_length, USB_HOST_MAX_NAK_RETRY_20US);
        s_gip_last_host_tx_cycles = auth_now_cycles();
        if(status == ERR_SUCCESS)
        {
            auth_gip_queue_pop(&s_gip_host_tx);
            s_gip_host_tx_failures = 0u;
        }
        else if(++s_gip_host_tx_failures >= AUTH_POLL_LIMIT)
        {
            auth_engine_fail(USB_AUTH_ERROR_TIMEOUT);
            return;
        }
    }

    packet_length = 0u;
    status = usb_host_interrupt_in(
        s_host_interface.interrupt_in_endpoint,
        s_control_buffer, USB_GIP_PACKET_MAX_BYTES,
        &packet_length, 0u);
    if(status == ERR_SUCCESS)
    {
        if(packet_length != 0u)
        {
            auth_gip_process_host_frame(s_control_buffer, packet_length);
        }
    }
    else if(!usb_host_is_enumerated())
    {
        s_host_bound = 0u;
        s_host_device_ready = 0u;
        s_engine_state = USB_AUTH_ENGINE_WAIT_DEVICE;
        s_phase = ENGINE_PHASE_BIND;
        s_wait_started_cycles = auth_now_cycles();
    }
}

void usb_auth_host_engine_init(void)
{
    s_scheme = USB_AUTH_SCHEME_NONE;
    s_engine_state = USB_AUTH_ENGINE_IDLE;
    s_engine_error = USB_AUTH_ERROR_NONE;
    s_phase = ENGINE_PHASE_IDLE;
    auth_reset_runtime();
}

void usb_auth_host_engine_clear(void)
{
    s_scheme = USB_AUTH_SCHEME_NONE;
    s_engine_state = USB_AUTH_ENGINE_IDLE;
    s_engine_error = USB_AUTH_ERROR_NONE;
    s_phase = ENGINE_PHASE_IDLE;
    auth_reset_runtime();
}

bool usb_auth_host_engine_begin(usb_auth_scheme_t scheme)
{
    if((scheme != USB_AUTH_SCHEME_PS4) &&
       (scheme != USB_AUTH_SCHEME_XINPUT) &&
       (scheme != USB_AUTH_SCHEME_XBOX_GIP))
    {
        s_engine_error = USB_AUTH_ERROR_UNSUPPORTED;
        s_engine_state = USB_AUTH_ENGINE_FAILED;
        return false;
    }
    s_scheme = scheme;
    s_engine_error = USB_AUTH_ERROR_NONE;
    s_engine_state = USB_AUTH_ENGINE_WAIT_DEVICE;
    s_phase = ENGINE_PHASE_BIND;
    auth_reset_runtime();
    return true;
}

void usb_auth_host_engine_process(void)
{
    if(s_engine_state == USB_AUTH_ENGINE_IDLE)
    {
        return;
    }

    /*
     * A missing or wrong authentication device is fail-closed, but it must
     * not make hot-plug recovery impossible.  Once the failed device has
     * actually left the host bus, retain the console-side challenge cache and
     * return to discovery so a replacement authenticator can be enumerated.
     */
    if(s_engine_state == USB_AUTH_ENGINE_FAILED)
    {
        if(!usb_host_is_enumerated())
        {
            s_host_bound = 0u;
            s_host_device_ready = 0u;
            s_transfer_failures = 0u;
            s_poll_count = 0u;
            s_engine_error = USB_AUTH_ERROR_NONE;
            s_engine_state = USB_AUTH_ENGINE_WAIT_DEVICE;
            s_phase = ENGINE_PHASE_BIND;
            s_wait_started_cycles = auth_now_cycles();
        }
        return;
    }

    if(!usb_host_is_enumerated())
    {
        s_host_bound = 0u;
        s_host_device_ready = 0u;
        s_engine_state = USB_AUTH_ENGINE_WAIT_DEVICE;
        s_phase = ENGINE_PHASE_BIND;
        if(auth_elapsed_ms(s_wait_started_cycles,
                           AUTH_HOST_WAIT_TIMEOUT_MS))
        {
            auth_engine_fail(USB_AUTH_ERROR_TIMEOUT);
        }
        return;
    }

    if(s_host_bound == 0u)
    {
        if(!auth_bind_host())
        {
            return;
        }
    }

    switch(s_scheme)
    {
    case USB_AUTH_SCHEME_PS4:
        auth_process_ps4();
        break;
    case USB_AUTH_SCHEME_XINPUT:
        auth_process_x360();
        break;
    case USB_AUTH_SCHEME_XBOX_GIP:
        auth_process_gip();
        break;
    default:
        auth_engine_fail(USB_AUTH_ERROR_UNSUPPORTED);
        break;
    }
}

usb_auth_engine_state_t usb_auth_host_engine_state(void)
{
    return s_engine_state;
}

usb_auth_error_t usb_auth_host_engine_error(void)
{
    return s_engine_error;
}

bool usb_auth_host_engine_device_ready(void)
{
    return s_host_device_ready != 0u;
}

bool usb_auth_host_engine_hid_set_feature(uint8_t report_id,
                                          const uint8_t *data,
                                          uint16_t length)
{
    uint8_t full_report[64];
    uint8_t nonce_id;
    uint8_t nonce_page;
    uint8_t copy_length;
    uint32_t expected_crc;

    if((s_scheme != USB_AUTH_SCHEME_PS4) ||
       (report_id != PS4_REPORT_SET_AUTH) ||
       (data == 0) || (length != 63u))
    {
        return false;
    }
    full_report[0] = report_id;
    memcpy(&full_report[1], data, length);
    expected_crc = auth_load_u32_le(&full_report[60]);
    if(auth_crc32(full_report, 60u) != expected_crc)
    {
        s_engine_error = USB_AUTH_ERROR_PROTOCOL;
        return false;
    }

    nonce_id = data[0];
    nonce_page = data[1];
    if(nonce_page >= PS4_NONCE_PAGES)
    {
        s_engine_error = USB_AUTH_ERROR_PROTOCOL;
        return false;
    }
    if(nonce_page == 0u)
    {
        memset(s_ps4_nonce, 0, sizeof(s_ps4_nonce));
        s_ps4_nonce_id = nonce_id;
        s_ps4_nonce_pages = 0u;
        s_ps4_signature_ready = 0u;
    }
    else if(nonce_id != s_ps4_nonce_id)
    {
        s_engine_error = USB_AUTH_ERROR_PROTOCOL;
        return false;
    }
    if(nonce_page != s_ps4_nonce_pages)
    {
        s_engine_error = USB_AUTH_ERROR_OUT_OF_ORDER;
        return false;
    }

    copy_length = (nonce_page == (PS4_NONCE_PAGES - 1u)) ? 32u : 56u;
    memcpy(&s_ps4_nonce[(uint16_t)nonce_page * PS4_PAGE_BYTES],
           &full_report[4], copy_length);
    ++s_ps4_nonce_pages;
    if(s_ps4_nonce_pages == PS4_NONCE_PAGES)
    {
        s_ps4_nonce_ready = 1u;
    }
    s_engine_error = USB_AUTH_ERROR_NONE;
    return true;
}

bool usb_auth_host_engine_hid_get_feature(uint8_t report_id,
                                          uint8_t *data,
                                          uint16_t capacity,
                                          uint16_t *length)
{
    uint8_t full_report[64];
    uint32_t crc;

    if(length != 0)
    {
        *length = 0u;
    }
    if((s_scheme != USB_AUTH_SCHEME_PS4) ||
       (data == 0) || (length == 0))
    {
        return false;
    }

    memset(full_report, 0, sizeof(full_report));
    switch(report_id)
    {
    case PS4_REPORT_RESET:
        if(capacity < 7u)
        {
            return false;
        }
        data[0] = 0u;
        data[1] = 0x38u;
        data[2] = 0x38u;
        memset(&data[3], 0, 4u);
        *length = 7u;
        s_ps4_signature_ready = 0u;
        s_ps4_device_chunk = 0u;
        return true;

    case PS4_REPORT_GET_STATE:
        if(capacity < 15u)
        {
            return false;
        }
        full_report[0] = report_id;
        full_report[1] = s_ps4_nonce_id;
        full_report[2] = (s_ps4_signature_ready != 0u) ? 0u : 16u;
        crc = auth_crc32(full_report, 12u);
        auth_store_u32_le(&full_report[12], crc);
        memcpy(data, &full_report[1], 15u);
        *length = 15u;
        return true;

    case PS4_REPORT_GET_SIGNATURE:
        if((capacity < 63u) || (s_ps4_signature_ready == 0u) ||
           (s_ps4_device_chunk >= PS4_SIGNATURE_CHUNKS))
        {
            return false;
        }
        full_report[0] = report_id;
        full_report[1] = s_ps4_nonce_id;
        full_report[2] = s_ps4_device_chunk;
        memcpy(&full_report[4],
               &s_ps4_signature[
                   (uint16_t)s_ps4_device_chunk * PS4_PAGE_BYTES],
               PS4_PAGE_BYTES);
        crc = auth_crc32(full_report, 60u);
        auth_store_u32_le(&full_report[60], crc);
        memcpy(data, &full_report[1], 63u);
        *length = 63u;
        ++s_ps4_device_chunk;
        if(s_ps4_device_chunk == PS4_SIGNATURE_CHUNKS)
        {
            s_engine_state = USB_AUTH_ENGINE_AUTHENTICATED;
            s_ps4_signature_ready = 0u;
            s_ps4_device_chunk = 0u;
        }
        return true;

    default:
        return false;
    }
}

bool usb_auth_host_engine_vendor_out(uint8_t request,
                                     uint16_t value,
                                     uint16_t index,
                                     const uint8_t *data,
                                     uint16_t length)
{
    (void)value;
    (void)index;
    if((s_scheme != USB_AUTH_SCHEME_XINPUT) || (data == 0))
    {
        return false;
    }
    if((request == X360_INIT_AUTH) &&
       (length == X360_CONSOLE_INIT_BYTES))
    {
        memcpy(s_x360_console_initial, data, length);
        memcpy(s_x360_request, data, length);
        s_x360_has_initial = 1u;
    }
    else if((request == X360_VERIFY_AUTH) &&
            (length == X360_VERIFY_BYTES))
    {
        memcpy(s_x360_request, data, length);
    }
    else
    {
        return false;
    }
    s_x360_request_id = request;
    s_x360_request_length = (uint8_t)length;
    s_x360_request_pending = 1u;
    s_x360_reply_ready = 0u;
    return true;
}

bool usb_auth_host_engine_vendor_in(uint8_t request,
                                    uint16_t value,
                                    uint16_t index,
                                    uint8_t *data,
                                    uint16_t capacity,
                                    uint16_t *length)
{
    uint16_t state;
    (void)value;
    (void)index;

    if(length != 0)
    {
        *length = 0u;
    }
    if((s_scheme != USB_AUTH_SCHEME_XINPUT) || (length == 0))
    {
        return false;
    }

    switch(request)
    {
    case X360_GET_SERIAL:
        if((data == 0) || (capacity < X360_SERIAL_BYTES) ||
           (s_x360_serial_ready == 0u))
        {
            return false;
        }
        memcpy(data, s_x360_serial, X360_SERIAL_BYTES);
        *length = X360_SERIAL_BYTES;
        return true;

    case X360_RESPOND_CHALLENGE:
        if((data == 0) || (s_x360_reply_ready == 0u) ||
           (capacity < s_x360_reply_length))
        {
            return false;
        }
        memcpy(data, s_x360_reply, s_x360_reply_length);
        *length = s_x360_reply_length;
        s_x360_reply_ready = 0u;
        if(s_x360_request_id == X360_VERIFY_AUTH)
        {
            s_engine_state = USB_AUTH_ENGINE_AUTHENTICATED;
        }
        return true;

    case X360_AUTH_KEEPALIVE:
        *length = 0u;
        return true;

    case X360_REQUEST_STATE:
        if((data == 0) || (capacity < 2u))
        {
            return false;
        }
        state = (s_x360_reply_ready != 0u) ? 2u : 1u;
        data[0] = (uint8_t)state;
        data[1] = (uint8_t)(state >> 8);
        *length = 2u;
        return true;

    default:
        return false;
    }
}

bool usb_auth_host_engine_gip_device_out(const uint8_t *report,
                                         uint8_t length)
{
    bool queued;

    if((s_scheme != USB_AUTH_SCHEME_XBOX_GIP) ||
        (report == 0) || (length == 0u))
    {
        return false;
    }
    queued = auth_gip_queue_push(&s_gip_device_rx, report, length);
    if(queued &&
       ((report[0] == GIP_AUTH) || (report[0] == GIP_FINAL_AUTH)))
    {
        /*
         * A USB bus reset starts a new console authentication session.  Do
         * not let a previous session's AUTHENTICATED state release input
         * while the new challenge is still travelling to the real device.
         */
        s_engine_state = USB_AUTH_ENGINE_RUNNING;
    }
    return queued;
}

bool usb_auth_host_engine_gip_device_in(uint8_t *report,
                                        uint8_t capacity,
                                        uint8_t *length)
{
    const uint8_t *queued;
    uint8_t queued_length;

    if(length != 0)
    {
        *length = 0u;
    }
    if((s_scheme != USB_AUTH_SCHEME_XBOX_GIP) ||
       (report == 0) || (length == 0) ||
       !auth_gip_queue_peek(&s_gip_device_tx,
                            &queued, &queued_length) ||
       (capacity < queued_length))
    {
        return false;
    }
    memcpy(report, queued, queued_length);
    *length = queued_length;
    auth_gip_queue_pop(&s_gip_device_tx);
    return true;
}

bool usb_auth_host_engine_gip_device_in_pending(void)
{
    return (s_scheme == USB_AUTH_SCHEME_XBOX_GIP) &&
           (s_gip_device_tx.tail != s_gip_device_tx.head);
}
