#ifndef HBOX_HIGH_RATE_PROTOCOL_H
#define HBOX_HIGH_RATE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HBOX_CLIENT_CONTROL_MAGIC          0x31434248ul /* "HBC1" */
#define HBOX_CLIENT_INPUT_MAGIC            0x31494348ul /* "HCI1" */
#define HBOX_CLIENT_PROTOCOL_VERSION       1u
#define HBOX_CLIENT_CONTROL_BYTES          32u
#define HBOX_CLIENT_INPUT_BYTES            64u
#define HBOX_CLIENT_NATIVE_VID             0x045Eu
#define HBOX_CLIENT_NATIVE_PID             0x028Eu
#define HBOX_CLIENT_HIGH_RATE_VID          0xCAFEu
#define HBOX_CLIENT_HIGH_RATE_PID          0x4023u
#define HBOX_CLIENT_HEARTBEAT_MS            250u
#define HBOX_CLIENT_LEASE_TIMEOUT_MS       1000u
#define HBOX_CLIENT_NEUTRAL_DELAY_MS        100u
#define HBOX_CLIENT_REENUMERATE_DELAY_MS    250u
#define HBOX_CLIENT_WINUSB_MS_VENDOR_CODE  0x21u
#define HBOX_CLIENT_STATUS_VENDOR_CODE     0x22u
#define HBOX_CLIENT_MS_OS_20_INDEX       0x0007u

typedef enum
{
    HBOX_CLIENT_CONTROL_QUERY     = 0u,
    HBOX_CLIENT_CONTROL_ACQUIRE   = 1u,
    HBOX_CLIENT_CONTROL_HEARTBEAT = 2u,
    HBOX_CLIENT_CONTROL_RELEASE   = 3u
} hbox_client_control_opcode_t;

typedef enum
{
    HBOX_CLIENT_STATUS_OK          = 0u,
    HBOX_CLIENT_STATUS_BUSY        = 1u,
    HBOX_CLIENT_STATUS_BAD_VERSION = 2u,
    HBOX_CLIENT_STATUS_BAD_TOKEN   = 3u,
    HBOX_CLIENT_STATUS_BAD_STATE   = 4u,
    HBOX_CLIENT_STATUS_BAD_CRC     = 5u
} hbox_client_status_t;

enum
{
    HBOX_CLIENT_FLAG_ENABLED    = (1u << 0),
    HBOX_CLIENT_FLAG_STREAMING  = (1u << 1),
    HBOX_CLIENT_FLAG_USB_HS     = (1u << 2),
    HBOX_CLIENT_FLAG_FALLBACK   = (1u << 3)
};

enum
{
    HBOX_CLIENT_INPUT_FLAG_VALID            = (1u << 0),
    HBOX_CLIENT_INPUT_FLAG_BATTERY_VALID    = (1u << 1),
    HBOX_CLIENT_INPUT_FLAG_SOURCE_OVERWRITE = (1u << 2),
    HBOX_CLIENT_INPUT_FLAG_NEUTRAL           = (1u << 3)
};

#if defined(__GNUC__) || defined(__clang__)
#define HBOX_CLIENT_PACKED __attribute__((packed))
#else
#define HBOX_CLIENT_PACKED
#pragma pack(push, 1)
#endif

typedef struct HBOX_CLIENT_PACKED
{
    uint32_t magic_le;
    uint8_t version;
    uint8_t opcode;
    uint8_t status;
    uint8_t flags;
    uint32_t transaction_le;
    uint8_t lease_token[16];
    uint16_t effective_rate_hz_le;
    uint16_t crc16_le;
} hbox_client_control_v1_t;

typedef struct HBOX_CLIENT_PACKED
{
    uint32_t magic_le;
    uint8_t version;
    uint8_t reserved0;
    uint16_t length_le;
    uint32_t stream_sequence_le;
    uint32_t producer_time_us_le;
    uint8_t lease_token[16];
    uint32_t action_mask_le;
    uint16_t sample_age_us_le;
    uint16_t effective_rate_hz_le;
    uint16_t xinput_buttons_le;
    uint8_t left_trigger;
    uint8_t right_trigger;
    int16_t left_stick_x_le;
    int16_t left_stick_y_le;
    int16_t right_stick_x_le;
    int16_t right_stick_y_le;
    uint16_t flags_le;
    uint8_t battery_code;
    uint8_t reserved1;
    uint16_t device_overwrite_count_le;
    uint16_t board_link_fault_count_le;
    uint32_t crc32_le;
} hbox_client_input_v1_t;

#if !defined(__GNUC__) && !defined(__clang__)
#pragma pack(pop)
#endif

#if defined(__cplusplus)
static_assert(sizeof(hbox_client_control_v1_t) == HBOX_CLIENT_CONTROL_BYTES,
              "HBox client control packet must remain 32 bytes");
static_assert(sizeof(hbox_client_input_v1_t) == HBOX_CLIENT_INPUT_BYTES,
              "HBox client input packet must remain 64 bytes");
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(hbox_client_control_v1_t) == HBOX_CLIENT_CONTROL_BYTES,
               "HBox client control packet must remain 32 bytes");
_Static_assert(sizeof(hbox_client_input_v1_t) == HBOX_CLIENT_INPUT_BYTES,
               "HBox client input packet must remain 64 bytes");
#endif

static inline uint16_t hbox_client_crc16_ccitt(const uint8_t *data,
                                               size_t length)
{
    uint16_t crc = 0xFFFFu;
    size_t index;
    for(index = 0u; index < length; ++index)
    {
        uint8_t bit;
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

static inline uint32_t hbox_client_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFul;
    size_t index;
    for(index = 0u; index < length; ++index)
    {
        uint8_t bit;
        crc ^= data[index];
        for(bit = 0u; bit < 8u; ++bit)
        {
            crc = (crc & 1u) != 0u
                ? (crc >> 1) ^ 0xEDB88320ul
                : crc >> 1;
        }
    }
    return crc ^ 0xFFFFFFFFul;
}

static inline int hbox_client_token_is_zero(const uint8_t token[16])
{
    uint8_t combined = 0u;
    size_t index;
    for(index = 0u; index < 16u; ++index)
    {
        combined |= token[index];
    }
    return combined == 0u;
}

#ifdef __cplusplus
}
#endif

#endif /* HBOX_HIGH_RATE_PROTOCOL_H */
