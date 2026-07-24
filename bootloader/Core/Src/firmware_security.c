#include "firmware_security.h"

#include <stddef.h>
#include <string.h>

#include "firmware_signature.h"
#include "qspi-w25q64.h"
#include "security_version_store.h"
#include "sha256_simple.h"

typedef struct {
    const char* name;
    uint32_t address_a;
    uint32_t address_b;
    uint32_t maximum_size;
} ComponentPolicy;

static const ComponentPolicy k_component_policy[FIRMWARE_COMPONENT_COUNT] = {
    {"application", SLOT_A_APPLICATION_ADDR, SLOT_B_APPLICATION_ADDR,
     SLOT_A_APPLICATION_SIZE},
    {"webresources", SLOT_A_WEBRESOURCES_ADDR, SLOT_B_WEBRESOURCES_ADDR,
     SLOT_A_WEBRESOURCES_SIZE},
    {"adc_mapping", SLOT_A_ADC_MAPPING_ADDR, SLOT_B_ADC_MAPPING_ADDR,
     SLOT_A_ADC_MAPPING_SIZE},
};

static hbox_security_version_status_t
    g_last_security_version_status = HBOX_SECURITY_VERSION_UNAVAILABLE;

static int decode_hex_digit(char digit)
{
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'a' && digit <= 'f') {
        return digit - 'a' + 10;
    }
    if (digit >= 'A' && digit <= 'F') {
        return digit - 'A' + 10;
    }
    return -1;
}

static bool decode_sha256(const char encoded[65], uint8_t decoded[32])
{
    size_t i;

    if (encoded[64] != '\0') {
        return false;
    }
    for (i = 0u; i < 32u; ++i) {
        int high = decode_hex_digit(encoded[i * 2u]);
        int low = decode_hex_digit(encoded[i * 2u + 1u]);
        if (high < 0 || low < 0) {
            return false;
        }
        decoded[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static bool constant_time_equal(const uint8_t* a,
                                const uint8_t* b,
                                size_t length)
{
    uint8_t difference = 0u;
    size_t i;
    for (i = 0u; i < length; ++i) {
        difference |= (uint8_t)(a[i] ^ b[i]);
    }
    return difference == 0u;
}

static bool hash_external_flash(uint32_t mapped_address,
                                uint32_t size,
                                uint8_t hash[32])
{
    sha256_simple_ctx_t context;
    uint32_t offset = 0u;
    uint8_t buffer[256];

    if (mapped_address < EXTERNAL_FLASH_BASE ||
        mapped_address + size < mapped_address ||
        mapped_address + size >
            EXTERNAL_FLASH_BASE + EXTERNAL_FLASH_SIZE) {
        return false;
    }

    sha256_simple_init(&context);
    if (QSPI_W25Qxx_IsMemoryMappedMode()) {
        sha256_simple_update(
            &context, (const uint8_t*)mapped_address, size);
    } else {
        while (offset < size) {
            uint32_t remaining = size - offset;
            uint32_t count = remaining < sizeof(buffer)
                                 ? remaining
                                 : (uint32_t)sizeof(buffer);
            if (QSPI_W25Qxx_ReadBuffer(
                    buffer,
                    mapped_address - EXTERNAL_FLASH_BASE + offset,
                    count) != QSPI_W25Qxx_OK) {
                memset(buffer, 0, sizeof(buffer));
                return false;
            }
            sha256_simple_update(&context, buffer, count);
            offset += count;
        }
    }
    sha256_simple_final(&context, hash);
    memset(buffer, 0, sizeof(buffer));
    return true;
}

FirmwareValidationResult FirmwareSecurity_ValidateMetadata(
    const FirmwareMetadata* metadata)
{
    uint32_t stored_minimum = 0u;
    size_t i;

    if (metadata == NULL) {
        return FIRMWARE_INVALID_VERSION;
    }

    g_last_security_version_status =
        SecurityVersionStore_Load(&stored_minimum);
    if (g_last_security_version_status !=
            HBOX_SECURITY_VERSION_OK ||
        stored_minimum < FIRMWARE_SECURITY_VERSION ||
        metadata->security_version < stored_minimum ||
        metadata->target_slot >= FIRMWARE_SLOT_COUNT ||
        metadata->webresources_optional > 1u) {
        return FIRMWARE_INVALID_VERSION;
    }
    for (i = 0u; i < sizeof(metadata->reserved); ++i) {
        if (metadata->reserved[i] != 0u) {
            return FIRMWARE_CORRUPTED;
        }
    }
    return firmware_metadata_verify_signature(metadata);
}

bool FirmwareSecurity_CommitValidatedSecurityVersion(
    const FirmwareMetadata* metadata)
{
    if (metadata == NULL ||
        FirmwareSecurity_ValidateMetadata(metadata) != FIRMWARE_VALID) {
        return false;
    }

    g_last_security_version_status =
        SecurityVersionStore_Advance(metadata->security_version);
    return g_last_security_version_status ==
           HBOX_SECURITY_VERSION_OK;
}

hbox_security_version_status_t
FirmwareSecurity_LastSecurityVersionStatus(void)
{
    return g_last_security_version_status;
}

bool FirmwareSecurity_ValidateSlot(const FirmwareMetadata* metadata,
                                   FirmwareSlot slot)
{
    bool seen[FIRMWARE_COMPONENT_COUNT] = {false, false, false};
    uint32_t component_index;

    if (FirmwareSecurity_ValidateMetadata(metadata) != FIRMWARE_VALID ||
        slot >= FIRMWARE_SLOT_COUNT ||
        metadata->target_slot != (uint8_t)slot ||
        metadata->component_count != FIRMWARE_COMPONENT_COUNT) {
        return false;
    }

    for (component_index = 0u;
         component_index < metadata->component_count;
         ++component_index) {
        const FirmwareComponent* component =
            &metadata->components[component_index];
        size_t policy_index;
        const ComponentPolicy* policy = NULL;
        uint32_t expected_address;
        uint8_t expected_hash[32];
        uint8_t actual_hash[32];

        for (policy_index = 0u;
             policy_index < FIRMWARE_COMPONENT_COUNT;
             ++policy_index) {
            if (strncmp(component->name,
                        k_component_policy[policy_index].name,
                        sizeof(component->name)) == 0) {
                policy = &k_component_policy[policy_index];
                break;
            }
        }
        if (policy == NULL || seen[policy_index]) {
            return false;
        }
        seen[policy_index] = true;
        expected_address = slot == FIRMWARE_SLOT_A
                               ? policy->address_a
                               : policy->address_b;

        if (policy_index == FIRMWARE_COMPONENT_WEBRESOURCES &&
            metadata->webresources_optional == 1u &&
            !component->active && component->size == 0u) {
            if (component->address != expected_address) {
                return false;
            }
            continue;
        }

        if (!component->active ||
            component->address != expected_address ||
            component->size == 0u ||
            component->size > policy->maximum_size ||
            !decode_sha256(component->sha256, expected_hash) ||
            !hash_external_flash(component->address,
                                 component->size,
                                 actual_hash) ||
            !constant_time_equal(expected_hash,
                                 actual_hash,
                                 sizeof(actual_hash))) {
            memset(expected_hash, 0, sizeof(expected_hash));
            memset(actual_hash, 0, sizeof(actual_hash));
            return false;
        }
        memset(expected_hash, 0, sizeof(expected_hash));
        memset(actual_hash, 0, sizeof(actual_hash));
    }

    for (component_index = 0u;
         component_index < FIRMWARE_COMPONENT_COUNT;
         ++component_index) {
        if (!seen[component_index]) {
            return false;
        }
    }
    return true;
}
