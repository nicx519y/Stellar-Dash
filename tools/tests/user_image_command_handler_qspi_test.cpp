#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "configs/user_image_command_handler.hpp"
#include "configs/user_image_format.hpp"
#include "config_transport_sink.hpp"
#include "board_cfg.h"
#include "qspi-w25q64.h"
#include "image_ack_validator.hpp"

namespace {

constexpr uint32_t kUserBase =
    USER_IMAGE_RESOURCES_ADDR + HBoxUserImage::STORAGE_GUARD_SIZE;
constexpr uint32_t kUserArea =
    USER_IMAGE_RESOURCES_SIZE - HBoxUserImage::STORAGE_GUARD_SIZE;

std::vector<uint8_t> flash(W25Qxx_FlashSize, 0xffu);
std::vector<uint8_t> reply;
std::vector<std::pair<uint32_t, uint32_t>> writes;
std::vector<std::pair<uint32_t, uint32_t>> erases;
bool failErase = false;
bool failRead = false;
bool failWrite = false;
uint32_t fixtureTick = 0u;

uint32_t offsetOf(uint32_t address) { return address & 0x00ffffffu; }

bool rangeValid(uint32_t address, uint32_t length)
{
    const uint32_t offset = offsetOf(address);
    return length <= flash.size() && offset <= flash.size() - length;
}

void resetFixture()
{
    std::fill(flash.begin(), flash.end(), 0xffu);
    reply.clear();
    writes.clear();
    erases.clear();
    failErase = false;
    failRead = false;
    failWrite = false;
    fixtureTick = 0u;
    UserImageCommandHandler::resetUploadSession();
}

void require(bool value, const char *message)
{
    if (!value) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

std::string replyError()
{
    if (reply.size() < 79u) return {};
    const size_t length = std::min<size_t>(reply[14], 64u);
    return std::string(reinterpret_cast<const char *>(reply.data() + 15u), length);
}

bool replySucceeded(uint8_t opcode)
{
    const size_t expected = opcode == 0xb2u ? 83u : 79u;
    return reply.size() == expected && reply[0] == opcode && reply[1] == 1u;
}

#pragma pack(push, 1)
struct Begin {
    uint8_t command = 0x30u;
    uint8_t imageType = 0u;
    uint32_t cid = 0u;
    uint16_t width = 0u;
    uint16_t height = 0u;
    uint32_t total = 0u;
    uint8_t frameCount = 1u;
    uint8_t fps = 0u;
    uint8_t transferVersion = 3u;
    uint8_t reserved = 0u;
    uint32_t payloadCrc32 = 0u;
};
struct Mutation {
    uint8_t command = 0u;
    uint8_t reserved = 0u;
    uint32_t cid = 0u;
};
#pragma pack(pop)

void begin(uint32_t cid, uint16_t width, uint16_t height,
           uint8_t frameCount = 1u, uint8_t fps = 0u,
           uint32_t payloadCrc32 = 0u)
{
    Begin request;
    request.imageType = frameCount > 1u ? 1u : 0u;
    request.cid = cid;
    request.width = width;
    request.height = height;
    request.frameCount = frameCount;
    request.fps = fps;
    request.total = static_cast<uint32_t>(width) * height * 2u * frameCount;
    request.payloadCrc32 = payloadCrc32;
    UserImageCommandHandler::handleBinaryMessage(
        reinterpret_cast<const uint8_t *>(&request), sizeof(request));
}

void stream(const std::vector<uint8_t> &payload, bool sendLast = true)
{
    size_t offset = 0u;
    while (offset < payload.size()) {
        const size_t length = std::min<size_t>(996u, payload.size() - offset);
        const bool last = sendLast && offset + length == payload.size();
        require(UserImageCommandHandler::consumeStreamData(
                    payload.data() + offset, length, last),
                "IMAGE_DATA frame must be accepted");
        offset += length;
    }
}

void mutate(uint8_t opcode, uint32_t cid)
{
    Mutation request;
    request.command = opcode;
    request.cid = cid;
    UserImageCommandHandler::handleBinaryMessage(
        reinterpret_cast<const uint8_t *>(&request), sizeof(request));
}

HBoxUserImage::HeaderV4 storedUserHeader()
{
    HBoxUserImage::HeaderV4 header{};
    std::memcpy(&header, flash.data() + offsetOf(kUserBase), sizeof(header));
    return header;
}

void testLegacyHeaderMigrationPreservesGuardAndNewSlot()
{
    resetFixture();
    const uint32_t partition = offsetOf(USER_IMAGE_RESOURCES_ADDR);
    std::fill(flash.begin() + partition,
              flash.begin() + partition + HBoxUserImage::STORAGE_GUARD_SIZE,
              0xa5u);
    HBoxUserImage::HeaderV3 legacy{};
    legacy.magic = HBoxUserImage::MAGIC;
    legacy.version = 2u;
    legacy.valid = 1u;
    const uint32_t legacyAddress =
        USER_IMAGE_RESOURCES_ADDR + HBoxUserImage::LEGACY_USER_IMAGE_OFFSET;
    std::memcpy(flash.data() + offsetOf(legacyAddress), &legacy, sizeof(legacy));
    UserImageCommandHandler::initializeStorageMigration();
    require(std::all_of(flash.begin() + partition,
                        flash.begin() + partition + HBoxUserImage::STORAGE_GUARD_SIZE,
                        [](uint8_t value) { return value == 0xa5u; }),
            "legacy migration must preserve the first 64 KiB guard");
    require(std::all_of(flash.begin() + offsetOf(legacyAddress),
                        flash.begin() + offsetOf(legacyAddress) + HBoxUserImage::HEADER_SIZE,
                        [](uint8_t value) { return value == 0xffu; }),
            "legacy +0xD8000 header must be erased without migration");
    require(erases.size() == 1u && erases[0].first == offsetOf(legacyAddress) &&
                erases[0].second == HBoxUserImage::HEADER_SIZE,
            "legacy migration must erase only its old header sector");
}

void testCompleteCommitAndHeaderLast()
{
    resetFixture();
    const uint32_t cid = 0x11223344u;
    const std::vector<uint8_t> pixels{1, 2, 3, 4, 5, 6, 7, 8};
    begin(cid, 2u, 2u, 1u, 0u,
          CRC32::calculate(pixels.data(), pixels.size()));
    require(replySucceeded(0xb0u), "valid BEGIN must succeed");
    require(storedUserHeader().magic == UINT32_MAX,
            "BEGIN must leave the header erased");
    stream(pixels);
    require(storedUserHeader().magic == UINT32_MAX,
            "payload writes must not publish a header");
    require(!writes.empty() && writes.back().first >=
                offsetOf(kUserBase) + HBoxUserImage::HEADER_SIZE,
            "payload must be written after the reserved header sector");
    mutate(0x32u, cid);
    require(replySucceeded(0xb2u), "valid COMMIT must succeed");
    uint32_t committedCrc = 0u;
    std::memcpy(&committedCrc, reply.data() + 79u, sizeof(committedCrc));
    require(committedCrc == CRC32::calculate(pixels.data(), pixels.size()),
            "COMMIT ACK must report the verified payload CRC");

    const auto header = storedUserHeader();
    require(header.version == 4u && header.valid == 1u,
            "COMMIT must publish a UIMG v4 header");
    require(header.payload_crc32 == CRC32::calculate(pixels.data(), pixels.size()),
            "payload CRC must describe QSPI bytes");
    require(header.header_crc32 == HBoxUserImage::calculateHeaderCrc(header),
            "header CRC must cover the canonical header");
    require(HBoxUserImage::validateStructure(
                header, HBoxUserImage::USER_ID, kUserArea,
                HBoxUserImage::MAX_USER_FRAMES),
            "committed header must pass the shared validator");
    require(writes.back().first == offsetOf(kUserBase) &&
                writes.back().second == sizeof(HBoxUserImage::HeaderV4),
            "the final QSPI program operation must publish the header");
    require(UserImageCommandHandler::isBackgroundImageAvailable(
                HBoxUserImage::USER_ID),
            "strict directory validation must accept the committed image");
}

void testTwelveFrameGifCommit()
{
    resetFixture();
    constexpr uint32_t frameSize = 320u * 172u * 2u;
    std::vector<uint8_t> pixels(frameSize * 12u);
    for (uint32_t frame = 0u; frame < 12u; ++frame) {
        std::fill(pixels.begin() + frame * frameSize,
                  pixels.begin() + (frame + 1u) * frameSize,
                  static_cast<uint8_t>(frame + 1u));
    }
    const uint32_t cid = 0x55667788u;
    CRC32 crc;
    for (size_t offset = 0u; offset < pixels.size(); offset += 4096u) {
        crc.update(pixels.data() + offset,
                   static_cast<uint16_t>(std::min<size_t>(4096u, pixels.size() - offset)));
    }
    begin(cid, 320u, 172u, 12u, 6u,
          crc.finalize());
    require(replySucceeded(0xb0u), "twelve-frame GIF BEGIN must succeed");
    stream(pixels);
    mutate(0x32u, cid);
    require(replySucceeded(0xb2u),
            ("twelve-frame GIF COMMIT must succeed: " + replyError()).c_str());
    const auto header = storedUserHeader();
    require(header.frame_count == 12u && header.fps == 6u &&
                header.format == HBoxUserImage::FORMAT_RGB565LE_SEQUENCE,
            "committed GIF must retain twelve frames at 6 FPS");
    for (uint32_t frame = 0u; frame < 12u; ++frame) {
        require(header.frame_offsets[frame] ==
                    HBoxUserImage::HEADER_SIZE + frame * frameSize,
                "GIF frame offsets must identify each persisted frame");
    }
    require(UserImageCommandHandler::isBackgroundImageAvailable(
                HBoxUserImage::USER_ID),
            "twelve-frame GIF must be available after commit");
}

void testExistingV3ImageRemainsAvailable()
{
    resetFixture();
    const std::vector<uint8_t> pixels{1, 2, 3, 4, 5, 6, 7, 8};
    HBoxUserImage::HeaderV3 header{};
    header.magic = HBoxUserImage::MAGIC;
    header.version = HBoxUserImage::LEGACY_VERSION;
    header.valid = 1u;
    header.format = HBoxUserImage::FORMAT_RGB565LE_SINGLE;
    header.width = 2u;
    header.height = 2u;
    header.frame_count = 1u;
    header.frame_size = pixels.size();
    header.frames_offset = HBoxUserImage::HEADER_SIZE;
    header.total_size = pixels.size();
    header.frame_offsets[0] = HBoxUserImage::HEADER_SIZE;
    std::strncpy(header.id, HBoxUserImage::USER_ID, sizeof(header.id) - 1u);
    header.payload_crc32 = CRC32::calculate(pixels.data(), pixels.size());
    header.header_crc32 = HBoxUserImage::calculateHeaderCrc(header);
    std::memcpy(flash.data() + offsetOf(kUserBase), &header, sizeof(header));
    std::memcpy(flash.data() + offsetOf(kUserBase) + HBoxUserImage::HEADER_SIZE,
                pixels.data(), pixels.size());
    require(UserImageCommandHandler::isBackgroundImageAvailable(HBoxUserImage::USER_ID),
            "existing UIMG v3 image must remain readable");
}

void testConsecutiveUploadsReleaseFirmwareSession()
{
    resetFixture();
    const std::vector<uint8_t> first{1, 2, 3, 4, 5, 6, 7, 8};
    const std::vector<uint8_t> second{8, 7, 6, 5, 4, 3, 2, 1};

    begin(0x101u, 2u, 2u, 1u, 0u,
          CRC32::calculate(first.data(), first.size()));
    stream(first);
    mutate(0x32u, 0x101u);
    require(replySucceeded(0xb2u), "first consecutive COMMIT must succeed");

    begin(0x102u, 2u, 2u, 1u, 0u,
          CRC32::calculate(second.data(), second.size()));
    require(replySucceeded(0xb0u),
            "second BEGIN must start without resetting the device session");
    stream(second);
    mutate(0x32u, 0x102u);
    require(replySucceeded(0xb2u), "second consecutive COMMIT must succeed");
    require(std::equal(
                second.begin(), second.end(),
                flash.begin() + offsetOf(kUserBase) + HBoxUserImage::HEADER_SIZE),
            "second upload must replace the persisted payload");
}

void testInterruptedAndCorruptPayloadsStayInvalid()
{
    resetFixture();
    const std::vector<uint8_t> pixels{1, 2, 3, 4, 5, 6, 7, 8};
    begin(7u, 2u, 2u, 1u, 0u,
          CRC32::calculate(pixels.data(), pixels.size()));
    stream(std::vector<uint8_t>(pixels.begin(), pixels.begin() + 4), false);
    UserImageCommandHandler::resetUploadSession();
    mutate(0x32u, 7u);
    require(!replySucceeded(0xb2u) && replyError() == "No active session",
            "disconnect must invalidate the volatile upload session");
    require(!UserImageCommandHandler::isBackgroundImageAvailable(
                HBoxUserImage::USER_ID),
            "a partial payload must never become visible");

    resetFixture();
    begin(8u, 2u, 2u, 1u, 0u,
          CRC32::calculate(pixels.data(), pixels.size()));
    stream(pixels);
    flash[offsetOf(kUserBase) + HBoxUserImage::HEADER_SIZE + 3u] ^= 0x01u;
    mutate(0x32u, 8u);
    require(!replySucceeded(0xb2u) && replyError() == "Payload CRC mismatch",
            "COMMIT must reject a one-byte QSPI payload corruption");
    require(storedUserHeader().magic == UINT32_MAX,
            "CRC failure must leave the erased header invalid");
}

void testHeaderCorruptionAndLegacyVersionsFailClosed()
{
    resetFixture();
    const std::vector<uint8_t> pixels{1, 2, 3, 4, 5, 6, 7, 8};
    begin(9u, 2u, 2u, 1u, 0u,
          CRC32::calculate(pixels.data(), pixels.size()));
    stream(pixels);
    mutate(0x32u, 9u);
    require(replySucceeded(0xb2u), "fixture COMMIT must succeed");
    flash[offsetOf(kUserBase) + offsetof(HBoxUserImage::HeaderV3, width)] ^= 1u;
    require(!UserImageCommandHandler::isBackgroundImageAvailable(
                HBoxUserImage::USER_ID),
            "header corruption must fail strict validation");

    auto header = storedUserHeader();
    header.version = 2u;
    header.header_crc32 = HBoxUserImage::calculateHeaderCrc(header);
    std::memcpy(flash.data() + offsetOf(kUserBase), &header, sizeof(header));
    require(!UserImageCommandHandler::isBackgroundImageAvailable(
                HBoxUserImage::USER_ID),
            "UIMG v1/v2 must be invalid after the strict v3 migration");
}

void testCapacityAndDeleteFailure()
{
    resetFixture();
    begin(10u, 320u, 172u, 12u, 6u);
    require(replySucceeded(0xb0u), "twelve full user frames must fit");
    begin(11u, 320u, 172u, 13u, 6u);
    require(!replySucceeded(0xb0u) &&
                replyError().find("max 12") != std::string::npos,
            "the thirteenth user frame must return a capacity error");
    begin(11u, 320u, 172u, 2u, 2u);
    require(!replySucceeded(0xb0u) && replyError() == "Invalid animation metadata",
            "animated user images must use the supported 3 or 6 FPS rates");

    resetFixture();
    const std::vector<uint8_t> pixels{1, 2, 3, 4, 5, 6, 7, 8};
    begin(12u, 2u, 2u, 1u, 0u,
          CRC32::calculate(pixels.data(), pixels.size()));
    stream(pixels);
    mutate(0x32u, 12u);
    require(UserImageCommandHandler::isBackgroundImageAvailable(
                HBoxUserImage::USER_ID),
            "delete fixture image must be valid");
    failErase = true;
    mutate(0x33u, 13u);
    require(!replySucceeded(0xb3u) &&
                replyError().find("Erase failed") != std::string::npos,
            "delete must surface QSPI erase failure");
    failErase = false;
    require(UserImageCommandHandler::isBackgroundImageAvailable(
                HBoxUserImage::USER_ID),
            "failed delete must preserve the device-authoritative image");
}

void testPageStreamingTailAndWriteFailure()
{
    resetFixture();
    std::vector<uint8_t> pixels(300u);
    for (size_t i = 0u; i < pixels.size(); ++i) {
        pixels[i] = static_cast<uint8_t>(i);
    }
    begin(20u, 15u, 10u, 1u, 0u,
          CRC32::calculate(pixels.data(), pixels.size()));
    stream(pixels);
    require(writes.size() == 2u,
            "300 payload bytes must program one full page and one tail page");
    require(writes[0].second == W25Qxx_PageSize && writes[1].second == 44u,
            "page cache must flush exactly 256 and 44 bytes");
    mutate(0x32u, 20u);
    require(replySucceeded(0xb2u), "cross-page payload must commit");

    resetFixture();
    begin(21u, 15u, 10u, 1u, 0u,
          CRC32::calculate(pixels.data(), pixels.size()));
    failWrite = true;
    stream(pixels);
    mutate(0x32u, 21u);
    require(!replySucceeded(0xb2u) &&
                replyError().find("Write failed") != std::string::npos,
            "first QSPI error must be retained until terminal COMMIT");
    require(storedUserHeader().magic == UINT32_MAX,
            "write failure must never publish a header");
}

void testCatalogThroughRpcValidator()
{
    resetFixture();
    const std::vector<uint8_t> pixels(2u * 2u * 2u * 12u, 0x5au);
    begin(40u, 2u, 2u, 12u, 6u,
          CRC32::calculate(pixels.data(), pixels.size()));
    stream(pixels);
    mutate(0x32u, 40u);
    require(replySucceeded(0xb2u), "catalog fixture must commit twelve frames at 6 FPS");

    Mutation request;
    request.command = 0x34u;
    request.reserved = 2u;
    request.cid = 0x12345678u;
    UserImageCommandHandler::handleBinaryMessage(
        reinterpret_cast<const uint8_t *>(&request), sizeof(request));
    require(reply.size() == 82u && reply[1] == 1u &&
                reply[6] == 1u && loadLe16(&reply[80]) == 15u,
            "real catalog must advertise the stored image and 6 FPS capability");
    const auto catalog = reply;
    auto validate = [&]() {
        cJSON *ack = cJSON_CreateObject();
        const auto result = describeBinaryAck(
            reinterpret_cast<const uint8_t *>(&request), sizeof(request),
            reply.data(), reply.size(), false, false, ack);
        if (result == BinaryAckStatus::Accepted) {
            require(std::strcmp(cJSON_GetObjectItemCaseSensitive(ack, "kind")->valuestring,
                                "image.info") == 0 &&
                        cJSON_GetObjectItemCaseSensitive(ack, "cid")->valuedouble == request.cid,
                    "catalog ACK must retain kind and request correlation");
        }
        cJSON_Delete(ack);
        return result;
    };
    require(validate() == BinaryAckStatus::Accepted,
            "RPC must accept the real 6 FPS catalog response");
    reply[80] = 3u;
    require(validate() == BinaryAckStatus::Accepted,
            "RPC must accept legacy transfer capability flags");
    for (const uint8_t flags : {0u, 1u, 2u, 4u, 5u, 6u, 19u, 31u}) {
        reply = catalog;
        reply[80] = flags;
        require(validate() == BinaryAckStatus::ProtocolError,
                "missing required or unknown transfer flags must fail closed");
    }
    for (const size_t offset : {0u, 2u, 64u, 65u, 66u, 67u, 76u, 77u, 78u, 81u}) {
        reply = catalog;
        reply[offset] ^= 0x80u;
        require(validate() == BinaryAckStatus::ProtocolError,
                "correlation, version, limits and reserved bytes remain strict");
    }
    reply = catalog;
    reply.pop_back();
    require(validate() == BinaryAckStatus::ProtocolError,
            "truncated catalog must fail closed");
    request.reserved = 3u;
    UserImageCommandHandler::handleBinaryMessage(reinterpret_cast<const uint8_t*>(&request), sizeof(request));
    require(reply.size() == 88u && reply[64] == 5u && reply[86] == 180u && reply[87] == 12u,
            "capacity catalog must advertise 12 FPS and JPEG frame limit");
    require(loadLe32(&reply[82]) == kUserArea - HBoxUserImage::HEADER_SIZE,
            "capacity must derive from current device partition");
    require(validate() == BinaryAckStatus::Accepted, "RPC must accept capacity catalog");
    reply[86] = 0u;
    require(validate() == BinaryAckStatus::ProtocolError, "zero frame capacity must be rejected");
    require(writes.size() == 2u,
            "catalog reads and RPC validation must not write flash");
}

void testJpegSequenceCommitAndMalformedDirectory()
{
    std::FILE* file = std::fopen("common/test_vectors/uimg-jpeg/sequence-12fps.uimg", "rb");
    require(file != nullptr, "JPEG golden asset is required");
    std::fseek(file, 0, SEEK_END); const long size = std::ftell(file); std::rewind(file);
    std::vector<uint8_t> asset(static_cast<size_t>(size));
    require(std::fread(asset.data(), 1, asset.size(), file) == asset.size(), "read JPEG fixture");
    std::fclose(file);
    std::vector<uint8_t> payload(asset.begin() + 4096, asset.end());
    for (bool damage : {false, true}) {
        resetFixture();
        auto bytes = payload;
        if (damage) bytes[8] ^= 4u; // Valid CRC, invalid first JPEG frame offset.
        Begin request;
        request.imageType = 2; request.cid = 50; request.width = 320; request.height = 172;
        request.frameCount = 18; request.fps = 12; request.total = bytes.size();
        request.payloadCrc32 = CRC32::calculate(bytes.data(), bytes.size());
        UserImageCommandHandler::handleBinaryMessage(reinterpret_cast<const uint8_t*>(&request), sizeof(request));
        require(replySucceeded(0xb0u), "JPEG BEGIN permits variable size and more than twelve frames");
        stream(bytes); mutate(0x32u, 50);
        if (damage) {
            require(!replySucceeded(0xb2u), "malformed JPEG directory must not commit");
            require(storedUserHeader().magic == UINT32_MAX, "invalid JPEG must leave header uncommitted");
        } else {
            require(replySucceeded(0xb2u), "valid JPEG sequence must commit");
            const auto header = storedUserHeader();
            require(header.version == 5 && header.format == 3 && header.frame_count == 18 && header.fps == 12,
                    "JPEG header must preserve compressed size and timeline");
            require(std::memcmp(flash.data() + offsetOf(kUserBase) + 4096, bytes.data(), bytes.size()) == 0,
                    "JPEG payload must survive stream and Flash readback intact");
            require(writes.back().first == offsetOf(kUserBase), "JPEG header is committed last");
            require(UserImageCommandHandler::isBackgroundImageAvailable(HBoxUserImage::USER_ID),
                    "JPEG image must be readable after commit");
        }
    }
}

void testLastAndTimeoutValidation()
{
    resetFixture();
    const std::vector<uint8_t> pixels{1, 2, 3, 4, 5, 6, 7, 8};
    begin(30u, 2u, 2u, 1u, 0u,
          CRC32::calculate(pixels.data(), pixels.size()));
    require(!UserImageCommandHandler::consumeStreamData(
                pixels.data(), 4u, true),
            "early LAST must be rejected");
    UserImageCommandHandler::resetUploadSession();

    begin(31u, 2u, 2u, 1u, 0u,
          CRC32::calculate(pixels.data(), pixels.size()));
    require(!UserImageCommandHandler::consumeStreamData(
                pixels.data(), pixels.size(), false),
            "complete payload without LAST must be rejected");
    UserImageCommandHandler::resetUploadSession();

    begin(32u, 2u, 2u, 1u, 0u,
          CRC32::calculate(pixels.data(), pixels.size()));
    fixtureTick = 30000u;
    UserImageCommandHandler::pollUploadTimeout(fixtureTick);
    mutate(0x32u, 32u);
    require(!replySucceeded(0xb2u) && replyError() == "No active session",
            "30 second inactivity must abandon the transaction");
}

} // namespace

extern "C" int8_t QSPI_W25Qxx_WritePage(
    uint8_t *data, uint32_t address, uint16_t length)
{
    if (failWrite || !rangeValid(address, length)) return W25Qxx_ERROR_TRANSMIT;
    const uint32_t offset = offsetOf(address);
    std::memcpy(flash.data() + offset, data, length);
    writes.emplace_back(offset, length);
    return QSPI_W25Qxx_OK;
}

extern "C" int8_t QSPI_W25Qxx_ReadBuffer(
    uint8_t *data, uint32_t address, uint32_t length)
{
    if (failRead || !rangeValid(address, length)) return W25Qxx_ERROR_TRANSMIT;
    std::memcpy(data, flash.data() + offsetOf(address), length);
    return QSPI_W25Qxx_OK;
}

extern "C" int8_t QSPI_W25Qxx_BufferErase(uint32_t address, uint32_t length)
{
    if (failErase || !rangeValid(address, length)) return W25Qxx_ERROR_Erase;
    std::fill(
        flash.begin() + offsetOf(address),
        flash.begin() + offsetOf(address) + length,
        0xffu);
    erases.emplace_back(offsetOf(address), length);
    return QSPI_W25Qxx_OK;
}

extern "C" int8_t QSPI_W25Qxx_EnterMemoryMappedMode(void) { return 0; }
extern "C" int8_t QSPI_W25Qxx_ExitMemoryMappedMode(void) { return 0; }
extern "C" bool QSPI_W25Qxx_IsMemoryMappedMode(void) { return false; }
extern "C" uint32_t HAL_GetTick(void) { return fixtureTick; }

void ConfigTransport_SetJsonSink(config_json_sink_t) {}
void ConfigTransport_SetBinarySink(config_binary_sink_t) {}
void ConfigTransport_PublishJson(const char *, size_t) {}
void ConfigTransport_PublishBinary(const uint8_t *, size_t) {}
void ConfigTransport_ReplyBinary(const uint8_t *data, size_t length)
{
    reply.assign(data, data + length);
}

int main()
{
    testLegacyHeaderMigrationPreservesGuardAndNewSlot();
    testCompleteCommitAndHeaderLast();
    testTwelveFrameGifCommit();
    testExistingV3ImageRemainsAvailable();
    testConsecutiveUploadsReleaseFirmwareSession();
    testInterruptedAndCorruptPayloadsStayInvalid();
    testHeaderCorruptionAndLegacyVersionsFailClosed();
    testCapacityAndDeleteFailure();
    testPageStreamingTailAndWriteFailure();
    testCatalogThroughRpcValidator();
    testJpegSequenceCommitAndMalformedDirectory();
    testLastAndTimeoutValidation();
    std::puts("user image QSPI reliability tests passed");
    return 0;
}
