#include "security_version_internal_flash_provider.h"

#include <stddef.h>
#include <string.h>

#include "device_identity_internal_flash_provider.h"
#include "secure_access_handoff.h"
#include "stm32h7xx_hal.h"

#ifndef HBOX_SECURITY_VERSION_PROVIDER_READY
#define HBOX_SECURITY_VERSION_PROVIDER_READY 0
#endif

#ifndef HBOX_SECURITY_VERSION_INTERNAL_FLASH_PROVIDER
#define HBOX_SECURITY_VERSION_INTERNAL_FLASH_PROVIDER 0
#endif

#ifndef HBOX_FACTORY_IDENTITY_ENROLLMENT
#define HBOX_FACTORY_IDENTITY_ENROLLMENT 0
#endif

#if !HBOX_SECURITY_VERSION_PROVIDER_READY
#error "internal security-version provider requires HBOX_SECURITY_VERSION_PROVIDER_READY=1"
#endif

#if !HBOX_SECURITY_VERSION_INTERNAL_FLASH_PROVIDER
#error "internal security-version provider requires its explicit in-tree build switch"
#endif

static int record_address(uint32_t record_index, uint32_t *address)
{
    uint32_t offset;

    if (address == NULL ||
        record_index >= HBOX_SECURITY_VERSION_RECORD_COUNT) {
        return 0;
    }
    offset = record_index * HBOX_SECURITY_VERSION_RECORD_BYTES;
    if (offset >
        HBOX_SECURITY_VERSION_REGION_BYTES -
            HBOX_SECURITY_VERSION_RECORD_BYTES) {
        return 0;
    }
    *address = HBOX_SECURITY_VERSION_REGION_ADDRESS + offset;
    return (*address % HBOX_INTERNAL_FLASH_PROGRAM_BYTES) == 0u;
}

static int read_record(
    void *context,
    uint32_t record_index,
    hbox_security_version_record_v1_t *record)
{
    uint32_t address = 0u;
    (void)context;

    if (record == NULL ||
        !record_address(record_index, &address)) {
        return 0;
    }
    memcpy(record, (const void *)address, sizeof(*record));
    return 1;
}

static int bytes_are_erased(const uint8_t *bytes, size_t length)
{
    size_t index;

    for (index = 0u; index < length; ++index) {
        if (bytes[index] != 0xFFu) {
            return 0;
        }
    }
    return 1;
}

static int program_record_atomic(
    void *context,
    uint32_t record_index,
    const hbox_security_version_record_v1_t *record)
{
    uint32_t staging[
        HBOX_INTERNAL_FLASH_PROGRAM_BYTES / sizeof(uint32_t)]
        __attribute__((aligned(HBOX_INTERNAL_FLASH_PROGRAM_BYTES)));
    uint32_t address = 0u;
    HAL_StatusTypeDef status;
    int result = 0;
    (void)context;

    if (record == NULL ||
        HBoxSecureAccess_ValidateLifecycle() != HBOX_SECURE_ACCESS_OK ||
        !record_address(record_index, &address) ||
        !bytes_are_erased(
            (const uint8_t *)address,
            HBOX_SECURITY_VERSION_RECORD_BYTES)) {
        return 0;
    }
    memcpy(staging, record, sizeof(staging));

    if (HAL_FLASH_Unlock() != HAL_OK) {
        goto done;
    }
    status = HAL_FLASH_Program(
        FLASH_TYPEPROGRAM_FLASHWORD,
        address,
        (uint32_t)staging);
    (void)HAL_FLASH_Lock();
    if (status != HAL_OK) {
        goto done;
    }

    __DSB();
#if (__DCACHE_PRESENT == 1U)
    SCB_InvalidateDCache_by_Addr(
        (uint32_t *)address,
        (int32_t)HBOX_SECURITY_VERSION_RECORD_BYTES);
#endif
    __DSB();
    __ISB();
    result = memcmp(
                 (const void *)address,
                 record,
                 sizeof(*record)) == 0;

done:
    memset(staging, 0, sizeof(staging));
    return result;
}

static void open_backend(
    hbox_security_version_journal_backend_t *backend)
{
    memset(backend, 0, sizeof(*backend));
    backend->record_count = HBOX_SECURITY_VERSION_RECORD_COUNT;
    backend->read_record = read_record;
    backend->program_record_atomic = program_record_atomic;
}

hbox_security_version_status_t
HBoxSecurityVersionProvider_Load(uint32_t *minimum_version)
{
    hbox_security_version_journal_backend_t backend;
    hbox_security_version_status_t status;

    if (minimum_version == NULL) {
        return HBOX_SECURITY_VERSION_UNAVAILABLE;
    }
    *minimum_version = 0u;
    open_backend(&backend);
    status = HBoxSecurityVersionJournal_Load(
        &backend, minimum_version, NULL);
    memset(&backend, 0, sizeof(backend));
    if (status != HBOX_SECURITY_VERSION_OK) {
        *minimum_version = 0u;
    }
    return status;
}

hbox_security_version_status_t
HBoxSecurityVersionProvider_Advance(
    uint32_t requested_minimum_version)
{
    hbox_security_version_journal_backend_t backend;
    hbox_security_version_status_t status;

    if (HBoxSecureAccess_ValidateLifecycle() !=
        HBOX_SECURE_ACCESS_OK) {
        return HBOX_SECURITY_VERSION_UNAVAILABLE;
    }
    open_backend(&backend);
    status = HBoxSecurityVersionJournal_Advance(
        &backend, requested_minimum_version);
    memset(&backend, 0, sizeof(backend));
    return status;
}

hbox_security_version_status_t
HBoxSecurityVersionInternalFlash_ProvisionFactory(
    uint32_t initial_minimum_version)
{
#if HBOX_FACTORY_IDENTITY_ENROLLMENT
    hbox_security_version_journal_backend_t backend;
    hbox_security_version_status_t status;
    uint32_t current = 0u;

    if (HBoxSecureAccess_ValidateLifecycle() !=
            HBOX_SECURE_ACCESS_OK ||
        HBoxIdentityFactoryGate_IsAuthorized() == 0) {
        return HBOX_SECURITY_VERSION_UNAVAILABLE;
    }

    open_backend(&backend);
    status = HBoxSecurityVersionJournal_Load(
        &backend, &current, NULL);
    if (status == HBOX_SECURITY_VERSION_OK) {
        status = current == initial_minimum_version
                     ? HBOX_SECURITY_VERSION_OK
                     : HBOX_SECURITY_VERSION_ROLLBACK;
    } else if (status == HBOX_SECURITY_VERSION_UNPROVISIONED) {
        status = HBoxSecurityVersionJournal_Provision(
            &backend, initial_minimum_version);
    }
    current = 0u;
    memset(&backend, 0, sizeof(backend));
    return status;
#else
    (void)initial_minimum_version;
    return HBOX_SECURITY_VERSION_UNAVAILABLE;
#endif
}
