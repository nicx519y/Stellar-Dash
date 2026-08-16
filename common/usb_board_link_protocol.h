#ifndef USB_BOARD_LINK_PROTOCOL_H
#define USB_BOARD_LINK_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * STM32 <-> on-board CH585 USB control/data link.
 *
 * This namespace is intentionally separate from the frozen RF 0xA5 protocol.
 * A CH585 instance locks into exactly one role after USB_BOARD_CMD_SELECT_ROLE:
 * - RF role: this parser is disabled and the existing 0xA5 stack owns SPI.
 * - USB/maintenance role: the RF stack is never initialized.
 */
#define USB_BOARD_LINK_SYNC                 0x5Au
#define USB_BOARD_LINK_VERSION              1u
#define USB_BOARD_LINK_MAX_FRAME_BYTES      64u
#define USB_BOARD_LINK_HEADER_BYTES         3u
#define USB_BOARD_LINK_CHECKSUM_BYTES       1u
#define USB_BOARD_LINK_MAX_PAYLOAD_BYTES    \
    (USB_BOARD_LINK_MAX_FRAME_BYTES - USB_BOARD_LINK_HEADER_BYTES - USB_BOARD_LINK_CHECKSUM_BYTES)

#define USB_BOARD_INPUT_V1_BYTES            10u
#define USB_BOARD_TELEMETRY_FRAME_BYTES     32u
#define USB_BOARD_FRAGMENT_HEADER_BYTES     8u
#define USB_BOARD_FRAGMENT_DATA_BYTES       \
    (USB_BOARD_LINK_MAX_PAYLOAD_BYTES - USB_BOARD_FRAGMENT_HEADER_BYTES)
#define USB_BOARD_BULK_MESSAGE_MAX_BYTES    1536u
#define USB_BOARD_BULK_CREDIT_WINDOW        4u
/*
 * WebHID reports use one whole-report receiver slot. WebConfig capacity is
 * read through the transaction-correlated USB_CONTROL pull RPC; asynchronous
 * absolute credit events are intentionally ignored for this channel because a
 * delayed snapshot could re-authorize an already-consumed slot. Other bulk
 * channels retain the four-message window.
 */
#define USB_BOARD_WEBCONFIG_REPORT_CREDIT_WINDOW 1u

#define USB_BOARD_FRAGMENT_FLAG_FIRST       0x01u
#define USB_BOARD_FRAGMENT_FLAG_LAST        0x02u

#define USB_BOARD_INPUT_FLAG_PROCESSED      0x01u
#define USB_BOARD_INPUT_FLAG_BATTERY_VALID  0x02u
#define USB_BOARD_INPUT_FORMAT_VERSION      1u
#define USB_BOARD_INPUT_VERSION_SHIFT       4u

typedef enum
{
    USB_BOARD_CMD_SELECT_ROLE   = 0x01u,
    USB_BOARD_CMD_GET_CAPS      = 0x02u,
    USB_BOARD_CMD_SET_PROFILE   = 0x03u,
    USB_BOARD_CMD_INPUT_STATE   = 0x04u,
    USB_BOARD_CMD_USB_CONTROL   = 0x05u,
    USB_BOARD_CMD_BULK_FRAGMENT = 0x06u,
    USB_BOARD_CMD_BULK_CREDIT   = 0x07u,

    USB_BOARD_EVT_ROLE_SELECTED = 0x81u,
    USB_BOARD_EVT_CAPS          = 0x82u,
    USB_BOARD_EVT_PROFILE_SET   = 0x83u,
    USB_BOARD_EVT_USB_CONTROL   = 0x85u,
    USB_BOARD_EVT_BULK_FRAGMENT = 0x86u,
    USB_BOARD_EVT_BULK_CREDIT   = 0x87u,
    USB_BOARD_EVT_USB_STATE     = 0x88u,
    USB_BOARD_EVT_FAULT         = 0x8Fu
} usb_board_command_t;

typedef enum
{
    USB_BOARD_ROLE_NONE        = 0x00u,
    USB_BOARD_ROLE_RF          = 0x01u,
    USB_BOARD_ROLE_USB         = 0x02u,
    USB_BOARD_ROLE_MAINTENANCE = 0x03u
} usb_board_role_t;

typedef enum
{
    USB_BOARD_PROFILE_NONE       = 0x00u,
    USB_BOARD_PROFILE_XINPUT     = 0x01u,
    USB_BOARD_PROFILE_PS4        = 0x02u,
    USB_BOARD_PROFILE_PS5_COMPAT = 0x03u,
    USB_BOARD_PROFILE_SWITCH     = 0x04u,
    USB_BOARD_PROFILE_XBOX_ONE   = 0x05u,
    USB_BOARD_PROFILE_WEB_CONFIG = 0x06u
} usb_board_profile_t;

typedef enum
{
    USB_BOARD_STATUS_OK             = 0x00u,
    USB_BOARD_STATUS_BAD_FRAME      = 0x01u,
    USB_BOARD_STATUS_BAD_LENGTH     = 0x02u,
    USB_BOARD_STATUS_BAD_ROLE       = 0x03u,
    USB_BOARD_STATUS_ROLE_LOCKED    = 0x04u,
    USB_BOARD_STATUS_UNSUPPORTED    = 0x05u,
    USB_BOARD_STATUS_NOT_READY      = 0x06u,
    USB_BOARD_STATUS_BUSY           = 0x07u,
    USB_BOARD_STATUS_CRC_ERROR      = 0x08u,
    USB_BOARD_STATUS_QUEUE_FULL     = 0x09u,
    USB_BOARD_STATUS_INTERNAL_ERROR = 0x0Au
} usb_board_status_t;

typedef enum
{
    USB_BOARD_CHANNEL_USB_DEVICE = 0x01u,
    USB_BOARD_CHANNEL_USB_HOST   = 0x02u,
    USB_BOARD_CHANNEL_NETWORK    = 0x03u,
    USB_BOARD_CHANNEL_TELEMETRY  = 0x04u,
    USB_BOARD_CHANNEL_AUTH       = 0x05u,
    /* Opaque 64-byte SecureHidReportV1 messages; maintenance role only. */
    USB_BOARD_CHANNEL_WEBCONFIG  = 0x06u
} usb_board_channel_t;

#define USB_BOARD_CHANNEL_LAST USB_BOARD_CHANNEL_WEBCONFIG
#define USB_BOARD_CHANNEL_SLOTS ((uint8_t)USB_BOARD_CHANNEL_LAST + 1u)

/*
 * USB_BOARD_CMD_USB_CONTROL is a board-management RPC.  Host EP0 requests are
 * intentionally not tunneled over SPI: enumeration and console
 * authentication timing remain local to CH585. SET_MAC is a retained legacy
 * opcode and returns UNSUPPORTED in the V2 WebHID profile.
 */
typedef enum
{
    USB_BOARD_CONTROL_CONNECT         = 0x01u,
    USB_BOARD_CONTROL_DISCONNECT      = 0x02u,
    USB_BOARD_CONTROL_GET_LINK_STATE  = 0x03u,
    USB_BOARD_CONTROL_CLEAR_FAULT     = 0x04u,
    USB_BOARD_CONTROL_SET_MAC         = 0x05u,
    USB_BOARD_CONTROL_GET_AUTH_STATUS = 0x06u,
    /*
     * Read-only, transaction-correlated snapshot of the CH585 whole-report
     * WebConfig credit. Unlike EVT_BULK_CREDIT, a lost response can be safely
     * retried without replaying an uncorrelated absolute grant.
     */
    USB_BOARD_CONTROL_GET_WEBCONFIG_CREDIT = 0x07u
} usb_board_control_opcode_t;

typedef enum
{
    USB_BOARD_USB_SPEED_NONE = 0x00u,
    USB_BOARD_USB_SPEED_FULL = 0x01u,
    USB_BOARD_USB_SPEED_HIGH = 0x02u
} usb_board_usb_speed_t;

enum
{
    USB_BOARD_CAP_ROLE_RF          = (1u << 0),
    USB_BOARD_CAP_ROLE_USB         = (1u << 1),
    USB_BOARD_CAP_ROLE_MAINTENANCE = (1u << 2)
};

enum
{
    USB_BOARD_CAP_PROFILE_XINPUT     = (1u << 0),
    USB_BOARD_CAP_PROFILE_PS4        = (1u << 1),
    USB_BOARD_CAP_PROFILE_PS5_COMPAT = (1u << 2),
    USB_BOARD_CAP_PROFILE_SWITCH     = (1u << 3),
    USB_BOARD_CAP_PROFILE_XBOX_ONE   = (1u << 4),
    USB_BOARD_CAP_PROFILE_WEB_CONFIG = (1u << 5)
};

enum
{
    /*
     * XInput exposes a second, vendor-defined HID interface whose only IN
     * report is a complete 32-byte MPW2 telemetry frame.
     */
    USB_BOARD_CAP_FEATURE_TELEMETRY_HID = (1u << 0),
    USB_BOARD_CAP_FEATURE_CONTROL_V1    = (1u << 1),
    USB_BOARD_CAP_FEATURE_CDC_NCM       = (1u << 2), /* legacy capability */
    USB_BOARD_CAP_FEATURE_LOCAL_AUTH    = (1u << 3),
    USB_BOARD_CAP_FEATURE_WEBHID_V1     = (1u << 4),
    USB_BOARD_CAP_FEATURE_WEBCONFIG_PULL_CREDIT = (1u << 5)
};

#if defined(__GNUC__)
#define USB_BOARD_PACKED __attribute__((packed))
#else
#define USB_BOARD_PACKED
#endif

typedef struct USB_BOARD_PACKED
{
    uint8_t role;
} usb_board_role_select_v1_t;

typedef struct USB_BOARD_PACKED
{
    uint8_t role;
    uint8_t status;
} usb_board_role_selected_v1_t;

typedef struct USB_BOARD_PACKED
{
    uint8_t protocol_version;
    uint8_t role_flags;
    uint16_t profile_flags;
    uint8_t max_frame_bytes;
    uint8_t input_state_bytes;
    uint8_t firmware_major;
    uint8_t firmware_minor;
    uint8_t firmware_patch;
    uint8_t feature_flags;
} usb_board_caps_v1_t;

/*
 * This is an independent USB-side logical input ABI.  It deliberately mirrors
 * the compact processed state size without including any RF protocol header.
 */
typedef struct USB_BOARD_PACKED
{
    uint8_t seq;
    uint8_t flags;
    uint32_t action_mask_le;
    uint16_t age_us_le;
    uint8_t battery_code;
    uint8_t crc8;
} usb_board_input_v1_t;

typedef struct USB_BOARD_PACKED
{
    uint8_t channel;
    uint8_t transaction;
    uint8_t fragment_index;
    uint8_t flags;
    uint16_t total_length_le;
    uint16_t message_crc16_le;
} usb_board_fragment_header_v1_t;

typedef struct USB_BOARD_PACKED
{
    uint8_t profile;
} usb_board_set_profile_v1_t;

typedef struct USB_BOARD_PACKED
{
    uint8_t profile;
    uint8_t status;
} usb_board_profile_set_v1_t;

typedef struct USB_BOARD_PACKED
{
    uint8_t device_mounted;
    uint8_t device_suspended;
    /* Host controller initialized; downstream enumeration is reported by
     * host_attached/auth status and must not gate PI13 VBUS enable. */
    uint8_t host_ready;
    uint8_t host_attached;
    uint8_t profile;
    uint8_t last_fault;
} usb_board_usb_state_v1_t;

typedef struct USB_BOARD_PACKED
{
    uint8_t channel;
    uint8_t credits;
} usb_board_bulk_credit_v1_t;

/*
 * Variable-length USB_CONTROL messages use the first four bytes as a common
 * header.  A request sets status to USB_BOARD_STATUS_OK.  A response echoes
 * opcode/transaction and places the operation result in status.
 *
 * The serialized length is sizeof(header) + data_length; the fixed-size
 * request/response containers make the 60-byte payload ceiling explicit.
 */
#define USB_BOARD_CONTROL_HEADER_BYTES 4u
#define USB_BOARD_CONTROL_DATA_BYTES \
    (USB_BOARD_LINK_MAX_PAYLOAD_BYTES - USB_BOARD_CONTROL_HEADER_BYTES)

typedef struct USB_BOARD_PACKED
{
    uint8_t opcode;
    uint8_t transaction;
    uint8_t status;
    uint8_t data_length;
} usb_board_control_header_v1_t;

typedef struct USB_BOARD_PACKED
{
    usb_board_control_header_v1_t header;
    uint8_t data[USB_BOARD_CONTROL_DATA_BYTES];
} usb_board_control_request_v1_t;

typedef struct USB_BOARD_PACKED
{
    usb_board_control_header_v1_t header;
    uint8_t data[USB_BOARD_CONTROL_DATA_BYTES];
} usb_board_control_response_v1_t;

typedef struct USB_BOARD_PACKED
{
    uint8_t connected;
    uint8_t link_up;
    uint8_t data_alt_setting;
    uint8_t speed;
    uint8_t last_fault;
    uint8_t reserved;
} usb_board_control_link_state_v1_t;

typedef struct USB_BOARD_PACKED
{
    uint8_t state;
    uint8_t scheme;
    uint8_t last_error;
    uint8_t complete;
    uint16_t blob_length_le;
    uint32_t capabilities_le;
} usb_board_control_auth_status_v1_t;

/*
 * Credits are receiver-owned absolute values, not deltas:
 * - CMD_BULK_CREDIT grants CH585 permission to emit 0x86 fragments.
 * - EVT_BULK_CREDIT grants STM32 permission to emit 0x06 traffic on non-
 *   WebConfig channels.
 * - WEBCONFIG never publishes EVT_BULK_CREDIT. STM32 pulls its current
 *   complete-report capacity through GET_WEBCONFIG_CREDIT and consumes that
 *   one slot across all BoardLink fragments. The slot is returned only after
 *   the report has completed USB EP1 IN, or after an explicit transport reset.
 * - Other channels retain one-credit-per-fragment behavior.
 */

typedef struct USB_BOARD_PACKED
{
    uint8_t status;
} usb_board_control_result_v1_t;

#define USB_BOARD_STATIC_ASSERT_GLUE_(a, b) a##b
#define USB_BOARD_STATIC_ASSERT_GLUE(a, b) USB_BOARD_STATIC_ASSERT_GLUE_(a, b)
#define USB_BOARD_STATIC_ASSERT(expr) \
    typedef char USB_BOARD_STATIC_ASSERT_GLUE(usb_board_static_assert_, __LINE__)[(expr) ? 1 : -1]

USB_BOARD_STATIC_ASSERT(USB_BOARD_LINK_MAX_PAYLOAD_BYTES == 60u);
USB_BOARD_STATIC_ASSERT(USB_BOARD_FRAGMENT_DATA_BYTES == 52u);
USB_BOARD_STATIC_ASSERT(USB_BOARD_CHANNEL_SLOTS == 7u);
USB_BOARD_STATIC_ASSERT(USB_BOARD_TELEMETRY_FRAME_BYTES <=
                        USB_BOARD_FRAGMENT_DATA_BYTES);
USB_BOARD_STATIC_ASSERT(sizeof(usb_board_caps_v1_t) == 10u);
USB_BOARD_STATIC_ASSERT(sizeof(usb_board_input_v1_t) == USB_BOARD_INPUT_V1_BYTES);
USB_BOARD_STATIC_ASSERT(sizeof(usb_board_fragment_header_v1_t) == USB_BOARD_FRAGMENT_HEADER_BYTES);
USB_BOARD_STATIC_ASSERT(sizeof(usb_board_usb_state_v1_t) == 6u);
USB_BOARD_STATIC_ASSERT(sizeof(usb_board_bulk_credit_v1_t) == 2u);
USB_BOARD_STATIC_ASSERT(sizeof(usb_board_control_result_v1_t) == 1u);
USB_BOARD_STATIC_ASSERT(sizeof(usb_board_control_header_v1_t) ==
                        USB_BOARD_CONTROL_HEADER_BYTES);
USB_BOARD_STATIC_ASSERT(sizeof(usb_board_control_request_v1_t) ==
                        USB_BOARD_LINK_MAX_PAYLOAD_BYTES);
USB_BOARD_STATIC_ASSERT(sizeof(usb_board_control_response_v1_t) ==
                        USB_BOARD_LINK_MAX_PAYLOAD_BYTES);
USB_BOARD_STATIC_ASSERT(sizeof(usb_board_control_link_state_v1_t) == 6u);
USB_BOARD_STATIC_ASSERT(sizeof(usb_board_control_auth_status_v1_t) == 10u);

static inline uint8_t usb_board_link_checksum(const uint8_t *data, uint16_t length)
{
    uint8_t sum = 0u;
    uint16_t index;

    if(data == (const uint8_t *)0)
    {
        return 0u;
    }

    for(index = 0u; index < length; ++index)
    {
        sum = (uint8_t)(sum + data[index]);
    }
    return sum;
}

static inline uint8_t usb_board_input_crc8(const uint8_t *data, uint8_t length)
{
    uint8_t crc = 0u;
    uint8_t index;
    uint8_t bit;

    if(data == (const uint8_t *)0)
    {
        return 0u;
    }

    for(index = 0u; index < length; ++index)
    {
        crc ^= data[index];
        for(bit = 0u; bit < 8u; ++bit)
        {
            crc = (crc & 0x80u) != 0u
                ? (uint8_t)((crc << 1) ^ 0x07u)
                : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static inline uint16_t usb_board_crc16_ccitt(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFu;
    uint16_t index;
    uint8_t bit;

    if((data == (const uint8_t *)0) && (length != 0u))
    {
        return 0u;
    }

    for(index = 0u; index < length; ++index)
    {
        crc ^= (uint16_t)data[index] << 8;
        for(bit = 0u; bit < 8u; ++bit)
        {
            crc = (crc & 0x8000u) != 0u
                ? (uint16_t)((crc << 1) ^ 0x1021u)
                : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

#ifdef __cplusplus
}
#endif

#endif
