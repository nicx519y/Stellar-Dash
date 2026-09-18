#include "device_identity_store.h"

#include <stddef.h>
#include <string.h>

#include "device_identity_internal_flash_provider.h"
#include "secure_access_handoff.h"
#include "stm32h7xx_hal.h"

#ifndef HBOX_DEVICE_IDENTITY_PROVIDER_READY
#define HBOX_DEVICE_IDENTITY_PROVIDER_READY 0
#endif

#ifndef HBOX_DEVICE_IDENTITY_FACTORY_PROVISIONING
#define HBOX_DEVICE_IDENTITY_FACTORY_PROVISIONING 0
#endif

#if !HBOX_DEVICE_IDENTITY_PROVIDER_READY
#error "internal identity provider requires HBOX_DEVICE_IDENTITY_PROVIDER_READY=1"
#endif

static int flashword_address(
    uint32_t slot_index,
    uint32_t flashword_index,
    uint32_t *address)
{
    uint32_t offset;

    if (address == NULL ||
        slot_index >= HBOX_DEVICE_IDENTITY_SLOT_COUNT ||
        flashword_index >=
            HBOX_DEVICE_IDENTITY_SLOT_BYTES /
                HBOX_INTERNAL_FLASH_PROGRAM_BYTES) {
        return 0;
    }
    offset = slot_index * HBOX_DEVICE_IDENTITY_SLOT_BYTES +
             flashword_index * HBOX_INTERNAL_FLASH_PROGRAM_BYTES;
    if (offset >
        HBOX_DEVICE_IDENTITY_REGION_BYTES -
            HBOX_INTERNAL_FLASH_PROGRAM_BYTES) {
        return 0;
    }
    *address = HBOX_DEVICE_IDENTITY_REGION_ADDRESS + offset;
    return (*address % HBOX_INTERNAL_FLASH_PROGRAM_BYTES) == 0u;
}

static int read_flashword(
    void *context,
    uint32_t slot_index,
    uint32_t flashword_index,
    uint8_t output[HBOX_INTERNAL_FLASH_PROGRAM_BYTES])
{
    uint32_t address = 0u;
    (void)context;

    if (output == NULL ||
        !flashword_address(slot_index, flashword_index, &address)) {
        return 0;
    }
    memcpy(output, (const void *)address,
           HBOX_INTERNAL_FLASH_PROGRAM_BYTES);
    return 1;
}

#if HBOX_DEVICE_IDENTITY_FACTORY_PROVISIONING

static int factory_authorized(void *context)
{
    (void)context;
    return HBoxSecureAccess_ValidateLifecycle() ==
               HBOX_SECURE_ACCESS_OK &&
           HBoxIdentityFactoryGate_IsAuthorized() != 0;
}

static int program_flashword(
    void *context,
    uint32_t slot_index,
    uint32_t flashword_index,
    const uint8_t input[HBOX_INTERNAL_FLASH_PROGRAM_BYTES])
{
    uint32_t staging[HBOX_INTERNAL_FLASH_PROGRAM_BYTES / sizeof(uint32_t)]
        __attribute__((aligned(HBOX_INTERNAL_FLASH_PROGRAM_BYTES)));
    uint32_t address = 0u;
    HAL_StatusTypeDef status;
    size_t index;
    int result = 0;
    (void)context;

    if (input == NULL ||
        HBoxSecureAccess_ValidateLifecycle() !=
            HBOX_SECURE_ACCESS_OK ||
        !HBoxIdentityFactoryGate_IsAuthorized() ||
        !flashword_address(slot_index, flashword_index, &address)) {
        return 0;
    }
    for (index = 0u;
         index < HBOX_INTERNAL_FLASH_PROGRAM_BYTES;
         ++index) {
        if (((const uint8_t *)address)[index] != 0xFFu) {
            return 0;
        }
    }
    memcpy(staging, input, sizeof(staging));

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
        (int32_t)HBOX_INTERNAL_FLASH_PROGRAM_BYTES);
#endif
    __DSB();
    __ISB();
    result = memcmp(
                 (const void *)address,
                 input,
                 HBOX_INTERNAL_FLASH_PROGRAM_BYTES) == 0;

done:
    memset(staging, 0, sizeof(staging));
    return result;
}
#endif /* HBOX_DEVICE_IDENTITY_FACTORY_PROVISIONING */

int HBoxIdentityStoreProvider_Open(
    hbox_device_identity_backend_t *backend)
{
    if (backend == NULL) {
        return 0;
    }
    memset(backend, 0, sizeof(*backend));
    backend->slot_count = HBOX_DEVICE_IDENTITY_SLOT_COUNT;
    backend->read_flashword = read_flashword;
#if HBOX_DEVICE_IDENTITY_FACTORY_PROVISIONING
    backend->program_flashword = program_flashword;
    backend->factory_authorized = factory_authorized;
#endif
    return 1;
}
