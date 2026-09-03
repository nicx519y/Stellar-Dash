#ifndef HBOX_SECURITY_VERSION_JOURNAL_H
#define HBOX_SECURITY_VERSION_JOURNAL_H

#include <stddef.h>
#include <stdint.h>

#include "internal_flash_security_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Portable append-only anti-rollback journal.
 *
 * This portable module does not access a physical address.  The H750 linker
 * reserves HBOX_SECURITY_VERSION_REGION_ADDRESS separately from identity and
 * code, while a production provider still owns the actual 32-byte programming
 * operation and lifecycle checks.  No field provider may erase the single
 * internal-Flash sector.  An erased record is all 0xff.  Any partially
 * programmed, reordered, missing, or modified record is corruption.
 */
#define HBOX_SECURITY_VERSION_RECORD_MAGIC       0x31564A53u /* "SJV1" */
#define HBOX_SECURITY_VERSION_RECORD_COMMITTED   0x434D4954u /* "TIMC" LE */
#define HBOX_SECURITY_VERSION_JOURNAL_FORMAT     1u

#if defined(__GNUC__)
#define HBOX_SECURITY_VERSION_PACKED __attribute__((packed))
#else
#define HBOX_SECURITY_VERSION_PACKED
#endif

typedef enum
{
    HBOX_SECURITY_VERSION_OK = 0,
    HBOX_SECURITY_VERSION_UNAVAILABLE,
    HBOX_SECURITY_VERSION_UNPROVISIONED,
    HBOX_SECURITY_VERSION_CORRUPT,
    HBOX_SECURITY_VERSION_IO_ERROR,
    HBOX_SECURITY_VERSION_FULL,
    HBOX_SECURITY_VERSION_ROLLBACK
} hbox_security_version_status_t;

typedef struct HBOX_SECURITY_VERSION_PACKED
{
    uint32_t magic_le;
    uint16_t format_le;
    uint16_t record_bytes_le;
    uint32_t ordinal_le;
    uint32_t minimum_version_le;
    uint32_t minimum_version_inverse_le;
    uint32_t previous_record_crc32_le;
    uint32_t record_crc32_le;
    uint32_t committed_le;
} hbox_security_version_record_v1_t;

typedef int (*hbox_security_version_read_record_fn)(
    void *context,
    uint32_t record_index,
    hbox_security_version_record_v1_t *record);

typedef int (*hbox_security_version_program_record_atomic_fn)(
    void *context,
    uint32_t record_index,
    const hbox_security_version_record_v1_t *record);

typedef struct
{
    void *context;
    uint32_t record_count;
    hbox_security_version_read_record_fn read_record;
    hbox_security_version_program_record_atomic_fn program_record_atomic;
} hbox_security_version_journal_backend_t;

#ifdef __cplusplus
static_assert(sizeof(hbox_security_version_record_v1_t) ==
                  HBOX_SECURITY_VERSION_RECORD_BYTES,
              "security-version record layout changed");
#else
_Static_assert(sizeof(hbox_security_version_record_v1_t) ==
                   HBOX_SECURITY_VERSION_RECORD_BYTES,
               "security-version record layout changed");
#endif

hbox_security_version_status_t HBoxSecurityVersionJournal_Load(
    const hbox_security_version_journal_backend_t *backend,
    uint32_t *minimum_version,
    uint32_t *used_records);

/*
 * Factory-only initialization.  Runtime boot code must not call this: an
 * erased/corrupt provider is a provisioning failure, not permission to adopt
 * the version offered by an update package.
 */
hbox_security_version_status_t HBoxSecurityVersionJournal_Provision(
    const hbox_security_version_journal_backend_t *backend,
    uint32_t initial_minimum_version);

/*
 * Monotonically advances an already provisioned journal.  Equal is idempotent;
 * a lower requested version is rejected without writing.
 */
hbox_security_version_status_t HBoxSecurityVersionJournal_Advance(
    const hbox_security_version_journal_backend_t *backend,
    uint32_t requested_minimum_version);

const char *HBoxSecurityVersion_StatusString(
    hbox_security_version_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* HBOX_SECURITY_VERSION_JOURNAL_H */
