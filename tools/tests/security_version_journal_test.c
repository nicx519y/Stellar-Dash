#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "security_version_journal.h"
#include "security_version_store.h"

#define TEST_RECORDS 4u

typedef struct
{
    uint8_t bytes[TEST_RECORDS][HBOX_SECURITY_VERSION_RECORD_BYTES];
    int fail_read;
    int fail_program;
    int tear_program;
} MockStorage;

#if HBOX_SECURITY_VERSION_PROVIDER_READY
static uint32_t g_provider_minimum = 1u;
static hbox_security_version_status_t g_provider_status =
    HBOX_SECURITY_VERSION_OK;

hbox_security_version_status_t
HBoxSecurityVersionProvider_Load(uint32_t *minimum_version)
{
    if (g_provider_status != HBOX_SECURITY_VERSION_OK) {
        return g_provider_status;
    }
    *minimum_version = g_provider_minimum;
    return HBOX_SECURITY_VERSION_OK;
}

hbox_security_version_status_t
HBoxSecurityVersionProvider_Advance(uint32_t requested_minimum_version)
{
    if (g_provider_status != HBOX_SECURITY_VERSION_OK) {
        return g_provider_status;
    }
    if (requested_minimum_version < g_provider_minimum) {
        return HBOX_SECURITY_VERSION_ROLLBACK;
    }
    g_provider_minimum = requested_minimum_version;
    return HBOX_SECURITY_VERSION_OK;
}
#endif

static void erase_storage(MockStorage *storage)
{
    memset(storage, 0xFF, sizeof(*storage));
    storage->fail_read = 0;
    storage->fail_program = 0;
    storage->tear_program = 0;
}

static int read_record(
    void *context,
    uint32_t record_index,
    hbox_security_version_record_v1_t *record)
{
    MockStorage *storage = (MockStorage *)context;
    if (storage->fail_read || record_index >= TEST_RECORDS) {
        return 0;
    }
    memcpy(record, storage->bytes[record_index], sizeof(*record));
    return 1;
}

static int program_record_atomic(
    void *context,
    uint32_t record_index,
    const hbox_security_version_record_v1_t *record)
{
    MockStorage *storage = (MockStorage *)context;
    size_t index;

    if (record_index >= TEST_RECORDS || storage->fail_program) {
        return 0;
    }
    for (index = 0u; index < sizeof(*record); ++index) {
        if (storage->bytes[record_index][index] != 0xFFu) {
            return 0;
        }
    }
    if (storage->tear_program) {
        memcpy(storage->bytes[record_index], record, 7u);
        return 0;
    }
    memcpy(storage->bytes[record_index], record, sizeof(*record));
    return 1;
}

static hbox_security_version_journal_backend_t backend_for(
    MockStorage *storage)
{
    hbox_security_version_journal_backend_t backend;
    backend.context = storage;
    backend.record_count = TEST_RECORDS;
    backend.read_record = read_record;
    backend.program_record_atomic = program_record_atomic;
    return backend;
}

static void test_default_boot_provider_fails_closed(void)
{
#if HBOX_SECURITY_VERSION_PROVIDER_READY
    uint32_t version = 0u;

    g_provider_minimum = 1u;
    g_provider_status = HBOX_SECURITY_VERSION_OK;
    assert(SecurityVersionStore_Load(&version) ==
           HBOX_SECURITY_VERSION_OK);
    assert(version == 1u);
    assert(SecurityVersionStore_Advance(2u) ==
           HBOX_SECURITY_VERSION_OK);
    assert(g_provider_minimum == 2u);
    assert(SecurityVersionStore_Advance(1u) ==
           HBOX_SECURITY_VERSION_ROLLBACK);

    g_provider_minimum = 0u;
    assert(SecurityVersionStore_Load(&version) ==
           HBOX_SECURITY_VERSION_CORRUPT);
    g_provider_status = HBOX_SECURITY_VERSION_UNPROVISIONED;
    assert(SecurityVersionStore_Load(&version) ==
           HBOX_SECURITY_VERSION_UNPROVISIONED);
#else
    uint32_t version = 123u;
    assert(SecurityVersionStore_Load(&version) ==
           HBOX_SECURITY_VERSION_UNAVAILABLE);
    assert(version == 0u);
    assert(SecurityVersionStore_Advance(2u) ==
           HBOX_SECURITY_VERSION_UNAVAILABLE);
#endif
}

static void test_provision_and_monotonic_advance(void)
{
    MockStorage storage;
    hbox_security_version_journal_backend_t backend;
    uint32_t minimum = 0u;
    uint32_t used = 0u;

    erase_storage(&storage);
    backend = backend_for(&storage);
    minimum = 99u;
    used = 99u;
    assert(HBoxSecurityVersionJournal_Load(
               &backend, &minimum, &used) ==
           HBOX_SECURITY_VERSION_UNPROVISIONED);
    assert(minimum == 0u);
    assert(used == 0u);
    assert(HBoxSecurityVersionJournal_Provision(&backend, 1u) ==
           HBOX_SECURITY_VERSION_OK);
    assert(HBoxSecurityVersionJournal_Load(
               &backend, &minimum, &used) ==
           HBOX_SECURITY_VERSION_OK);
    assert(minimum == 1u);
    assert(used == 1u);

    assert(HBoxSecurityVersionJournal_Advance(&backend, 3u) ==
           HBOX_SECURITY_VERSION_OK);
    assert(HBoxSecurityVersionJournal_Advance(&backend, 3u) ==
           HBOX_SECURITY_VERSION_OK);
    assert(HBoxSecurityVersionJournal_Advance(&backend, 2u) ==
           HBOX_SECURITY_VERSION_ROLLBACK);
    assert(HBoxSecurityVersionJournal_Load(
               &backend, &minimum, &used) ==
           HBOX_SECURITY_VERSION_OK);
    assert(minimum == 3u);
    assert(used == 2u);
}

static void test_corruption_and_gaps_fail_closed(void)
{
    MockStorage storage;
    hbox_security_version_journal_backend_t backend;
    uint32_t minimum = 0u;

    erase_storage(&storage);
    backend = backend_for(&storage);
    assert(HBoxSecurityVersionJournal_Provision(&backend, 1u) ==
           HBOX_SECURITY_VERSION_OK);
    storage.bytes[0][12] ^= 0x01u;
    assert(HBoxSecurityVersionJournal_Load(
               &backend, &minimum, NULL) ==
           HBOX_SECURITY_VERSION_CORRUPT);

    erase_storage(&storage);
    backend = backend_for(&storage);
    assert(HBoxSecurityVersionJournal_Provision(&backend, 1u) ==
           HBOX_SECURITY_VERSION_OK);
    memcpy(storage.bytes[2], storage.bytes[0],
           HBOX_SECURITY_VERSION_RECORD_BYTES);
    assert(HBoxSecurityVersionJournal_Load(
               &backend, &minimum, NULL) ==
           HBOX_SECURITY_VERSION_CORRUPT);
}

static void test_torn_write_is_never_accepted(void)
{
    MockStorage storage;
    hbox_security_version_journal_backend_t backend;
    uint32_t minimum = 0u;

    erase_storage(&storage);
    backend = backend_for(&storage);
    assert(HBoxSecurityVersionJournal_Provision(&backend, 1u) ==
           HBOX_SECURITY_VERSION_OK);
    storage.tear_program = 1;
    assert(HBoxSecurityVersionJournal_Advance(&backend, 2u) ==
           HBOX_SECURITY_VERSION_IO_ERROR);
    assert(HBoxSecurityVersionJournal_Load(
               &backend, &minimum, NULL) ==
           HBOX_SECURITY_VERSION_CORRUPT);
}

static void test_full_and_io_failures(void)
{
    MockStorage storage;
    hbox_security_version_journal_backend_t backend;
    uint32_t minimum = 0u;

    erase_storage(&storage);
    backend = backend_for(&storage);
    assert(HBoxSecurityVersionJournal_Provision(&backend, 1u) ==
           HBOX_SECURITY_VERSION_OK);
    assert(HBoxSecurityVersionJournal_Advance(&backend, 2u) ==
           HBOX_SECURITY_VERSION_OK);
    assert(HBoxSecurityVersionJournal_Advance(&backend, 3u) ==
           HBOX_SECURITY_VERSION_OK);
    assert(HBoxSecurityVersionJournal_Advance(&backend, 4u) ==
           HBOX_SECURITY_VERSION_OK);
    assert(HBoxSecurityVersionJournal_Advance(&backend, 5u) ==
           HBOX_SECURITY_VERSION_FULL);

    storage.fail_read = 1;
    assert(HBoxSecurityVersionJournal_Load(
               &backend, &minimum, NULL) ==
           HBOX_SECURITY_VERSION_IO_ERROR);
}

int main(void)
{
    test_default_boot_provider_fails_closed();
    test_provision_and_monotonic_advance();
    test_corruption_and_gaps_fail_closed();
    test_torn_write_is_never_accepted();
    test_full_and_io_failures();
    puts("security version journal tests passed");
    return 0;
}
