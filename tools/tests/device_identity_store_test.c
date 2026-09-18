#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "device_identity_store.h"
#include "device_security_boot_context.h"

typedef struct
{
    uint8_t slots[HBOX_DEVICE_IDENTITY_SLOT_COUNT]
                 [HBOX_DEVICE_IDENTITY_SLOT_BYTES];
    int authorized;
    int fail_on_program_call;
    int program_calls;
    int fail_on_read_call;
    int read_calls;
    uint32_t last_flashword;
} MockIdentityStorage;

static void erase_mock(MockIdentityStorage *storage)
{
    memset(storage, 0xFF, sizeof(storage->slots));
    storage->authorized = 0;
    storage->fail_on_program_call = -1;
    storage->program_calls = 0;
    storage->fail_on_read_call = -1;
    storage->read_calls = 0;
    storage->last_flashword = UINT32_MAX;
}

static int read_flashword(
    void *context,
    uint32_t slot_index,
    uint32_t flashword_index,
    uint8_t output[HBOX_INTERNAL_FLASH_PROGRAM_BYTES])
{
    MockIdentityStorage *storage = (MockIdentityStorage *)context;
    uint32_t offset =
        flashword_index * HBOX_INTERNAL_FLASH_PROGRAM_BYTES;

    if (storage->read_calls++ == storage->fail_on_read_call) {
        return 0;
    }
    if (slot_index >= HBOX_DEVICE_IDENTITY_SLOT_COUNT ||
        offset >
            HBOX_DEVICE_IDENTITY_SLOT_BYTES -
                HBOX_INTERNAL_FLASH_PROGRAM_BYTES) {
        return 0;
    }
    memcpy(
        output,
        storage->slots[slot_index] + offset,
        HBOX_INTERNAL_FLASH_PROGRAM_BYTES);
    return 1;
}

static int program_flashword(
    void *context,
    uint32_t slot_index,
    uint32_t flashword_index,
    const uint8_t input[HBOX_INTERNAL_FLASH_PROGRAM_BYTES])
{
    MockIdentityStorage *storage = (MockIdentityStorage *)context;
    uint32_t offset =
        flashword_index * HBOX_INTERNAL_FLASH_PROGRAM_BYTES;
    uint8_t *destination;
    size_t index;
    int call = storage->program_calls++;

    if (slot_index >= HBOX_DEVICE_IDENTITY_SLOT_COUNT ||
        offset >
            HBOX_DEVICE_IDENTITY_SLOT_BYTES -
                HBOX_INTERNAL_FLASH_PROGRAM_BYTES) {
        return 0;
    }
    destination = storage->slots[slot_index] + offset;
    for (index = 0u;
         index < HBOX_INTERNAL_FLASH_PROGRAM_BYTES;
         ++index) {
        if (destination[index] != 0xFFu) {
            return 0;
        }
    }
    storage->last_flashword = flashword_index;
    if (call == storage->fail_on_program_call) {
        /* Model a torn 32-byte operation. */
        memcpy(destination, input, 11u);
        return 0;
    }
    memcpy(destination, input, HBOX_INTERNAL_FLASH_PROGRAM_BYTES);
    return 1;
}

static int factory_authorized(void *context)
{
    return ((MockIdentityStorage *)context)->authorized;
}

static hbox_device_identity_backend_t backend_for(
    MockIdentityStorage *storage)
{
    hbox_device_identity_backend_t backend;
    backend.context = storage;
    backend.slot_count = HBOX_DEVICE_IDENTITY_SLOT_COUNT;
    backend.read_flashword = read_flashword;
    backend.program_flashword = program_flashword;
    backend.factory_authorized = factory_authorized;
    return backend;
}

static void build_record(hbox_device_identity_record_v1_t *record)
{
    memset(record, 0, sizeof(*record));
    record->magic_le = HBOX_DEVICE_IDENTITY_MAGIC;
    record->version = HBOX_DEVICE_IDENTITY_VERSION;
    record->locked = HBOX_DEVICE_IDENTITY_LOCKED;
    record->total_bytes_le = sizeof(*record);
    record->device_private_key[31] = 1u;
    record->device_certificate.magic_le =
        HBOX_DEVICE_CERTIFICATE_MAGIC;
    record->device_certificate.version =
        HBOX_SECURITY_PROTOCOL_VERSION;
    record->device_certificate.signed_bytes_le =
        HBOX_DEVICE_CERTIFICATE_SIGNED_BYTES;
    record->crc32_le = HBoxSecurity_Crc32Skipping(
        (const uint8_t *)record,
        sizeof(*record),
        offsetof(hbox_device_identity_record_v1_t, crc32_le),
        sizeof(record->crc32_le));
    assert(record->crc32_le != 0u);
}

static void test_default_provider_fails_closed(void)
{
    hbox_device_identity_record_v1_t record;
    memset(&record, 0xA5, sizeof(record));
    assert(HBoxIdentityStore_LoadStatus(&record) ==
           HBOX_DEVICE_IDENTITY_UNAVAILABLE);
    assert(HBoxIdentityStore_Load(&record) == 0);
    assert(record.magic_le == 0u);
}

static void test_factory_gate_and_commit_last(void)
{
    MockIdentityStorage storage;
    hbox_device_identity_backend_t backend;
    hbox_device_identity_record_v1_t expected;
    hbox_device_identity_record_v1_t actual;

    erase_mock(&storage);
    backend = backend_for(&storage);
    build_record(&expected);
    assert(HBoxIdentityStore_LoadFromBackend(&backend, &actual) ==
           HBOX_DEVICE_IDENTITY_UNPROVISIONED);
    assert(HBoxIdentityStore_ProvisionFactory(&backend, &expected) ==
           HBOX_DEVICE_IDENTITY_FACTORY_DENIED);
    assert(storage.program_calls == 0);

    storage.authorized = 1;
    assert(HBoxIdentityStore_ProvisionFactory(&backend, &expected) ==
           HBOX_DEVICE_IDENTITY_OK);
    assert(storage.program_calls == 9);
    assert(storage.last_flashword == 8u);
    assert(HBoxIdentityStore_LoadFromBackend(&backend, &actual) ==
           HBOX_DEVICE_IDENTITY_OK);
    assert(memcmp(&actual, &expected, sizeof(actual)) == 0);
    assert(HBoxIdentityStore_ProvisionFactory(&backend, &expected) ==
           HBOX_DEVICE_IDENTITY_ALREADY_PROVISIONED);
    assert(storage.program_calls == 9);
}

static void test_every_program_boundary_is_power_loss_safe(void)
{
    int fail_call;

    for (fail_call = 0; fail_call < 9; ++fail_call) {
        MockIdentityStorage storage;
        hbox_device_identity_backend_t backend;
        hbox_device_identity_record_v1_t expected;
        hbox_device_identity_record_v1_t actual;

        erase_mock(&storage);
        storage.authorized = 1;
        storage.fail_on_program_call = fail_call;
        backend = backend_for(&storage);
        build_record(&expected);

        assert(HBoxIdentityStore_ProvisionFactory(
                   &backend, &expected) ==
               HBOX_DEVICE_IDENTITY_IO_ERROR);
        assert(HBoxIdentityStore_LoadFromBackend(
                   &backend, &actual) ==
               HBOX_DEVICE_IDENTITY_UNPROVISIONED);

        /* No erase: retry consumes the next wholly erased slot. */
        storage.fail_on_program_call = -1;
        storage.program_calls = 0;
        assert(HBoxIdentityStore_ProvisionFactory(
                   &backend, &expected) ==
               HBOX_DEVICE_IDENTITY_OK);
        assert(HBoxIdentityStore_LoadFromBackend(
                   &backend, &actual) ==
               HBOX_DEVICE_IDENTITY_OK);
        assert(memcmp(&actual, &expected, sizeof(actual)) == 0);
    }
}

static void test_committed_record_corruption_fails_closed(void)
{
    MockIdentityStorage storage;
    hbox_device_identity_backend_t backend;
    hbox_device_identity_record_v1_t expected;
    hbox_device_identity_record_v1_t actual;

    erase_mock(&storage);
    storage.authorized = 1;
    backend = backend_for(&storage);
    build_record(&expected);
    assert(HBoxIdentityStore_ProvisionFactory(&backend, &expected) ==
           HBOX_DEVICE_IDENTITY_OK);
    storage.slots[0][100] ^= 1u;
    assert(HBoxIdentityStore_LoadFromBackend(&backend, &actual) ==
           HBOX_DEVICE_IDENTITY_CORRUPT);
    assert(actual.magic_le == 0u);
}

static void test_read_failure_after_valid_slot_clears_output(void)
{
    MockIdentityStorage storage;
    hbox_device_identity_backend_t backend;
    hbox_device_identity_record_v1_t expected;
    hbox_device_identity_record_v1_t actual;

    erase_mock(&storage);
    storage.authorized = 1;
    backend = backend_for(&storage);
    build_record(&expected);
    assert(HBoxIdentityStore_ProvisionFactory(&backend, &expected) ==
           HBOX_DEVICE_IDENTITY_OK);

    storage.read_calls = 0;
    storage.fail_on_read_call = 9;
    memset(&actual, 0xA5, sizeof(actual));
    assert(HBoxIdentityStore_LoadFromBackend(&backend, &actual) ==
           HBOX_DEVICE_IDENTITY_IO_ERROR);
    assert(actual.magic_le == 0u);
    assert(actual.device_private_key[31] == 0u);
}

int main(void)
{
    test_default_provider_fails_closed();
    test_factory_gate_and_commit_last();
    test_every_program_boundary_is_power_loss_safe();
    test_committed_record_corruption_fails_closed();
    test_read_failure_after_valid_slot_clears_output();
    puts("device identity store tests passed");
    return 0;
}
