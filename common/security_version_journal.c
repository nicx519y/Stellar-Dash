#include "security_version_journal.h"

#include <stddef.h>
#include <string.h>

static uint32_t crc32_update(uint32_t crc, uint8_t value)
{
    uint32_t bit;

    crc ^= value;
    for (bit = 0u; bit < 8u; ++bit) {
        uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
        crc = (crc >> 1u) ^ (0xEDB88320u & mask);
    }
    return crc;
}

static uint32_t record_crc32(
    const hbox_security_version_record_v1_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    const size_t skip_offset =
        offsetof(hbox_security_version_record_v1_t, record_crc32_le);
    uint32_t crc = 0xFFFFFFFFu;
    size_t index;

    for (index = 0u; index < sizeof(*record); ++index) {
        if (index >= skip_offset &&
            index < skip_offset + sizeof(record->record_crc32_le)) {
            continue;
        }
        crc = crc32_update(crc, bytes[index]);
    }
    return crc ^ 0xFFFFFFFFu;
}

static int record_is_erased(
    const hbox_security_version_record_v1_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    size_t index;

    for (index = 0u; index < sizeof(*record); ++index) {
        if (bytes[index] != 0xFFu) {
            return 0;
        }
    }
    return 1;
}

static int backend_is_valid(
    const hbox_security_version_journal_backend_t *backend,
    int require_program)
{
    return backend != NULL &&
           backend->record_count != 0u &&
           backend->read_record != NULL &&
           (!require_program || backend->program_record_atomic != NULL);
}

static int record_is_valid(
    const hbox_security_version_record_v1_t *record,
    uint32_t expected_ordinal,
    uint32_t previous_minimum,
    uint32_t previous_crc)
{
    return record->magic_le == HBOX_SECURITY_VERSION_RECORD_MAGIC &&
           record->format_le == HBOX_SECURITY_VERSION_JOURNAL_FORMAT &&
           record->record_bytes_le ==
               HBOX_SECURITY_VERSION_RECORD_BYTES &&
           record->ordinal_le == expected_ordinal &&
           record->minimum_version_le != 0u &&
           record->minimum_version_inverse_le ==
               ~record->minimum_version_le &&
           record->minimum_version_le > previous_minimum &&
           record->previous_record_crc32_le == previous_crc &&
           record->committed_le ==
               HBOX_SECURITY_VERSION_RECORD_COMMITTED &&
           record->record_crc32_le == record_crc32(record);
}

static void build_record(
    hbox_security_version_record_v1_t *record,
    uint32_t ordinal,
    uint32_t minimum_version,
    uint32_t previous_crc)
{
    memset(record, 0, sizeof(*record));
    record->magic_le = HBOX_SECURITY_VERSION_RECORD_MAGIC;
    record->format_le = HBOX_SECURITY_VERSION_JOURNAL_FORMAT;
    record->record_bytes_le = HBOX_SECURITY_VERSION_RECORD_BYTES;
    record->ordinal_le = ordinal;
    record->minimum_version_le = minimum_version;
    record->minimum_version_inverse_le = ~minimum_version;
    record->previous_record_crc32_le = previous_crc;
    record->committed_le = HBOX_SECURITY_VERSION_RECORD_COMMITTED;
    record->record_crc32_le = record_crc32(record);
}

static hbox_security_version_status_t verify_programmed_state(
    const hbox_security_version_journal_backend_t *backend,
    uint32_t expected_minimum)
{
    uint32_t actual_minimum = 0u;
    hbox_security_version_status_t status =
        HBoxSecurityVersionJournal_Load(
            backend, &actual_minimum, NULL);

    if (status != HBOX_SECURITY_VERSION_OK) {
        return status;
    }
    return actual_minimum == expected_minimum
               ? HBOX_SECURITY_VERSION_OK
               : HBOX_SECURITY_VERSION_CORRUPT;
}

hbox_security_version_status_t HBoxSecurityVersionJournal_Load(
    const hbox_security_version_journal_backend_t *backend,
    uint32_t *minimum_version,
    uint32_t *used_records)
{
    hbox_security_version_record_v1_t record;
    uint32_t previous_minimum = 0u;
    uint32_t previous_crc = 0u;
    uint32_t used = 0u;
    uint32_t index;
    int saw_erased = 0;

    if (minimum_version == NULL) {
        return HBOX_SECURITY_VERSION_UNAVAILABLE;
    }
    *minimum_version = 0u;
    if (used_records != NULL) {
        *used_records = 0u;
    }
    if (!backend_is_valid(backend, 0)) {
        return HBOX_SECURITY_VERSION_UNAVAILABLE;
    }

    for (index = 0u; index < backend->record_count; ++index) {
        if (!backend->read_record(
                backend->context, index, &record)) {
            memset(&record, 0, sizeof(record));
            return HBOX_SECURITY_VERSION_IO_ERROR;
        }
        if (record_is_erased(&record)) {
            saw_erased = 1;
            continue;
        }
        if (saw_erased ||
            !record_is_valid(
                &record, used + 1u, previous_minimum, previous_crc)) {
            memset(&record, 0, sizeof(record));
            return HBOX_SECURITY_VERSION_CORRUPT;
        }
        previous_minimum = record.minimum_version_le;
        previous_crc = record.record_crc32_le;
        ++used;
    }

    memset(&record, 0, sizeof(record));
    if (used == 0u) {
        return HBOX_SECURITY_VERSION_UNPROVISIONED;
    }
    *minimum_version = previous_minimum;
    if (used_records != NULL) {
        *used_records = used;
    }
    return HBOX_SECURITY_VERSION_OK;
}

hbox_security_version_status_t HBoxSecurityVersionJournal_Provision(
    const hbox_security_version_journal_backend_t *backend,
    uint32_t initial_minimum_version)
{
    hbox_security_version_record_v1_t record;
    uint32_t ignored = 0u;
    hbox_security_version_status_t status;

    if (initial_minimum_version == 0u ||
        !backend_is_valid(backend, 1)) {
        return HBOX_SECURITY_VERSION_UNAVAILABLE;
    }

    status = HBoxSecurityVersionJournal_Load(
        backend, &ignored, NULL);
    if (status != HBOX_SECURITY_VERSION_UNPROVISIONED) {
        return status == HBOX_SECURITY_VERSION_OK
                   ? HBOX_SECURITY_VERSION_ROLLBACK
                   : status;
    }

    build_record(&record, 1u, initial_minimum_version, 0u);
    if (!backend->program_record_atomic(
            backend->context, 0u, &record)) {
        memset(&record, 0, sizeof(record));
        return HBOX_SECURITY_VERSION_IO_ERROR;
    }
    memset(&record, 0, sizeof(record));
    return verify_programmed_state(backend, initial_minimum_version);
}

hbox_security_version_status_t HBoxSecurityVersionJournal_Advance(
    const hbox_security_version_journal_backend_t *backend,
    uint32_t requested_minimum_version)
{
    hbox_security_version_record_v1_t previous;
    hbox_security_version_record_v1_t record;
    uint32_t current_minimum = 0u;
    uint32_t used = 0u;
    hbox_security_version_status_t status;

    if (requested_minimum_version == 0u ||
        !backend_is_valid(backend, 1)) {
        return HBOX_SECURITY_VERSION_UNAVAILABLE;
    }

    status = HBoxSecurityVersionJournal_Load(
        backend, &current_minimum, &used);
    if (status != HBOX_SECURITY_VERSION_OK) {
        return status;
    }
    if (requested_minimum_version < current_minimum) {
        return HBOX_SECURITY_VERSION_ROLLBACK;
    }
    if (requested_minimum_version == current_minimum) {
        return HBOX_SECURITY_VERSION_OK;
    }
    if (used >= backend->record_count) {
        return HBOX_SECURITY_VERSION_FULL;
    }
    if (!backend->read_record(
            backend->context, used - 1u, &previous)) {
        return HBOX_SECURITY_VERSION_IO_ERROR;
    }

    build_record(
        &record,
        used + 1u,
        requested_minimum_version,
        previous.record_crc32_le);
    memset(&previous, 0, sizeof(previous));
    if (!backend->program_record_atomic(
            backend->context, used, &record)) {
        memset(&record, 0, sizeof(record));
        return HBOX_SECURITY_VERSION_IO_ERROR;
    }
    memset(&record, 0, sizeof(record));
    return verify_programmed_state(backend, requested_minimum_version);
}

const char *HBoxSecurityVersion_StatusString(
    hbox_security_version_status_t status)
{
    switch (status) {
    case HBOX_SECURITY_VERSION_OK:
        return "ok";
    case HBOX_SECURITY_VERSION_UNAVAILABLE:
        return "provider-unavailable";
    case HBOX_SECURITY_VERSION_UNPROVISIONED:
        return "unprovisioned";
    case HBOX_SECURITY_VERSION_CORRUPT:
        return "corrupt";
    case HBOX_SECURITY_VERSION_IO_ERROR:
        return "io-error";
    case HBOX_SECURITY_VERSION_FULL:
        return "journal-full";
    case HBOX_SECURITY_VERSION_ROLLBACK:
        return "rollback";
    default:
        return "unknown";
    }
}
