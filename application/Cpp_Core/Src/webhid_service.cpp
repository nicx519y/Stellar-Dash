#include "webhid_service.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "adc_btns/adc_btns_worker.hpp"
#include "adc_btns/adc_btns_marker.hpp"
#include "adc_btns/adc_calibration.hpp"
#include "board_security_confirmation.h"
#include "board_cfg.h"
#include "config_transport_sink.hpp"
#include "configs/webconfig_btns_manager.hpp"
#include "configs/common_command_handler.hpp"
#include "configs/firmware_command_handler.hpp"
#include "configs/user_image_command_handler.hpp"
#include "configs/webconfig_leds_manager.hpp"
#include "device_security_crypto.h"
#include "device_identity_store.h"
#include "firmware/firmware_manager.hpp"
#include "firmware_metadata.h"
#include "hardware_rng.h"
#include "manufacturer_ca_public_key.h"
#include "mbedtls/base64.h"
#include "stm32h7xx_hal.h"
#include "usb_board_link.hpp"
#include "usb_board_link_c_api.h"
#include "usbdriver.hpp"
#include "webconfig_authorization_public_keys.h"
#include "webhid_rpc_dispatcher.hpp"
#include "cJSON.h"
#include "main_runtime_control.hpp"

namespace {

constexpr uint8_t kKnownFrameFlags =
    WEBHID_REPORT_FLAG_ENCRYPTED |
    WEBHID_REPORT_FLAG_FRAGMENTED |
    WEBHID_REPORT_FLAG_LAST |
    WEBHID_REPORT_FLAG_ACK_REQUIRED;
constexpr uint8_t kStreamCreditWindow = 4u;
constexpr uint32_t kSampleIntervalMs = 10u;
constexpr uint32_t kCheckpointIntervalMs = 1000u;
constexpr uint32_t kPermitInstallTimeoutMs =
    HBOX_SECURITY_CHALLENGE_SECONDS * 1000u;
constexpr uint32_t kCpuCyclesPerMicrosecond =
    SYSTEM_CLOCK_FREQ / 1000000u;
constexpr size_t kResponseScratchBytes = 16u * 1024u + 5u;

/*
 * This CPU-only workspace is explicitly cleared before every use, so it does
 * not depend on startup .bss clearing. Keep it in the existing SRAM-D2 buffer
 * area: AXI SRAM also contains executable code and is close to its link limit.
 * While a physical WebHID report is pending, process() gates final inbound
 * requests, making the first report-sized prefix safe for exact-byte resume.
 */
__attribute__((section(".DMA_Section.WebHidResponseScratch"), aligned(32)))
std::array<char, kResponseScratchBytes> g_responseScratch;

bool allZero(const uint8_t *value, size_t length);

bool extractTransactionId(
    const uint8_t *json,
    size_t length,
    uint32_t &transactionId)
{
    static constexpr char kKey[] = "\"transactionId\"";
    if (json == nullptr || length < sizeof(kKey)) {
        return false;
    }
    const uint8_t *const end = json + length;
    const uint8_t *cursor = std::search(
        json,
        end,
        reinterpret_cast<const uint8_t *>(kKey),
        reinterpret_cast<const uint8_t *>(kKey) + sizeof(kKey) - 1u);
    if (cursor == end) {
        return false;
    }
    cursor += sizeof(kKey) - 1u;
    while (cursor != end &&
           (*cursor == ' ' || *cursor == '\t' ||
            *cursor == '\r' || *cursor == '\n')) {
        ++cursor;
    }
    if (cursor == end || *cursor != ':') {
        return false;
    }
    ++cursor;
    while (cursor != end &&
           (*cursor == ' ' || *cursor == '\t' ||
            *cursor == '\r' || *cursor == '\n')) {
        ++cursor;
    }
    uint64_t value = 0u;
    const uint8_t *const firstDigit = cursor;
    while (cursor != end && *cursor >= '0' && *cursor <= '9') {
        value = value * 10u + static_cast<uint64_t>(*cursor - '0');
        if (value > UINT32_MAX) {
            return false;
        }
        ++cursor;
    }
    if (cursor == firstDigit || value == 0u) {
        return false;
    }
    transactionId = static_cast<uint32_t>(value);
    return true;
}

#if WEBCONFIG_TEST_FORCE_BOOT
int developmentIdentityRead(
    void *,
    uint32_t slotIndex,
    uint32_t flashwordIndex,
    uint8_t output[HBOX_INTERNAL_FLASH_PROGRAM_BYTES])
{
    if (output == nullptr ||
        slotIndex >= HBOX_DEVICE_IDENTITY_SLOT_COUNT ||
        flashwordIndex >=
            HBOX_DEVICE_IDENTITY_SLOT_BYTES /
                HBOX_INTERNAL_FLASH_PROGRAM_BYTES) {
        return 0;
    }
    const uint32_t offset =
        slotIndex * HBOX_DEVICE_IDENTITY_SLOT_BYTES +
        flashwordIndex * HBOX_INTERNAL_FLASH_PROGRAM_BYTES;
    if (offset > HBOX_DEVICE_IDENTITY_REGION_BYTES -
                     HBOX_INTERNAL_FLASH_PROGRAM_BYTES) {
        return 0;
    }
    memcpy(output,
           reinterpret_cast<const void *>(
               HBOX_DEVICE_IDENTITY_REGION_ADDRESS + offset),
           HBOX_INTERNAL_FLASH_PROGRAM_BYTES);
    return 1;
}

bool prepareDevelopmentBootContext()
{
    hbox_device_identity_backend_t backend = {};
    hbox_device_identity_record_v1_t identity = {};
    hbox_boot_security_context_v1_t staging = {};
    uint8_t digest[HBOX_SECURITY_HASH_BYTES] = {};
    uint8_t derivedDeviceId[HBOX_SECURITY_HASH_BYTES] = {};
    uint8_t derivedPublic[HBOX_SECURITY_P256_PUBLIC_KEY_BYTES] = {};
    const auto *metadata =
        reinterpret_cast<const FirmwareMetadata *>(METADATA_ADDR);
    bool valid = false;

    backend.slot_count = HBOX_DEVICE_IDENTITY_SLOT_COUNT;
    backend.read_flashword = developmentIdentityRead;
    if (HBOX_MANUFACTURER_CA_KEY_PROVISIONED == 0u ||
        allZero(HBOX_MANUFACTURER_CA_PUBLIC_KEY,
                sizeof(HBOX_MANUFACTURER_CA_PUBLIC_KEY)) ||
        HBoxIdentityStore_LoadFromBackend(&backend, &identity) !=
            HBOX_DEVICE_IDENTITY_OK ||
        identity.device_certificate.product_id_le != HBOX_PRODUCT_ID ||
        identity.device_certificate.hardware_version_le != HARDWARE_VERSION ||
        metadata->magic != FIRMWARE_MAGIC ||
        metadata->signature_algorithm !=
            FIRMWARE_SIGNATURE_ECDSA_P256_SHA256 ||
        metadata->security_version < FIRMWARE_SECURITY_VERSION ||
        HBoxCrypto_P256PublicFromPrivate(identity.device_private_key,
                                         derivedPublic) != 0 ||
        memcmp(derivedPublic,
               identity.device_certificate.device_public_key,
               sizeof(derivedPublic)) != 0 ||
        HBoxCrypto_Sha256(
            identity.device_certificate.device_public_key,
            sizeof(identity.device_certificate.device_public_key),
            derivedDeviceId) != 0 ||
        memcmp(derivedDeviceId,
               identity.device_certificate.device_id,
               HBOX_SECURITY_DEVICE_ID_BYTES) != 0 ||
        HBoxCrypto_Sha256(
            reinterpret_cast<const uint8_t *>(
                &identity.device_certificate),
            HBOX_DEVICE_CERTIFICATE_SIGNED_BYTES,
            digest) != 0 ||
        HBoxCrypto_P256VerifyDigest(
            HBOX_MANUFACTURER_CA_PUBLIC_KEY,
            digest,
            identity.device_certificate.manufacturer_signature) != 0) {
        goto done;
    }

    staging.magic_le = HBOX_BOOT_CONTEXT_MAGIC;
    staging.version = HBOX_BOOT_CONTEXT_VERSION;
    staging.total_bytes_le = sizeof(staging);
    staging.created_at_tick_le = HAL_GetTick();
    memcpy(&staging.device_certificate,
           &identity.device_certificate,
           sizeof(staging.device_certificate));
    staging.boot_attestation.magic_le = HBOX_BOOT_ATTESTATION_MAGIC;
    staging.boot_attestation.version = HBOX_SECURITY_PROTOCOL_VERSION;
    staging.boot_attestation.signed_bytes_le =
        HBOX_BOOT_ATTESTATION_SIGNED_BYTES;
    staging.boot_attestation.security_version_le =
        metadata->security_version;
    staging.boot_attestation.bootloader_version_le = BOOTLOADER_VERSION;
    memcpy(staging.boot_attestation.device_id,
           identity.device_certificate.device_id,
           HBOX_SECURITY_DEVICE_ID_BYTES);
    memcpy(staging.boot_attestation.firmware_hash,
           metadata->firmware_hash,
           HBOX_SECURITY_HASH_BYTES);

    if (HBoxHardwareRng_Fill(
            nullptr,
            staging.boot_attestation.boot_nonce,
            sizeof(staging.boot_attestation.boot_nonce)) != 0 ||
        HBoxCrypto_P256Generate(
            staging.boot_private_key,
            staging.boot_attestation.boot_public_key,
            HBoxHardwareRng_Fill,
            nullptr) != 0 ||
        HBoxCrypto_Sha256(
            reinterpret_cast<const uint8_t *>(
                &staging.boot_attestation),
            HBOX_BOOT_ATTESTATION_SIGNED_BYTES,
            digest) != 0 ||
        HBoxCrypto_P256SignDigest(
            identity.device_private_key,
            digest,
            staging.boot_attestation.device_signature,
            HBoxHardwareRng_Fill,
            nullptr) != 0) {
        goto done;
    }

    strncpy(staging.firmware_version,
            metadata->firmware_version,
            sizeof(staging.firmware_version) - 1u);
    staging.crc32_le = HBoxSecurity_Crc32Skipping(
        reinterpret_cast<const uint8_t *>(&staging),
        sizeof(staging),
        HBOX_BOOT_CONTEXT_CRC_OFFSET,
        sizeof(staging.crc32_le));
    if (staging.crc32_le == 0u ||
        !HBoxSecurity_ValidateBootContext(&staging)) {
        goto done;
    }
    memcpy(reinterpret_cast<void *>(HBOX_BOOT_CONTEXT_ADDRESS),
           &staging,
           sizeof(staging));
    __DSB();
    valid = true;

done:
    HBoxCrypto_Zeroize(&identity, sizeof(identity));
    HBoxCrypto_Zeroize(&staging, sizeof(staging));
    HBoxCrypto_Zeroize(digest, sizeof(digest));
    HBoxCrypto_Zeroize(derivedDeviceId, sizeof(derivedDeviceId));
    HBoxCrypto_Zeroize(derivedPublic, sizeof(derivedPublic));
    return valid;
}
#endif

void webhidReportReceived(const uint8_t report[WEBHID_REPORT_BYTES])
{
    if (!WEBHID_SERVICE.enqueueReport(report)) {
        USB_BOARD_LINK.releaseWebConfigReceiveCredit();
    }
}

void configJsonEvent(const char *json, size_t length)
{
    WEBHID_SERVICE.enqueueJsonEvent(json, length);
}

void configBinaryEvent(const uint8_t *data, size_t length)
{
    WEBHID_SERVICE.enqueueBinaryEvent(data, length);
}

bool allZero(const uint8_t *value, size_t length)
{
    uint8_t combined = 0u;
    for (size_t index = 0u; index < length; ++index) {
        combined |= value[index];
    }
    return combined == 0u;
}

bool decodeBase64(const char *encoded,
                  std::vector<uint8_t> &decoded,
                  size_t expectedLength,
                  bool urlSafe)
{
    if (encoded == nullptr) {
        return false;
    }
    std::string normalized(encoded);
    if (normalized.empty() || normalized.size() > 128u * 1024u) {
        return false;
    }
    if (urlSafe) {
        for (char &value : normalized) {
            if (value == '-') {
                value = '+';
            } else if (value == '_') {
                value = '/';
            }
        }
        while ((normalized.size() % 4u) != 0u) {
            normalized.push_back('=');
        }
    }
    decoded.assign((normalized.size() * 3u) / 4u + 3u, 0u);
    size_t written = 0u;
    const int result = mbedtls_base64_decode(
        decoded.data(),
        decoded.size(),
        &written,
        reinterpret_cast<const unsigned char *>(normalized.data()),
        normalized.size());
    if (result != 0 ||
        (expectedLength != 0u && written != expectedLength)) {
        decoded.clear();
        return false;
    }
    decoded.resize(written);
    return true;
}

std::string encodeBase64(const uint8_t *data, size_t length)
{
    std::vector<uint8_t> encoded(((length + 2u) / 3u) * 4u + 1u);
    size_t written = 0u;
    if ((data == nullptr && length != 0u) ||
        mbedtls_base64_encode(
            encoded.data(),
            encoded.size(),
            &written,
            data,
            length) != 0) {
        return {};
    }
    return std::string(
        reinterpret_cast<const char *>(encoded.data()), written);
}

std::string encodeBase64Url(const uint8_t *data, size_t length)
{
    std::string result = encodeBase64(data, length);
    for (char &value : result) {
        if (value == '+') {
            value = '-';
        } else if (value == '/') {
            value = '_';
        }
    }
    while (!result.empty() && result.back() == '=') {
        result.pop_back();
    }
    return result;
}

std::string encodeHex(const uint8_t *data,
                      size_t length,
                      bool uppercase)
{
    const char *alphabet =
        uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string result(length * 2u, '0');
    for (size_t index = 0u; index < length; ++index) {
        result[index * 2u] = alphabet[data[index] >> 4u];
        result[index * 2u + 1u] = alphabet[data[index] & 0x0Fu];
    }
    return result;
}

bool parseU32(cJSON *value, uint32_t &result)
{
    if (value == nullptr || !cJSON_IsNumber(value) ||
        value->valuedouble < 0.0 ||
        value->valuedouble > 4294967295.0 ||
        std::floor(value->valuedouble) != value->valuedouble) {
        return false;
    }
    result = static_cast<uint32_t>(value->valuedouble);
    return true;
}

uint32_t loadLe32(const uint8_t *value)
{
    return static_cast<uint32_t>(value[0]) |
           (static_cast<uint32_t>(value[1]) << 8u) |
           (static_cast<uint32_t>(value[2]) << 16u) |
           (static_cast<uint32_t>(value[3]) << 24u);
}

uint16_t loadLe16(const uint8_t *value)
{
    return static_cast<uint16_t>(value[0]) |
           static_cast<uint16_t>(
               static_cast<uint16_t>(value[1]) << 8u);
}

bool constantTimeEqual(const uint8_t *left,
                       const uint8_t *right,
                       size_t length)
{
    uint8_t difference = 0u;
    for (size_t index = 0u; index < length; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0u;
}

bool canonicalBase64Url(const char *encoded,
                        std::vector<uint8_t> &decoded,
                        size_t expectedLength)
{
    if (!decodeBase64(encoded, decoded, expectedLength, true)) {
        return false;
    }
    return encodeBase64Url(decoded.data(), decoded.size()) == encoded;
}

const char *safeFirmwareVersion(
    const hbox_boot_security_context_v1_t &context,
    char output[17])
{
    memcpy(output, context.firmware_version, 16u);
    output[16] = '\0';
    unsigned int major = 0u;
    unsigned int minor = 0u;
    unsigned int patch = 0u;
    char trailing = '\0';
    if (sscanf(output,
               "%u.%u.%u%c",
               &major,
               &minor,
               &patch,
               &trailing) != 3) {
        strcpy(output, "0.0.0");
    }
    return output;
}

uint8_t streamTypeForName(const char *name)
{
    if (name == nullptr) {
        return 0u;
    }
    if (strcmp(name, "firmware") == 0) {
        return 1u;
    }
    if (strcmp(name, "config-import") == 0) {
        return 3u;
    }
    return 0u;
}

uint32_t streamScope(uint8_t type)
{
    switch (type) {
    case 1u:
        return HBOX_SCOPE_FIRMWARE_UPDATE;
    case 3u:
        return HBOX_SCOPE_CONFIG_WRITE;
    default:
        return 0u;
    }
}

uint32_t binaryOpcodeScope(uint8_t opcode)
{
    if (opcode == BINARY_CMD_UPLOAD_FIRMWARE_CHUNK) {
        return HBOX_SCOPE_FIRMWARE_UPDATE;
    }
    if (opcode >= 0x30u && opcode <= 0x33u) {
        return HBOX_SCOPE_ASSET_WRITE;
    }
    if (opcode == 0x34u || opcode == 0x35u) {
        return HBOX_SCOPE_CONFIG_READ;
    }
    return 0u;
}

enum class BinaryAckStatus : uint8_t
{
    Accepted = 0u,
    Rejected,
    ProtocolError,
};

constexpr uint8_t kFirmwareChunkAckOpcode = 0x81u;
constexpr uint8_t kImageBeginOpcode = 0x30u;
constexpr uint8_t kImageChunkOpcode = 0x31u;
constexpr uint8_t kImageCommitOpcode = 0x32u;
constexpr uint8_t kImageDeleteOpcode = 0x33u;
constexpr uint8_t kImageInfoOpcode = 0x34u;
constexpr uint8_t kImageReadOpcode = 0x35u;
constexpr size_t kFirmwareChunkRequestHeaderBytes =
    sizeof(BinaryFirmwareChunkHeader);
constexpr size_t kFirmwareChunkResponseBytes = 75u;
constexpr size_t kImageMutationResponseBytes = 79u;
constexpr size_t kImageCommitResponseBytes = 83u;
constexpr size_t kImageInfoResponseBytes = 64u;
constexpr size_t kExtendedImageInfoResponseBytes = 76u;
constexpr size_t kFastImageInfoResponseBytes = 80u;
constexpr size_t kImageReadResponseHeaderBytes = 55u;

bool binaryRequestShapeValid(
    const uint8_t *request,
    size_t requestLength)
{
    if (request == nullptr || requestLength == 0u) {
        return false;
    }
    switch (request[0]) {
    case BINARY_CMD_UPLOAD_FIRMWARE_CHUNK: {
        if (requestLength < kFirmwareChunkRequestHeaderBytes) {
            return false;
        }
        const uint16_t sessionLength = loadLe16(&request[2]);
        const uint16_t componentLength = loadLe16(&request[36]);
        const uint32_t chunkLength = loadLe32(&request[62]);
        return sessionLength != 0u && sessionLength <= 32u &&
               componentLength != 0u && componentLength <= 16u &&
               chunkLength ==
                   requestLength - kFirmwareChunkRequestHeaderBytes;
    }
    case kImageBeginOpcode:
        return requestLength == 22u;
    case kImageChunkOpcode: {
        if (requestLength < 14u) {
            return false;
        }
        const uint16_t chunkLength = loadLe16(&request[10]);
        return chunkLength == requestLength - 14u;
    }
    case kImageCommitOpcode:
    case kImageDeleteOpcode:
    case kImageInfoOpcode:
        return requestLength == 6u;
    case kImageReadOpcode:
        return requestLength == 14u && request[1] <= 1u;
    default:
        return false;
    }
}

BinaryAckStatus describeBinaryAck(
    const uint8_t *request,
    size_t requestLength,
    const uint8_t *response,
    size_t responseLength,
    bool handlerResultKnown,
    bool handlerSucceeded,
    cJSON *ack)
{
    if (!binaryRequestShapeValid(request, requestLength) ||
        response == nullptr || responseLength < 2u || ack == nullptr) {
        return BinaryAckStatus::ProtocolError;
    }
    const uint8_t requestOpcode = request[0];
    const uint8_t expectedOpcode = requestOpcode ==
            BINARY_CMD_UPLOAD_FIRMWARE_CHUNK
        ? kFirmwareChunkAckOpcode
        : static_cast<uint8_t>(requestOpcode | 0x80u);
    if (response[0] != expectedOpcode || response[1] > 1u) {
        return BinaryAckStatus::ProtocolError;
    }

    const bool accepted = response[1] == 1u;
    if (handlerResultKnown && handlerSucceeded != accepted) {
        return BinaryAckStatus::ProtocolError;
    }
    cJSON_AddNumberToObject(ack, "requestOpcode", requestOpcode);
    cJSON_AddNumberToObject(ack, "opcode", response[0]);
    cJSON_AddBoolToObject(ack, "success", accepted);

    switch (requestOpcode) {
    case BINARY_CMD_UPLOAD_FIRMWARE_CHUNK: {
        if (responseLength != kFirmwareChunkResponseBytes ||
            response[10] > 64u ||
            (accepted && response[10] != 0u)) {
            return BinaryAckStatus::ProtocolError;
        }
        const uint32_t requestedChunkIndex = loadLe32(&request[54]);
        const uint32_t acknowledgedChunkIndex = loadLe32(&response[2]);
        const uint32_t progress = loadLe32(&response[6]);
        if (acknowledgedChunkIndex != requestedChunkIndex ||
            progress > 100u) {
            return BinaryAckStatus::ProtocolError;
        }
        cJSON_AddStringToObject(ack, "kind", "firmware.chunk");
        cJSON_AddNumberToObject(
            ack, "chunkIndex", acknowledgedChunkIndex);
        cJSON_AddNumberToObject(ack, "progress", progress);
        break;
    }
    case kImageBeginOpcode:
    case kImageChunkOpcode:
    case kImageCommitOpcode:
    case kImageDeleteOpcode: {
        const size_t expectedMutationResponseBytes =
            requestOpcode == kImageCommitOpcode
                ? kImageCommitResponseBytes
                : kImageMutationResponseBytes;
        if (responseLength != expectedMutationResponseBytes ||
            response[14] > 64u ||
            (accepted && response[14] != 0u)) {
            return BinaryAckStatus::ProtocolError;
        }
        const uint32_t requestedCid = loadLe32(&request[2]);
        const uint32_t acknowledgedCid = loadLe32(&response[2]);
        const uint32_t received = loadLe32(&response[6]);
        const uint32_t total = loadLe32(&response[10]);
        if (acknowledgedCid != requestedCid || received > total) {
            return BinaryAckStatus::ProtocolError;
        }
        if (requestOpcode == kImageBeginOpcode && accepted &&
            (received != 0u || total != loadLe32(&request[10]))) {
            return BinaryAckStatus::ProtocolError;
        }
        if (requestOpcode == kImageChunkOpcode) {
            const uint32_t offset = loadLe32(&request[6]);
            const uint16_t chunkLength = loadLe16(&request[10]);
            if (offset > UINT32_MAX - chunkLength ||
                (accepted && received != offset + chunkLength)) {
                return BinaryAckStatus::ProtocolError;
            }
            /*
             * Correlation fields come from the authenticated request and are
             * present for both positive and negative ACKs. This lets the
             * typed client distinguish an explicit device rejection from an
             * unrelated binary event without weakening fail-closed matching.
             */
            cJSON_AddNumberToObject(ack, "offset", offset);
            cJSON_AddNumberToObject(
                ack, "chunkSize", chunkLength);
        }
        cJSON_AddStringToObject(
            ack,
            "kind",
            requestOpcode == kImageBeginOpcode
                ? "image.begin"
                : requestOpcode == kImageChunkOpcode
                    ? "image.chunk"
                    : requestOpcode == kImageCommitOpcode
                        ? "image.commit"
                        : "image.delete");
        cJSON_AddNumberToObject(ack, "cid", acknowledgedCid);
        cJSON_AddNumberToObject(ack, "received", received);
        cJSON_AddNumberToObject(ack, "total", total);
        if (requestOpcode == kImageCommitOpcode) {
            cJSON_AddNumberToObject(
                ack, "crc32", loadLe32(&response[79]));
        }
        break;
    }
    case kImageInfoOpcode: {
        const uint8_t requestedVersion = request[1];
        const bool extendedRequested = requestedVersion == 1u;
        const bool fastRequested = requestedVersion == 2u;
        const size_t expectedResponseLength = fastRequested
            ? kFastImageInfoResponseBytes
            : extendedRequested
                ? kExtendedImageInfoResponseBytes
                : kImageInfoResponseBytes;
        if (requestedVersion > 2u ||
            responseLength != expectedResponseLength ||
            loadLe32(&response[2]) != loadLe32(&request[2]) ||
            response[6] > 1u || response[7] > 1u) {
            return BinaryAckStatus::ProtocolError;
        }
        if (extendedRequested &&
            (response[64] != 2u ||
             response[65] == 0u || response[65] > 10u ||
             response[66] > 10u ||
             (response[7] == 1u && response[66] == 0u) ||
             response[67] != 0u)) {
            return BinaryAckStatus::ProtocolError;
        }
        if (fastRequested &&
            (response[64] != 3u ||
             response[65] == 0u || response[65] > 10u ||
             response[66] != 0u || response[67] != 0u ||
             response[76] != 2u || response[77] != 44u ||
             loadLe16(&response[78]) != 0x0003u)) {
            return BinaryAckStatus::ProtocolError;
        }
        cJSON_AddStringToObject(ack, "kind", "image.info");
        cJSON_AddNumberToObject(
            ack, "cid", loadLe32(&response[2]));
        break;
    }
    case kImageReadOpcode: {
        if (responseLength < kImageReadResponseHeaderBytes ||
            response[22] > 32u ||
            (accepted && response[22] != 0u) ||
            response[2] != request[1] ||
            loadLe32(&response[4]) != loadLe32(&request[2]) ||
            loadLe32(&response[16]) != loadLe32(&request[6])) {
            return BinaryAckStatus::ProtocolError;
        }
        const uint16_t returnedChunkLength = loadLe16(&response[20]);
        const uint16_t requestedChunkLength = loadLe16(&request[10]);
        if (responseLength !=
                kImageReadResponseHeaderBytes + returnedChunkLength ||
            (accepted && returnedChunkLength > requestedChunkLength) ||
            (!accepted && returnedChunkLength != 0u)) {
            return BinaryAckStatus::ProtocolError;
        }
        cJSON_AddStringToObject(ack, "kind", "image.read");
        cJSON_AddNumberToObject(
            ack, "cid", loadLe32(&response[4]));
        cJSON_AddNumberToObject(ack, "target", response[2]);
        cJSON_AddNumberToObject(
            ack, "offset", loadLe32(&response[16]));
        cJSON_AddNumberToObject(
            ack, "chunkSize", returnedChunkLength);
        cJSON_AddNumberToObject(
            ack, "total", loadLe32(&response[12]));
        break;
    }
    default:
        return BinaryAckStatus::ProtocolError;
    }
    return accepted ? BinaryAckStatus::Accepted
                    : BinaryAckStatus::Rejected;
}

cJSON *createBinaryAckData(
    const uint8_t *request,
    size_t requestLength,
    const uint8_t *response,
    size_t responseLength,
    bool handlerResultKnown,
    bool handlerSucceeded,
    bool streamComplete,
    BinaryAckStatus &status)
{
    status = BinaryAckStatus::ProtocolError;
    cJSON *data = cJSON_CreateObject();
    cJSON *ack = cJSON_CreateObject();
    if (data == nullptr || ack == nullptr) {
        cJSON_Delete(data);
        cJSON_Delete(ack);
        return nullptr;
    }
    status = describeBinaryAck(
        request,
        requestLength,
        response,
        responseLength,
        handlerResultKnown,
        handlerSucceeded,
        ack);
    if (status == BinaryAckStatus::ProtocolError) {
        cJSON_Delete(data);
        cJSON_Delete(ack);
        return nullptr;
    }
    const std::string encoded = encodeBase64(response, responseLength);
    if (encoded.empty()) {
        cJSON_Delete(data);
        cJSON_Delete(ack);
        status = BinaryAckStatus::ProtocolError;
        return nullptr;
    }
    if (streamComplete) {
        cJSON_AddBoolToObject(data, "complete", true);
    }
    cJSON_AddStringToObject(data, "encoding", "base64");
    cJSON_AddStringToObject(data, "data", encoded.c_str());
    cJSON_AddItemToObject(data, "ack", ack);
    return data;
}

uint32_t scopeForName(const char *name)
{
    if (name == nullptr) {
        return 0u;
    }
    if (strcmp(name, "config.read") == 0) {
        return HBOX_SCOPE_CONFIG_READ;
    }
    if (strcmp(name, "config.write") == 0) {
        return HBOX_SCOPE_CONFIG_WRITE;
    }
    if (strcmp(name, "monitor.read") == 0) {
        return HBOX_SCOPE_MONITOR_READ;
    }
    if (strcmp(name, "device.control") == 0) {
        return HBOX_SCOPE_DEVICE_CONTROL;
    }
    if (strcmp(name, "asset.write") == 0) {
        return HBOX_SCOPE_ASSET_WRITE;
    }
    if (strcmp(name, "firmware.update") == 0) {
        return HBOX_SCOPE_FIRMWARE_UPDATE;
    }
    return 0u;
}

bool parseScopeArray(cJSON *scopes, uint32_t &mask)
{
    mask = 0u;
    if (scopes == nullptr || !cJSON_IsArray(scopes) ||
        cJSON_GetArraySize(scopes) == 0 ||
        cJSON_GetArraySize(scopes) > 6) {
        return false;
    }
    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, scopes) {
        if (!cJSON_IsString(item)) {
            return false;
        }
        const uint32_t scope = scopeForName(item->valuestring);
        if (scope == 0u || (mask & scope) != 0u) {
            return false;
        }
        mask |= scope;
    }
    return true;
}

void makeNonce(const std::array<uint8_t, 8> &prefix,
               uint32_t sequence,
               uint8_t nonce[12])
{
    memcpy(nonce, prefix.data(), prefix.size());
    nonce[8] = static_cast<uint8_t>((sequence >> 24u) & 0xFFu);
    nonce[9] = static_cast<uint8_t>((sequence >> 16u) & 0xFFu);
    nonce[10] = static_cast<uint8_t>((sequence >> 8u) & 0xFFu);
    nonce[11] = static_cast<uint8_t>(sequence & 0xFFu);
}

uint16_t distanceMicrometres(ADCBtn *button, uint16_t value)
{
    if (button == nullptr) {
        return 0u;
    }
    const float millimetres =
        ADC_BTNS_WORKER.getDistanceByValue(button, value);
    if (!std::isfinite(millimetres) || millimetres <= 0.0f) {
        return 0u;
    }
    const float micrometres = millimetres * 1000.0f;
    if (micrometres >= 65535.0f) {
        return 65535u;
    }
    return static_cast<uint16_t>(micrometres + 0.5f);
}

} // namespace

WebHidService &WebHidService::getInstance()
{
    static WebHidService service;
    return service;
}

extern "C" void WebHidTelemetry_OnAdcTransition(
    uint8_t buttonIndex,
    uint8_t pressed)
{
    WEBHID_SERVICE.onAdcButtonTransition(
        buttonIndex, pressed != 0u);
}

bool WebHidService::validateBootContext()
{
    const auto *source =
        reinterpret_cast<const hbox_boot_security_context_v1_t *>(
            HBOX_BOOT_CONTEXT_ADDRESS);
    uint8_t digest[32] = {};
    uint8_t derivedPublic[65] = {};
    uint8_t deviceIdHash[32] = {};

    memcpy(&bootContext, source, sizeof(bootContext));
    HBoxCrypto_Zeroize(
        reinterpret_cast<void *>(HBOX_BOOT_CONTEXT_ADDRESS),
        sizeof(hbox_boot_security_context_v1_t));

    if (!HBoxSecurity_ValidateBootContext(&bootContext) ||
        HBOX_MANUFACTURER_CA_KEY_PROVISIONED == 0u ||
        allZero(HBOX_MANUFACTURER_CA_PUBLIC_KEY,
                sizeof(HBOX_MANUFACTURER_CA_PUBLIC_KEY))) {
        HBoxCrypto_Zeroize(&bootContext, sizeof(bootContext));
        bootContextValid = false;
        return false;
    }

    bool valid =
        bootContext.device_certificate.product_id_le == HBOX_PRODUCT_ID &&
        bootContext.device_certificate.hardware_version_le ==
            HARDWARE_VERSION &&
        bootContext.boot_attestation.security_version_le >=
            FIRMWARE_SECURITY_VERSION &&
        HBoxCrypto_Sha256(
            reinterpret_cast<const uint8_t *>(
                &bootContext.device_certificate),
            HBOX_DEVICE_CERTIFICATE_SIGNED_BYTES,
            digest) == 0 &&
        HBoxCrypto_P256VerifyDigest(
            HBOX_MANUFACTURER_CA_PUBLIC_KEY,
            digest,
            bootContext.device_certificate.manufacturer_signature) == 0 &&
        HBoxCrypto_Sha256(
            bootContext.device_certificate.device_public_key,
            sizeof(bootContext.device_certificate.device_public_key),
            deviceIdHash) == 0 &&
        memcmp(deviceIdHash,
               bootContext.device_certificate.device_id,
               HBOX_SECURITY_DEVICE_ID_BYTES) == 0 &&
        HBoxCrypto_P256PublicFromPrivate(
            bootContext.boot_private_key,
            derivedPublic) == 0 &&
        memcmp(derivedPublic,
               bootContext.boot_attestation.boot_public_key,
               sizeof(derivedPublic)) == 0 &&
        HBoxCrypto_Sha256(
            reinterpret_cast<const uint8_t *>(
                &bootContext.boot_attestation),
            HBOX_BOOT_ATTESTATION_SIGNED_BYTES,
            digest) == 0 &&
        HBoxCrypto_P256VerifyDigest(
            bootContext.device_certificate.device_public_key,
            digest,
            bootContext.boot_attestation.device_signature) == 0 &&
        memcmp(bootContext.device_certificate.device_id,
               bootContext.boot_attestation.device_id,
               HBOX_SECURITY_DEVICE_ID_BYTES) == 0 &&
        !allZero(bootContext.boot_attestation.boot_nonce,
                 HBOX_SECURITY_NONCE_BYTES);

    HBoxCrypto_Zeroize(digest, sizeof(digest));
    HBoxCrypto_Zeroize(derivedPublic, sizeof(derivedPublic));
    HBoxCrypto_Zeroize(deviceIdHash, sizeof(deviceIdHash));
    if (!valid) {
        HBoxCrypto_Zeroize(&bootContext, sizeof(bootContext));
    }
    bootContextValid = valid;
    return valid;
}

bool WebHidService::setup()
{
    shutdown();
    /*
     * WebConfig deliberately initializes before the CH585 is exposed to the
     * browser. USBDriver::prepare() has already selected the profile, but
     * USBDriver::connect() must not run until this service has installed the
     * receive callback. Otherwise an eager browser can send the first
     * attestation report into a real, credited channel while the callback is
     * still null, permanently losing that bootstrap generation.
     */
    const bool driverReady = USB_DRIVER.isPrepared();
    const bool profileReady =
        USB_DRIVER.profile() == USB_BOARD_PROFILE_WEB_CONFIG;
    const bool roleLocked = USB_BOARD_LINK.isRoleLocked();
    const bool maintenanceRole =
        USB_BOARD_LINK.role() == USB_BOARD_ROLE_MAINTENANCE;
    const bool rngReady = HBoxHardwareRng_Init();
    bool identityReady = bootContextValid || validateBootContext();
#if WEBCONFIG_TEST_FORCE_BOOT
    if (!identityReady && rngReady && prepareDevelopmentBootContext()) {
        identityReady = validateBootContext();
        APP_STAGE(identityReady ? "H00" : "H00E",
                  "temporary unlocked-development WebHID identity result=%u",
                  identityReady ? 1u : 0u);
    }
#endif
    if (!driverReady || !profileReady || !roleLocked || !maintenanceRole ||
        !identityReady || !rngReady) {
        APP_STAGE_ERROR(
            "H01",
            "WebHID prerequisites failed: driver=%u profile=%u locked=%u maintenance=%u identity=%u rng=%u",
            driverReady ? 1u : 0u,
            profileReady ? 1u : 0u,
            roleLocked ? 1u : 0u,
            maintenanceRole ? 1u : 0u,
            identityReady ? 1u : 0u,
            rngReady ? 1u : 0u);
        shutdown();
        return false;
    }

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    lastDwtCycles = DWT->CYCCNT;
    accumulatedCycles = 0u;
    bootContextValid = true;
    initialized = true;
    ConfigTransport_SetJsonSink(configJsonEvent);
    ConfigTransport_SetBinarySink(configBinaryEvent);
    UsbBoardLink_SetWebConfigReceiveCallback(webhidReportReceived);
    WEBHID_RPC_DISPATCHER.initialize();
    APP_STAGE("H01", "WebHID identity and entropy prerequisites ready");
    return true;
}

void WebHidService::shutdown()
{
    HBoxBoardSecurityConfirmation_Reset();
    UsbBoardLink_SetWebConfigReceiveCallback(nullptr);
    ConfigTransport_SetJsonSink(nullptr);
    ConfigTransport_SetBinarySink(nullptr);
    resetSession(true);
    HBoxHardwareRng_Shutdown();
    initialized = false;
}

void WebHidService::clearStream()
{
    const size_t used = std::min<size_t>(
        stream.received, kMaximumStreamBytes);
    if (used != 0u) {
        HBoxCrypto_Zeroize(stream.bytes.data(), used);
    }
    stream.bytes[used] = 0u;
    HBoxCrypto_Zeroize(
        stream.expectedHash.data(), stream.expectedHash.size());
    stream.active = false;
    stream.type = 0u;
    stream.transferId = 0u;
    stream.expectedLength = 0u;
    stream.received = 0u;
    stream.remainingCredit = 0u;
}

void WebHidService::clearBinaryCapture()
{
    captureBinary = false;
    captureBinaryInvalid = false;
    if (!capturedBinary.empty()) {
        HBoxCrypto_Zeroize(
            capturedBinary.data(), capturedBinary.size());
    }
    capturedBinary.clear();
}

void WebHidService::resetSession(bool keepBootIdentity,
                                 bool resetPhysicalTransport,
                                 bool clearQueuedReports)
{
    /*
     * Calibration and button monitoring are owned by the authenticated
     * browser session.  The legacy DeviceCommand transport stopped both from its
     * disconnect callback, but WebHID originally only discarded its crypto
     * state.  Leaving either worker alive lets ADC/event traffic from a dead
     * page interfere with the next bootstrap generation.
     *
     * Do this for every WebHID reset (not only an established session) so a
     * device left behind by an older firmware/frontend generation recovers
     * as soon as it sees the next bootstrap report.
     */
    const bool calibrationWasActive =
        ADC_CALIBRATION_MANAGER.isCalibrationActive();
    const StepInfo &markingStatus = ADC_BTNS_MARKER.getStepInfo();
    const bool markingWasActive =
        markingStatus.is_marking || markingStatus.is_sampling;
    WebConfigBtnsManager &buttons = WEBCONFIG_BTNS_MANAGER;
    const bool buttonWorkerWasActive = buttons.isActive();
    const bool buttonTestWasActive = buttons.isTestModeEnabled();
    if (calibrationWasActive) {
        (void)ADC_CALIBRATION_MANAGER.stopCalibration();
    }
    if (markingWasActive) {
        ADC_BTNS_MARKER.reset();
    }
    if (buttonTestWasActive) {
        buttons.enableTestMode(false);
    }
    if (buttonWorkerWasActive) {
        buttons.stopButtonWorkers();
    }
    if (calibrationWasActive || markingWasActive || buttonWorkerWasActive ||
        buttonTestWasActive) {
        APP_STAGE(
            "H10",
            "WebHID session runtime stopped: calibration=%u marking=%u buttons=%u test=%u",
            calibrationWasActive ? 1u : 0u,
            markingWasActive ? 1u : 0u,
            buttonWorkerWasActive ? 1u : 0u,
            buttonTestWasActive ? 1u : 0u);
    }
    if (WEBCONFIG_LEDS_MANAGER.isInPreviewMode()) {
        WEBCONFIG_LEDS_MANAGER.clearPreviewConfig();
    }
    UserImageCommandHandler::resetUploadSession();

    if (initialized && resetPhysicalTransport) {
        /*
         * A failed/ended cryptographic session invalidates any half-sent
         * 64-byte report too. UsbBoardLink performs a synchronized CH585
         * channel reset and waits for a fresh post-reset credit before
         * allowing the next bootstrap generation.
         */
        UsbBoardLink_WebConfigResetTransport();
    }
    clearFirmwareAuthorization(true);
    HBoxCrypto_Zeroize(rxKey.data(), rxKey.size());
    HBoxCrypto_Zeroize(txKey.data(), txKey.size());
    HBoxCrypto_Zeroize(
        rxNoncePrefix.data(), rxNoncePrefix.size());
    HBoxCrypto_Zeroize(
        txNoncePrefix.data(), txNoncePrefix.size());
    HBoxCrypto_Zeroize(
        deviceEphemeralPrivate.data(),
        deviceEphemeralPrivate.size());
    HBoxCrypto_Zeroize(
        deviceEphemeralPublic.data(),
        deviceEphemeralPublic.size());
    HBoxCrypto_Zeroize(
        browserEphemeralPublic.data(),
        browserEphemeralPublic.size());
    HBoxCrypto_Zeroize(
        pendingSessionId.data(), pendingSessionId.size());
    sessionEstablished = false;
    waitingForPermit = false;
    sessionActivationPending = false;
    grantedScopes = 0u;
    requestedScopes = 0u;
    firmwareActionAuthorized = false;
    permitDeadlineMs = 0u;
    lastRxSequence = 0u;
    nextTxSequence = 1u;
    pendingFrameSource = OutboundFrameSource::None;
    if (clearQueuedReports) {
        clearRxQueue();
    }
    if (assembler.length != 0u) {
        HBoxCrypto_Zeroize(
            assembler.bytes.data(), assembler.length + 1u);
    }
    assembler.active = false;
    assembler.discardingOversize = false;
    assembler.errorTransactionId = 0u;
    assembler.length = 0u;
    clearStream();
    HBoxCrypto_Zeroize(
        outboundStorage.data(), outboundStorage.size());
    outboundQueue.clear();
    outboundReadOffset = 0u;
    outboundWriteOffset = 0u;
    outboundQueuedBytes = 0u;
    g_responseScratch.fill('\0');
    for (std::string &event : eventQueue) {
        std::fill(event.begin(), event.end(), '\0');
    }
    eventQueue.clear();
    eventQueuedBytes = 0u;
    droppedEventCount = 0u;
    clearBinaryCapture();
    clearButtonStateQueue();
    buttonStateSequence = 0u;
    buttonStateDropped = 0u;
    performanceEnabled = false;
    samplePending = false;
    sampleTimestampUs = 0u;
    nextSampleAtMs = 0u;
    nextCheckpointAtMs = 0u;
    edgeSequence = 0u;
    droppedSamples = 0u;
    totalDroppedSamples = 0u;
    HBoxCrypto_Zeroize(edgeQueue.data(), sizeof(edgeQueue));
    edgeHead = 0u;
    edgeTail = 0u;
    edgeCount = 0u;
    telemetryOverflow = false;
    checkpointRequested = false;
    checkpointActive = false;
    checkpointChunk = 0u;
    HBoxCrypto_Zeroize(
        checkpointKeys.data(), sizeof(checkpointKeys));
    if (!keepBootIdentity) {
        HBoxCrypto_Zeroize(&bootContext, sizeof(bootContext));
        bootContextValid = false;
    }
}

bool WebHidService::firmwareAuthorizationValid(
    const char *sessionId,
    const uint8_t *chunk,
    size_t chunkLength) const
{
    if (!sessionEstablished ||
        !hasScope(HBOX_SCOPE_FIRMWARE_UPDATE) ||
        !firmwareActionAuthorized ||
        authorizedFirmwareSession[0] == '\0') {
        return false;
    }
    if (sessionId != nullptr &&
        strcmp(authorizedFirmwareSession.data(), sessionId) != 0) {
        return false;
    }
    if (chunk == nullptr) {
        return chunkLength == 0u;
    }
    if (chunkLength < sizeof(BinaryFirmwareChunkHeader)) {
        return false;
    }
    const auto *header =
        reinterpret_cast<const BinaryFirmwareChunkHeader *>(chunk);
    const size_t authorizedLength =
        strlen(authorizedFirmwareSession.data());
    return header->command ==
               BINARY_CMD_UPLOAD_FIRMWARE_CHUNK &&
           header->session_id_len != 0u &&
           header->session_id_len <=
               sizeof(header->session_id) &&
           header->session_id_len == authorizedLength &&
           memcmp(
               header->session_id,
               authorizedFirmwareSession.data(),
               authorizedLength) == 0;
}

void WebHidService::clearFirmwareAuthorization(
    bool abortSession)
{
    if (abortSession &&
        authorizedFirmwareSession[0] != '\0') {
        FirmwareManager *manager =
            FirmwareManager::GetInstance();
        if (manager != nullptr) {
            (void)manager->AbortUpgradeSession(
                authorizedFirmwareSession.data());
        }
    }
    HBoxCrypto_Zeroize(
        authorizedFirmwareSession.data(),
        authorizedFirmwareSession.size());
    firmwareActionAuthorized = false;
}

void WebHidService::clearRxQueue()
{
    __disable_irq();
    HBoxCrypto_Zeroize(rxQueue.data(), sizeof(rxQueue));
    rxHead = 0u;
    rxTail = 0u;
    rxCount = 0u;
    __enable_irq();
}

bool WebHidService::enqueueReport(
    const uint8_t report[WEBHID_REPORT_BYTES])
{
    if (!initialized || report == nullptr) {
        return false;
    }
    __disable_irq();
    if (rxCount >= kRxQueueDepth) {
        __enable_irq();
        resetSession(true);
        return false;
    }
    memcpy(rxQueue[rxTail].data(), report, WEBHID_REPORT_BYTES);
    rxTail = static_cast<uint8_t>((rxTail + 1u) % kRxQueueDepth);
    ++rxCount;
    __enable_irq();
    return true;
}

void WebHidService::enqueueJsonEvent(const char *json, size_t length)
{
    if (!sessionEstablished) {
        return;
    }
    if (json == nullptr || length == 0u) {
        return;
    }
    if (length > kMaximumEventBytes ||
        length > kMaximumQueuedEventBytes) {
        if (droppedEventCount != UINT32_MAX) {
            ++droppedEventCount;
        }
        return;
    }
    /*
     * JSON notifications are state hints, not request responses. When a slow
     * host falls behind, keep the newest bounded notifications and account
     * for discarded ones instead of tearing down the authenticated session.
     */
    while (!eventQueue.empty() &&
           (eventQueue.size() >= kMaximumEventQueue ||
            eventQueuedBytes > kMaximumQueuedEventBytes - length)) {
        std::string &oldest = eventQueue.front();
        eventQueuedBytes -= oldest.size();
        std::fill(oldest.begin(), oldest.end(), '\0');
        eventQueue.pop_front();
        if (droppedEventCount != UINT32_MAX) {
            ++droppedEventCount;
        }
    }
    if (eventQueue.size() >= kMaximumEventQueue ||
        eventQueuedBytes > kMaximumQueuedEventBytes - length) {
        if (droppedEventCount != UINT32_MAX) {
            ++droppedEventCount;
        }
        return;
    }
    eventQueue.emplace_back(json, length);
    eventQueuedBytes += length;
}

void WebHidService::enqueueBinaryEvent(const uint8_t *data, size_t length)
{
    if (captureBinary) {
        if (data == nullptr || length == 0u ||
            length > kMaximumStreamBytes ||
            captureBinaryInvalid || !capturedBinary.empty()) {
            if (!capturedBinary.empty()) {
                HBoxCrypto_Zeroize(
                    capturedBinary.data(), capturedBinary.size());
            }
            capturedBinary.clear();
            captureBinaryInvalid = true;
        } else {
            capturedBinary.assign(data, data + length);
        }
        return;
    }
    if (!sessionEstablished || data == nullptr || length == 0u) {
        return;
    }
    if (length != sizeof(ButtonStateBinaryData) ||
        data[0] != BUTTON_STATE_CHANGED_CMD) {
        if (droppedEventCount != UINT32_MAX) {
            ++droppedEventCount;
        }
        return;
    }
    ButtonStateBinaryData snapshot = {};
    memcpy(&snapshot, data, sizeof(snapshot));

    ++buttonStateSequence;
    if (buttonStateSequence == 0u) {
        ++buttonStateSequence;
    }

    /* A partially transferred report owns the queue head.  Under the
     * extremely unlikely condition that all 32 snapshots fill while that
     * report is pinned, replace only the newest queued snapshot.  This keeps
     * completion bookkeeping exact while ensuring the final visible mask is
     * still the most recent one. */
    bool replaceNewest = false;
    if (buttonStateCount >= kButtonStateQueueDepth) {
        if (buttonStateDropped != UINT16_MAX) {
            ++buttonStateDropped;
        }
        if (pendingFrameSource == OutboundFrameSource::ButtonState) {
            replaceNewest = true;
        } else {
            HBoxCrypto_Zeroize(
                &buttonStateQueue[buttonStateHead],
                sizeof(buttonStateQueue[buttonStateHead]));
            buttonStateHead =
                (buttonStateHead + 1u) % kButtonStateQueueDepth;
            --buttonStateCount;
        }
    }

    const size_t destination = replaceNewest
        ? (buttonStateTail + kButtonStateQueueDepth - 1u) %
              kButtonStateQueueDepth
        : buttonStateTail;
    webhid_button_state_v1_t &event =
        buttonStateQueue[destination];
    event = {};
    event.event_sequence_le = buttonStateSequence;
    event.trigger_mask_le = snapshot.triggerMask;
    event.total_dropped_snapshots_le = buttonStateDropped;
    event.total_buttons = snapshot.totalButtons;
    event.flags = snapshot.isActive != 0u
        ? WEBHID_BUTTON_STATE_FLAG_ACTIVE
        : 0u;
    if (!replaceNewest) {
        buttonStateTail =
            (buttonStateTail + 1u) % kButtonStateQueueDepth;
        ++buttonStateCount;
    }
}

void WebHidService::process()
{
    if (!initialized) {
        return;
    }
    if (!USB_DRIVER.isReady() ||
        USB_DRIVER.profile() != USB_BOARD_PROFILE_WEB_CONFIG ||
        !USB_BOARD_LINK.isRoleLocked() ||
        USB_BOARD_LINK.role() != USB_BOARD_ROLE_MAINTENANCE ||
        !USB_DRIVER.isMounted()) {
        /*
         * Startup, role/profile changes and USB unmount are external link
         * state, not protocol faults. Clear only local session-owned work;
         * repeatedly issuing CLEAR_FAULT here can race enumeration and detach
         * CH585 while the SIE is busy.
         */
        resetSession(true, false);
        return;
    }
    if (USB_DRIVER.isSuspended()) {
        /*
         * USB suspend is an idle pause. Preserve the authenticated session,
         * inbound assembly and queued responses; CH585 advertises zero credit
         * until resume, so pumpOutput() cannot advance the sequence space.
         */
        return;
    }
    if (waitingForPermit &&
        static_cast<int32_t>(
            HAL_GetTick() - permitDeadlineMs) >= 0) {
        resetSession(true);
        return;
    }

    HBoxBoardSecurityConfirmation_Poll();
    UserImageCommandHandler::pollUploadTimeout(HAL_GetTick());
    (void)monotonicMicros();
    // ADC DMA completion only raises a pending flag. Complete statistics and
    // publish marking/calibration notifications here in thread mode so cJSON,
    // std::string and WebHID queues are never touched from an IRQ.
    ADC_MANAGER.processPendingSamplingStats();
    size_t processed = 0u;
    while (rxCount != 0u && processed < kRxProcessBudget) {
        std::array<uint8_t, WEBHID_REPORT_BYTES> report;
        __disable_irq();
        memcpy(report.data(),
               rxQueue[rxHead].data(),
               WEBHID_REPORT_BYTES);
        webhid_secure_report_v1_t queuedHeader = {};
        memcpy(&queuedHeader, report.data(), sizeof(queuedHeader));
        /*
         * A complete logical request may consume the full 16 KiB response
         * arena. Leave its final report queued until the preceding response is
         * fully pumped instead of executing a side effect that cannot be ACKed.
         * Non-final fragments still advance so large requests can assemble.
         */
        if ((queuedHeader.flags & WEBHID_REPORT_FLAG_LAST) != 0u &&
            (!outboundQueue.empty() ||
             pendingFrameSource != OutboundFrameSource::None)) {
            __enable_irq();
            break;
        }
        rxHead =
            static_cast<uint8_t>((rxHead + 1u) % kRxQueueDepth);
        --rxCount;
        __enable_irq();
        const bool accepted = processReport(report.data());
        USB_BOARD_LINK.releaseWebConfigReceiveCredit();
        if (!accepted) {
            resetSession(true);
            return;
        }
        ++processed;
    }

    updateTelemetry();
    pumpOutput();
    if (DeviceCommandHandler::needReboot &&
        static_cast<int32_t>(
            HAL_GetTick() - DeviceCommandHandler::rebootTick) >= 0) {
        MainRuntime_RequestReset();
    }
}

bool WebHidService::processReport(
    const uint8_t raw[WEBHID_REPORT_BYTES])
{
    webhid_secure_report_v1_t report;
    uint8_t plaintext[WEBHID_REPORT_PAYLOAD_BYTES] = {};
    uint8_t nonce[12] = {};
    bool secure;

    memcpy(&report, raw, sizeof(report));
    if (report.version != WEBHID_PROTOCOL_VERSION ||
        report.payload_length > WEBHID_REPORT_PAYLOAD_BYTES ||
        (report.flags & ~kKnownFrameFlags) != 0u ||
        report.sequence_le == 0u) {
        return false;
    }
    for (uint8_t index = report.payload_length;
         index < WEBHID_REPORT_PAYLOAD_BYTES;
         ++index) {
        if (report.payload[index] != 0u) {
            return false;
        }
    }
    secure =
        (report.flags & WEBHID_REPORT_FLAG_ENCRYPTED) != 0u;
    /*
     * A browser that disappeared cannot send session.end, so a later page
     * starts at cleartext bootstrap sequence 1 while the old encrypted
     * session is still resident. Treat that structurally valid first frame as
     * an explicit local session takeover: discard the old keys and logical
     * queues, but keep the CH585 endpoint queues intact so the host's current
     * HID sendReport() is allowed to complete. Resetting the physical board
     * link here would CLEAR_FAULT the very OUT transfer carrying this frame,
     * leaving Windows WebHID pending forever.
     */
    if (!secure &&
        report.type == WEBHID_REPORT_BOOTSTRAP_REQUEST &&
        report.sequence_le == 1u &&
        allZero(report.tag, sizeof(report.tag))) {
        /*
         * Sequence one is the generation boundary in every cleartext state,
         * including a lost public-info/attestation response, waitingForPermit,
         * sessionActivationPending and an established stale session. Reports
         * already queued behind it belong to this same new generation. Keep
         * them as well as the physical endpoint; otherwise a fragmented
         * attestation request loses sequences 2..N.
         */
        resetSession(true, false, false);
    }
    if (lastRxSequence != 0u &&
        report.sequence_le != lastRxSequence + 1u) {
        return false;
    }
    if (secure != sessionEstablished) {
        return false;
    }
    if (secure) {
        makeNonce(rxNoncePrefix, report.sequence_le, nonce);
        if (HBoxCrypto_Aes256GcmDecrypt(
                rxKey.data(),
                nonce,
                raw,
                WEBHID_REPORT_HEADER_BYTES,
                report.payload,
                report.payload_length,
                report.tag,
                plaintext) != 0) {
            return false;
        }
    } else {
        if (report.type != WEBHID_REPORT_BOOTSTRAP_REQUEST ||
            !allZero(report.tag, sizeof(report.tag))) {
            return false;
        }
        memcpy(plaintext, report.payload, report.payload_length);
    }
    lastRxSequence = report.sequence_le;

    if (report.type == WEBHID_REPORT_STREAM_FRAGMENT) {
        return secure &&
               processStreamFragment(
                   plaintext, report.payload_length);
    }
    if (report.type == WEBHID_REPORT_IMAGE_DATA) {
        return secure &&
               processImageData(
                   report.flags,
                   plaintext,
                   report.payload_length);
    }
    if ((!secure &&
         report.type != WEBHID_REPORT_BOOTSTRAP_REQUEST) ||
        (secure &&
         report.type != WEBHID_REPORT_SECURE_REQUEST)) {
        return false;
    }
    return acceptLogicalFragment(report.type,
                                 secure,
                                 report.flags,
                                 plaintext,
                                 report.payload_length);
}

bool WebHidService::acceptLogicalFragment(
    uint8_t type,
    bool secure,
    uint8_t flags,
    const uint8_t *payload,
    uint8_t length)
{
    const bool fragmented =
        (flags & WEBHID_REPORT_FLAG_FRAGMENTED) != 0u;
    const bool last = (flags & WEBHID_REPORT_FLAG_LAST) != 0u;
    if (!last && !fragmented) {
        return false;
    }
    if (!assembler.active) {
        assembler.active = true;
        assembler.type = type;
        assembler.secure = secure;
        assembler.discardingOversize = false;
        assembler.errorTransactionId = 0u;
        assembler.length = 0u;
    } else if (!fragmented || assembler.type != type ||
               assembler.secure != secure) {
        return false;
    }
    if (assembler.discardingOversize) {
        if (!last) {
            return true;
        }
        const uint32_t transactionId =
            assembler.errorTransactionId;
        const bool wasSecure = assembler.secure;
        if (assembler.length != 0u) {
            HBoxCrypto_Zeroize(
                assembler.bytes.data(), assembler.length + 1u);
        }
        assembler.active = false;
        assembler.discardingOversize = false;
        assembler.errorTransactionId = 0u;
        assembler.length = 0u;
        return wasSecure
            ? sendRpcResult(
                  transactionId,
                  413,
                  nullptr,
                  "Logical request exceeds the 16 KiB limit")
            : sendResponse(
                  WEBHID_REPORT_BOOTSTRAP_RESPONSE,
                  false,
                  transactionId,
                  413,
                  nullptr,
                  "Bootstrap request exceeds the 16 KiB limit");
    }
    if (length > kMaximumLogicalBytes - assembler.length) {
        const size_t remaining =
            kMaximumLogicalBytes - assembler.length;
        if (remaining != 0u) {
            memcpy(assembler.bytes.data() + assembler.length,
                   payload,
                   remaining);
            assembler.length += remaining;
        }
        assembler.bytes[assembler.length] = 0u;
        (void)extractTransactionId(
            assembler.bytes.data(),
            assembler.length,
            assembler.errorTransactionId);
        assembler.discardingOversize = true;
        if (!last) {
            return true;
        }
        const uint32_t transactionId =
            assembler.errorTransactionId;
        const bool wasSecure = assembler.secure;
        HBoxCrypto_Zeroize(
            assembler.bytes.data(), assembler.length + 1u);
        assembler.active = false;
        assembler.discardingOversize = false;
        assembler.errorTransactionId = 0u;
        assembler.length = 0u;
        return wasSecure
            ? sendRpcResult(
                  transactionId,
                  413,
                  nullptr,
                  "Logical request exceeds the 16 KiB limit")
            : sendResponse(
                  WEBHID_REPORT_BOOTSTRAP_RESPONSE,
                  false,
                  transactionId,
                  413,
                  nullptr,
                  "Bootstrap request exceeds the 16 KiB limit");
    }
    memcpy(assembler.bytes.data() + assembler.length,
           payload,
           length);
    assembler.length += length;
    if (fragmented && !last) {
        return true;
    }
    const size_t completeLength = assembler.length;
    assembler.bytes[completeLength] = 0u;
    assembler.active = false;
    const bool result =
        secure ? processSecureRpc(assembler.bytes.data(), completeLength)
               : processBootstrap(assembler.bytes.data(), completeLength);
    HBoxCrypto_Zeroize(
        assembler.bytes.data(), completeLength + 1u);
    assembler.length = 0u;
    return result;
}

bool WebHidService::processBootstrap(
    const uint8_t *message,
    size_t length)
{
    if (message == nullptr || length == 0u ||
        length > kMaximumLogicalBytes ||
        std::find(message, message + length, 0u) !=
            message + length) {
        return false;
    }
    cJSON *root = cJSON_Parse(
        reinterpret_cast<const char *>(message));
    if (root == nullptr || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    uint32_t transactionId = 0u;
    cJSON *transaction =
        cJSON_GetObjectItemCaseSensitive(root, "transactionId");
    cJSON *command =
        cJSON_GetObjectItemCaseSensitive(root, "command");
    cJSON *params =
        cJSON_GetObjectItemCaseSensitive(root, "params");
    if (!parseU32(transaction, transactionId) ||
        transactionId == 0u ||
        !cJSON_IsString(command) ||
        command->valuestring == nullptr ||
        strlen(command->valuestring) > 64u ||
        (params != nullptr && !cJSON_IsObject(params))) {
        cJSON_Delete(root);
        return false;
    }

    bool result = false;
    if (strcmp(command->valuestring, "attestation.create") == 0) {
        result = handleAttestationCreate(transactionId, params);
    } else if (strcmp(command->valuestring,
                      "session.install-permit") == 0) {
        result = handleInstallPermit(transactionId, params);
    } else if (strcmp(command->valuestring,
                      "device.public-info") == 0) {
        uint8_t certificateHash[32] = {};
        char version[17] = {};
        cJSON *data = cJSON_CreateObject();
        if (data != nullptr &&
            HBoxCrypto_Sha256(
                reinterpret_cast<const uint8_t *>(
                    &bootContext.device_certificate),
                sizeof(bootContext.device_certificate),
                certificateHash) == 0) {
            const std::string fingerprint =
                encodeHex(certificateHash,
                          sizeof(certificateHash),
                          false);
            cJSON_AddStringToObject(
                data, "model", DEVICE_MODEL_STRING);
            cJSON_AddNumberToObject(
                data, "protocolVersion", WEBHID_PROTOCOL_VERSION);
            cJSON_AddStringToObject(
                data,
                "hardwareVersion",
                HARDWARE_VERSION_STRING);
            cJSON_AddStringToObject(
                data,
                "firmwareVersion",
                safeFirmwareVersion(bootContext, version));
            cJSON_AddStringToObject(
                data,
                "certificateFingerprint",
                fingerprint.c_str());
            cJSON_AddStringToObject(
                data, "authenticationState", "required");
            result = sendResponse(
                WEBHID_REPORT_BOOTSTRAP_RESPONSE,
                false,
                transactionId,
                0,
                data);
        } else {
            result = sendBootstrapError(
                transactionId, "Public device information unavailable");
        }
        HBoxCrypto_Zeroize(
            certificateHash, sizeof(certificateHash));
        cJSON_Delete(data);
    } else {
        result = sendBootstrapError(
            transactionId, "Bootstrap command is not allowed");
    }
    cJSON_Delete(root);
    return result;
}

bool WebHidService::handleAttestationCreate(
    uint32_t transactionId,
    void *opaqueParams)
{
    cJSON *params = static_cast<cJSON *>(opaqueParams);
    cJSON *challengeIdItem = params == nullptr
        ? nullptr
        : cJSON_GetObjectItemCaseSensitive(
              params, "challengeId");
    cJSON *challengeNonceItem = params == nullptr
        ? nullptr
        : cJSON_GetObjectItemCaseSensitive(
              params, "challengeNonce");
    cJSON *browserKeyItem = params == nullptr
        ? nullptr
        : cJSON_GetObjectItemCaseSensitive(
              params, "browserEphemeralPublicKey");
    cJSON *scopesItem = params == nullptr
        ? nullptr
        : cJSON_GetObjectItemCaseSensitive(
              params, "requestedScopes");
    std::vector<uint8_t> challengeId;
    std::vector<uint8_t> challengeNonce;
    std::vector<uint8_t> browserKey;
    uint32_t scopeMask = 0u;
    if (!cJSON_IsString(challengeIdItem) ||
        !cJSON_IsString(challengeNonceItem) ||
        !cJSON_IsString(browserKeyItem) ||
        !canonicalBase64Url(
            challengeIdItem->valuestring,
            challengeId,
            HBOX_SECURITY_ID_BYTES) ||
        !decodeBase64(
            challengeNonceItem->valuestring,
            challengeNonce,
            HBOX_SECURITY_NONCE_BYTES,
            false) ||
        !decodeBase64(
            browserKeyItem->valuestring,
            browserKey,
            HBOX_SECURITY_P256_PUBLIC_KEY_BYTES,
            false) ||
        browserKey[0] != 0x04u ||
        !parseScopeArray(scopesItem, scopeMask)) {
        return sendBootstrapError(
            transactionId, "Invalid attestation request");
    }

    /*
     * Validate an attacker-controlled ECDH point without involving Kboot.
     * Kboot is a per-boot attestation signing key and must never be reused as
     * an ECDH scalar.  The independently generated device ephemeral key below
     * is the only private key used for the session key agreement.
     */
    if (HBoxCrypto_P256ValidatePublicKey(browserKey.data()) != 0) {
        return sendBootstrapError(
            transactionId, "Invalid browser ephemeral key");
    }

    HBoxCrypto_Zeroize(
        deviceEphemeralPrivate.data(),
        deviceEphemeralPrivate.size());
    HBoxCrypto_Zeroize(
        deviceEphemeralPublic.data(),
        deviceEphemeralPublic.size());
    HBoxCrypto_Zeroize(
        browserEphemeralPublic.data(),
        browserEphemeralPublic.size());
    HBoxCrypto_Zeroize(
        pendingSessionId.data(), pendingSessionId.size());

    hbox_attestation_transcript_v1_t transcript = {};
    uint8_t digest[32] = {};
    bool generated =
        HBoxCrypto_P256Generate(
            deviceEphemeralPrivate.data(),
            deviceEphemeralPublic.data(),
            HBoxHardwareRng_Fill,
            nullptr) == 0 &&
        HBoxHardwareRng_Fill(
            nullptr,
            pendingSessionId.data(),
            pendingSessionId.size()) == 0 &&
        !allZero(pendingSessionId.data(),
                 pendingSessionId.size());
    if (!generated) {
        HBoxCrypto_Zeroize(&transcript, sizeof(transcript));
        HBoxCrypto_Zeroize(digest, sizeof(digest));
        return sendBootstrapError(
            transactionId, "Ephemeral key generation failed");
    }

    memcpy(browserEphemeralPublic.data(),
           browserKey.data(),
           browserEphemeralPublic.size());
    requestedScopes = scopeMask;
    transcript.magic_le = HBOX_ATTESTATION_TRANSCRIPT_MAGIC;
    transcript.version = HBOX_SECURITY_PROTOCOL_VERSION;
    transcript.protocol_version = WEBHID_PROTOCOL_VERSION;
    transcript.signed_bytes_le =
        HBOX_ATTESTATION_TRANSCRIPT_SIGNED_BYTES;
    memcpy(transcript.challenge_id,
           challengeId.data(),
           sizeof(transcript.challenge_id));
    memcpy(transcript.server_nonce,
           challengeNonce.data(),
           sizeof(transcript.server_nonce));
    memcpy(transcript.webhid_session_id,
           pendingSessionId.data(),
           sizeof(transcript.webhid_session_id));
    transcript.requested_scopes_le = requestedScopes;
    memcpy(transcript.device_id,
           bootContext.device_certificate.device_id,
           sizeof(transcript.device_id));
    memcpy(transcript.boot_nonce,
           bootContext.boot_attestation.boot_nonce,
           sizeof(transcript.boot_nonce));
    memcpy(transcript.browser_ephemeral_public_key,
           browserEphemeralPublic.data(),
           browserEphemeralPublic.size());
    memcpy(transcript.device_ephemeral_public_key,
           deviceEphemeralPublic.data(),
           deviceEphemeralPublic.size());
    memcpy(transcript.firmware_hash,
           bootContext.boot_attestation.firmware_hash,
           sizeof(transcript.firmware_hash));
    transcript.security_version_le =
        bootContext.boot_attestation.security_version_le;

    if (HBoxCrypto_Sha256(
            reinterpret_cast<const uint8_t *>(&transcript),
            HBOX_ATTESTATION_TRANSCRIPT_SIGNED_BYTES,
            digest) != 0 ||
        HBoxCrypto_P256SignDigest(
            bootContext.boot_private_key,
            digest,
            transcript.boot_signature,
            HBoxHardwareRng_Fill,
            nullptr) != 0) {
        HBoxCrypto_Zeroize(&transcript, sizeof(transcript));
        HBoxCrypto_Zeroize(digest, sizeof(digest));
        return sendBootstrapError(
            transactionId, "Boot attestation signing failed");
    }

    const std::string certificate = encodeBase64(
        reinterpret_cast<const uint8_t *>(
            &bootContext.device_certificate),
        sizeof(bootContext.device_certificate));
    const std::string bootAttestation = encodeBase64(
        reinterpret_cast<const uint8_t *>(
            &bootContext.boot_attestation),
        sizeof(bootContext.boot_attestation));
    const std::string bootNonce = encodeBase64(
        bootContext.boot_attestation.boot_nonce,
        sizeof(bootContext.boot_attestation.boot_nonce));
    const std::string ephemeralKey = encodeBase64(
        deviceEphemeralPublic.data(),
        deviceEphemeralPublic.size());
    const std::string signedTranscript = encodeBase64(
        reinterpret_cast<const uint8_t *>(&transcript),
        sizeof(transcript));
    const std::string deviceId = encodeHex(
        bootContext.device_certificate.device_id,
        sizeof(bootContext.device_certificate.device_id),
        true);
    const std::string firmwareMeasurement = encodeHex(
        bootContext.boot_attestation.firmware_hash,
        sizeof(bootContext.boot_attestation.firmware_hash),
        false);

    const uint32_t hardwareCode =
        bootContext.device_certificate.hardware_version_le;
    char hardwareVersion[16] = {};
    char firmwareVersion[17] = {};
    snprintf(hardwareVersion,
             sizeof(hardwareVersion),
             "%lu.%lu.%lu",
             static_cast<unsigned long>(
                 (hardwareCode >> 16u) & 0xFFu),
             static_cast<unsigned long>(
                 (hardwareCode >> 8u) & 0xFFu),
             static_cast<unsigned long>(
                 hardwareCode & 0xFFu));

    cJSON *data = cJSON_CreateObject();
    bool result = false;
    if (data != nullptr &&
        !certificate.empty() &&
        !bootAttestation.empty() &&
        !bootNonce.empty() &&
        !ephemeralKey.empty() &&
        !signedTranscript.empty()) {
        cJSON_AddStringToObject(
            data, "deviceId", deviceId.c_str());
        cJSON_AddStringToObject(
            data, "certificate", certificate.c_str());
        cJSON_AddStringToObject(
            data, "bootAttestation", bootAttestation.c_str());
        cJSON_AddStringToObject(
            data, "bootNonce", bootNonce.c_str());
        cJSON_AddStringToObject(
            data,
            "deviceEphemeralPublicKey",
            ephemeralKey.c_str());
        cJSON_AddStringToObject(
            data,
            "firmwareMeasurement",
            firmwareMeasurement.c_str());
        cJSON_AddStringToObject(
            data, "hardwareVersion", hardwareVersion);
        cJSON_AddStringToObject(
            data,
            "firmwareVersion",
            safeFirmwareVersion(
                bootContext, firmwareVersion));
        /*
         * The compatibility field name is locked by the first browser
         * client.  It carries the complete signed transcript, not only r||s.
         */
        cJSON_AddStringToObject(
            data, "signature", signedTranscript.c_str());
        result = sendResponse(
            WEBHID_REPORT_BOOTSTRAP_RESPONSE,
            false,
            transactionId,
            0,
            data);
    }
    cJSON_Delete(data);
    HBoxCrypto_Zeroize(&transcript, sizeof(transcript));
    HBoxCrypto_Zeroize(digest, sizeof(digest));
    if (!result) {
        return false;
    }
    waitingForPermit = true;
    permitDeadlineMs = HAL_GetTick() + kPermitInstallTimeoutMs;
    return true;
}

bool WebHidService::handleInstallPermit(
    uint32_t transactionId,
    void *opaqueParams)
{
    cJSON *params = static_cast<cJSON *>(opaqueParams);
    cJSON *sessionItem = params == nullptr
        ? nullptr
        : cJSON_GetObjectItemCaseSensitive(params, "sessionId");
    cJSON *permitItem = params == nullptr
        ? nullptr
        : cJSON_GetObjectItemCaseSensitive(params, "permit");
    std::vector<uint8_t> sessionId;
    std::vector<uint8_t> permit;
    if (!waitingForPermit ||
        static_cast<int32_t>(
            HAL_GetTick() - permitDeadlineMs) >= 0 ||
        !cJSON_IsString(sessionItem) ||
        !cJSON_IsString(permitItem) ||
        !canonicalBase64Url(
            sessionItem->valuestring,
            sessionId,
            HBOX_SECURITY_ID_BYTES) ||
        !decodeBase64(
            permitItem->valuestring,
            permit,
            sizeof(hbox_device_session_permit_v1_t),
            false)) {
        return sendBootstrapError(
            transactionId, "Invalid or expired session permit");
    }

    uint32_t scopes = 0u;
    uint8_t permitHash[32] = {};
    if (!verifyPermit(
            permit.data(),
            permit.size(),
            sessionId.data(),
            scopes) ||
        HBoxCrypto_Sha256(
            permit.data(), permit.size(), permitHash) != 0 ||
        !installSessionKeys(
            permitHash,
            browserEphemeralPublic.data(),
            sessionItem->valuestring)) {
        HBoxCrypto_Zeroize(permitHash, sizeof(permitHash));
        return sendBootstrapError(
            transactionId, "Session permit verification failed");
    }
    HBoxCrypto_Zeroize(permitHash, sizeof(permitHash));

    cJSON *data = cJSON_CreateObject();
    if (data == nullptr) {
        resetSession(true);
        return false;
    }
    cJSON_AddBoolToObject(data, "accepted", true);
    cJSON_AddStringToObject(
        data, "sessionId", sessionItem->valuestring);
    /*
     * This ACK must be completely delivered without encryption.  The browser
     * installs its cipher only after receiving it.
     */
    const bool acknowledged = sendResponse(
        WEBHID_REPORT_BOOTSTRAP_RESPONSE,
        false,
        transactionId,
        0,
        data);
    cJSON_Delete(data);
    if (!acknowledged || outboundQueue.empty() ||
        outboundQueue.back().type !=
            WEBHID_REPORT_BOOTSTRAP_RESPONSE ||
        outboundQueue.back().secure) {
        resetSession(true);
        return false;
    }

    outboundQueue.back().activateSession = true;
    grantedScopes = scopes;
    sessionActivationPending = true;
    HBoxCrypto_Zeroize(
        deviceEphemeralPrivate.data(),
        deviceEphemeralPrivate.size());
    HBoxCrypto_Zeroize(
        browserEphemeralPublic.data(),
        browserEphemeralPublic.size());
    HBoxCrypto_Zeroize(
        pendingSessionId.data(), pendingSessionId.size());
    return true;
}

bool WebHidService::verifyPermit(
    const uint8_t *permitBytes,
    size_t length,
    const uint8_t sessionId[16],
    uint32_t &scopes)
{
    if (permitBytes == nullptr ||
        length != sizeof(hbox_device_session_permit_v1_t) ||
        sessionId == nullptr) {
        return false;
    }
    hbox_device_session_permit_v1_t permit = {};
    memcpy(&permit, permitBytes, sizeof(permit));
    if (permit.magic_le != HBOX_SESSION_PERMIT_MAGIC ||
        permit.version != HBOX_SECURITY_PROTOCOL_VERSION ||
        permit.signed_bytes_le !=
            HBOX_SESSION_PERMIT_SIGNED_BYTES ||
        permit.signing_key_slot >=
            HBOX_WEBCONFIG_AUTH_KEY_SLOT_COUNT ||
        (HBOX_WEBCONFIG_AUTH_KEY_PROVISIONED_MASK &
         (1u << permit.signing_key_slot)) == 0u ||
        allZero(
            HBOX_WEBCONFIG_AUTHORIZATION_PUBLIC_KEYS[
                permit.signing_key_slot],
            HBOX_SECURITY_P256_PUBLIC_KEY_BYTES) ||
        allZero(permit.permit_id, sizeof(permit.permit_id)) ||
        !constantTimeEqual(
            permit.session_id,
            sessionId,
            sizeof(permit.session_id)) ||
        !constantTimeEqual(
            permit.session_id,
            pendingSessionId.data(),
            pendingSessionId.size()) ||
        !constantTimeEqual(
            permit.device_id,
            bootContext.device_certificate.device_id,
            sizeof(permit.device_id)) ||
        !constantTimeEqual(
            permit.boot_nonce,
            bootContext.boot_attestation.boot_nonce,
            sizeof(permit.boot_nonce))) {
        HBoxCrypto_Zeroize(&permit, sizeof(permit));
        return false;
    }

    uint8_t browserHash[32] = {};
    uint8_t deviceHash[32] = {};
    uint8_t digest[32] = {};
    const bool signaturesValid =
        HBoxCrypto_Sha256(
            browserEphemeralPublic.data(),
            browserEphemeralPublic.size(),
            browserHash) == 0 &&
        HBoxCrypto_Sha256(
            deviceEphemeralPublic.data(),
            deviceEphemeralPublic.size(),
            deviceHash) == 0 &&
        constantTimeEqual(
            browserHash,
            permit.browser_public_key_hash,
            sizeof(browserHash)) &&
        constantTimeEqual(
            deviceHash,
            permit.device_public_key_hash,
            sizeof(deviceHash)) &&
        HBoxCrypto_Sha256(
            permitBytes,
            HBOX_SESSION_PERMIT_SIGNED_BYTES,
            digest) == 0 &&
        HBoxCrypto_P256VerifyDigest(
            HBOX_WEBCONFIG_AUTHORIZATION_PUBLIC_KEYS[
                permit.signing_key_slot],
            digest,
            permit.server_signature) == 0;
    HBoxCrypto_Zeroize(browserHash, sizeof(browserHash));
    HBoxCrypto_Zeroize(deviceHash, sizeof(deviceHash));
    HBoxCrypto_Zeroize(digest, sizeof(digest));
    if (!signaturesValid) {
        HBoxCrypto_Zeroize(&permit, sizeof(permit));
        return false;
    }

    scopes = permit.granted_scopes_le;
    const bool policyValid =
        scopes != 0u &&
        (scopes & ~HBOX_SCOPE_ALL) == 0u &&
        (scopes & requestedScopes) == scopes &&
        permit.max_duration_ms_le == 0u &&
        permit.issued_at_le == 0u &&
        permit.expires_at_le == 0u &&
        permit.policy_version_le != 0u;
    HBoxCrypto_Zeroize(&permit, sizeof(permit));
    return policyValid;
}

bool WebHidService::installSessionKeys(
    const uint8_t permitHash[32],
    const uint8_t peerPublicKey[65],
    const std::string &sessionId)
{
    static const char contextPrefix[] = "HBox WebHID v1";
    static const char browserDirection[] = "browser-to-device";
    static const char deviceDirection[] = "device-to-browser";
    uint8_t sharedSecret[32] = {};
    std::vector<uint8_t> context(
        contextPrefix,
        contextPrefix + sizeof(contextPrefix) - 1u);
    context.push_back(0u);
    context.insert(
        context.end(), sessionId.begin(), sessionId.end());

    auto makeInfo =
        [&context](const char *direction, bool nonce) {
            std::vector<uint8_t> info = context;
            info.push_back(0u);
            info.insert(
                info.end(),
                direction,
                direction + strlen(direction));
            if (nonce) {
                static const char suffix[] = "nonce";
                info.push_back(0u);
                info.insert(
                    info.end(),
                    suffix,
                    suffix + sizeof(suffix) - 1u);
            }
            return info;
        };

    if (HBoxCrypto_P256Ecdh(
            deviceEphemeralPrivate.data(),
            peerPublicKey,
            sharedSecret,
            HBoxHardwareRng_Fill,
            nullptr) != 0) {
        HBoxCrypto_Zeroize(sharedSecret, sizeof(sharedSecret));
        return false;
    }
    const std::vector<uint8_t> rxKeyInfo =
        makeInfo(browserDirection, false);
    const std::vector<uint8_t> txKeyInfo =
        makeInfo(deviceDirection, false);
    const std::vector<uint8_t> rxNonceInfo =
        makeInfo(browserDirection, true);
    const std::vector<uint8_t> txNonceInfo =
        makeInfo(deviceDirection, true);
    const bool derived =
        HBoxCrypto_HkdfSha256(
            permitHash,
            32u,
            sharedSecret,
            sizeof(sharedSecret),
            rxKeyInfo.data(),
            rxKeyInfo.size(),
            rxKey.data(),
            rxKey.size()) == 0 &&
        HBoxCrypto_HkdfSha256(
            permitHash,
            32u,
            sharedSecret,
            sizeof(sharedSecret),
            txKeyInfo.data(),
            txKeyInfo.size(),
            txKey.data(),
            txKey.size()) == 0 &&
        HBoxCrypto_HkdfSha256(
            permitHash,
            32u,
            sharedSecret,
            sizeof(sharedSecret),
            rxNonceInfo.data(),
            rxNonceInfo.size(),
            rxNoncePrefix.data(),
            rxNoncePrefix.size()) == 0 &&
        HBoxCrypto_HkdfSha256(
            permitHash,
            32u,
            sharedSecret,
            sizeof(sharedSecret),
            txNonceInfo.data(),
            txNonceInfo.size(),
            txNoncePrefix.data(),
            txNoncePrefix.size()) == 0;
    HBoxCrypto_Zeroize(sharedSecret, sizeof(sharedSecret));
    if (!derived) {
        HBoxCrypto_Zeroize(rxKey.data(), rxKey.size());
        HBoxCrypto_Zeroize(txKey.data(), txKey.size());
        HBoxCrypto_Zeroize(
            rxNoncePrefix.data(), rxNoncePrefix.size());
        HBoxCrypto_Zeroize(
            txNoncePrefix.data(), txNoncePrefix.size());
    }
    return derived;
}

bool WebHidService::processSecureRpc(
    const uint8_t *message,
    size_t length)
{
    if (!sessionEstablished ||
        message == nullptr || length == 0u ||
        length > kMaximumLogicalBytes ||
        std::find(message, message + length, 0u) !=
            message + length) {
        return false;
    }
    cJSON *root = cJSON_Parse(
        reinterpret_cast<const char *>(message));
    if (root == nullptr || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    uint32_t transactionId = 0u;
    cJSON *transaction =
        cJSON_GetObjectItemCaseSensitive(root, "transactionId");
    cJSON *commandItem =
        cJSON_GetObjectItemCaseSensitive(root, "command");
    cJSON *params =
        cJSON_GetObjectItemCaseSensitive(root, "params");
    if (!parseU32(transaction, transactionId) ||
        transactionId == 0u ||
        !cJSON_IsString(commandItem) ||
        commandItem->valuestring == nullptr ||
        strlen(commandItem->valuestring) > 96u ||
        (params != nullptr && !cJSON_IsObject(params))) {
        cJSON_Delete(root);
        return false;
    }

    const std::string command(commandItem->valuestring);
    const bool createFirmware =
        command == "create_firmware_upgrade_session" ||
        command == "ch585_update_begin";
    const bool completeFirmware =
        command == "complete_firmware_upgrade_session" ||
        command == "ch585_update_complete";
    const bool uploadFirmware =
        command == "upload_firmware_chunk" ||
        command == "ch585_update_chunk";
    const bool abortFirmware =
        command == "abort_firmware_upgrade_session";
    const bool statusFirmware =
        command == "get_firmware_upgrade_status" ||
        command == "ch585_update_status";
    const bool cleanupFirmware =
        command == "cleanup_firmware_upgrade_session";
    const char *firmwareSessionId = nullptr;
    std::array<char, 33u> firmwareSessionIdStorage = {};
    if (createFirmware || completeFirmware || uploadFirmware ||
        abortFirmware || statusFirmware || cleanupFirmware) {
        cJSON *sessionItem = params == nullptr
            ? nullptr
            : cJSON_GetObjectItemCaseSensitive(
                  static_cast<cJSON *>(params), "session_id");
        if (cJSON_IsString(sessionItem) &&
            sessionItem->valuestring != nullptr) {
            firmwareSessionId = sessionItem->valuestring;
            const size_t sessionLength = strlen(firmwareSessionId);
            if (sessionLength < firmwareSessionIdStorage.size()) {
                memcpy(firmwareSessionIdStorage.data(),
                       firmwareSessionId,
                       sessionLength + 1u);
                firmwareSessionId = firmwareSessionIdStorage.data();
            }
        }
    }
    if (createFirmware) {
        if (!HBoxBoard_DangerousActionConfirmed() ||
            firmwareSessionId == nullptr ||
            firmwareSessionId[0] == '\0' ||
            strlen(firmwareSessionId) >
                authorizedFirmwareSession.size() - 1u ||
            authorizedFirmwareSession[0] != '\0') {
            cJSON_Delete(root);
            return sendRpcResult(
                transactionId,
                423,
                nullptr,
                "Release, then hold GPIO1+FN for 2 seconds");
        }
    } else if ((completeFirmware || uploadFirmware ||
                abortFirmware || statusFirmware ||
                cleanupFirmware) &&
               (firmwareSessionId == nullptr ||
                !firmwareAuthorizationValid(
                    firmwareSessionId))) {
            cJSON_Delete(root);
            return sendRpcResult(
                transactionId,
                423,
                nullptr,
                "Firmware update confirmation or session is invalid");
    } else if (command == "reboot" &&
               !HBoxBoard_DangerousActionConfirmed()) {
        cJSON_Delete(root);
        return sendRpcResult(
            transactionId,
            423,
            nullptr,
            "Release, then hold GPIO1+FN for 2 seconds");
    }
    bool handled = false;
    bool result = handleSpecialRpc(
        root, transactionId, command, params, handled);
    if (handled) {
        cJSON_Delete(root);
        return result;
    }

    WebHidRpcResult dispatched =
        WEBHID_RPC_DISPATCHER.dispatch(root, grantedScopes);
    if (dispatched.json == nullptr || dispatched.jsonLength == 0u) {
        WEBHID_RPC_DISPATCHER.clearSerializedResponse();
        cJSON_Delete(root);
        return sendRpcResult(
            transactionId,
            dispatched.error > 0 ? dispatched.error : 500,
            nullptr,
            dispatched.failureMessage != nullptr
                ? dispatched.failureMessage
                : "RPC dispatcher did not return a response");
    }
    const bool responseOversized =
        dispatched.jsonLength > kMaximumLogicalBytes;
    result = responseOversized
        ? sendRpcResult(
              transactionId,
              413,
              nullptr,
              "RPC response exceeds the 16 KiB limit")
        : sendLogical(
              WEBHID_REPORT_SECURE_RESPONSE,
              reinterpret_cast<const uint8_t *>(dispatched.json),
              dispatched.jsonLength,
              true);
    WEBHID_RPC_DISPATCHER.clearSerializedResponse();
    const bool explicitSuccess =
        !responseOversized && dispatched.explicitSuccess;
    if (createFirmware) {
        if (result && explicitSuccess) {
            strncpy(
                authorizedFirmwareSession.data(),
                firmwareSessionId,
                authorizedFirmwareSession.size() - 1u);
            authorizedFirmwareSession.back() = '\0';
            firmwareActionAuthorized = true;
        } else if (explicitSuccess) {
            FirmwareManager *manager =
                FirmwareManager::GetInstance();
            if (manager != nullptr) {
                (void)manager->AbortUpgradeSession(
                    firmwareSessionId);
            }
        }
    /*
     * Keep the exact session authorization after a successful COMPLETE until
     * the scheduled reset/session teardown. If the encrypted response is lost,
     * the host can retry and reach FirmwareManager's idempotent COMPLETED path.
     * Abort and cleanup are the only explicit terminal commands that revoke it
     * immediately.
     */
    } else if ((abortFirmware || cleanupFirmware) &&
               explicitSuccess) {
        clearFirmwareAuthorization(false);
    }
    if (result && dispatched.error == 0) {
        if (command == "start_button_monitoring" ||
            command == "stop_button_monitoring") {
            /* A new ordinary-monitor lease must never inherit snapshots from
             * the previous owner.  The command response is already queued,
             * and process() never dispatches a final request while a physical
             * button-state report is pinned, so clearing here is exact. */
            clearButtonStateQueue();
        } else if (command ==
            "start_button_performance_monitoring") {
            /* Ordinary and performance monitoring share the same workers but
             * use different report schemas.  Do not leak an ordinary snapshot
             * across the mode boundary after the performance ACK. */
            clearButtonStateQueue();
            performanceEnabled = true;
            nextSampleAtMs = HAL_GetTick();
            nextCheckpointAtMs = HAL_GetTick();
            samplePending = false;
            droppedSamples = 0u;
            totalDroppedSamples = 0u;
            edgeHead = 0u;
            edgeTail = 0u;
            edgeCount = 0u;
            telemetryOverflow = false;
            HBoxCrypto_Zeroize(
                edgeQueue.data(), sizeof(edgeQueue));
            requestCheckpoint();
        } else if (
            command ==
            "stop_button_performance_monitoring") {
            performanceEnabled = false;
            samplePending = false;
            checkpointRequested = false;
            checkpointActive = false;
            edgeHead = 0u;
            edgeTail = 0u;
            edgeCount = 0u;
            HBoxCrypto_Zeroize(
                edgeQueue.data(), sizeof(edgeQueue));
        }
    }
    cJSON_Delete(root);
    return result;
}

bool WebHidService::handleSpecialRpc(
    void *opaqueRoot,
    uint32_t transactionId,
    const std::string &command,
    void *params,
    bool &handled)
{
    (void)opaqueRoot;
    handled = true;
    if (command == "binary.exchange") {
        return handleBinaryExchange(transactionId, params);
    }
    if (command == "stream.begin" ||
        command == "stream.credit" ||
        command == "stream.complete" ||
        command == "stream.abort") {
        return handleStreamRpc(
            transactionId, command, params);
    }
    if (command == "performance.get-checkpoint") {
        if (!hasScope(HBOX_SCOPE_MONITOR_READ)) {
            return sendRpcResult(
                transactionId,
                403,
                nullptr,
                "monitor.read scope required");
        }
        cJSON *data = cJSON_CreateObject();
        if (data == nullptr) {
            return false;
        }
        cJSON_AddBoolToObject(data, "queued", true);
        const bool responseSent =
            sendRpcResult(transactionId, 0, data);
        cJSON_Delete(data);
        if (!responseSent) {
            return false;
        }
        requestCheckpoint();
        return true;
    }
    if (command == "performance.clock-sync") {
        if (!hasScope(HBOX_SCOPE_MONITOR_READ)) {
            return sendRpcResult(
                transactionId,
                403,
                nullptr,
                "monitor.read scope required");
        }
        uint32_t sampleId = 0u;
        if (params == nullptr ||
            !parseU32(
                cJSON_GetObjectItemCaseSensitive(
                    static_cast<cJSON *>(params), "sampleId"),
                sampleId)) {
            return sendRpcResult(
                transactionId,
                400,
                nullptr,
                "sampleId must be a u32");
        }
        cJSON *data = cJSON_CreateObject();
        if (data == nullptr) {
            return false;
        }
        cJSON_AddNumberToObject(data, "sampleId", sampleId);
        cJSON_AddNumberToObject(
            data,
            "deviceTimestampUs",
            monotonicMicros());
        const bool responseSent =
            sendRpcResult(transactionId, 0, data);
        cJSON_Delete(data);
        return responseSent;
    }
    if (command == "session.end") {
        if (!hasScope(HBOX_SCOPE_CONFIG_READ)) {
            return sendRpcResult(
                transactionId,
                403,
                nullptr,
                "config.read scope required");
        }
        cJSON *data = cJSON_CreateObject();
        if (data == nullptr) {
            return false;
        }
        cJSON_AddBoolToObject(data, "ended", true);
        const bool responseSent =
            sendRpcResult(transactionId, 0, data);
        cJSON_Delete(data);
        if (!responseSent || outboundQueue.empty()) {
            return false;
        }
        outboundQueue.back().endSession = true;
        return true;
    }
    if (command == "export_all_config") {
        if (!hasScope(HBOX_SCOPE_CONFIG_READ)) {
            return sendRpcResult(
                transactionId,
                403,
                nullptr,
                "config.read scope required");
        }
        /*
         * The legacy DeviceCommand command publishes every profile
         * synchronously. That cannot provide bounded backpressure over a
         * 64-byte HID transport. The hosted client performs the equivalent
         * get_* RPCs sequentially and synthesizes the legacy section events.
         */
        return sendRpcResult(
            transactionId,
            409,
            nullptr,
            "Use the incremental WebHID configuration export");
    }
    handled = false;
    return true;
}

bool WebHidService::handleBinaryExchange(
    uint32_t transactionId,
    void *opaqueParams)
{
    /* A rejected exchange must never inherit or leave a capture transaction. */
    clearBinaryCapture();
    cJSON *params = static_cast<cJSON *>(opaqueParams);
    cJSON *encoding = params == nullptr
        ? nullptr
        : cJSON_GetObjectItemCaseSensitive(params, "encoding");
    cJSON *dataItem = params == nullptr
        ? nullptr
        : cJSON_GetObjectItemCaseSensitive(params, "data");
    std::vector<uint8_t> binary;
    if (!cJSON_IsString(encoding) ||
        strcmp(encoding->valuestring, "base64") != 0 ||
        !cJSON_IsString(dataItem) ||
        !decodeBase64(
            dataItem->valuestring, binary, 0u, false) ||
        binary.empty() ||
        binary.size() > kMaximumStreamBytes) {
        return sendRpcResult(
            transactionId,
            400,
            nullptr,
            "Invalid binary.exchange payload");
    }

    const uint8_t opcode = binary[0];
    if (!binaryRequestShapeValid(binary.data(), binary.size())) {
        return sendRpcResult(
            transactionId,
            400,
            nullptr,
            "Invalid binary.exchange command shape");
    }
    const uint32_t requiredScope =
        binaryOpcodeScope(opcode);
    if (requiredScope == 0u ||
        !hasScope(requiredScope) ||
        (requiredScope == HBOX_SCOPE_FIRMWARE_UPDATE &&
         !firmwareActionAuthorized)) {
        return sendRpcResult(
            transactionId,
            403,
            nullptr,
            "Binary opcode scope denied");
    }
    if (opcode == BINARY_CMD_UPLOAD_FIRMWARE_CHUNK) {
        if (!firmwareAuthorizationValid(
                nullptr, binary.data(), binary.size())) {
            return sendRpcResult(
                transactionId,
                403,
                nullptr,
                "Firmware chunk does not match the confirmed session");
        }
    }

    captureBinary = true;
    bool handlerResultKnown = false;
    bool handlerSucceeded = true;
    if (opcode == BINARY_CMD_UPLOAD_FIRMWARE_CHUNK) {
        handlerResultKnown = true;
        handlerSucceeded = FirmwareCommandHandler::getInstance()
            .handleBinaryFirmwareChunk(
                binary.data(), binary.size());
    } else {
        UserImageCommandHandler::handleBinaryMessage(
            binary.data(), binary.size());
    }
    captureBinary = false;

    BinaryAckStatus ackStatus = BinaryAckStatus::ProtocolError;
    cJSON *response = captureBinaryInvalid
        ? nullptr
        : createBinaryAckData(
              binary.data(),
              binary.size(),
              capturedBinary.data(),
              capturedBinary.size(),
              handlerResultKnown,
              handlerSucceeded,
              false,
              ackStatus);
    clearBinaryCapture();
    if (response == nullptr ||
        ackStatus == BinaryAckStatus::ProtocolError) {
        cJSON_Delete(response);
        return sendRpcResult(
            transactionId,
            502,
            nullptr,
            "Binary handler returned an invalid or uncorrelated response");
    }
    /*
     * A correlated negative ACK is a valid application result, just like the
     * stream-complete path below.  Keep it inside the typed ACK with a
     * successful RPC envelope. Only malformed or unrelated responses use the
     * 502 protocol-failure path above.
     */
    const bool result = sendRpcResult(
        transactionId,
        0,
        response,
        nullptr);
    cJSON_Delete(response);
    return result;
}

bool WebHidService::handleStreamRpc(
    uint32_t transactionId,
    const std::string &command,
    void *opaqueParams)
{
    cJSON *params = static_cast<cJSON *>(opaqueParams);
    if (params == nullptr || !cJSON_IsObject(params)) {
        return sendRpcResult(
            transactionId, 400, nullptr, "Stream parameters required");
    }

    if (command == "stream.begin") {
        cJSON *streamItem =
            cJSON_GetObjectItemCaseSensitive(params, "stream");
        cJSON *lengthItem =
            cJSON_GetObjectItemCaseSensitive(params, "length");
        cJSON *hashItem =
            cJSON_GetObjectItemCaseSensitive(params, "sha256");
        uint32_t expectedLength = 0u;
        std::vector<uint8_t> hash;
        const uint8_t streamType =
            cJSON_IsString(streamItem)
                ? streamTypeForName(streamItem->valuestring)
                : 0u;
        const uint32_t required = streamScope(streamType);
        const bool lengthValid = parseU32(lengthItem, expectedLength);
        if (lengthValid && expectedLength > kMaximumStreamBytes) {
            return sendRpcResult(
                transactionId,
                413,
                nullptr,
                "Stream payload exceeds the 8 KiB limit");
        }
        if (stream.active ||
            streamType == 0u ||
            required == 0u ||
            !lengthValid ||
            expectedLength == 0u ||
            !cJSON_IsString(hashItem) ||
            !decodeBase64(
                hashItem->valuestring, hash, 32u, false) ||
            (grantedScopes & required) != required ||
            (required == HBOX_SCOPE_FIRMWARE_UPDATE &&
             !firmwareAuthorizationValid())) {
            return sendRpcResult(
                transactionId,
                400,
                nullptr,
                "Stream request is invalid or unauthorized");
        }
        uint32_t transferId = 0u;
        if (HBoxHardwareRng_Fill(
                nullptr,
                reinterpret_cast<unsigned char *>(&transferId),
                sizeof(transferId)) != 0 ||
            transferId == 0u) {
            return sendRpcResult(
                transactionId,
                500,
                nullptr,
                "Stream identifier generation failed");
        }
        clearStream();
        stream.active = true;
        stream.type = streamType;
        stream.transferId = transferId;
        stream.expectedLength = expectedLength;
        stream.remainingCredit = kStreamCreditWindow;
        memcpy(stream.expectedHash.data(),
               hash.data(),
               stream.expectedHash.size());

        cJSON *response = cJSON_CreateObject();
        if (response == nullptr) {
            clearStream();
            return false;
        }
        cJSON_AddNumberToObject(
            response, "transferId", transferId);
        cJSON_AddNumberToObject(
            response, "credit", kStreamCreditWindow);
        const bool result =
            sendRpcResult(transactionId, 0, response);
        cJSON_Delete(response);
        return result;
    }

    uint32_t transferId = 0u;
    if (!parseU32(
            cJSON_GetObjectItemCaseSensitive(
                params, "transferId"),
            transferId) ||
        !stream.active ||
        transferId != stream.transferId) {
        return sendRpcResult(
            transactionId,
            409,
            nullptr,
            "No matching stream transfer");
    }
    if (command == "stream.credit") {
        stream.remainingCredit = kStreamCreditWindow;
        cJSON *response = cJSON_CreateObject();
        if (response == nullptr) {
            return false;
        }
        cJSON_AddNumberToObject(
            response, "credit", kStreamCreditWindow);
        const bool result =
            sendRpcResult(transactionId, 0, response);
        cJSON_Delete(response);
        return result;
    }
    if (command == "stream.abort") {
        clearStream();
        cJSON *response = cJSON_CreateObject();
        if (response == nullptr) {
            return false;
        }
        cJSON_AddBoolToObject(response, "aborted", true);
        const bool result =
            sendRpcResult(transactionId, 0, response);
        cJSON_Delete(response);
        return result;
    }
    if (command != "stream.complete") {
        return sendRpcResult(
            transactionId, 404, nullptr, "Unknown stream command");
    }

    cJSON *hashItem =
        cJSON_GetObjectItemCaseSensitive(params, "sha256");
    std::vector<uint8_t> requestedHash;
    uint8_t actualHash[32] = {};
    if (!cJSON_IsString(hashItem) ||
        !decodeBase64(
            hashItem->valuestring,
            requestedHash,
            32u,
            false) ||
        stream.received != stream.expectedLength ||
        HBoxCrypto_Sha256(
            stream.bytes.data(),
            stream.received,
            actualHash) != 0 ||
        !constantTimeEqual(
            requestedHash.data(),
            stream.expectedHash.data(),
            32u) ||
        !constantTimeEqual(
            actualHash,
            stream.expectedHash.data(),
            32u)) {
        HBoxCrypto_Zeroize(actualHash, sizeof(actualHash));
        clearStream();
        return sendRpcResult(
            transactionId,
            422,
            nullptr,
            "Stream integrity verification failed");
    }
    HBoxCrypto_Zeroize(actualHash, sizeof(actualHash));

    const uint8_t completedType = stream.type;
    const size_t completedLength = stream.received;
    uint8_t *const completed = stream.bytes.data();
    completed[completedLength] = 0u;
    if (completedType == 1u) {
        const uint8_t opcode = completedLength == 0u
            ? 0u
            : completed[0];
        const uint32_t requiredScope = binaryOpcodeScope(opcode);
        const bool firmwareStream =
            completedType == 1u &&
            opcode == BINARY_CMD_UPLOAD_FIRMWARE_CHUNK;
        const bool requestAccepted =
            binaryRequestShapeValid(completed, completedLength) &&
            firmwareStream &&
            hasScope(requiredScope) &&
            (!firmwareStream ||
             firmwareAuthorizationValid(
                 nullptr, completed, completedLength));
        if (!requestAccepted) {
            clearStream();
            clearBinaryCapture();
            return sendRpcResult(
                transactionId,
                422,
                nullptr,
                "Stream consumer rejected the command envelope");
        }

        clearBinaryCapture();
        captureBinary = true;
        bool handlerResultKnown = false;
        bool handlerSucceeded = true;
        if (firmwareStream) {
            handlerResultKnown = true;
            handlerSucceeded = FirmwareCommandHandler::getInstance()
                .handleBinaryFirmwareChunk(
                    completed, completedLength);
        }
        captureBinary = false;

        BinaryAckStatus ackStatus = BinaryAckStatus::ProtocolError;
        cJSON *response = captureBinaryInvalid
            ? nullptr
            : createBinaryAckData(
                  completed,
                  completedLength,
                  capturedBinary.data(),
                  capturedBinary.size(),
                  handlerResultKnown,
                  handlerSucceeded,
                  true,
                  ackStatus);
        clearBinaryCapture();
        clearStream();
        if (response == nullptr ||
            ackStatus == BinaryAckStatus::ProtocolError) {
            cJSON_Delete(response);
            return sendRpcResult(
                transactionId,
                502,
                nullptr,
                "Stream consumer returned an invalid or uncorrelated ACK");
        }
        /*
         * A correlated negative ACK is an application result, not a transport
         * failure. Preserve its typed fields for the client so it can stop or
         * restart the upgrade deterministically. Only malformed or
         * uncorrelated ACKs take the 502 protocol-failure path above.
         */
        const bool result = sendRpcResult(
            transactionId,
            0,
            response,
            nullptr);
        cJSON_Delete(response);
        return result;
    }

    bool succeeded = true;
    if (completedType == 3u) {
        if (!hasScope(HBOX_SCOPE_CONFIG_WRITE) ||
            completedLength == 0u ||
            std::find(completed, completed + completedLength, 0u) !=
                completed + completedLength) {
            succeeded = false;
        } else {
            cJSON *config = cJSON_Parse(
                reinterpret_cast<const char *>(completed));
            cJSON *request = cJSON_CreateObject();
            if (config == nullptr ||
                !cJSON_IsObject(config) ||
                request == nullptr) {
                succeeded = false;
            } else {
                cJSON_AddNumberToObject(
                    request, "transactionId", transactionId);
                cJSON_AddStringToObject(
                    request, "command", "import_all_config");
                cJSON_AddItemToObject(
                    request, "params", config);
                config = nullptr;
                WebHidRpcResult dispatched =
                    WEBHID_RPC_DISPATCHER.dispatch(
                        request, grantedScopes);
                succeeded = dispatched.error == 0;
                WEBHID_RPC_DISPATCHER.clearSerializedResponse();
            }
            cJSON_Delete(request);
            cJSON_Delete(config);
        }
    } else {
        succeeded = false;
    }
    clearStream();
    if (!succeeded) {
        return sendRpcResult(
            transactionId,
            422,
            nullptr,
            "Stream consumer rejected the data");
    }
    cJSON *response = cJSON_CreateObject();
    if (response == nullptr) {
        return false;
    }
    cJSON_AddBoolToObject(response, "complete", true);
    const bool result =
        sendRpcResult(transactionId, 0, response);
    cJSON_Delete(response);
    return result;
}

bool WebHidService::processStreamFragment(
    const uint8_t *payload,
    uint8_t length)
{
    static constexpr uint8_t kHeaderBytes = 14u;
    static constexpr uint8_t kDataBytes =
        WEBHID_REPORT_PAYLOAD_BYTES - kHeaderBytes;
    if (!sessionEstablished ||
        payload == nullptr ||
        length < kHeaderBytes ||
        !stream.active ||
        stream.remainingCredit == 0u) {
        return false;
    }
    const uint8_t type = payload[0];
    const uint32_t transferId = loadLe32(&payload[1]);
    const uint32_t offset = loadLe32(&payload[5]);
    const uint32_t total = loadLe32(&payload[9]);
    const uint8_t dataLength = payload[13];
    if (type != stream.type ||
        transferId != stream.transferId ||
        total != stream.expectedLength ||
        offset != stream.received ||
        dataLength > kDataBytes ||
        length != static_cast<uint8_t>(
                      kHeaderBytes + dataLength) ||
        static_cast<uint64_t>(offset) + dataLength >
            stream.expectedLength) {
        return false;
    }
    memcpy(stream.bytes.data() + stream.received,
           &payload[kHeaderBytes],
           dataLength);
    stream.received += dataLength;
    stream.bytes[stream.received] = 0u;
    --stream.remainingCredit;
    return true;
}

bool WebHidService::processImageData(
    uint8_t flags,
    const uint8_t *payload,
    uint8_t length)
{
    const uint8_t imageFlags = static_cast<uint8_t>(
        flags & static_cast<uint8_t>(~WEBHID_REPORT_FLAG_ENCRYPTED));
    if (!sessionEstablished ||
        !hasScope(HBOX_SCOPE_ASSET_WRITE) ||
        !UserImageCommandHandler::isUploadActive() ||
        payload == nullptr || length == 0u ||
        length > WEBHID_REPORT_PAYLOAD_BYTES ||
        (imageFlags & static_cast<uint8_t>(
            ~WEBHID_REPORT_FLAG_LAST)) != 0u) {
        return false;
    }
    return UserImageCommandHandler::consumeStreamData(
        payload,
        length,
        (imageFlags & WEBHID_REPORT_FLAG_LAST) != 0u);
}

bool WebHidService::sendLogical(
    uint8_t type,
    const uint8_t *data,
    size_t length,
    bool secure)
{
    if ((data == nullptr && length != 0u) ||
        length > kMaximumLogicalBytes ||
        (secure && !sessionEstablished) ||
        outboundQueue.size() >= kMaximumOutboundMessages ||
        length > kMaximumOutboundBytes - outboundQueuedBytes) {
        return false;
    }

    OutboundLogical message;
    message.type = type;
    message.secure = secure;
    message.start = outboundWriteOffset;
    message.length = length;
    if (length != 0u) {
        const size_t first = std::min(
            length,
            outboundStorage.size() - outboundWriteOffset);
        memcpy(&outboundStorage[outboundWriteOffset], data, first);
        if (first < length) {
            memcpy(outboundStorage.data(),
                   data + first,
                   length - first);
        }
        outboundWriteOffset =
            (outboundWriteOffset + length) % outboundStorage.size();
    }
    outboundQueuedBytes += length;
    outboundQueue.emplace_back(std::move(message));
    return true;
}

bool WebHidService::sendJson(
    uint8_t type,
    const std::string &json,
    bool secure)
{
    return !json.empty() &&
           sendLogical(
               type,
               reinterpret_cast<const uint8_t *>(json.data()),
               json.size(),
               secure);
}

bool WebHidService::sendFrame(
    uint8_t type,
    uint8_t flags,
    const uint8_t *payload,
    uint8_t length,
    bool secure,
    OutboundFrameSource source)
{
    if (length > WEBHID_REPORT_PAYLOAD_BYTES ||
        (payload == nullptr && length != 0u) ||
        (flags & ~static_cast<uint8_t>(
                     WEBHID_REPORT_FLAG_FRAGMENTED |
                     WEBHID_REPORT_FLAG_LAST |
                     WEBHID_REPORT_FLAG_ACK_REQUIRED)) != 0u ||
        nextTxSequence == 0u ||
        (secure && !sessionEstablished) ||
        (!secure &&
         type != WEBHID_REPORT_BOOTSTRAP_RESPONSE) ||
        source == OutboundFrameSource::None) {
        return false;
    }

    /*
     * UsbBoardLink retains a report after a partial STM32->CH585 transfer and
     * returns true when that retained report is eventually completed. Retrying
     * with a newly generated report would therefore make the new producer
     * believe its own bytes were sent. Resume the exact encrypted bytes first
     * and keep every other producer behind this ownership barrier.
     */
    if (pendingFrameSource != OutboundFrameSource::None) {
        if (pendingFrameSource != source) {
            return false;
        }
        if (!UsbBoardLink_WebConfigSendReport(
                reinterpret_cast<const uint8_t *>(
                    g_responseScratch.data()))) {
            return false;
        }
        HBoxCrypto_Zeroize(
            g_responseScratch.data(), WEBHID_REPORT_BYTES);
        pendingFrameSource = OutboundFrameSource::None;
        ++nextTxSequence;
        return true;
    }

    webhid_secure_report_v1_t report = {};
    report.version = WEBHID_PROTOCOL_VERSION;
    report.type = type;
    report.flags = flags;
    report.payload_length = length;
    report.sequence_le = nextTxSequence;
    if (secure) {
        report.flags |= WEBHID_REPORT_FLAG_ENCRYPTED;
        uint8_t nonce[12] = {};
        makeNonce(txNoncePrefix, nextTxSequence, nonce);
        if (HBoxCrypto_Aes256GcmEncrypt(
                txKey.data(),
                nonce,
                reinterpret_cast<const uint8_t *>(&report),
                WEBHID_REPORT_HEADER_BYTES,
                payload,
                length,
                report.payload,
                report.tag) != 0) {
            HBoxCrypto_Zeroize(nonce, sizeof(nonce));
            return false;
        }
        HBoxCrypto_Zeroize(nonce, sizeof(nonce));
    } else if (length != 0u) {
        memcpy(report.payload, payload, length);
    }

    if (!UsbBoardLink_WebConfigSendReport(
            reinterpret_cast<const uint8_t *>(&report))) {
        memcpy(g_responseScratch.data(), &report, sizeof(report));
        pendingFrameSource = source;
        HBoxCrypto_Zeroize(&report, sizeof(report));
        return false;
    }
    HBoxCrypto_Zeroize(&report, sizeof(report));
    ++nextTxSequence;
    return true;
}

bool WebHidService::sendResponse(
    uint8_t type,
    bool secure,
    uint32_t transactionId,
    int error,
    void *opaqueData,
    const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *data = static_cast<cJSON *>(opaqueData);
    if (root == nullptr) {
        return false;
    }
    cJSON_AddNumberToObject(
        root, "transactionId", transactionId);
    cJSON_AddNumberToObject(root, "errNo", error);
    if (data == nullptr) {
        cJSON *emptyData = cJSON_CreateObject();
        if (emptyData == nullptr ||
            !cJSON_AddItemToObject(root, "data", emptyData)) {
            cJSON_Delete(emptyData);
            cJSON_Delete(root);
            return false;
        }
    } else if (!cJSON_AddItemReferenceToObject(root, "data", data)) {
        cJSON_Delete(root);
        return false;
    }
    if (message != nullptr) {
        cJSON_AddStringToObject(
            root, "errorMessage", message);
    }
    g_responseScratch.fill('\0');
    const bool encoded = cJSON_PrintPreallocated(
        root,
        g_responseScratch.data(),
        g_responseScratch.size(),
        false);
    cJSON_Delete(root);
    const size_t encodedLength = encoded
        ? strlen(g_responseScratch.data())
        : 0u;
    if (!encoded || encodedLength == 0u ||
        encodedLength > kMaximumLogicalBytes) {
        if (error == 413) {
            return false;
        }
        return sendResponse(
            type,
            secure,
            transactionId,
            413,
            nullptr,
            "Response exceeds the 16 KiB limit");
    }
    return sendLogical(
        type,
        reinterpret_cast<const uint8_t *>(g_responseScratch.data()),
        encodedLength,
        secure);
}

bool WebHidService::sendRpcResult(
    uint32_t transactionId,
    int error,
    void *data,
    const char *message)
{
    return sendResponse(
        WEBHID_REPORT_SECURE_RESPONSE,
        true,
        transactionId,
        error,
        data,
        message);
}

bool WebHidService::sendBootstrapError(
    uint32_t transactionId,
    const char *message)
{
    return sendResponse(
        WEBHID_REPORT_BOOTSTRAP_RESPONSE,
        false,
        transactionId,
        400,
        nullptr,
        message);
}

bool WebHidService::hasScope(uint32_t scope) const
{
    return sessionEstablished &&
           scope != 0u &&
           (grantedScopes & scope) == scope;
}

bool WebHidService::pumpLogicalOutput()
{
    if (outboundQueue.empty()) {
        return false;
    }
    OutboundLogical &message = outboundQueue.front();
    if (message.secure && !sessionEstablished) {
        return false;
    }
    const size_t total = message.length;
    const size_t remaining =
        total > message.offset ? total - message.offset : 0u;
    const uint8_t fragmentLength =
        static_cast<uint8_t>(
            std::min(
                remaining,
                static_cast<size_t>(
                    WEBHID_REPORT_PAYLOAD_BYTES)));
    const bool fragmented =
        total > WEBHID_REPORT_PAYLOAD_BYTES;
    const bool last =
        message.offset + fragmentLength >= total;
    uint8_t flags =
        fragmented ? WEBHID_REPORT_FLAG_FRAGMENTED : 0u;
    if (last) {
        flags |= WEBHID_REPORT_FLAG_LAST;
    }
    std::array<uint8_t, WEBHID_REPORT_PAYLOAD_BYTES> fragment = {};
    if (fragmentLength != 0u) {
        const size_t source =
            (message.start + message.offset) % outboundStorage.size();
        const size_t first = std::min(
            static_cast<size_t>(fragmentLength),
            outboundStorage.size() - source);
        memcpy(fragment.data(), &outboundStorage[source], first);
        if (first < fragmentLength) {
            memcpy(fragment.data() + first,
                   outboundStorage.data(),
                   fragmentLength - first);
        }
    }
    if (!sendFrame(
            message.type,
            flags,
            fragmentLength == 0u
                ? nullptr
                : fragment.data(),
            fragmentLength,
            message.secure,
            OutboundFrameSource::Logical)) {
        return false;
    }
    message.offset += fragmentLength;
    if (!last) {
        return true;
    }

    const bool activateSession = message.activateSession;
    const bool endSession = message.endSession;
    outboundQueuedBytes -= message.length;
    for (size_t index = 0u; index < message.length; ++index) {
        outboundStorage[(message.start + index) %
                        outboundStorage.size()] = 0u;
    }
    outboundReadOffset =
        (message.start + message.length) % outboundStorage.size();
    outboundQueue.pop_front();
    if (activateSession) {
        sessionActivationPending = false;
        waitingForPermit = false;
        permitDeadlineMs = 0u;
        sessionEstablished = true;
    }
    if (endSession) {
        /* The ACK is already owned by the board-link credit generation. */
        resetSession(true, false);
    }
    return true;
}

void WebHidService::clearButtonStateQueue()
{
    HBoxCrypto_Zeroize(
        buttonStateQueue.data(), sizeof(buttonStateQueue));
    buttonStateHead = 0u;
    buttonStateTail = 0u;
    buttonStateCount = 0u;
}

bool WebHidService::sendOneButtonState()
{
    if (buttonStateCount == 0u) {
        return true;
    }
    const webhid_button_state_v1_t &snapshot =
        buttonStateQueue[buttonStateHead];
    if (!sendFrame(
            WEBHID_REPORT_BUTTON_STATE,
            WEBHID_REPORT_FLAG_LAST,
            reinterpret_cast<const uint8_t *>(&snapshot),
            sizeof(snapshot),
            true,
            OutboundFrameSource::ButtonState)) {
        return false;
    }
    HBoxCrypto_Zeroize(
        &buttonStateQueue[buttonStateHead],
        sizeof(buttonStateQueue[buttonStateHead]));
    buttonStateHead =
        (buttonStateHead + 1u) % kButtonStateQueueDepth;
    --buttonStateCount;
    return true;
}

bool WebHidService::queueOneEvent()
{
    if (!sessionEstablished || eventQueue.empty() ||
        !outboundQueue.empty()) {
        return false;
    }
    std::string event = std::move(eventQueue.front());
    eventQueue.pop_front();
    eventQueuedBytes -= event.size();
    const bool queued = sendJson(
        WEBHID_REPORT_SECURE_EVENT, event, true);
    std::fill(event.begin(), event.end(), '\0');
    if (!queued) {
        if (droppedEventCount != UINT32_MAX) {
            ++droppedEventCount;
        }
    }
    return queued;
}

uint32_t WebHidService::monotonicMicros()
{
    const uint32_t current = DWT->CYCCNT;
    const uint32_t delta = current - lastDwtCycles;
    lastDwtCycles = current;
    accumulatedCycles += delta;
    if (kCpuCyclesPerMicrosecond == 0u) {
        return 0u;
    }
    return static_cast<uint32_t>(
        accumulatedCycles / kCpuCyclesPerMicrosecond);
}

void WebHidService::onAdcButtonTransition(
    uint8_t buttonIndex,
    bool pressed)
{
    if (!initialized || !sessionEstablished ||
        !performanceEnabled ||
        buttonIndex >= WEBHID_PERF_KEY_COUNT) {
        return;
    }
    ADCBtn *button =
        ADC_BTNS_WORKER.getButtonState(buttonIndex);
    if (button == nullptr) {
        return;
    }
    if (edgeSequence == UINT32_MAX) {
        telemetryOverflow = true;
        return;
    }
    if (edgeCount >= kEdgeQueueDepth) {
        /* Preserve the newest edge and report the discontinuity in the next
         * checkpoint instead of invalidating the WebHID session. */
        if (pendingFrameSource == OutboundFrameSource::Edge) {
            /* The physical report still owns edgeQueue[edgeHead]. Dropping
             * that entry would make sendOneEdge() pop a different edge when
             * the retained report completes, so discard the newest arrival. */
            telemetryOverflow = true;
            if (totalDroppedSamples != UINT32_MAX) {
                ++totalDroppedSamples;
            }
            if (droppedSamples != UINT8_MAX) {
                ++droppedSamples;
            }
            return;
        }
        HBoxCrypto_Zeroize(&edgeQueue[edgeHead], sizeof(PerfEdge));
        edgeHead = (edgeHead + 1u) % kEdgeQueueDepth;
        --edgeCount;
        telemetryOverflow = true;
        if (totalDroppedSamples != UINT32_MAX) {
            ++totalDroppedSamples;
        }
        if (droppedSamples != UINT8_MAX) {
            ++droppedSamples;
        }
    }

    PerfEdge &edge = edgeQueue[edgeTail];
    edge = {};
    edge.timestampUs = monotonicMicros();
    edge.edgeSequence = ++edgeSequence;
    edge.buttonIndex = buttonIndex;
    edge.pressed = pressed ? 1u : 0u;
    edge.rawAdc = button->currentValue;
    edge.currentDistanceUm =
        distanceMicrometres(button, button->currentValue);
    edge.pressTriggerDistanceUm =
        distanceMicrometres(
            button, button->pressTriggerSnapshot);
    edge.pressStartDistanceUm =
        distanceMicrometres(
            button, button->pressStartSnapshot);
    edge.releaseTriggerDistanceUm =
        distanceMicrometres(
            button, button->releaseTriggerSnapshot);
    edge.releaseStartDistanceUm =
        distanceMicrometres(
            button, button->releaseStartSnapshot);
    edgeTail = (edgeTail + 1u) % kEdgeQueueDepth;
    ++edgeCount;
}

bool WebHidService::sendOneEdge()
{
    if (edgeCount == 0u) {
        return true;
    }
    const PerfEdge &edge = edgeQueue[edgeHead];
    if (!sendFrame(
            WEBHID_REPORT_PERF_EDGE,
            WEBHID_REPORT_FLAG_LAST,
            reinterpret_cast<const uint8_t *>(&edge),
            sizeof(edge),
            true,
            OutboundFrameSource::Edge)) {
        return false;
    }
    HBoxCrypto_Zeroize(&edgeQueue[edgeHead], sizeof(PerfEdge));
    edgeHead = (edgeHead + 1u) % kEdgeQueueDepth;
    --edgeCount;
    return true;
}

bool WebHidService::sendLatestSample(uint32_t timestampUs)
{
    webhid_perf_sample_v1_t sample = {};
    sample.device_timestamp_us_le = timestampUs;
    sample.dropped_samples = droppedSamples;
    uint32_t mask = 0u;
    for (uint8_t index = 0u;
         index < WEBHID_PERF_KEY_COUNT;
         ++index) {
        ADCBtn *button =
            ADC_BTNS_WORKER.getButtonState(index);
        if (button != nullptr &&
            button->state == ButtonState::PRESSED) {
            mask |= (1u << index);
        }
        sample.current_distance_um_le[index] =
            button == nullptr
                ? 0u
                : distanceMicrometres(
                      button, button->currentValue);
    }
    sample.pressed_mask_le[0] =
        static_cast<uint8_t>(mask);
    sample.pressed_mask_le[1] =
        static_cast<uint8_t>(mask >> 8u);
    sample.pressed_mask_le[2] =
        static_cast<uint8_t>(mask >> 16u);
    const bool sent = sendFrame(
        WEBHID_REPORT_PERF_SAMPLE,
        WEBHID_REPORT_FLAG_LAST,
        reinterpret_cast<const uint8_t *>(&sample),
        sizeof(sample),
        true,
        OutboundFrameSource::Sample);
    HBoxCrypto_Zeroize(&sample, sizeof(sample));
    if (sent) {
        droppedSamples = 0u;
    }
    return sent;
}

void WebHidService::requestCheckpoint()
{
    checkpointRequested = true;
}

bool WebHidService::prepareCheckpoint(uint32_t timestampUs)
{
    if (!sessionEstablished || !performanceEnabled ||
        !hasScope(HBOX_SCOPE_MONITOR_READ)) {
        return false;
    }
    const ADCValuesMapping *mapping =
        ADC_BTNS_WORKER.getCurrentMapping();
    uint32_t maximumTravelUm = 0u;
    if (mapping != nullptr && mapping->length > 0u &&
        std::isfinite(mapping->step) &&
        mapping->step > 0.0f) {
        const double value =
            static_cast<double>(mapping->length - 1u) *
            static_cast<double>(mapping->step) *
            1000.0;
        maximumTravelUm = value >= 65535.0
            ? UINT16_MAX
            : static_cast<uint32_t>(value + 0.5);
    }
    checkpointTimestampUs = timestampUs;
    checkpointEdgeSequence = edgeSequence;
    checkpointMaximumTravelUm =
        static_cast<uint16_t>(maximumTravelUm);
    checkpointDroppedSamples =
        static_cast<uint16_t>(
            std::min<uint32_t>(
                totalDroppedSamples, UINT16_MAX));
    for (uint8_t index = 0u;
         index < WEBHID_PERF_KEY_COUNT;
         ++index) {
        ADCBtn *button =
            ADC_BTNS_WORKER.getButtonState(index);
        webhid_perf_checkpoint_key_v1_t &key =
            checkpointKeys[index];
        key = {};
        key.virtual_pin =
            ADC_BTNS_WORKER.getButtonVirtualPin(index);
        if (button != nullptr &&
            button->state == ButtonState::PRESSED) {
            key.flags = 1u;
        }
        key.raw_adc_le =
            button == nullptr ? 0u : button->currentValue;
        key.current_distance_um_le =
            button == nullptr
                ? 0u
                : distanceMicrometres(
                      button, button->currentValue);
        key.press_trigger_distance_um_le =
            button == nullptr
                ? 0u
                : distanceMicrometres(
                      button, button->pressTriggerSnapshot);
        key.press_start_distance_um_le =
            button == nullptr
                ? 0u
                : distanceMicrometres(
                      button, button->pressStartSnapshot);
        key.release_trigger_distance_um_le =
            button == nullptr
                ? 0u
                : distanceMicrometres(
                      button, button->releaseTriggerSnapshot);
        key.release_start_distance_um_le =
            button == nullptr
                ? 0u
                : distanceMicrometres(
                      button, button->releaseStartSnapshot);
    }
    ++checkpointId;
    if (checkpointId == 0u) {
        ++checkpointId;
    }
    checkpointChunk = 0u;
    checkpointActive = true;
    checkpointRequested = false;
    return true;
}

bool WebHidService::sendCheckpointChunk()
{
    static constexpr uint8_t kChunkCount =
        WEBHID_PERF_KEY_COUNT /
        WEBHID_PERF_CHECKPOINT_KEYS;
    if (!checkpointActive ||
        checkpointChunk >= kChunkCount) {
        return false;
    }

    webhid_perf_checkpoint_v1_t checkpoint = {};
    checkpoint.device_timestamp_us_le =
        checkpointTimestampUs;
    checkpoint.edge_sequence_le =
        checkpointEdgeSequence;
    checkpoint.max_travel_distance_um_le =
        checkpointMaximumTravelUm;
    checkpoint.total_dropped_samples_le =
        checkpointDroppedSamples;
    checkpoint.checkpoint_id = checkpointId;
    checkpoint.chunk_index = checkpointChunk;
    checkpoint.chunk_count = kChunkCount;
    checkpoint.first_button =
        static_cast<uint8_t>(
            checkpointChunk *
            WEBHID_PERF_CHECKPOINT_KEYS);
    for (uint8_t index = 0u;
         index < WEBHID_PERF_CHECKPOINT_KEYS;
         ++index) {
        checkpoint.keys[index] =
            checkpointKeys[
                checkpoint.first_button + index];
    }
    const bool sent = sendFrame(
        WEBHID_REPORT_PERF_CHECKPOINT,
        WEBHID_REPORT_FLAG_LAST,
        reinterpret_cast<const uint8_t *>(&checkpoint),
        sizeof(checkpoint),
        true,
        OutboundFrameSource::Checkpoint);
    HBoxCrypto_Zeroize(&checkpoint, sizeof(checkpoint));
    if (!sent) {
        return false;
    }
    ++checkpointChunk;
    if (checkpointChunk >= kChunkCount) {
        checkpointActive = false;
        checkpointChunk = 0u;
        HBoxCrypto_Zeroize(
            checkpointKeys.data(), sizeof(checkpointKeys));
    }
    return true;
}

void WebHidService::updateTelemetry()
{
    if (!sessionEstablished ||
        !performanceEnabled ||
        !hasScope(HBOX_SCOPE_MONITOR_READ)) {
        return;
    }
    if (telemetryOverflow) {
        telemetryOverflow = false;
        requestCheckpoint();
    }
    const uint32_t timestampUs = monotonicMicros();
    const uint32_t now = HAL_GetTick();
    const uint32_t sampleInterval =
        stream.active ? 40u : kSampleIntervalMs;
    if (nextSampleAtMs == 0u) {
        nextSampleAtMs = now;
    }
    if (pendingFrameSource != OutboundFrameSource::Sample &&
        static_cast<int32_t>(now - nextSampleAtMs) >= 0) {
        const uint32_t due =
            ((now - nextSampleAtMs) / sampleInterval) + 1u;
        uint32_t dropped = due - 1u;
        if (samplePending) {
            ++dropped;
        }
        if (dropped != 0u) {
            const uint32_t room =
                UINT32_MAX - totalDroppedSamples;
            totalDroppedSamples +=
                dropped > room ? room : dropped;
            const uint32_t local =
                static_cast<uint32_t>(droppedSamples) + dropped;
            droppedSamples = static_cast<uint8_t>(
                std::min<uint32_t>(local, 255u));
        }
        nextSampleAtMs += due * sampleInterval;
        sampleTimestampUs = timestampUs;
        samplePending = true;
    }
    if (nextCheckpointAtMs == 0u) {
        nextCheckpointAtMs = now;
    }
    if (static_cast<int32_t>(
            now - nextCheckpointAtMs) >= 0) {
        nextCheckpointAtMs = now + kCheckpointIntervalMs;
        requestCheckpoint();
    }
    if (checkpointRequested && !checkpointActive) {
        (void)prepareCheckpoint(timestampUs);
    }
}

void WebHidService::pumpOutput()
{
    if (!initialized) {
        return;
    }
    /* A partially transferred physical report owns the sequence space even if
     * another producer has since become higher priority. Resume its original
     * producer so that the existing completion bookkeeping stays exact. */
    switch (pendingFrameSource) {
    case OutboundFrameSource::Logical:
        (void)pumpLogicalOutput();
        return;
    case OutboundFrameSource::Edge:
        (void)sendOneEdge();
        return;
    case OutboundFrameSource::ButtonState:
        (void)sendOneButtonState();
        return;
    case OutboundFrameSource::Sample:
        if (sendLatestSample(sampleTimestampUs)) {
            samplePending = false;
        }
        return;
    case OutboundFrameSource::Checkpoint:
        (void)sendCheckpointChunk();
        return;
    case OutboundFrameSource::None:
        break;
    }

    /* RPC/control output is reliable and owns the sequence space. Optional
     * telemetry may resume only after the complete logical response drains. */
    if (!outboundQueue.empty()) {
        (void)pumpLogicalOutput();
        return;
    }
    if (!sessionEstablished) {
        return;
    }
    if (edgeCount != 0u) {
        (void)sendOneEdge();
        return;
    }
    if (buttonStateCount != 0u) {
        (void)sendOneButtonState();
        return;
    }
    if (checkpointActive) {
        (void)sendCheckpointChunk();
        return;
    }
    if (samplePending) {
        if (sendLatestSample(sampleTimestampUs)) {
            samplePending = false;
        }
        return;
    }
    if (queueOneEvent()) {
        (void)pumpLogicalOutput();
    }
}
