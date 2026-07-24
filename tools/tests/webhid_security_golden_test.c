#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device_security_crypto.h"
#include "device_security_protocol.h"
#include "webhid_protocol.h"

#define ARRAY_SIZE(value) (sizeof(value) / sizeof((value)[0]))

static const char kPermitLabel[] =
    "HBox TEST ONLY WebHID V1 permit signing scalar";
static const char kBrowserLabel[] =
    "HBox TEST ONLY WebHID V1 browser ECDH scalar";
static const char kDeviceLabel[] =
    "HBox TEST ONLY WebHID V1 device ECDH scalar";

static uint8_t hex_nibble(char value)
{
    if (value >= '0' && value <= '9') {
        return (uint8_t)(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return (uint8_t)(value - 'a' + 10);
    }
    if (value >= 'A' && value <= 'F') {
        return (uint8_t)(value - 'A' + 10);
    }
    assert(!"invalid hexadecimal input");
    return 0u;
}

static size_t decode_hex(
    const char *encoded,
    uint8_t *output,
    size_t capacity)
{
    const size_t encoded_length = strlen(encoded);
    size_t index;

    assert((encoded_length & 1u) == 0u);
    assert(encoded_length / 2u <= capacity);
    for (index = 0u; index < encoded_length / 2u; ++index) {
        output[index] = (uint8_t)(
            (hex_nibble(encoded[index * 2u]) << 4u) |
            hex_nibble(encoded[index * 2u + 1u]));
    }
    return encoded_length / 2u;
}

static uint32_t read_u32_le(const uint8_t *value)
{
    return (uint32_t)value[0] |
           ((uint32_t)value[1] << 8u) |
           ((uint32_t)value[2] << 16u) |
           ((uint32_t)value[3] << 24u);
}

static int test_rng(
    void *context,
    unsigned char *output,
    size_t output_length)
{
    uint32_t *state = (uint32_t *)context;
    size_t index;

    for (index = 0u; index < output_length; ++index) {
        *state = (*state * 1664525u) + 1013904223u;
        output[index] = (unsigned char)(*state >> 24u);
    }
    return 0;
}

/*
 * The fixture generator maps SHA-256(label) to
 * 1 + digest mod (P-256-order - 1).  These three public test labels produce
 * digests below the group order, so the operation is a big-endian +1.
 */
static void derive_test_scalar(const char *label, uint8_t scalar[32])
{
    int index;
    static const uint8_t order_minus_one[32] = {
        0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17, 0x9e, 0x84,
        0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x50
    };

    assert(HBoxCrypto_Sha256(
               (const uint8_t *)label,
               strlen(label),
               scalar) == 0);
    assert(memcmp(scalar, order_minus_one, 32u) < 0);
    for (index = 31; index >= 0; --index) {
        scalar[index] = (uint8_t)(scalar[index] + 1u);
        if (scalar[index] != 0u) {
            break;
        }
    }
    assert(index >= 0);
}

static size_t make_info(
    const char *session_id,
    const char *direction,
    int nonce,
    uint8_t *output,
    size_t capacity)
{
    static const char prefix[] = "HBox WebHID v1";
    static const char suffix[] = "nonce";
    size_t length = 0u;

#define APPEND_BYTES(source, count)                                      \
    do {                                                                 \
        assert(length + (count) <= capacity);                            \
        memcpy(&output[length], (source), (count));                      \
        length += (count);                                               \
    } while (0)
#define APPEND_ZERO()                                                     \
    do {                                                                 \
        assert(length < capacity);                                       \
        output[length++] = 0u;                                           \
    } while (0)

    APPEND_BYTES(prefix, sizeof(prefix) - 1u);
    APPEND_ZERO();
    APPEND_BYTES(session_id, strlen(session_id));
    APPEND_ZERO();
    APPEND_BYTES(direction, strlen(direction));
    if (nonce) {
        APPEND_ZERO();
        APPEND_BYTES(suffix, sizeof(suffix) - 1u);
    }

#undef APPEND_ZERO
#undef APPEND_BYTES
    return length;
}

static void make_nonce(
    const uint8_t prefix[8],
    uint32_t sequence,
    uint8_t nonce[12])
{
    memcpy(nonce, prefix, 8u);
    nonce[8] = (uint8_t)(sequence >> 24u);
    nonce[9] = (uint8_t)(sequence >> 16u);
    nonce[10] = (uint8_t)(sequence >> 8u);
    nonce[11] = (uint8_t)sequence;
}

static void verify_report(
    const uint8_t report[64],
    const uint8_t *plaintext,
    size_t plaintext_length,
    const uint8_t key[32],
    const uint8_t nonce_prefix[8])
{
    uint8_t nonce[12] = {0};
    uint8_t ciphertext[44] = {0};
    uint8_t tag[12] = {0};
    uint8_t decrypted[44] = {0};
    uint8_t damaged[64] = {0};
    size_t index;

    assert(report[0] == WEBHID_PROTOCOL_VERSION);
    assert((report[2] & WEBHID_REPORT_FLAG_ENCRYPTED) != 0u);
    assert(report[3] == plaintext_length);
    assert(plaintext_length <= WEBHID_REPORT_PAYLOAD_BYTES);
    for (index = 8u + plaintext_length; index < 52u; ++index) {
        assert(report[index] == 0u);
    }

    make_nonce(
        nonce_prefix,
        read_u32_le(&report[4]),
        nonce);
    assert(HBoxCrypto_Aes256GcmEncrypt(
               key,
               nonce,
               report,
               WEBHID_REPORT_HEADER_BYTES,
               plaintext,
               plaintext_length,
               ciphertext,
               tag) == 0);
    assert(memcmp(ciphertext, &report[8], plaintext_length) == 0);
    assert(memcmp(tag, &report[52], sizeof(tag)) == 0);
    assert(HBoxCrypto_Aes256GcmDecrypt(
               key,
               nonce,
               report,
               WEBHID_REPORT_HEADER_BYTES,
               &report[8],
               plaintext_length,
               &report[52],
               decrypted) == 0);
    assert(memcmp(decrypted, plaintext, plaintext_length) == 0);

    memcpy(damaged, report, sizeof(damaged));
    damaged[8] ^= 1u;
    assert(HBoxCrypto_Aes256GcmDecrypt(
               key,
               nonce,
               damaged,
               WEBHID_REPORT_HEADER_BYTES,
               &damaged[8],
               plaintext_length,
               &damaged[52],
               decrypted) != 0);

    memcpy(damaged, report, sizeof(damaged));
    damaged[63] ^= 1u;
    assert(HBoxCrypto_Aes256GcmDecrypt(
               key,
               nonce,
               damaged,
               WEBHID_REPORT_HEADER_BYTES,
               &damaged[8],
               plaintext_length,
               &damaged[52],
               decrypted) != 0);

    memcpy(damaged, report, sizeof(damaged));
    damaged[4] ^= 1u;
    make_nonce(
        nonce_prefix,
        read_u32_le(&damaged[4]),
        nonce);
    assert(HBoxCrypto_Aes256GcmDecrypt(
               key,
               nonce,
               damaged,
               WEBHID_REPORT_HEADER_BYTES,
               &damaged[8],
               plaintext_length,
               &damaged[52],
               decrypted) != 0);
}

int main(int argc, char **argv)
{
    uint8_t permit[236] = {0};
    uint8_t authorization_public[65] = {0};
    uint8_t permit_hash[32] = {0};
    uint8_t expected_permit_hash[32] = {0};
    uint8_t browser_public[65] = {0};
    uint8_t device_public[65] = {0};
    uint8_t expected_shared[32] = {0};
    uint8_t browser_to_device_key[32] = {0};
    uint8_t device_to_browser_key[32] = {0};
    uint8_t browser_to_device_prefix[8] = {0};
    uint8_t device_to_browser_prefix[8] = {0};
    uint8_t expected_browser_to_device_key[32] = {0};
    uint8_t expected_device_to_browser_key[32] = {0};
    uint8_t expected_browser_to_device_prefix[8] = {0};
    uint8_t expected_device_to_browser_prefix[8] = {0};
    uint8_t browser_scalar[32] = {0};
    uint8_t device_scalar[32] = {0};
    uint8_t permit_scalar[32] = {0};
    uint8_t computed_public[65] = {0};
    uint8_t browser_shared[32] = {0};
    uint8_t device_shared[32] = {0};
    uint8_t digest[32] = {0};
    uint8_t info[128] = {0};
    uint8_t browser_report[64] = {0};
    uint8_t device_report[64] = {0};
    uint8_t browser_plaintext[44] = {0};
    uint8_t device_plaintext[44] = {0};
    uint32_t rng_state = 0x48424f58u;
    size_t browser_plaintext_length;
    size_t device_plaintext_length;
    size_t info_length;
    hbox_device_session_permit_v1_t permit_struct;

    assert(argc == 16);
    assert(decode_hex(argv[1], permit, sizeof(permit)) == sizeof(permit));
    assert(decode_hex(
               argv[2],
               authorization_public,
               sizeof(authorization_public)) ==
           sizeof(authorization_public));
    assert(decode_hex(
               argv[3],
               expected_permit_hash,
               sizeof(expected_permit_hash)) ==
           sizeof(expected_permit_hash));
    assert(decode_hex(
               argv[4], browser_public, sizeof(browser_public)) ==
           sizeof(browser_public));
    assert(decode_hex(
               argv[5], device_public, sizeof(device_public)) ==
           sizeof(device_public));
    assert(decode_hex(
               argv[6], expected_shared, sizeof(expected_shared)) ==
           sizeof(expected_shared));
    assert(decode_hex(
               argv[7],
               expected_browser_to_device_key,
               sizeof(expected_browser_to_device_key)) ==
           sizeof(expected_browser_to_device_key));
    assert(decode_hex(
               argv[8],
               expected_device_to_browser_key,
               sizeof(expected_device_to_browser_key)) ==
           sizeof(expected_device_to_browser_key));
    assert(decode_hex(
               argv[9],
               expected_browser_to_device_prefix,
               sizeof(expected_browser_to_device_prefix)) ==
           sizeof(expected_browser_to_device_prefix));
    assert(decode_hex(
               argv[10],
               expected_device_to_browser_prefix,
               sizeof(expected_device_to_browser_prefix)) ==
           sizeof(expected_device_to_browser_prefix));
    assert(decode_hex(
               argv[12], browser_report, sizeof(browser_report)) ==
           sizeof(browser_report));
    browser_plaintext_length = decode_hex(
        argv[13], browser_plaintext, sizeof(browser_plaintext));
    assert(decode_hex(
               argv[14], device_report, sizeof(device_report)) ==
           sizeof(device_report));
    device_plaintext_length = decode_hex(
        argv[15], device_plaintext, sizeof(device_plaintext));

    assert(sizeof(permit_struct) == sizeof(permit));
    memcpy(&permit_struct, permit, sizeof(permit_struct));
    assert(permit_struct.magic_le == HBOX_SESSION_PERMIT_MAGIC);
    assert(permit_struct.version == HBOX_SECURITY_PROTOCOL_VERSION);
    assert(permit_struct.signed_bytes_le ==
           HBOX_SESSION_PERMIT_SIGNED_BYTES);
    assert(permit_struct.granted_scopes_le == 7u);
    assert(permit_struct.max_duration_ms_le == 300000u);
    assert(permit_struct.policy_version_le == 1u);
    assert(HBoxCrypto_Sha256(
               permit,
               sizeof(permit),
               permit_hash) == 0);
    assert(memcmp(
               permit_hash,
               expected_permit_hash,
               sizeof(permit_hash)) == 0);
    assert(HBoxCrypto_Sha256(
               permit,
               HBOX_SESSION_PERMIT_SIGNED_BYTES,
               digest) == 0);
    assert(HBoxCrypto_P256VerifyDigest(
               authorization_public,
               digest,
               permit_struct.server_signature) == 0);
    digest[0] ^= 1u;
    assert(HBoxCrypto_P256VerifyDigest(
               authorization_public,
               digest,
               permit_struct.server_signature) != 0);

    derive_test_scalar(kPermitLabel, permit_scalar);
    assert(HBoxCrypto_P256PublicFromPrivate(
               permit_scalar,
               computed_public) == 0);
    assert(memcmp(
               computed_public,
               authorization_public,
               sizeof(computed_public)) == 0);
    derive_test_scalar(kBrowserLabel, browser_scalar);
    assert(HBoxCrypto_P256PublicFromPrivate(
               browser_scalar,
               computed_public) == 0);
    assert(memcmp(
               computed_public,
               browser_public,
               sizeof(computed_public)) == 0);
    assert(HBoxCrypto_P256ValidatePublicKey(browser_public) == 0);
    computed_public[0] = 0x05u;
    assert(HBoxCrypto_P256ValidatePublicKey(computed_public) != 0);
    memcpy(computed_public, browser_public, sizeof(computed_public));
    memset(&computed_public[1], 0, sizeof(computed_public) - 1u);
    assert(HBoxCrypto_P256ValidatePublicKey(computed_public) != 0);
    derive_test_scalar(kDeviceLabel, device_scalar);
    assert(HBoxCrypto_P256PublicFromPrivate(
               device_scalar,
               computed_public) == 0);
    assert(memcmp(
               computed_public,
               device_public,
               sizeof(computed_public)) == 0);

    assert(HBoxCrypto_P256Ecdh(
               browser_scalar,
               device_public,
               browser_shared,
               test_rng,
               &rng_state) == 0);
    assert(HBoxCrypto_P256Ecdh(
               device_scalar,
               browser_public,
               device_shared,
               test_rng,
               &rng_state) == 0);
    assert(memcmp(
               browser_shared,
               expected_shared,
               sizeof(browser_shared)) == 0);
    assert(memcmp(
               browser_shared,
               device_shared,
               sizeof(browser_shared)) == 0);

    info_length = make_info(
        argv[11],
        "browser-to-device",
        0,
        info,
        sizeof(info));
    assert(HBoxCrypto_HkdfSha256(
               permit_hash,
               sizeof(permit_hash),
               browser_shared,
               sizeof(browser_shared),
               info,
               info_length,
               browser_to_device_key,
               sizeof(browser_to_device_key)) == 0);
    info_length = make_info(
        argv[11],
        "device-to-browser",
        0,
        info,
        sizeof(info));
    assert(HBoxCrypto_HkdfSha256(
               permit_hash,
               sizeof(permit_hash),
               browser_shared,
               sizeof(browser_shared),
               info,
               info_length,
               device_to_browser_key,
               sizeof(device_to_browser_key)) == 0);
    info_length = make_info(
        argv[11],
        "browser-to-device",
        1,
        info,
        sizeof(info));
    assert(HBoxCrypto_HkdfSha256(
               permit_hash,
               sizeof(permit_hash),
               browser_shared,
               sizeof(browser_shared),
               info,
               info_length,
               browser_to_device_prefix,
               sizeof(browser_to_device_prefix)) == 0);
    info_length = make_info(
        argv[11],
        "device-to-browser",
        1,
        info,
        sizeof(info));
    assert(HBoxCrypto_HkdfSha256(
               permit_hash,
               sizeof(permit_hash),
               browser_shared,
               sizeof(browser_shared),
               info,
               info_length,
               device_to_browser_prefix,
               sizeof(device_to_browser_prefix)) == 0);
    assert(memcmp(
               browser_to_device_key,
               expected_browser_to_device_key,
               sizeof(browser_to_device_key)) == 0);
    assert(memcmp(
               device_to_browser_key,
               expected_device_to_browser_key,
               sizeof(device_to_browser_key)) == 0);
    assert(memcmp(
               browser_to_device_prefix,
               expected_browser_to_device_prefix,
               sizeof(browser_to_device_prefix)) == 0);
    assert(memcmp(
               device_to_browser_prefix,
               expected_device_to_browser_prefix,
               sizeof(device_to_browser_prefix)) == 0);

    verify_report(
        browser_report,
        browser_plaintext,
        browser_plaintext_length,
        browser_to_device_key,
        browser_to_device_prefix);
    verify_report(
        device_report,
        device_plaintext,
        device_plaintext_length,
        device_to_browser_key,
        device_to_browser_prefix);

    HBoxCrypto_Zeroize(browser_scalar, sizeof(browser_scalar));
    HBoxCrypto_Zeroize(device_scalar, sizeof(device_scalar));
    HBoxCrypto_Zeroize(permit_scalar, sizeof(permit_scalar));
    HBoxCrypto_Zeroize(browser_shared, sizeof(browser_shared));
    HBoxCrypto_Zeroize(device_shared, sizeof(device_shared));
    puts("WebHID security golden vectors passed in host C");
    return 0;
}
