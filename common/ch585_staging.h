#ifndef CH585_STAGING_H
#define CH585_STAGING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CH585_STAGING_RECORD_MAGIC        0x32433835u /* "58C2" */
#define CH585_STAGING_RECORD_VERSION      2u
#define CH585_STAGING_RECORD_BYTES        256u
#define CH585_STAGING_RECORD_COMMIT       0x54494D43u /* "CMIT" */
#define CH585_STAGING_RECORD_CRC_BYTES    64u

typedef enum {
    CH585_STAGING_STATE_READY = 1u,
    CH585_STAGING_STATE_CLAIMED = 2u,
    CH585_STAGING_STATE_APPLIED = 3u,
    CH585_STAGING_STATE_FAILED = 4u
} ch585_staging_state_t;

typedef enum {
    CH585_STAGING_STAGE_NONE = 0u,
    CH585_STAGING_STAGE_STAGING = 1u,
    CH585_STAGING_STAGE_PROBE = 2u,
    CH585_STAGING_STAGE_BEGIN = 3u,
    CH585_STAGING_STAGE_WRITE = 4u,
    CH585_STAGING_STAGE_END = 5u,
    CH585_STAGING_STAGE_VERIFY_APP = 6u,
    CH585_STAGING_STAGE_COMPLETE = 7u
} ch585_staging_stage_t;

/* Append-only, one flash page per record.  record_crc32 covers bytes 0..63;
 * commit is programmed last, making power-loss during a record append benign. */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t record_bytes;
    uint8_t state;
    uint8_t stage;
    uint8_t client_status;
    uint8_t device_status;
    uint32_t generation;
    uint32_t image_size;
    uint32_t error_offset;
    uint32_t progress;
    uint8_t sha256[32];
    uint32_t image_crc32;
    uint32_t record_crc32;
    uint8_t reserved[184];
    uint32_t commit;
} ch585_staging_record_t;

#ifdef __cplusplus
static_assert(sizeof(ch585_staging_record_t) == CH585_STAGING_RECORD_BYTES,
              "CH585 staging record must occupy exactly one flash page");
#endif

#ifdef __cplusplus
}
#endif

#endif
