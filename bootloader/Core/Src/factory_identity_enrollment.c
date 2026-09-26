#include "factory_identity_enrollment.h"

#include <stddef.h>
#include <string.h>

#include "device_identity_internal_flash_provider.h"
#include "device_identity_store.h"
#include "device_security_boot_context.h"
#include "device_security_crypto.h"
#include "firmware_metadata.h"
#include "hardware_rng.h"
#include "manufacturer_ca_public_key.h"
#include "secure_access_handoff.h"
#include "security_version_internal_flash_provider.h"

#ifndef HBOX_FACTORY_IDENTITY_ENROLLMENT
#define HBOX_FACTORY_IDENTITY_ENROLLMENT 0
#endif

#if !HBOX_FACTORY_IDENTITY_ENROLLMENT
#error "factory identity enrollment must be selected by its explicit build switch"
#endif

typedef struct
{
    uint8_t private_key[HBOX_CRYPTO_P256_PRIVATE_BYTES];
    uint8_t public_key[HBOX_CRYPTO_P256_PUBLIC_BYTES];
    uint8_t pending;
} hbox_pending_factory_identity_t;

static hbox_pending_factory_identity_t g_pending_identity;

static int any_nonzero(const uint8_t *value, size_t length)
{
    uint8_t combined = 0u;
    size_t index;

    for (index = 0u; index < length; ++index) {
        combined |= value[index];
    }
    return combined != 0u;
}

static int all_zero(const uint8_t *value, size_t length)
{
    return !any_nonzero(value, length);
}

static int factory_runtime_authorized(void)
{
    return HBoxSecureAccess_ValidateLifecycle() ==
               HBOX_SECURE_ACCESS_OK &&
           HBoxIdentityFactoryGate_IsAuthorized() != 0;
}

static void clear_pending_identity(void)
{
    HBoxCrypto_Zeroize(
        &g_pending_identity, sizeof(g_pending_identity));
}

static int identity_is_unprovisioned(void)
{
    hbox_device_identity_backend_t backend;
    hbox_device_identity_record_v1_t record;
    hbox_device_identity_status_t status;

    memset(&backend, 0, sizeof(backend));
    memset(&record, 0, sizeof(record));
    if (!HBoxIdentityStoreProvider_Open(&backend)) {
        return 0;
    }
    status = HBoxIdentityStore_LoadFromBackend(
        &backend, &record);
    HBoxCrypto_Zeroize(&record, sizeof(record));
    memset(&backend, 0, sizeof(backend));
    return status == HBOX_DEVICE_IDENTITY_UNPROVISIONED;
}

static int production_batch_is_valid(
    const uint8_t production_batch[16])
{
    size_t content_length = 0u;
    size_t index;

    while (content_length < 16u &&
           production_batch[content_length] != 0u) {
        if (production_batch[content_length] < 0x20u ||
            production_batch[content_length] > 0x7Eu) {
            return 0;
        }
        ++content_length;
    }
    if (content_length == 0u) {
        return 0;
    }
    for (index = content_length; index < 16u; ++index) {
        if (production_batch[index] != 0u) {
            return 0;
        }
    }
    return 1;
}

static int certificate_is_valid(
    const hbox_device_certificate_v1_t *certificate)
{
    uint8_t digest[HBOX_CRYPTO_SHA256_BYTES];
    uint8_t derived_device_id[HBOX_CRYPTO_SHA256_BYTES];
    int valid = 0;

    memset(digest, 0, sizeof(digest));
    memset(derived_device_id, 0, sizeof(derived_device_id));
    if (certificate == NULL ||
        HBOX_MANUFACTURER_CA_KEY_PROVISIONED == 0u ||
        all_zero(HBOX_MANUFACTURER_CA_PUBLIC_KEY,
                 sizeof(HBOX_MANUFACTURER_CA_PUBLIC_KEY)) ||
        certificate->magic_le != HBOX_DEVICE_CERTIFICATE_MAGIC ||
        certificate->version != HBOX_SECURITY_PROTOCOL_VERSION ||
        certificate->auth_level != HBOX_AUTH_LEVEL_MCU_PROTECTED ||
        certificate->signed_bytes_le !=
            HBOX_DEVICE_CERTIFICATE_SIGNED_BYTES ||
        !any_nonzero(certificate->certificate_serial,
                     sizeof(certificate->certificate_serial)) ||
        certificate->hardware_version_le != HARDWARE_VERSION ||
        certificate->product_id_le != HBOX_PRODUCT_ID ||
        certificate->issued_at_le == 0u ||
        certificate->device_public_key[0] != 0x04u ||
        memcmp(certificate->device_public_key,
               g_pending_identity.public_key,
               sizeof(g_pending_identity.public_key)) != 0 ||
        !production_batch_is_valid(certificate->production_batch) ||
        !all_zero(certificate->reserved,
                  sizeof(certificate->reserved)) ||
        HBoxCrypto_Sha256(
            certificate->device_public_key,
            sizeof(certificate->device_public_key),
            derived_device_id) != 0 ||
        memcmp(derived_device_id,
               certificate->device_id,
               HBOX_SECURITY_DEVICE_ID_BYTES) != 0 ||
        HBoxCrypto_Sha256(
            (const uint8_t *)certificate,
            HBOX_DEVICE_CERTIFICATE_SIGNED_BYTES,
            digest) != 0) {
        goto done;
    }
    valid = HBoxCrypto_P256VerifyDigest(
                HBOX_MANUFACTURER_CA_PUBLIC_KEY,
                digest,
                certificate->manufacturer_signature) == 0;

done:
    HBoxCrypto_Zeroize(digest, sizeof(digest));
    HBoxCrypto_Zeroize(
        derived_device_id, sizeof(derived_device_id));
    return valid;
}

hbox_factory_enrollment_status_t
HBoxFactoryIdentityEnrollment_Begin(
    const uint8_t challenge[HBOX_FACTORY_CHALLENGE_BYTES],
    hbox_factory_enrollment_proof_v1_t *proof)
{
    static const uint8_t proof_domain[] =
        HBOX_FACTORY_POP_DOMAIN;
    uint8_t transcript[
        sizeof(proof_domain) + HBOX_FACTORY_CHALLENGE_BYTES];
    uint8_t digest[HBOX_CRYPTO_SHA256_BYTES];
    uint8_t signature[HBOX_CRYPTO_P256_SIGNATURE_BYTES];
    hbox_factory_enrollment_status_t result =
        HBOX_FACTORY_ENROLLMENT_CRYPTO_FAILURE;
    int rng_initialized = 0;

    memset(transcript, 0, sizeof(transcript));
    memset(digest, 0, sizeof(digest));
    memset(signature, 0, sizeof(signature));
    if (proof != NULL) {
        memset(proof, 0, sizeof(*proof));
    }
    if (challenge == NULL || proof == NULL ||
        !any_nonzero(challenge, HBOX_FACTORY_CHALLENGE_BYTES)) {
        return HBOX_FACTORY_ENROLLMENT_INVALID_ARGUMENT;
    }
    if (!factory_runtime_authorized()) {
        return HBOX_FACTORY_ENROLLMENT_DENIED;
    }
    if (g_pending_identity.pending != 0u) {
        return HBOX_FACTORY_ENROLLMENT_PENDING;
    }
    if (!identity_is_unprovisioned()) {
        return HBOX_FACTORY_ENROLLMENT_ALREADY_PROVISIONED;
    }
    if (!HBoxHardwareRng_Init()) {
        return HBOX_FACTORY_ENROLLMENT_ENTROPY_FAILURE;
    }
    rng_initialized = 1;

    if (HBoxCrypto_P256Generate(
            g_pending_identity.private_key,
            g_pending_identity.public_key,
            HBoxHardwareRng_Fill,
            NULL) != 0) {
        result = HBOX_FACTORY_ENROLLMENT_ENTROPY_FAILURE;
        goto done;
    }

    memcpy(transcript, proof_domain, sizeof(proof_domain));
    memcpy(transcript + sizeof(proof_domain),
           challenge,
           HBOX_FACTORY_CHALLENGE_BYTES);
    if (HBoxCrypto_Sha256(
            transcript, sizeof(transcript), digest) != 0 ||
        HBoxCrypto_P256SignDigest(
            g_pending_identity.private_key,
            digest,
            signature,
            HBoxHardwareRng_Fill,
            NULL) != 0) {
        result = HBOX_FACTORY_ENROLLMENT_CRYPTO_FAILURE;
        goto done;
    }

    memcpy(proof->device_public_key,
           g_pending_identity.public_key,
           sizeof(proof->device_public_key));
    memcpy(proof->proof_signature,
           signature,
           sizeof(proof->proof_signature));
    g_pending_identity.pending = 1u;
    result = HBOX_FACTORY_ENROLLMENT_OK;

done:
    if (rng_initialized) {
        HBoxHardwareRng_Shutdown();
    }
    HBoxCrypto_Zeroize(transcript, sizeof(transcript));
    HBoxCrypto_Zeroize(digest, sizeof(digest));
    HBoxCrypto_Zeroize(signature, sizeof(signature));
    if (result != HBOX_FACTORY_ENROLLMENT_OK) {
        clear_pending_identity();
        memset(proof, 0, sizeof(*proof));
    }
    return result;
}

hbox_factory_enrollment_status_t
HBoxFactoryIdentityEnrollment_Install(
    const hbox_device_certificate_v1_t *certificate,
    uint32_t initial_security_version)
{
    hbox_device_identity_backend_t backend;
    hbox_device_identity_record_v1_t record;
    hbox_device_identity_status_t identity_status;
    hbox_security_version_status_t version_status;
    hbox_factory_enrollment_status_t result =
        HBOX_FACTORY_ENROLLMENT_STORAGE_FAILURE;

    memset(&backend, 0, sizeof(backend));
    memset(&record, 0, sizeof(record));
    if (certificate == NULL || initial_security_version == 0u ||
        initial_security_version < FIRMWARE_SECURITY_VERSION) {
        return HBOX_FACTORY_ENROLLMENT_INVALID_ARGUMENT;
    }
    if (g_pending_identity.pending == 0u) {
        return HBOX_FACTORY_ENROLLMENT_INVALID_ARGUMENT;
    }
    if (!factory_runtime_authorized()) {
        result = HBOX_FACTORY_ENROLLMENT_DENIED;
        goto done;
    }
    if (!certificate_is_valid(certificate)) {
        result = HBOX_FACTORY_ENROLLMENT_CERTIFICATE_INVALID;
        goto done;
    }
    if (!HBoxIdentityStoreProvider_Open(&backend)) {
        goto done;
    }

    /*
     * Provision the anti-rollback floor before making identity authoritative.
     * ProvisionFactory is idempotent for the same value, so a power loss here
     * can be resumed with a freshly generated identity without an erase.
     */
    version_status =
        HBoxSecurityVersionInternalFlash_ProvisionFactory(
            initial_security_version);
    if (version_status != HBOX_SECURITY_VERSION_OK) {
        result = HBOX_FACTORY_ENROLLMENT_VERSION_FAILURE;
        goto done;
    }

    record.magic_le = HBOX_DEVICE_IDENTITY_MAGIC;
    record.version = HBOX_DEVICE_IDENTITY_VERSION;
    record.locked = HBOX_DEVICE_IDENTITY_LOCKED;
    record.total_bytes_le = sizeof(record);
    memcpy(record.device_private_key,
           g_pending_identity.private_key,
           sizeof(record.device_private_key));
    memcpy(&record.device_certificate,
           certificate,
           sizeof(record.device_certificate));
    record.crc32_le = HBoxSecurity_Crc32Skipping(
        (const uint8_t *)&record,
        sizeof(record),
        offsetof(hbox_device_identity_record_v1_t, crc32_le),
        sizeof(record.crc32_le));
    if (record.crc32_le == 0u) {
        result = HBOX_FACTORY_ENROLLMENT_CRYPTO_FAILURE;
        goto done;
    }

    identity_status = HBoxIdentityStore_ProvisionFactory(
        &backend, &record);
    if (identity_status == HBOX_DEVICE_IDENTITY_OK) {
        result = HBOX_FACTORY_ENROLLMENT_OK;
    } else if (identity_status ==
               HBOX_DEVICE_IDENTITY_ALREADY_PROVISIONED) {
        result = HBOX_FACTORY_ENROLLMENT_ALREADY_PROVISIONED;
    }

done:
    HBoxCrypto_Zeroize(&record, sizeof(record));
    memset(&backend, 0, sizeof(backend));
    /*
     * Any install attempt is terminal. A rejected certificate cannot retain a
     * secret-bearing pending enrollment for a later unaudited request.
     */
    clear_pending_identity();
    return result;
}

void HBoxFactoryIdentityEnrollment_Cancel(void)
{
    clear_pending_identity();
}

const char *HBoxFactoryIdentityEnrollment_StatusString(
    hbox_factory_enrollment_status_t status)
{
    switch (status) {
    case HBOX_FACTORY_ENROLLMENT_OK:
        return "ok";
    case HBOX_FACTORY_ENROLLMENT_DENIED:
        return "factory-denied";
    case HBOX_FACTORY_ENROLLMENT_INVALID_ARGUMENT:
        return "invalid-argument";
    case HBOX_FACTORY_ENROLLMENT_ALREADY_PROVISIONED:
        return "already-provisioned";
    case HBOX_FACTORY_ENROLLMENT_PENDING:
        return "enrollment-pending";
    case HBOX_FACTORY_ENROLLMENT_ENTROPY_FAILURE:
        return "entropy-failure";
    case HBOX_FACTORY_ENROLLMENT_CRYPTO_FAILURE:
        return "crypto-failure";
    case HBOX_FACTORY_ENROLLMENT_CERTIFICATE_INVALID:
        return "certificate-invalid";
    case HBOX_FACTORY_ENROLLMENT_VERSION_FAILURE:
        return "security-version-failure";
    case HBOX_FACTORY_ENROLLMENT_STORAGE_FAILURE:
        return "storage-failure";
    default:
        return "unknown";
    }
}

const hbox_factory_enrollment_api_v1_t *
HBoxFactoryIdentityEnrollment_GetApi(void)
{
    static const hbox_factory_enrollment_api_v1_t api = {
        HBOX_FACTORY_ENROLLMENT_API_VERSION,
        HBoxFactoryIdentityEnrollment_Begin,
        HBoxFactoryIdentityEnrollment_Install,
        HBoxFactoryIdentityEnrollment_Cancel,
        HBoxFactoryIdentityEnrollment_StatusString,
    };

    return &api;
}
