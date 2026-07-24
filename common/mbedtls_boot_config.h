#ifndef HBOX_MBEDTLS_BOOT_CONFIG_H
#define HBOX_MBEDTLS_BOOT_CONFIG_H

/*
 * Minimal Mbed TLS feature set used by the immutable boot verifier.
 * Keep this deliberately small: the bootloader only verifies raw P-256
 * signatures over SHA-256 digests.
 */
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_GCM_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_HKDF_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_ECP_NO_INTERNAL_RNG
#define MBEDTLS_MPI_MAX_SIZE 32
#define MBEDTLS_ECP_MAX_BITS 256
#define MBEDTLS_ECP_WINDOW_SIZE 2
#define MBEDTLS_ECP_FIXED_POINT_OPTIM 0
#define MBEDTLS_AES_FEWER_TABLES

#endif /* HBOX_MBEDTLS_BOOT_CONFIG_H */
