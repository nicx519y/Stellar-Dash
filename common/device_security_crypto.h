#ifndef HBOX_DEVICE_SECURITY_CRYPTO_H
#define HBOX_DEVICE_SECURITY_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HBOX_CRYPTO_P256_PRIVATE_BYTES 32u
#define HBOX_CRYPTO_P256_PUBLIC_BYTES  65u
#define HBOX_CRYPTO_P256_SIGNATURE_BYTES 64u
#define HBOX_CRYPTO_SHA256_BYTES       32u
#define HBOX_CRYPTO_AES256_BYTES       32u
#define HBOX_CRYPTO_GCM_TAG_BYTES      12u

typedef int (*hbox_crypto_rng_fn)(void *, unsigned char *, size_t);

int HBoxCrypto_Sha256(const uint8_t *data, size_t length, uint8_t out[32]);
int HBoxCrypto_P256PublicFromPrivate(const uint8_t private_key[32],
                                    uint8_t public_key[65]);
int HBoxCrypto_P256Generate(uint8_t private_key[32],
                            uint8_t public_key[65],
                            hbox_crypto_rng_fn rng,
                            void *rng_context);
int HBoxCrypto_P256SignDigest(const uint8_t private_key[32],
                              const uint8_t digest[32],
                              uint8_t signature[64],
                              hbox_crypto_rng_fn rng,
                              void *rng_context);
int HBoxCrypto_P256ValidatePublicKey(const uint8_t public_key[65]);
int HBoxCrypto_P256VerifyDigest(const uint8_t public_key[65],
                                const uint8_t digest[32],
                                const uint8_t signature[64]);
int HBoxCrypto_P256Ecdh(const uint8_t private_key[32],
                        const uint8_t peer_public_key[65],
                        uint8_t shared_secret[32],
                        hbox_crypto_rng_fn rng,
                        void *rng_context);
int HBoxCrypto_HkdfSha256(const uint8_t *salt,
                          size_t salt_length,
                          const uint8_t *input,
                          size_t input_length,
                          const uint8_t *info,
                          size_t info_length,
                          uint8_t *output,
                          size_t output_length);
int HBoxCrypto_Aes256GcmEncrypt(const uint8_t key[32],
                                const uint8_t nonce[12],
                                const uint8_t *aad,
                                size_t aad_length,
                                const uint8_t *plaintext,
                                size_t plaintext_length,
                                uint8_t *ciphertext,
                                uint8_t tag[12]);
int HBoxCrypto_Aes256GcmDecrypt(const uint8_t key[32],
                                const uint8_t nonce[12],
                                const uint8_t *aad,
                                size_t aad_length,
                                const uint8_t *ciphertext,
                                size_t ciphertext_length,
                                const uint8_t tag[12],
                                uint8_t *plaintext);
void HBoxCrypto_Zeroize(void *value, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* HBOX_DEVICE_SECURITY_CRYPTO_H */
