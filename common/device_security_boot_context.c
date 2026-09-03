#include "device_security_boot_context.h"

#include <string.h>

static int all_zero(const uint8_t *value, size_t length)
{
    uint8_t combined = 0u;
    size_t index;
    for (index = 0u; index < length; ++index) {
        combined |= value[index];
    }
    return combined == 0u;
}

static int contains_nul(const char *value, size_t length)
{
    size_t index;
    for (index = 0u; index < length; ++index) {
        if (value[index] == '\0') {
            return 1;
        }
    }
    return 0;
}

uint32_t HBoxSecurity_Crc32Skipping(const uint8_t *data,
                                    size_t length,
                                    size_t skip_offset,
                                    size_t skip_size)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t index;
    uint8_t bit;

    if (data == NULL || skip_offset > length ||
        skip_size > (length - skip_offset)) {
        return 0u;
    }

    for (index = 0u; index < length; ++index) {
        if (index >= skip_offset && index < (skip_offset + skip_size)) {
            continue;
        }
        crc ^= data[index];
        for (bit = 0u; bit < 8u; ++bit) {
            const uint32_t mask =
                (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

void HBoxSecurity_InvalidateBootContext(void)
{
    volatile uint8_t *context =
        (volatile uint8_t *)HBOX_BOOT_CONTEXT_ADDRESS;
    size_t index;

    /*
     * SRAM4 can survive some reset paths.  Clearing only magic/CRC would
     * invalidate parsing while leaving the previous boot private key in RAM.
     * Wipe the complete handoff before every attempt.
     */
    for (index = 0u;
         index < sizeof(hbox_boot_security_context_v1_t);
         ++index) {
        context[index] = 0u;
    }
}

int HBoxSecurity_ValidateBootContext(
    const hbox_boot_security_context_v1_t *context)
{
    uint32_t expected_crc;
    uint8_t nonzero_private = 0u;
    size_t index;

    if (context == NULL ||
        sizeof(*context) > HBOX_BOOT_CONTEXT_MAX_BYTES ||
        context->magic_le != HBOX_BOOT_CONTEXT_MAGIC ||
        context->version != HBOX_BOOT_CONTEXT_VERSION ||
        context->reserved0 != 0u ||
        context->total_bytes_le != sizeof(*context) ||
        !all_zero(context->reserved, sizeof(context->reserved)) ||
        !contains_nul(context->firmware_version,
                      sizeof(context->firmware_version))) {
        return 0;
    }
    for (index = 0u; index < sizeof(context->boot_private_key); ++index) {
        nonzero_private |= context->boot_private_key[index];
    }
    if (nonzero_private == 0u ||
        context->device_certificate.magic_le !=
            HBOX_DEVICE_CERTIFICATE_MAGIC ||
        context->device_certificate.version !=
            HBOX_SECURITY_PROTOCOL_VERSION ||
        context->device_certificate.auth_level <
            HBOX_AUTH_LEVEL_MCU_PROTECTED ||
        context->device_certificate.auth_level >
            HBOX_AUTH_LEVEL_SECURE_ELEMENT ||
        context->device_certificate.signed_bytes_le !=
            HBOX_DEVICE_CERTIFICATE_SIGNED_BYTES ||
        context->device_certificate.device_public_key[0] != 0x04u ||
        all_zero(context->device_certificate.certificate_serial,
                 sizeof(context->device_certificate.certificate_serial)) ||
        all_zero(context->device_certificate.device_id,
                 sizeof(context->device_certificate.device_id)) ||
        context->device_certificate.product_id_le != HBOX_PRODUCT_ID ||
        !all_zero(context->device_certificate.reserved,
                  sizeof(context->device_certificate.reserved)) ||
        context->boot_attestation.magic_le !=
            HBOX_BOOT_ATTESTATION_MAGIC ||
        context->boot_attestation.version !=
            HBOX_SECURITY_PROTOCOL_VERSION ||
        !all_zero(context->boot_attestation.reserved0,
                  sizeof(context->boot_attestation.reserved0)) ||
        context->boot_attestation.signed_bytes_le !=
            HBOX_BOOT_ATTESTATION_SIGNED_BYTES ||
        context->boot_attestation.reserved1 != 0u ||
        context->boot_attestation.boot_public_key[0] != 0x04u ||
        all_zero(context->boot_attestation.boot_nonce,
                 sizeof(context->boot_attestation.boot_nonce)) ||
        all_zero(context->boot_attestation.firmware_hash,
                 sizeof(context->boot_attestation.firmware_hash)) ||
        context->boot_attestation.security_version_le == 0u ||
        memcmp(context->device_certificate.device_id,
               context->boot_attestation.device_id,
               HBOX_SECURITY_DEVICE_ID_BYTES) != 0) {
        return 0;
    }
    expected_crc = HBoxSecurity_Crc32Skipping(
        (const uint8_t *)context,
        sizeof(*context),
        HBOX_BOOT_CONTEXT_CRC_OFFSET,
        sizeof(context->crc32_le));
    return expected_crc != 0u && expected_crc == context->crc32_le;
}
