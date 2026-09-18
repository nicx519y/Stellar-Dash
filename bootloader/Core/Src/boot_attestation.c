#include "boot_attestation.h"

#include <stddef.h>
#include <string.h>

#include "device_identity_store.h"
#include "device_security_boot_context.h"
#include "device_security_crypto.h"
#include "dual_slot_config.h"
#include "firmware_metadata.h"
#include "hardware_rng.h"
#include "manufacturer_ca_public_key.h"
#include "stm32h7xx_hal.h"

static int all_zero(const uint8_t *value, size_t length)
{
    uint8_t combined = 0u;
    size_t index;
    for (index = 0u; index < length; ++index) {
        combined |= value[index];
    }
    return combined == 0u;
}

static int verify_device_identity(
    const hbox_device_identity_record_v1_t *identity)
{
    uint8_t public_key[HBOX_SECURITY_P256_PUBLIC_KEY_BYTES];
    uint8_t digest[HBOX_SECURITY_HASH_BYTES];
    uint8_t derived_device_id[HBOX_SECURITY_HASH_BYTES];
    int valid = 0;

    if (identity == NULL ||
        HBOX_MANUFACTURER_CA_KEY_PROVISIONED == 0u ||
        all_zero(HBOX_MANUFACTURER_CA_PUBLIC_KEY,
                 sizeof(HBOX_MANUFACTURER_CA_PUBLIC_KEY))) {
        return 0;
    }
    if (identity->device_certificate.magic_le !=
            HBOX_DEVICE_CERTIFICATE_MAGIC ||
        identity->device_certificate.version !=
            HBOX_SECURITY_PROTOCOL_VERSION ||
        identity->device_certificate.auth_level <
            HBOX_AUTH_LEVEL_MCU_PROTECTED ||
        identity->device_certificate.auth_level >
            HBOX_AUTH_LEVEL_SECURE_ELEMENT ||
        identity->device_certificate.signed_bytes_le !=
            HBOX_DEVICE_CERTIFICATE_SIGNED_BYTES ||
        identity->device_certificate.hardware_version_le !=
            HARDWARE_VERSION ||
        identity->device_certificate.product_id_le != HBOX_PRODUCT_ID ||
        identity->device_certificate.device_public_key[0] != 0x04u ||
        all_zero(identity->device_certificate.certificate_serial,
                 sizeof(identity->device_certificate.certificate_serial)) ||
        all_zero(identity->device_certificate.device_id,
                 sizeof(identity->device_certificate.device_id)) ||
        !all_zero(identity->device_certificate.reserved,
                  sizeof(identity->device_certificate.reserved)) ||
        HBoxCrypto_P256PublicFromPrivate(identity->device_private_key,
                                         public_key) != 0 ||
        memcmp(public_key,
               identity->device_certificate.device_public_key,
               sizeof(public_key)) != 0 ||
        HBoxCrypto_Sha256(
            identity->device_certificate.device_public_key,
            sizeof(identity->device_certificate.device_public_key),
            derived_device_id) != 0 ||
        memcmp(derived_device_id,
               identity->device_certificate.device_id,
               HBOX_SECURITY_DEVICE_ID_BYTES) != 0 ||
        HBoxCrypto_Sha256(
            (const uint8_t *)&identity->device_certificate,
            HBOX_DEVICE_CERTIFICATE_SIGNED_BYTES,
            digest) != 0) {
        goto done;
    }
    valid = HBoxCrypto_P256VerifyDigest(
                HBOX_MANUFACTURER_CA_PUBLIC_KEY,
                digest,
                identity->device_certificate.manufacturer_signature) == 0;

done:
    HBoxCrypto_Zeroize(public_key, sizeof(public_key));
    HBoxCrypto_Zeroize(digest, sizeof(digest));
    HBoxCrypto_Zeroize(derived_device_id, sizeof(derived_device_id));
    return valid;
}

void BootAttestation_Invalidate(void)
{
    HBoxSecurity_InvalidateBootContext();
    __DSB();
}

bool BootAttestation_Prepare(const FirmwareMetadata *metadata)
{
    hbox_device_identity_record_v1_t identity;
    hbox_boot_security_context_v1_t staging;
    volatile hbox_boot_security_context_v1_t *destination =
        (volatile hbox_boot_security_context_v1_t *)
            HBOX_BOOT_CONTEXT_ADDRESS;
    uint8_t digest[HBOX_SECURITY_HASH_BYTES];
    bool result = false;

    BootAttestation_Invalidate();
    memset(&identity, 0, sizeof(identity));
    memset(&staging, 0, sizeof(staging));
    memset(digest, 0, sizeof(digest));

    if (metadata == NULL ||
        metadata->signature_algorithm !=
            FIRMWARE_SIGNATURE_ECDSA_P256_SHA256 ||
        metadata->security_version < FIRMWARE_SECURITY_VERSION ||
        !HBoxIdentityStore_Load(&identity) ||
        !verify_device_identity(&identity) ||
        !HBoxHardwareRng_Init()) {
        goto done;
    }

    staging.magic_le = HBOX_BOOT_CONTEXT_MAGIC;
    staging.version = HBOX_BOOT_CONTEXT_VERSION;
    staging.total_bytes_le = sizeof(staging);
    staging.created_at_tick_le = HAL_GetTick();
    memcpy(&staging.device_certificate,
           &identity.device_certificate,
           sizeof(staging.device_certificate));
    memcpy(staging.boot_attestation.device_id,
           identity.device_certificate.device_id,
           HBOX_SECURITY_DEVICE_ID_BYTES);
    memcpy(staging.boot_attestation.firmware_hash,
           metadata->firmware_hash,
           HBOX_SECURITY_HASH_BYTES);
    staging.boot_attestation.magic_le =
        HBOX_BOOT_ATTESTATION_MAGIC;
    staging.boot_attestation.version =
        HBOX_SECURITY_PROTOCOL_VERSION;
    staging.boot_attestation.signed_bytes_le =
        HBOX_BOOT_ATTESTATION_SIGNED_BYTES;
    staging.boot_attestation.security_version_le =
        metadata->security_version;
    staging.boot_attestation.bootloader_version_le =
        BOOTLOADER_VERSION;

    if (HBoxHardwareRng_Fill(
            NULL,
            staging.boot_attestation.boot_nonce,
            sizeof(staging.boot_attestation.boot_nonce)) != 0 ||
        HBoxCrypto_P256Generate(
            staging.boot_private_key,
            staging.boot_attestation.boot_public_key,
            HBoxHardwareRng_Fill,
            NULL) != 0 ||
        HBoxCrypto_Sha256(
            (const uint8_t *)&staging.boot_attestation,
            HBOX_BOOT_ATTESTATION_SIGNED_BYTES,
            digest) != 0 ||
        HBoxCrypto_P256SignDigest(
            identity.device_private_key,
            digest,
            staging.boot_attestation.device_signature,
            HBoxHardwareRng_Fill,
            NULL) != 0) {
        goto done;
    }

    strncpy(staging.firmware_version,
            metadata->firmware_version,
            sizeof(staging.firmware_version) - 1u);
    staging.crc32_le = HBoxSecurity_Crc32Skipping(
        (const uint8_t *)&staging,
        sizeof(staging),
        HBOX_BOOT_CONTEXT_CRC_OFFSET,
        sizeof(staging.crc32_le));
    if (staging.crc32_le == 0u ||
        !HBoxSecurity_ValidateBootContext(&staging)) {
        goto done;
    }

    memcpy((void *)destination, &staging, sizeof(staging));
    __DSB();
    result = HBoxSecurity_ValidateBootContext(
        (const hbox_boot_security_context_v1_t *)destination) != 0;

done:
    HBoxHardwareRng_Shutdown();
    HBoxCrypto_Zeroize(&identity, sizeof(identity));
    HBoxCrypto_Zeroize(&staging, sizeof(staging));
    HBoxCrypto_Zeroize(digest, sizeof(digest));
    if (!result) {
        BootAttestation_Invalidate();
    }
    return result;
}
