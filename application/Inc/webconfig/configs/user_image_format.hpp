#pragma once

#include "CRC32.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace HBoxUserImage {

constexpr uint32_t MAGIC = 0x474D4955u; // "UIMG"
constexpr uint16_t VERSION = 3u;
constexpr uint8_t FORMAT_RGB565LE_SINGLE = 1u;
constexpr uint8_t FORMAT_RGB565LE_SEQUENCE = 2u;
constexpr uint32_t HEADER_SIZE = 4096u;
constexpr uint32_t STORAGE_GUARD_SIZE = 0x00010000u;
constexpr uint16_t MAX_WIDTH = 320u;
constexpr uint16_t MAX_HEIGHT = 172u;
constexpr uint32_t FULL_FRAME_SIZE =
    static_cast<uint32_t>(MAX_WIDTH) * static_cast<uint32_t>(MAX_HEIGHT) * 2u;
constexpr uint8_t MAX_USER_FRAMES = 6u;
constexpr uint32_t LEGACY_USER_IMAGE_OFFSET =
    HEADER_SIZE + 8u * FULL_FRAME_SIZE;
constexpr uint8_t ANIMATION_FPS = 3u;
constexpr uint8_t MAX_INDEXED_FRAMES = 10u;
constexpr char USER_ID[] = "USER_IMAGE";

#pragma pack(push, 1)
struct HeaderV3 {
    uint32_t magic;
    uint16_t version;
    uint8_t valid;
    uint8_t format;
    uint16_t width;
    uint16_t height;
    uint8_t frame_count;
    uint8_t fps;
    uint16_t reserved0;
    uint32_t frame_size;
    uint32_t frames_offset;
    uint32_t total_size;
    uint32_t frame_offsets[MAX_INDEXED_FRAMES];
    char id[16];
    uint32_t payload_crc32;
    uint32_t header_crc32;
};
#pragma pack(pop)

static_assert(sizeof(HeaderV3) == 92u, "Unexpected UIMG v3 header size");

inline uint32_t calculateHeaderCrc(const HeaderV3& header)
{
    return CRC32::calculate(
        reinterpret_cast<const uint8_t*>(&header),
        static_cast<uint16_t>(offsetof(HeaderV3, header_crc32)));
}

inline bool idMatches(const char (&stored)[16], const char* expected)
{
    if (!expected) return false;
    const size_t expectedLength = std::strlen(expected);
    if (expectedLength >= sizeof(stored)) return false;
    if (std::memcmp(stored, expected, expectedLength) != 0 ||
        stored[expectedLength] != '\0') {
        return false;
    }
    for (size_t i = expectedLength + 1u; i < sizeof(stored); ++i) {
        if (stored[i] != '\0') return false;
    }
    return true;
}

inline bool validateStructure(const HeaderV3& header,
                              const char* expectedId,
                              uint32_t areaSize,
                              uint8_t maxFrames)
{
    if (header.magic != MAGIC || header.version != VERSION || header.valid != 1u ||
        header.reserved0 != 0u) {
        return false;
    }
    if (!idMatches(header.id, expectedId)) return false;
    if (header.width == 0u || header.width > MAX_WIDTH ||
        header.height == 0u || header.height > MAX_HEIGHT) {
        return false;
    }
    if (header.frame_count == 0u || header.frame_count > maxFrames ||
        header.frame_count > MAX_INDEXED_FRAMES) {
        return false;
    }

    if (header.format == FORMAT_RGB565LE_SINGLE) {
        if (header.frame_count != 1u || header.fps != 0u) return false;
    } else if (header.format == FORMAT_RGB565LE_SEQUENCE) {
        if (header.frame_count < 2u || header.fps != ANIMATION_FPS) {
            return false;
        }
    } else {
        return false;
    }

    const uint32_t frameSize =
        static_cast<uint32_t>(header.width) * static_cast<uint32_t>(header.height) * 2u;
    const uint64_t totalSize =
        static_cast<uint64_t>(frameSize) * static_cast<uint64_t>(header.frame_count);
    if (frameSize == 0u || header.frame_size != frameSize ||
        totalSize > UINT32_MAX || header.total_size != static_cast<uint32_t>(totalSize)) {
        return false;
    }
    if (header.frames_offset != HEADER_SIZE || areaSize <= HEADER_SIZE ||
        header.total_size > areaSize - HEADER_SIZE) {
        return false;
    }

    for (uint8_t i = 0u; i < MAX_INDEXED_FRAMES; ++i) {
        const uint32_t expectedOffset = i < header.frame_count
            ? HEADER_SIZE + static_cast<uint32_t>(i) * frameSize
            : 0u;
        if (header.frame_offsets[i] != expectedOffset) return false;
        if (i < header.frame_count &&
            static_cast<uint64_t>(header.frame_offsets[i]) + frameSize > areaSize) {
            return false;
        }
    }

    return header.header_crc32 == calculateHeaderCrc(header);
}

} // namespace HBoxUserImage
