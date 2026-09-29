#pragma once

#include "CRC32.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace HBoxUserImage {

constexpr uint32_t MAGIC = 0x474D4955u; // "UIMG"
constexpr uint16_t VERSION = 4u;
constexpr uint16_t LEGACY_VERSION = 3u;
constexpr uint8_t FORMAT_RGB565LE_SINGLE = 1u;
constexpr uint8_t FORMAT_RGB565LE_SEQUENCE = 2u;
constexpr uint32_t HEADER_SIZE = 4096u;
constexpr uint32_t STORAGE_GUARD_SIZE = 0x00010000u;
constexpr uint16_t MAX_WIDTH = 320u;
constexpr uint16_t MAX_HEIGHT = 172u;
constexpr uint32_t FULL_FRAME_SIZE =
    static_cast<uint32_t>(MAX_WIDTH) * static_cast<uint32_t>(MAX_HEIGHT) * 2u;
constexpr uint8_t MAX_USER_FRAMES = 12u;
constexpr uint32_t LEGACY_USER_IMAGE_OFFSET =
    HEADER_SIZE + 8u * FULL_FRAME_SIZE;
constexpr uint8_t ANIMATION_FPS = 6u;
constexpr uint8_t LEGACY_ANIMATION_FPS = 3u;
constexpr uint16_t IMAGE_TRANSFER_FLAG_CONTINUOUS = 1u << 0;
constexpr uint16_t IMAGE_TRANSFER_FLAG_TERMINAL_ACK_ONLY = 1u << 1;
constexpr uint16_t IMAGE_TRANSFER_FLAG_6_FPS = 1u << 2;
constexpr uint16_t IMAGE_TRANSFER_REQUIRED_FLAGS =
    IMAGE_TRANSFER_FLAG_CONTINUOUS | IMAGE_TRANSFER_FLAG_TERMINAL_ACK_ONLY;
constexpr uint16_t IMAGE_TRANSFER_SUPPORTED_FLAGS =
    IMAGE_TRANSFER_REQUIRED_FLAGS | IMAGE_TRANSFER_FLAG_6_FPS;

inline bool isSupportedImageTransferFlags(uint16_t flags)
{
    return (flags & IMAGE_TRANSFER_REQUIRED_FLAGS) == IMAGE_TRANSFER_REQUIRED_FLAGS &&
           (flags & ~IMAGE_TRANSFER_SUPPORTED_FLAGS) == 0u;
}

inline bool isSupportedAnimationFps(uint8_t fps)
{
    return fps == ANIMATION_FPS || fps == LEGACY_ANIMATION_FPS;
}
constexpr uint8_t MAX_V3_INDEXED_FRAMES = 10u;
constexpr uint8_t MAX_INDEXED_FRAMES = 12u;
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
    uint32_t frame_offsets[MAX_V3_INDEXED_FRAMES];
    char id[16];
    uint32_t payload_crc32;
    uint32_t header_crc32;
};

struct HeaderV4 {
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
static_assert(sizeof(HeaderV4) == 100u, "Unexpected UIMG v4 header size");

template <typename Header>
inline uint32_t calculateHeaderCrc(const Header& header)
{
    return CRC32::calculate(
        reinterpret_cast<const uint8_t*>(&header),
        static_cast<uint16_t>(offsetof(Header, header_crc32)));
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

template <typename Header>
inline bool validateStructureVersion(const Header& header,
                                     uint16_t version,
                                     uint8_t indexedFrames,
                                     const char* expectedId,
                                     uint32_t areaSize,
                                     uint8_t maxFrames)
{
    if (header.magic != MAGIC || header.version != version || header.valid != 1u ||
        header.reserved0 != 0u) {
        return false;
    }
    if (!idMatches(header.id, expectedId)) return false;
    if (header.width == 0u || header.width > MAX_WIDTH ||
        header.height == 0u || header.height > MAX_HEIGHT) {
        return false;
    }
    if (header.frame_count == 0u || header.frame_count > maxFrames ||
        header.frame_count > indexedFrames) {
        return false;
    }

    if (header.format == FORMAT_RGB565LE_SINGLE) {
        if (header.frame_count != 1u || header.fps != 0u) return false;
    } else if (header.format == FORMAT_RGB565LE_SEQUENCE) {
        if (header.frame_count < 2u || !isSupportedAnimationFps(header.fps) ||
            (version == LEGACY_VERSION && header.fps != LEGACY_ANIMATION_FPS)) {
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

    for (uint8_t i = 0u; i < indexedFrames; ++i) {
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

inline bool validateStructure(const HeaderV3& header, const char* expectedId,
                              uint32_t areaSize, uint8_t maxFrames)
{
    return validateStructureVersion(header, LEGACY_VERSION, MAX_V3_INDEXED_FRAMES,
                                    expectedId, areaSize, maxFrames);
}

inline bool validateStructure(const HeaderV4& header, const char* expectedId,
                              uint32_t areaSize, uint8_t maxFrames)
{
    return validateStructureVersion(header, VERSION, MAX_INDEXED_FRAMES,
                                    expectedId, areaSize, maxFrames);
}

inline bool decodeHeader(const uint8_t* bytes, size_t length, const char* expectedId,
                         uint32_t areaSize, uint8_t maxFrames, HeaderV4& out)
{
    if (!bytes || length < sizeof(HeaderV4)) return false;
    uint16_t version = 0u;
    std::memcpy(&version, bytes + offsetof(HeaderV3, version), sizeof(version));
    if (version == VERSION) {
        std::memcpy(&out, bytes, sizeof(out));
        return validateStructure(out, expectedId, areaSize, maxFrames);
    }
    if (version != LEGACY_VERSION) return false;
    HeaderV3 legacy = {};
    std::memcpy(&legacy, bytes, sizeof(legacy));
    if (!validateStructure(legacy, expectedId, areaSize, maxFrames)) return false;
    out = {};
    out.magic = legacy.magic;
    out.version = legacy.version;
    out.valid = legacy.valid;
    out.format = legacy.format;
    out.width = legacy.width;
    out.height = legacy.height;
    out.frame_count = legacy.frame_count;
    out.fps = legacy.fps;
    out.reserved0 = legacy.reserved0;
    out.frame_size = legacy.frame_size;
    out.frames_offset = legacy.frames_offset;
    out.total_size = legacy.total_size;
    std::memcpy(out.frame_offsets, legacy.frame_offsets, sizeof(legacy.frame_offsets));
    std::memcpy(out.id, legacy.id, sizeof(legacy.id));
    out.payload_crc32 = legacy.payload_crc32;
    out.header_crc32 = legacy.header_crc32;
    return true;
}

} // namespace HBoxUserImage
