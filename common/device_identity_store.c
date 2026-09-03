#include "device_identity_store.h"

#include <stddef.h>
#include <string.h>

#include "device_security_boot_context.h"

#ifndef HBOX_DEVICE_IDENTITY_PROVIDER_READY
#define HBOX_DEVICE_IDENTITY_PROVIDER_READY 0
#endif

#define IDENTITY_RECORD_FLASHWORDS \
    (HBOX_DEVICE_IDENTITY_RECORD_BYTES / HBOX_INTERNAL_FLASH_PROGRAM_BYTES)
#define IDENTITY_COMMIT_FLASHWORD IDENTITY_RECORD_FLASHWORDS
#define IDENTITY_SLOT_FLASHWORDS \
    (HBOX_DEVICE_IDENTITY_SLOT_BYTES / HBOX_INTERNAL_FLASH_PROGRAM_BYTES)

static int all_zero(const uint8_t *value, size_t length)
{
    uint8_t combined = 0u;
    size_t index;
    for (index = 0u; index < length; ++index) {
        combined |= value[index];
    }
    return combined == 0u;
}

static int all_erased(const uint8_t *value, size_t length)
{
    size_t index;
    for (index = 0u; index < length; ++index) {
        if (value[index] != 0xFFu) {
            return 0;
        }
    }
    return 1;
}

static uint32_t commit_crc32(
    const hbox_device_identity_commit_v1_t *commit)
{
    return HBoxSecurity_Crc32Skipping(
        (const uint8_t *)commit,
        sizeof(*commit),
        offsetof(hbox_device_identity_commit_v1_t, commit_crc32_le),
        sizeof(commit->commit_crc32_le));
}

static int record_is_valid(
    const hbox_device_identity_record_v1_t *record)
{
    uint32_t expected_crc;
    uint8_t nonzero_private = 0u;
    size_t index;

    if (record->magic_le != HBOX_DEVICE_IDENTITY_MAGIC ||
        record->version != HBOX_DEVICE_IDENTITY_VERSION ||
        record->locked != HBOX_DEVICE_IDENTITY_LOCKED ||
        record->total_bytes_le != sizeof(*record) ||
        !all_zero(record->reserved, sizeof(record->reserved)) ||
        record->device_certificate.magic_le !=
            HBOX_DEVICE_CERTIFICATE_MAGIC ||
        record->device_certificate.version !=
            HBOX_SECURITY_PROTOCOL_VERSION ||
        record->device_certificate.signed_bytes_le !=
            HBOX_DEVICE_CERTIFICATE_SIGNED_BYTES) {
        return 0;
    }
    for (index = 0u; index < sizeof(record->device_private_key); ++index) {
        nonzero_private |= record->device_private_key[index];
    }
    expected_crc = HBoxSecurity_Crc32Skipping(
        (const uint8_t *)record,
        sizeof(*record),
        offsetof(hbox_device_identity_record_v1_t, crc32_le),
        sizeof(record->crc32_le));
    if (nonzero_private == 0u || expected_crc == 0u ||
        expected_crc != record->crc32_le) {
        return 0;
    }
    return 1;
}

static int commit_is_valid(
    const hbox_device_identity_commit_v1_t *commit,
    uint32_t slot_index,
    uint32_t record_crc)
{
    uint32_t expected_crc;

    if (commit->magic_le != HBOX_DEVICE_IDENTITY_COMMIT_MAGIC ||
        commit->version != HBOX_DEVICE_IDENTITY_COMMIT_VERSION ||
        commit->reserved0 != 0u ||
        commit->total_bytes_le != sizeof(*commit) ||
        commit->slot_ordinal_le != slot_index + 1u ||
        commit->record_crc32_le != record_crc ||
        commit->record_crc32_inverse_le != ~record_crc ||
        commit->committed_le != HBOX_DEVICE_IDENTITY_COMMITTED ||
        !all_zero(commit->reserved1, sizeof(commit->reserved1))) {
        return 0;
    }
    expected_crc = commit_crc32(commit);
    return expected_crc != 0u &&
           expected_crc == commit->commit_crc32_le;
}

static int backend_is_valid(
    const hbox_device_identity_backend_t *backend,
    int require_factory_write)
{
    return backend != NULL &&
           backend->slot_count != 0u &&
           backend->slot_count <= HBOX_DEVICE_IDENTITY_SLOT_COUNT &&
           backend->read_flashword != NULL &&
           (!require_factory_write ||
            (backend->program_flashword != NULL &&
             backend->factory_authorized != NULL));
}

static int read_slot(
    const hbox_device_identity_backend_t *backend,
    uint32_t slot_index,
    uint8_t slot[HBOX_DEVICE_IDENTITY_SLOT_BYTES])
{
    uint32_t flashword;

    for (flashword = 0u;
         flashword < IDENTITY_SLOT_FLASHWORDS;
         ++flashword) {
        if (!backend->read_flashword(
                backend->context,
                slot_index,
                flashword,
                slot + flashword * HBOX_INTERNAL_FLASH_PROGRAM_BYTES)) {
            memset(slot, 0, HBOX_DEVICE_IDENTITY_SLOT_BYTES);
            return 0;
        }
    }
    return 1;
}

hbox_device_identity_status_t HBoxIdentityStore_LoadFromBackend(
    const hbox_device_identity_backend_t *backend,
    hbox_device_identity_record_v1_t *record)
{
    uint8_t slot[HBOX_DEVICE_IDENTITY_SLOT_BYTES];
    hbox_device_identity_record_v1_t candidate;
    hbox_device_identity_commit_v1_t commit;
    uint32_t valid_count = 0u;
    uint32_t slot_index;

    if (record == NULL || !backend_is_valid(backend, 0)) {
        if (record != NULL) {
            memset(record, 0, sizeof(*record));
        }
        return HBOX_DEVICE_IDENTITY_UNAVAILABLE;
    }
    memset(record, 0, sizeof(*record));
    memset(&candidate, 0, sizeof(candidate));
    memset(&commit, 0, sizeof(commit));

    for (slot_index = 0u;
         slot_index < backend->slot_count;
        ++slot_index) {
        if (!read_slot(backend, slot_index, slot)) {
            memset(&candidate, 0, sizeof(candidate));
            memset(&commit, 0, sizeof(commit));
            memset(record, 0, sizeof(*record));
            return HBOX_DEVICE_IDENTITY_IO_ERROR;
        }
        if (all_erased(slot, sizeof(slot))) {
            continue;
        }
        memcpy(&candidate, slot, sizeof(candidate));
        memcpy(&commit, slot + sizeof(candidate), sizeof(commit));

        /*
         * A slot without a valid final commit flashword is an abandoned
         * factory attempt.  It is never accepted and a later erased slot can
         * be used without erasing the single internal-Flash sector.
         */
        if (!commit_is_valid(
                &commit, slot_index, candidate.crc32_le)) {
            continue;
        }
        if (!record_is_valid(&candidate)) {
            memset(slot, 0, sizeof(slot));
            memset(&candidate, 0, sizeof(candidate));
            memset(&commit, 0, sizeof(commit));
            memset(record, 0, sizeof(*record));
            return HBOX_DEVICE_IDENTITY_CORRUPT;
        }
        ++valid_count;
        if (valid_count != 1u) {
            memset(slot, 0, sizeof(slot));
            memset(&candidate, 0, sizeof(candidate));
            memset(&commit, 0, sizeof(commit));
            memset(record, 0, sizeof(*record));
            return HBOX_DEVICE_IDENTITY_CORRUPT;
        }
        memcpy(record, &candidate, sizeof(*record));
    }

    memset(slot, 0, sizeof(slot));
    memset(&candidate, 0, sizeof(candidate));
    memset(&commit, 0, sizeof(commit));
    return valid_count == 1u
               ? HBOX_DEVICE_IDENTITY_OK
               : HBOX_DEVICE_IDENTITY_UNPROVISIONED;
}

static void build_commit(
    hbox_device_identity_commit_v1_t *commit,
    uint32_t slot_index,
    uint32_t record_crc)
{
    memset(commit, 0, sizeof(*commit));
    commit->magic_le = HBOX_DEVICE_IDENTITY_COMMIT_MAGIC;
    commit->version = HBOX_DEVICE_IDENTITY_COMMIT_VERSION;
    commit->total_bytes_le = sizeof(*commit);
    commit->slot_ordinal_le = slot_index + 1u;
    commit->record_crc32_le = record_crc;
    commit->record_crc32_inverse_le = ~record_crc;
    commit->committed_le = HBOX_DEVICE_IDENTITY_COMMITTED;
    commit->commit_crc32_le = commit_crc32(commit);
}

static int program_and_verify_flashword(
    const hbox_device_identity_backend_t *backend,
    uint32_t slot_index,
    uint32_t flashword_index,
    const uint8_t input[HBOX_INTERNAL_FLASH_PROGRAM_BYTES])
{
    uint8_t erased[HBOX_INTERNAL_FLASH_PROGRAM_BYTES];
    uint8_t readback[HBOX_INTERNAL_FLASH_PROGRAM_BYTES];
    int result = 0;

    if (!backend->read_flashword(
            backend->context,
            slot_index,
            flashword_index,
            erased) ||
        !all_erased(erased, sizeof(erased)) ||
        !backend->program_flashword(
            backend->context,
            slot_index,
            flashword_index,
            input) ||
        !backend->read_flashword(
            backend->context,
            slot_index,
            flashword_index,
            readback)) {
        goto done;
    }
    result = memcmp(input, readback, sizeof(readback)) == 0;

done:
    memset(erased, 0, sizeof(erased));
    memset(readback, 0, sizeof(readback));
    return result;
}

hbox_device_identity_status_t HBoxIdentityStore_ProvisionFactory(
    const hbox_device_identity_backend_t *backend,
    const hbox_device_identity_record_v1_t *record)
{
    uint8_t slot[HBOX_DEVICE_IDENTITY_SLOT_BYTES];
    hbox_device_identity_record_v1_t loaded;
    hbox_device_identity_commit_v1_t commit;
    hbox_device_identity_status_t status;
    uint32_t selected_slot = UINT32_MAX;
    uint32_t slot_index;
    uint32_t flashword;

    if (record == NULL || !backend_is_valid(backend, 1)) {
        return HBOX_DEVICE_IDENTITY_UNAVAILABLE;
    }
    if (!backend->factory_authorized(backend->context)) {
        return HBOX_DEVICE_IDENTITY_FACTORY_DENIED;
    }
    if (!record_is_valid(record)) {
        return HBOX_DEVICE_IDENTITY_CORRUPT;
    }

    status = HBoxIdentityStore_LoadFromBackend(backend, &loaded);
    memset(&loaded, 0, sizeof(loaded));
    if (status == HBOX_DEVICE_IDENTITY_OK) {
        return HBOX_DEVICE_IDENTITY_ALREADY_PROVISIONED;
    }
    if (status != HBOX_DEVICE_IDENTITY_UNPROVISIONED) {
        return status;
    }

    for (slot_index = 0u;
         slot_index < backend->slot_count;
         ++slot_index) {
        if (!read_slot(backend, slot_index, slot)) {
            memset(slot, 0, sizeof(slot));
            return HBOX_DEVICE_IDENTITY_IO_ERROR;
        }
        if (all_erased(slot, sizeof(slot))) {
            selected_slot = slot_index;
            break;
        }
    }
    memset(slot, 0, sizeof(slot));
    if (selected_slot == UINT32_MAX) {
        return HBOX_DEVICE_IDENTITY_FULL;
    }

    /*
     * Program and verify all eight record flashwords first.  The ninth
     * flashword is the only authority that makes a slot visible to Load().
     */
    for (flashword = 0u;
         flashword < IDENTITY_RECORD_FLASHWORDS;
         ++flashword) {
        if (!program_and_verify_flashword(
                backend,
                selected_slot,
                flashword,
                (const uint8_t *)record +
                    flashword * HBOX_INTERNAL_FLASH_PROGRAM_BYTES)) {
            return HBOX_DEVICE_IDENTITY_IO_ERROR;
        }
    }

    build_commit(&commit, selected_slot, record->crc32_le);
    if (!program_and_verify_flashword(
            backend,
            selected_slot,
            IDENTITY_COMMIT_FLASHWORD,
            (const uint8_t *)&commit)) {
        memset(&commit, 0, sizeof(commit));
        return HBOX_DEVICE_IDENTITY_IO_ERROR;
    }
    memset(&commit, 0, sizeof(commit));

    status = HBoxIdentityStore_LoadFromBackend(backend, &loaded);
    if (status == HBOX_DEVICE_IDENTITY_OK &&
        memcmp(&loaded, record, sizeof(loaded)) != 0) {
        status = HBOX_DEVICE_IDENTITY_CORRUPT;
    }
    memset(&loaded, 0, sizeof(loaded));
    return status;
}

hbox_device_identity_status_t HBoxIdentityStore_LoadStatus(
    hbox_device_identity_record_v1_t *record)
{
#if HBOX_DEVICE_IDENTITY_PROVIDER_READY
    hbox_device_identity_backend_t backend;

    if (record == NULL) {
        return HBOX_DEVICE_IDENTITY_UNAVAILABLE;
    }
    memset(&backend, 0, sizeof(backend));
    if (!HBoxIdentityStoreProvider_Open(&backend)) {
        memset(record, 0, sizeof(*record));
        return HBOX_DEVICE_IDENTITY_UNAVAILABLE;
    }
    return HBoxIdentityStore_LoadFromBackend(&backend, record);
#else
    if (record != NULL) {
        memset(record, 0, sizeof(*record));
    }
    return HBOX_DEVICE_IDENTITY_UNAVAILABLE;
#endif
}

int HBoxIdentityStore_Load(hbox_device_identity_record_v1_t *record)
{
    return HBoxIdentityStore_LoadStatus(record) ==
           HBOX_DEVICE_IDENTITY_OK;
}

const char *HBoxIdentityStore_StatusString(
    hbox_device_identity_status_t status)
{
    switch (status) {
    case HBOX_DEVICE_IDENTITY_OK:
        return "ok";
    case HBOX_DEVICE_IDENTITY_UNAVAILABLE:
        return "provider-unavailable";
    case HBOX_DEVICE_IDENTITY_UNPROVISIONED:
        return "unprovisioned";
    case HBOX_DEVICE_IDENTITY_CORRUPT:
        return "corrupt";
    case HBOX_DEVICE_IDENTITY_IO_ERROR:
        return "io-error";
    case HBOX_DEVICE_IDENTITY_FULL:
        return "full";
    case HBOX_DEVICE_IDENTITY_ALREADY_PROVISIONED:
        return "already-provisioned";
    case HBOX_DEVICE_IDENTITY_FACTORY_DENIED:
        return "factory-denied";
    default:
        return "unknown";
    }
}
