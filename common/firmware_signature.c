#include "firmware_signature.h"

#include <stddef.h>
#include <string.h>

#include "firmware_release_public_key.h"
#include "sha256_simple.h"

#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"

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

bool firmware_metadata_calculate_hash(const FirmwareMetadata* metadata,
                                      uint8_t hash[32])
{
    FirmwareMetadata canonical;

    if (metadata == NULL || hash == NULL) {
        return false;
    }

    memcpy(&canonical, metadata, sizeof(canonical));
    canonical.metadata_crc32 = 0u;
    memset(canonical.firmware_hash, 0, sizeof(canonical.firmware_hash));
    memset(canonical.signature, 0, sizeof(canonical.signature));
    return sha256_calculate_raw((const uint8_t*)&canonical,
                                sizeof(canonical),
                                hash) == 1;
}

bool firmware_release_key_is_provisioned(void)
{
    uint8_t nonzero = 0u;
    size_t i;

    if (HBOX_FIRMWARE_RELEASE_PUBLIC_KEY_PROVISIONED != 1u ||
        hbox_firmware_release_public_key[0] != 0x04u) {
        return false;
    }
    for (i = 1u; i < sizeof(hbox_firmware_release_public_key); ++i) {
        nonzero |= hbox_firmware_release_public_key[i];
    }
    return nonzero != 0u;
}

FirmwareValidationResult firmware_metadata_verify_signature(
    const FirmwareMetadata* metadata)
{
    uint8_t calculated_hash[32];
    mbedtls_ecp_group group;
    mbedtls_ecp_point public_key;
    mbedtls_mpi r;
    mbedtls_mpi s;
    int result = -1;

    if (metadata == NULL ||
        metadata->signature_algorithm !=
            FIRMWARE_SIGNATURE_ECDSA_P256_SHA256 ||
        !firmware_release_key_is_provisioned()) {
        return FIRMWARE_INVALID_SIGNATURE;
    }
    if (!firmware_metadata_calculate_hash(metadata, calculated_hash) ||
        !constant_time_equal(calculated_hash,
                             metadata->firmware_hash,
                             sizeof(calculated_hash))) {
        return FIRMWARE_INVALID_HASH;
    }

    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&public_key);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    if (mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_ecp_point_read_binary(
            &group,
            &public_key,
            hbox_firmware_release_public_key,
            sizeof(hbox_firmware_release_public_key)) == 0 &&
        mbedtls_mpi_read_binary(&r, metadata->signature, 32u) == 0 &&
        mbedtls_mpi_read_binary(&s, metadata->signature + 32u, 32u) == 0) {
        result = mbedtls_ecdsa_verify(
            &group, calculated_hash, sizeof(calculated_hash),
            &public_key, &r, &s);
    }

    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&public_key);
    mbedtls_ecp_group_free(&group);
    memset(calculated_hash, 0, sizeof(calculated_hash));

    return result == 0 ? FIRMWARE_VALID : FIRMWARE_INVALID_SIGNATURE;
}
