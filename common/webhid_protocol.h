#ifndef HBOX_WEBHID_PROTOCOL_H
#define HBOX_WEBHID_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Browser <-> STM32 WebConfig transport carried opaquely by the on-board
 * CH585.  The CH585 must not inspect, authenticate, decrypt, or rewrite these
 * reports; it only moves complete 64-byte reports between USB and BoardLink.
 */
#define WEBHID_PROTOCOL_VERSION             1u
#define WEBHID_REPORT_BYTES                64u
#define WEBHID_REPORT_HEADER_BYTES          8u
#define WEBHID_REPORT_PAYLOAD_BYTES        44u
#define WEBHID_REPORT_TAG_BYTES            12u
#define WEBHID_PERF_KEY_COUNT              18u
#define WEBHID_PERF_SAMPLE_BYTES           44u
#define WEBHID_PERF_CHECKPOINT_BYTES       44u
#define WEBHID_PERF_CHECKPOINT_KEYS        2u

typedef enum
{
    WEBHID_REPORT_BOOTSTRAP_REQUEST  = 0x01u,
    WEBHID_REPORT_BOOTSTRAP_RESPONSE = 0x02u,
    WEBHID_REPORT_SECURE_REQUEST     = 0x10u,
    WEBHID_REPORT_SECURE_RESPONSE    = 0x11u,
    WEBHID_REPORT_SECURE_EVENT       = 0x12u,
    WEBHID_REPORT_PERF_SAMPLE        = 0x20u,
    WEBHID_REPORT_PERF_EDGE          = 0x21u,
    WEBHID_REPORT_PERF_CHECKPOINT    = 0x22u,
    WEBHID_REPORT_BUTTON_STATE       = 0x23u,
    WEBHID_REPORT_STREAM_FRAGMENT    = 0x30u,
    /*
     * Dedicated image payload report.  An authenticated IMAGE_BEGIN owns the
     * receive lane until IMAGE_COMMIT; every encrypted payload byte is image
     * data and only the final report carries REPORT_FLAG_LAST.
     */
    WEBHID_REPORT_IMAGE_DATA         = 0x31u
} webhid_report_type_t;

enum
{
    WEBHID_REPORT_FLAG_ENCRYPTED    = (1u << 0),
    WEBHID_REPORT_FLAG_FRAGMENTED   = (1u << 1),
    WEBHID_REPORT_FLAG_LAST         = (1u << 2),
    WEBHID_REPORT_FLAG_ACK_REQUIRED = (1u << 3)
};

#if defined(__GNUC__)
#define WEBHID_PACKED __attribute__((packed))
#else
#define WEBHID_PACKED
#endif

/*
 * sequence_le is monotonic independently in each direction.  The 12-byte tag
 * is an AES-GCM authentication tag after a secure session is established.
 * Bootstrap reports carry a zero tag; their complete logical transcript is
 * authenticated by the STM32 attestation protocol rather than by the CH585.
 */
typedef struct WEBHID_PACKED
{
    uint8_t version;
    uint8_t type;
    uint8_t flags;
    uint8_t payload_length;
    uint32_t sequence_le;
    uint8_t payload[WEBHID_REPORT_PAYLOAD_BYTES];
    uint8_t tag[WEBHID_REPORT_TAG_BYTES];
} webhid_secure_report_v1_t;

/*
 * Compact 100 Hz Hall telemetry.  Distances are unsigned micrometres.  The
 * browser reconstructs richer per-key state from reliable PERF_EDGE and
 * periodic PERF_CHECKPOINT messages.
 */
typedef struct WEBHID_PACKED
{
    uint32_t device_timestamp_us_le;
    uint8_t pressed_mask_le[3];
    uint8_t dropped_samples;
    uint16_t current_distance_um_le[WEBHID_PERF_KEY_COUNT];
} webhid_perf_sample_v1_t;

typedef struct WEBHID_PACKED
{
    uint8_t virtual_pin;
    uint8_t flags;
    uint16_t raw_adc_le;
    uint16_t current_distance_um_le;
    uint16_t press_trigger_distance_um_le;
    uint16_t press_start_distance_um_le;
    uint16_t release_trigger_distance_um_le;
    uint16_t release_start_distance_um_le;
} webhid_perf_checkpoint_key_v1_t;

/*
 * One complete checkpoint is nine independently authenticated reports.  Each
 * report is self-describing, so telemetry and control reports may be sent
 * between chunks without a generic fragment assembler or a long synchronous
 * USB burst.
 */
typedef struct WEBHID_PACKED
{
    uint32_t device_timestamp_us_le;
    uint32_t edge_sequence_le;
    uint16_t max_travel_distance_um_le;
    uint16_t total_dropped_samples_le;
    uint8_t checkpoint_id;
    uint8_t chunk_index;
    uint8_t chunk_count;
    uint8_t first_button;
    webhid_perf_checkpoint_key_v1_t
        keys[WEBHID_PERF_CHECKPOINT_KEYS];
} webhid_perf_checkpoint_v1_t;

enum
{
    WEBHID_BUTTON_STATE_FLAG_ACTIVE = (1u << 0)
};

/*
 * Compact full-state notification shared by every ordinary button consumer
 * (Keys, LED preview, macro recording and reusable Hitbox components).
 *
 * Each snapshot fits in one authenticated HID report.  event_sequence_le is
 * independent from the physical report sequence and advances for every
 * state transition accepted by the firmware.  total_dropped_snapshots_le is
 * cumulative within the authenticated session, allowing diagnostics to
 * distinguish device-side queue pressure from USB report loss.
 */
typedef struct WEBHID_PACKED
{
    uint32_t event_sequence_le;
    uint32_t trigger_mask_le;
    uint16_t total_dropped_snapshots_le;
    uint8_t total_buttons;
    uint8_t flags;
} webhid_button_state_v1_t;

#define WEBHID_STATIC_ASSERT_GLUE_(a, b) a##b
#define WEBHID_STATIC_ASSERT_GLUE(a, b) WEBHID_STATIC_ASSERT_GLUE_(a, b)
#define WEBHID_STATIC_ASSERT(expr) \
    typedef char WEBHID_STATIC_ASSERT_GLUE(webhid_static_assert_, __LINE__)[(expr) ? 1 : -1]

WEBHID_STATIC_ASSERT(sizeof(webhid_secure_report_v1_t) ==
                     WEBHID_REPORT_BYTES);
WEBHID_STATIC_ASSERT(sizeof(webhid_perf_sample_v1_t) ==
                     WEBHID_PERF_SAMPLE_BYTES);
WEBHID_STATIC_ASSERT(sizeof(webhid_perf_checkpoint_key_v1_t) == 14u);
WEBHID_STATIC_ASSERT(sizeof(webhid_perf_checkpoint_v1_t) ==
                     WEBHID_PERF_CHECKPOINT_BYTES);
WEBHID_STATIC_ASSERT(sizeof(webhid_button_state_v1_t) == 12u);
WEBHID_STATIC_ASSERT(WEBHID_REPORT_HEADER_BYTES +
                     WEBHID_REPORT_PAYLOAD_BYTES +
                     WEBHID_REPORT_TAG_BYTES == WEBHID_REPORT_BYTES);

#ifdef __cplusplus
}
#endif

#endif
