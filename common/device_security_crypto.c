#include "device_security_crypto.h"

#include <string.h>

#include "mbedtls/ecdh.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/gcm.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/sha256.h"

static int load_group(mbedtls_ecp_group *group)
{
    mbedtls_ecp_group_init(group);
    return mbedtls_ecp_group_load(group,
                                  MBEDTLS_ECP_DP_SECP256R1);
}

static int load_public(const mbedtls_ecp_group *group,
                       mbedtls_ecp_point *point,
                       const uint8_t public_key[65])
{
    int result;
    mbedtls_ecp_point_init(point);
    result = mbedtls_ecp_point_read_binary(group,
                                           point,
                                           public_key,
                                           65u);
    if (result == 0) {
        result = mbedtls_ecp_check_pubkey(group, point);
    }
    return result;
}

static int write_public(const mbedtls_ecp_group *group,
                        const mbedtls_ecp_point *point,
                        uint8_t public_key[65])
{
    size_t written = 0u;
    int result = mbedtls_ecp_point_write_binary(
        group,
        point,
        MBEDTLS_ECP_PF_UNCOMPRESSED,
        &written,
        public_key,
        65u);
    return (result == 0 && written == 65u) ? 0 : -1;
}

int HBoxCrypto_Sha256(const uint8_t *data, size_t length, uint8_t out[32])
{
    if ((data == NULL && length != 0u) || out == NULL) {
        return -1;
    }
    return mbedtls_sha256_ret(data, length, out, 0);
}

int HBoxCrypto_P256PublicFromPrivate(const uint8_t private_key[32],
                                    uint8_t public_key[65])
{
    int result;
    mbedtls_ecp_group group;
    mbedtls_mpi private_value;
    mbedtls_ecp_point public_value;

    if (private_key == NULL || public_key == NULL) {
        return -1;
    }
    mbedtls_mpi_init(&private_value);
    mbedtls_ecp_point_init(&public_value);
    if ((result = load_group(&group)) == 0 &&
        (result = mbedtls_mpi_read_binary(
             &private_value, private_key, 32u)) == 0 &&
        (result = mbedtls_ecp_check_privkey(
             &group, &private_value)) == 0 &&
        (result = mbedtls_ecp_mul(
             &group,
             &public_value,
             &private_value,
             &group.G,
             NULL,
             NULL)) == 0) {
        result = write_public(&group, &public_value, public_key);
    }
    mbedtls_ecp_point_free(&public_value);
    mbedtls_mpi_free(&private_value);
    mbedtls_ecp_group_free(&group);
    return result;
}

int HBoxCrypto_P256Generate(uint8_t private_key[32],
                            uint8_t public_key[65],
                            hbox_crypto_rng_fn rng,
                            void *rng_context)
{
    int result;
    mbedtls_ecp_group group;
    mbedtls_mpi private_value;
    mbedtls_ecp_point public_value;

    if (private_key == NULL || public_key == NULL || rng == NULL) {
        return -1;
    }
    mbedtls_mpi_init(&private_value);
    mbedtls_ecp_point_init(&public_value);
    if ((result = load_group(&group)) == 0 &&
        (result = mbedtls_ecp_gen_keypair(
             &group,
             &private_value,
             &public_value,
             rng,
             rng_context)) == 0 &&
        (result = mbedtls_mpi_write_binary(
             &private_value, private_key, 32u)) == 0) {
        result = write_public(&group, &public_value, public_key);
    }
    if (result != 0) {
        memset(private_key, 0, 32u);
        memset(public_key, 0, 65u);
    }
    mbedtls_ecp_point_free(&public_value);
    mbedtls_mpi_free(&private_value);
    mbedtls_ecp_group_free(&group);
    return result;
}

int HBoxCrypto_P256SignDigest(const uint8_t private_key[32],
                              const uint8_t digest[32],
                              uint8_t signature[64],
                              hbox_crypto_rng_fn rng,
                              void *rng_context)
{
    int result;
    mbedtls_ecp_group group;
    mbedtls_mpi private_value;
    mbedtls_mpi r;
    mbedtls_mpi s;

    if (private_key == NULL || digest == NULL || signature == NULL ||
        rng == NULL) {
        return -1;
    }
    mbedtls_mpi_init(&private_value);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    if ((result = load_group(&group)) == 0 &&
        (result = mbedtls_mpi_read_binary(
             &private_value, private_key, 32u)) == 0 &&
        (result = mbedtls_ecp_check_privkey(
             &group, &private_value)) == 0 &&
        (result = mbedtls_ecdsa_sign(
             &group,
             &r,
             &s,
             &private_value,
             digest,
             32u,
             rng,
             rng_context)) == 0 &&
        (result = mbedtls_mpi_write_binary(
             &r, signature, 32u)) == 0) {
        result = mbedtls_mpi_write_binary(&s, &signature[32], 32u);
    }
    if (result != 0) {
        memset(signature, 0, 64u);
    }
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&private_value);
    mbedtls_ecp_group_free(&group);
    return result;
}

int HBoxCrypto_P256VerifyDigest(const uint8_t public_key[65],
                                const uint8_t digest[32],
                                const uint8_t signature[64])
{
    int result;
    mbedtls_ecp_group group;
    mbedtls_ecp_point public_value;
    mbedtls_mpi r;
    mbedtls_mpi s;

    if (public_key == NULL || digest == NULL || signature == NULL) {
        return -1;
    }
    mbedtls_ecp_point_init(&public_value);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    if ((result = load_group(&group)) == 0 &&
        (result = load_public(&group, &public_value, public_key)) == 0 &&
        (result = mbedtls_mpi_read_binary(&r, signature, 32u)) == 0 &&
        (result = mbedtls_mpi_read_binary(
             &s, &signature[32], 32u)) == 0) {
        result = mbedtls_ecdsa_verify(
            &group, digest, 32u, &public_value, &r, &s);
    }
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&public_value);
    mbedtls_ecp_group_free(&group);
    return result;
}

int HBoxCrypto_P256ValidatePublicKey(const uint8_t public_key[65])
{
    int result;
    mbedtls_ecp_group group;
    mbedtls_ecp_point public_value;

    if (public_key == NULL) {
        return -1;
    }
    mbedtls_ecp_point_init(&public_value);
    if ((result = load_group(&group)) == 0) {
        result = load_public(&group, &public_value, public_key);
    }
    mbedtls_ecp_point_free(&public_value);
    mbedtls_ecp_group_free(&group);
    return result;
}

int HBoxCrypto_P256Ecdh(const uint8_t private_key[32],
                        const uint8_t peer_public_key[65],
                        uint8_t shared_secret[32],
                        hbox_crypto_rng_fn rng,
                        void *rng_context)
{
    int result;
    mbedtls_ecp_group group;
    mbedtls_mpi private_value;
    mbedtls_mpi secret;
    mbedtls_ecp_point peer;

    if (private_key == NULL || peer_public_key == NULL ||
        shared_secret == NULL || rng == NULL) {
        return -1;
    }
    mbedtls_mpi_init(&private_value);
    mbedtls_mpi_init(&secret);
    mbedtls_ecp_point_init(&peer);
    if ((result = load_group(&group)) == 0 &&
        (result = mbedtls_mpi_read_binary(
             &private_value, private_key, 32u)) == 0 &&
        (result = mbedtls_ecp_check_privkey(
             &group, &private_value)) == 0 &&
        (result = load_public(&group, &peer, peer_public_key)) == 0 &&
        (result = mbedtls_ecdh_compute_shared(
             &group,
             &secret,
             &peer,
             &private_value,
             rng,
             rng_context)) == 0) {
        result = mbedtls_mpi_write_binary(&secret, shared_secret, 32u);
    }
    if (result != 0) {
        memset(shared_secret, 0, 32u);
    }
    mbedtls_ecp_point_free(&peer);
    mbedtls_mpi_free(&secret);
    mbedtls_mpi_free(&private_value);
    mbedtls_ecp_group_free(&group);
    return result;
}

int HBoxCrypto_HkdfSha256(const uint8_t *salt,
                          size_t salt_length,
                          const uint8_t *input,
                          size_t input_length,
                          const uint8_t *info,
                          size_t info_length,
                          uint8_t *output,
                          size_t output_length)
{
    const mbedtls_md_info_t *sha256 =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (sha256 == NULL || (salt == NULL && salt_length != 0u) ||
        (input == NULL && input_length != 0u) ||
        (info == NULL && info_length != 0u) ||
        (output == NULL && output_length != 0u)) {
        return -1;
    }
    return mbedtls_hkdf(sha256,
                        salt,
                        salt_length,
                        input,
                        input_length,
                        info,
                        info_length,
                        output,
                        output_length);
}

int HBoxCrypto_Aes256GcmEncrypt(const uint8_t key[32],
                                const uint8_t nonce[12],
                                const uint8_t *aad,
                                size_t aad_length,
                                const uint8_t *plaintext,
                                size_t plaintext_length,
                                uint8_t *ciphertext,
                                uint8_t tag[12])
{
    int result;
    mbedtls_gcm_context context;
    if (key == NULL || nonce == NULL ||
        (aad == NULL && aad_length != 0u) ||
        (plaintext == NULL && plaintext_length != 0u) ||
        (ciphertext == NULL && plaintext_length != 0u) || tag == NULL) {
        return -1;
    }
    mbedtls_gcm_init(&context);
    result = mbedtls_gcm_setkey(
        &context, MBEDTLS_CIPHER_ID_AES, key, 256u);
    if (result == 0) {
        result = mbedtls_gcm_crypt_and_tag(
            &context,
            MBEDTLS_GCM_ENCRYPT,
            plaintext_length,
            nonce,
            12u,
            aad,
            aad_length,
            plaintext,
            ciphertext,
            12u,
            tag);
    }
    mbedtls_gcm_free(&context);
    return result;
}

int HBoxCrypto_Aes256GcmDecrypt(const uint8_t key[32],
                                const uint8_t nonce[12],
                                const uint8_t *aad,
                                size_t aad_length,
                                const uint8_t *ciphertext,
                                size_t ciphertext_length,
                                const uint8_t tag[12],
                                uint8_t *plaintext)
{
    int result;
    mbedtls_gcm_context context;
    if (key == NULL || nonce == NULL ||
        (aad == NULL && aad_length != 0u) ||
        (ciphertext == NULL && ciphertext_length != 0u) ||
        (plaintext == NULL && ciphertext_length != 0u) || tag == NULL) {
        return -1;
    }
    mbedtls_gcm_init(&context);
    result = mbedtls_gcm_setkey(
        &context, MBEDTLS_CIPHER_ID_AES, key, 256u);
    if (result == 0) {
        result = mbedtls_gcm_auth_decrypt(
            &context,
            ciphertext_length,
            nonce,
            12u,
            aad,
            aad_length,
            tag,
            12u,
            ciphertext,
            plaintext);
    }
    mbedtls_gcm_free(&context);
    return result;
}

void HBoxCrypto_Zeroize(void *value, size_t length)
{
    if (value != NULL) {
        mbedtls_platform_zeroize(value, length);
    }
}
