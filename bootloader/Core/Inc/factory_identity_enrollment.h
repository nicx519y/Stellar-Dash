#ifndef HBOX_FACTORY_IDENTITY_ENROLLMENT_H
#define HBOX_FACTORY_IDENTITY_ENROLLMENT_H

#include <stdint.h>

#include "device_security_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HBOX_FACTORY_CHALLENGE_BYTES 32u
#define HBOX_FACTORY_POP_DOMAIN "HBOX-FACTORY-POP-V1"

typedef enum
{
    HBOX_FACTORY_ENROLLMENT_OK = 0,
    HBOX_FACTORY_ENROLLMENT_DENIED,
    HBOX_FACTORY_ENROLLMENT_INVALID_ARGUMENT,
    HBOX_FACTORY_ENROLLMENT_ALREADY_PROVISIONED,
    HBOX_FACTORY_ENROLLMENT_PENDING,
    HBOX_FACTORY_ENROLLMENT_ENTROPY_FAILURE,
    HBOX_FACTORY_ENROLLMENT_CRYPTO_FAILURE,
    HBOX_FACTORY_ENROLLMENT_CERTIFICATE_INVALID,
    HBOX_FACTORY_ENROLLMENT_VERSION_FAILURE,
    HBOX_FACTORY_ENROLLMENT_STORAGE_FAILURE
} hbox_factory_enrollment_status_t;

typedef struct
{
    uint8_t device_public_key[HBOX_SECURITY_P256_PUBLIC_KEY_BYTES];
    uint8_t proof_signature[HBOX_SECURITY_P256_SIGNATURE_BYTES];
} hbox_factory_enrollment_proof_v1_t;

/*
 * Generate Kdev exclusively from the STM32 hardware RNG and return a raw
 * P-256 proof over:
 *
 *   "HBOX-FACTORY-POP-V1\0" || challenge[32]
 *
 * Kdev remains only in this secure factory service until Install or Cancel.
 */
hbox_factory_enrollment_status_t
HBoxFactoryIdentityEnrollment_Begin(
    const uint8_t challenge[HBOX_FACTORY_CHALLENGE_BYTES],
    hbox_factory_enrollment_proof_v1_t *proof);

/*
 * Verify the manufacturer certificate against the generated Kdev public key,
 * deviceId, current hardware version and the compiled manufacturer CA. Then
 * initialize the append-only minimum-version journal and commit the identity.
 */
hbox_factory_enrollment_status_t
HBoxFactoryIdentityEnrollment_Install(
    const hbox_device_certificate_v1_t *certificate,
    uint32_t initial_security_version);

void HBoxFactoryIdentityEnrollment_Cancel(void);

const char *HBoxFactoryIdentityEnrollment_StatusString(
    hbox_factory_enrollment_status_t status);

/*
 * Explicit factory-service capability table.  A factory transport receives
 * this table from main() only in an HBOX_FACTORY_IDENTITY_ENROLLMENT build.
 * Keeping the references here also prevents the enrollment implementation
 * from being discarded by --gc-sections.
 */
#define HBOX_FACTORY_ENROLLMENT_API_VERSION 1u

typedef struct
{
    uint32_t api_version;
    hbox_factory_enrollment_status_t (*begin)(
        const uint8_t challenge[HBOX_FACTORY_CHALLENGE_BYTES],
        hbox_factory_enrollment_proof_v1_t *proof);
    hbox_factory_enrollment_status_t (*install)(
        const hbox_device_certificate_v1_t *certificate,
        uint32_t initial_security_version);
    void (*cancel)(void);
    const char *(*status_string)(
        hbox_factory_enrollment_status_t status);
} hbox_factory_enrollment_api_v1_t;

const hbox_factory_enrollment_api_v1_t *
HBoxFactoryIdentityEnrollment_GetApi(void);

#ifdef __cplusplus
}
#endif

#endif /* HBOX_FACTORY_IDENTITY_ENROLLMENT_H */
