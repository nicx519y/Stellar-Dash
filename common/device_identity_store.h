#ifndef HBOX_DEVICE_IDENTITY_STORE_H
#define HBOX_DEVICE_IDENTITY_STORE_H

#include <stdint.h>

#include "device_security_protocol.h"
#include "internal_flash_security_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HBOX_DEVICE_IDENTITY_MAGIC         0x31444948u /* "HID1" */
#define HBOX_DEVICE_IDENTITY_VERSION       1u
#define HBOX_DEVICE_IDENTITY_LOCKED        1u
#define HBOX_DEVICE_IDENTITY_COMMIT_MAGIC  0x31434948u /* "HIC1" */
#define HBOX_DEVICE_IDENTITY_COMMIT_VERSION 1u
#define HBOX_DEVICE_IDENTITY_COMMITTED     0x54494D43u /* "CMIT" LE */

#if defined(__GNUC__)
#define HBOX_IDENTITY_PACKED __attribute__((packed))
#else
#define HBOX_IDENTITY_PACKED
#endif

typedef struct HBOX_IDENTITY_PACKED
{
    uint32_t magic_le;
    uint8_t version;
    uint8_t locked;
    uint16_t total_bytes_le;
    uint32_t crc32_le;
    uint8_t device_private_key[HBOX_SECURITY_P256_SIGNATURE_BYTES / 2u];
    hbox_device_certificate_v1_t device_certificate;
    uint8_t reserved[4];
} hbox_device_identity_record_v1_t;

typedef struct HBOX_IDENTITY_PACKED
{
    uint32_t magic_le;
    uint8_t version;
    uint8_t reserved0;
    uint16_t total_bytes_le;
    uint32_t slot_ordinal_le;
    uint32_t record_crc32_le;
    uint32_t record_crc32_inverse_le;
    uint32_t commit_crc32_le;
    uint32_t committed_le;
    uint8_t reserved1[4];
} hbox_device_identity_commit_v1_t;

typedef enum
{
    HBOX_DEVICE_IDENTITY_OK = 0,
    HBOX_DEVICE_IDENTITY_UNAVAILABLE,
    HBOX_DEVICE_IDENTITY_UNPROVISIONED,
    HBOX_DEVICE_IDENTITY_CORRUPT,
    HBOX_DEVICE_IDENTITY_IO_ERROR,
    HBOX_DEVICE_IDENTITY_FULL,
    HBOX_DEVICE_IDENTITY_ALREADY_PROVISIONED,
    HBOX_DEVICE_IDENTITY_FACTORY_DENIED
} hbox_device_identity_status_t;

typedef int (*hbox_device_identity_read_flashword_fn)(
    void *context,
    uint32_t slot_index,
    uint32_t flashword_index,
    uint8_t output[HBOX_INTERNAL_FLASH_PROGRAM_BYTES]);

typedef int (*hbox_device_identity_program_flashword_fn)(
    void *context,
    uint32_t slot_index,
    uint32_t flashword_index,
    const uint8_t input[HBOX_INTERNAL_FLASH_PROGRAM_BYTES]);

typedef int (*hbox_device_identity_factory_authorized_fn)(void *context);

typedef struct
{
    void *context;
    uint32_t slot_count;
    hbox_device_identity_read_flashword_fn read_flashword;
    hbox_device_identity_program_flashword_fn program_flashword;
    hbox_device_identity_factory_authorized_fn factory_authorized;
} hbox_device_identity_backend_t;

#if defined(__cplusplus)
static_assert(sizeof(hbox_device_identity_record_v1_t) ==
                  HBOX_DEVICE_IDENTITY_RECORD_BYTES,
              "identity record layout changed");
static_assert(sizeof(hbox_device_identity_commit_v1_t) ==
                  HBOX_DEVICE_IDENTITY_COMMIT_BYTES,
              "identity commit layout changed");
#else
_Static_assert(sizeof(hbox_device_identity_record_v1_t) ==
                   HBOX_DEVICE_IDENTITY_RECORD_BYTES,
               "identity record layout changed");
_Static_assert(sizeof(hbox_device_identity_commit_v1_t) ==
                   HBOX_DEVICE_IDENTITY_COMMIT_BYTES,
               "identity commit layout changed");
#endif

/*
 * A production provider is injected at build time.  Repository defaults never
 * expose a memory-mapped fallback that could be mistaken for provisioned
 * hardware.
 */
int HBoxIdentityStoreProvider_Open(
    hbox_device_identity_backend_t *backend);

hbox_device_identity_status_t HBoxIdentityStore_LoadFromBackend(
    const hbox_device_identity_backend_t *backend,
    hbox_device_identity_record_v1_t *record);

/*
 * Factory-only, append-only installation.  The backend must independently
 * prove an authenticated factory session and approved RDP1/Secure-Access
 * lifecycle before factory_authorized() returns true.  This function never
 * erases Flash and commits the slot with the final 32-byte program operation.
 */
hbox_device_identity_status_t HBoxIdentityStore_ProvisionFactory(
    const hbox_device_identity_backend_t *backend,
    const hbox_device_identity_record_v1_t *record);

hbox_device_identity_status_t HBoxIdentityStore_LoadStatus(
    hbox_device_identity_record_v1_t *record);

int HBoxIdentityStore_Load(hbox_device_identity_record_v1_t *record);

const char *HBoxIdentityStore_StatusString(
    hbox_device_identity_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* HBOX_DEVICE_IDENTITY_STORE_H */
