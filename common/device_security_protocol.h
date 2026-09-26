#ifndef HBOX_DEVICE_SECURITY_PROTOCOL_H
#define HBOX_DEVICE_SECURITY_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * HBox V2 device identity and WebConfig authorization protocol.
 *
 * All multi-byte integer fields are little-endian on the wire.  Signed
 * structures are packed and the signature covers every byte before the
 * signature field.  JSON transports carry these structures as base64url;
 * JSON itself is never signed.
 */
#define HBOX_SECURITY_PROTOCOL_VERSION          1u
#define HBOX_SECURITY_P256_PUBLIC_KEY_BYTES     65u
#define HBOX_SECURITY_P256_SIGNATURE_BYTES      64u
#define HBOX_SECURITY_DEVICE_ID_BYTES           16u
#define HBOX_SECURITY_ID_BYTES                  16u
#define HBOX_SECURITY_NONCE_BYTES               32u
#define HBOX_SECURITY_HASH_BYTES                32u
#define HBOX_SECURITY_PRODUCT_ID_BYTES          4u
#define HBOX_SECURITY_CHALLENGE_SECONDS         60u

/*
 * Manufacturer-assigned product family identifier.  The little-endian wire
 * bytes for this value spell "HBOX".  This is deliberately independent from
 * the STM32 DEV_ID/REV_ID: it identifies the product whose PCB is certified.
 */
#define HBOX_PRODUCT_ID                         0x584F4248u

#define HBOX_DEVICE_CERTIFICATE_MAGIC           0x31434448u /* "HDC1" */
#define HBOX_BOOT_ATTESTATION_MAGIC             0x31414248u /* "HBA1" */
#define HBOX_ATTESTATION_TRANSCRIPT_MAGIC        0x31544148u /* "HAT1" */
#define HBOX_SESSION_PERMIT_MAGIC               0x31505348u /* "HSP1" */

typedef enum
{
    HBOX_AUTH_LEVEL_LEGACY_WEAK          = 0u,
    HBOX_AUTH_LEVEL_MCU_PROTECTED        = 1u,
    HBOX_AUTH_LEVEL_RETROFIT_PROTECTED   = 2u,
    HBOX_AUTH_LEVEL_SECURE_ELEMENT       = 3u
} hbox_auth_level_t;

typedef enum
{
    HBOX_SCOPE_CONFIG_READ      = (1u << 0),
    HBOX_SCOPE_CONFIG_WRITE     = (1u << 1),
    HBOX_SCOPE_MONITOR_READ     = (1u << 2),
    HBOX_SCOPE_DEVICE_CONTROL   = (1u << 3),
    HBOX_SCOPE_ASSET_WRITE      = (1u << 4),
    HBOX_SCOPE_FIRMWARE_UPDATE  = (1u << 5),
    HBOX_SCOPE_ALL              = 0x3Fu
} hbox_authorization_scope_t;

#if defined(__GNUC__)
#define HBOX_SECURITY_PACKED __attribute__((packed))
#else
#define HBOX_SECURITY_PACKED
#endif

/*
 * Manufacturer-issued identity.  device_public_key is SEC1 uncompressed
 * P-256 (0x04 || X || Y).  manufacturer_signature is raw r || s.
 */
typedef struct HBOX_SECURITY_PACKED
{
    uint32_t magic_le;
    uint8_t version;
    uint8_t auth_level;
    uint16_t signed_bytes_le;
    uint8_t certificate_serial[HBOX_SECURITY_ID_BYTES];
    uint8_t device_id[HBOX_SECURITY_DEVICE_ID_BYTES];
    /* SemVer-style PCB revision: major << 16 | minor << 8 | patch. */
    uint32_t hardware_version_le;
    uint32_t issued_at_le;
    uint8_t device_public_key[HBOX_SECURITY_P256_PUBLIC_KEY_BYTES];
    uint8_t production_batch[16];
    uint32_t product_id_le;
    uint8_t reserved[11];
    uint8_t manufacturer_signature[HBOX_SECURITY_P256_SIGNATURE_BYTES];
} hbox_device_certificate_v1_t;

/*
 * The reset-stage secure service certifies an ephemeral boot signing key and
 * the exact application measurement.  The application never receives Kdev.
 */
typedef struct HBOX_SECURITY_PACKED
{
    uint32_t magic_le;
    uint8_t version;
    uint8_t reserved0[3];
    uint16_t signed_bytes_le;
    uint16_t reserved1;
    uint8_t device_id[HBOX_SECURITY_DEVICE_ID_BYTES];
    uint8_t boot_nonce[HBOX_SECURITY_NONCE_BYTES];
    uint8_t boot_public_key[HBOX_SECURITY_P256_PUBLIC_KEY_BYTES];
    uint8_t firmware_hash[HBOX_SECURITY_HASH_BYTES];
    uint32_t security_version_le;
    uint32_t bootloader_version_le;
    uint8_t device_signature[HBOX_SECURITY_P256_SIGNATURE_BYTES];
} hbox_boot_attestation_v1_t;

/*
 * Canonical online proof signed by the boot key.  The server challenge,
 * browser key, requested privileges, negotiated device key and measured
 * firmware are all bound into one proof.
 */
typedef struct HBOX_SECURITY_PACKED
{
    uint32_t magic_le;
    uint8_t version;
    uint8_t protocol_version;
    uint16_t signed_bytes_le;
    uint8_t challenge_id[HBOX_SECURITY_ID_BYTES];
    uint8_t server_nonce[HBOX_SECURITY_NONCE_BYTES];
    uint8_t webhid_session_id[HBOX_SECURITY_ID_BYTES];
    uint32_t requested_scopes_le;
    uint8_t device_id[HBOX_SECURITY_DEVICE_ID_BYTES];
    uint8_t boot_nonce[HBOX_SECURITY_NONCE_BYTES];
    uint8_t browser_ephemeral_public_key[HBOX_SECURITY_P256_PUBLIC_KEY_BYTES];
    uint8_t device_ephemeral_public_key[HBOX_SECURITY_P256_PUBLIC_KEY_BYTES];
    uint8_t firmware_hash[HBOX_SECURITY_HASH_BYTES];
    uint32_t security_version_le;
    uint8_t boot_signature[HBOX_SECURITY_P256_SIGNATURE_BYTES];
} hbox_attestation_transcript_v1_t;

/*
 * Server authorization consumed by the device.  WebHID sessions are bound to
 * the physical/browser connection instead of a wall-clock lifetime, so
 * max_duration_ms, issued_at and expires_at are reserved and must be zero.
 */
typedef struct HBOX_SECURITY_PACKED
{
    uint32_t magic_le;
    uint8_t version;
    uint8_t signing_key_slot;
    uint16_t signed_bytes_le;
    uint8_t permit_id[HBOX_SECURITY_ID_BYTES];
    uint8_t session_id[HBOX_SECURITY_ID_BYTES];
    uint8_t device_id[HBOX_SECURITY_DEVICE_ID_BYTES];
    uint8_t boot_nonce[HBOX_SECURITY_NONCE_BYTES];
    uint8_t browser_public_key_hash[HBOX_SECURITY_HASH_BYTES];
    uint8_t device_public_key_hash[HBOX_SECURITY_HASH_BYTES];
    uint32_t granted_scopes_le;
    uint32_t max_duration_ms_le;
    uint32_t issued_at_le;
    uint32_t expires_at_le;
    uint32_t policy_version_le;
    uint8_t server_signature[HBOX_SECURITY_P256_SIGNATURE_BYTES];
} hbox_device_session_permit_v1_t;

#define HBOX_DEVICE_CERTIFICATE_SIGNED_BYTES \
    ((uint16_t)offsetof(hbox_device_certificate_v1_t, manufacturer_signature))
#define HBOX_BOOT_ATTESTATION_SIGNED_BYTES \
    ((uint16_t)offsetof(hbox_boot_attestation_v1_t, device_signature))
#define HBOX_ATTESTATION_TRANSCRIPT_SIGNED_BYTES \
    ((uint16_t)offsetof(hbox_attestation_transcript_v1_t, boot_signature))
#define HBOX_SESSION_PERMIT_SIGNED_BYTES \
    ((uint16_t)offsetof(hbox_device_session_permit_v1_t, server_signature))

#define HBOX_SECURITY_STATIC_ASSERT_GLUE_(a, b) a##b
#define HBOX_SECURITY_STATIC_ASSERT_GLUE(a, b) \
    HBOX_SECURITY_STATIC_ASSERT_GLUE_(a, b)
#define HBOX_SECURITY_STATIC_ASSERT(expr) \
    typedef char HBOX_SECURITY_STATIC_ASSERT_GLUE( \
        hbox_security_static_assert_, __LINE__)[(expr) ? 1 : -1]

HBOX_SECURITY_STATIC_ASSERT(HBOX_DEVICE_CERTIFICATE_SIGNED_BYTES < 256u);
HBOX_SECURITY_STATIC_ASSERT(HBOX_BOOT_ATTESTATION_SIGNED_BYTES < 256u);
HBOX_SECURITY_STATIC_ASSERT(HBOX_SESSION_PERMIT_SIGNED_BYTES < 256u);
HBOX_SECURITY_STATIC_ASSERT(sizeof(((hbox_device_certificate_v1_t *)0)
                                       ->device_public_key) ==
                            HBOX_SECURITY_P256_PUBLIC_KEY_BYTES);
HBOX_SECURITY_STATIC_ASSERT(
    offsetof(hbox_device_certificate_v1_t, product_id_le) == 129u);
HBOX_SECURITY_STATIC_ASSERT(sizeof(((hbox_device_session_permit_v1_t *)0)
                                       ->server_signature) ==
                            HBOX_SECURITY_P256_SIGNATURE_BYTES);

#ifdef __cplusplus
}
#endif

#endif /* HBOX_DEVICE_SECURITY_PROTOCOL_H */
