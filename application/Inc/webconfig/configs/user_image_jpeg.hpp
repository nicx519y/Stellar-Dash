#pragma once
#include "configs/user_image_format.hpp"

namespace HBoxUserImage {
inline uint32_t jpegLe32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
struct JpegFrame { uint32_t offset; uint32_t length; };
// Reader offsets are relative to the supplied payload/frame, never absolute Flash addresses.
template<class Read>
bool validateBaselineJpeg(Read read, uint32_t size, uint16_t width, uint16_t height) {
    uint8_t b[19]{};
    if (size < 4u || !read(0u, b, 2u) || b[0] != 255u || b[1] != 216u ||
        !read(size - 2u, b, 2u) || b[0] != 255u || b[1] != 217u) return false;
    uint32_t offset = 2u; bool found = false; uint8_t tables = 0u;
    while (offset + 4u <= size) {
        if (!read(offset++, b, 1u) || b[0] != 255u) return false;
        do { if (offset >= size || !read(offset++, b, 1u)) return false; } while (b[0] == 255u);
        const uint8_t marker = b[0];
        if (size - offset < 2u || !read(offset, b, 2u)) return false;
        const uint32_t length = uint32_t(b[0]) * 256u + b[1];
        if (length < 2u || length > size - offset) return false;
        if (marker == 0xdbu) tables |= 1u;
        if (marker == 0xc4u) tables |= 2u;
        if (marker == 0xc0u) {
            if (found || length != 17u || !read(offset, b, 17u) || b[2] != 8u || b[7] != 3u ||
                uint16_t(b[3] * 256u + b[4]) != height || uint16_t(b[5] * 256u + b[6]) != width ||
                b[8] != 1u || b[11] != 2u || b[14] != 3u ||
                (b[9] != 0x11u && b[9] != 0x21u && b[9] != 0x22u) || b[12] != 0x11u || b[15] != 0x11u) return false;
            found = true;
        } else if (marker == 0xdau) {
            return found && tables == 3u && length == 12u && read(offset, b, 12u) &&
                b[2] == 3u && b[3] == 1u && b[5] == 2u && b[7] == 3u && b[9] == 0u && b[10] == 63u && b[11] == 0u;
        } else if (marker != 0xdbu && marker != 0xc4u && marker != 0xddu && marker != 0xfeu &&
                   (marker < 0xe0u || marker > 0xefu)) return false;
        offset += length;
    }
    return false;
}
template<class Read>
bool validateJpegPayload(Read read, uint32_t size, uint8_t count, uint16_t width, uint16_t height) {
    uint8_t b[8]{};
    if (count == 0u || count > MAX_JPEG_FRAMES || size > MAX_JPEG_PAYLOAD_BYTES || size < 8u + count * 8u ||
        !read(0u, b, 8u) || jpegLe32(b) != 0x5145534au || b[4] != count || b[5] || b[6] || b[7]) return false;
    JpegFrame frames[MAX_JPEG_FRAMES]{};
    uint32_t end = 8u + count * 8u;
    for (uint32_t i = 0; i < count; i++) {
        if (!read(8u + i * 8u, b, 8u)) return false;
        const JpegFrame frame{jpegLe32(b), jpegLe32(b + 4)};
        if ((frame.offset & 3u) || frame.length < 4u || frame.offset > size || frame.length > size - frame.offset) return false;
        bool duplicate = false;
        for (uint32_t j = 0; j < i; j++) {
            if (frames[j].offset != frame.offset) continue;
            if (frames[j].length != frame.length) return false;
            duplicate = true; break;
        }
        if (!duplicate) {
            if (frame.offset != end || !validateBaselineJpeg([&](uint32_t offset, uint8_t* out, uint32_t length) {
                    return read(frame.offset + offset, out, length);
                }, frame.length, width, height)) return false;
            end = (frame.offset + frame.length + 3u) & ~3u;
            if (end > size) return false;
            const uint32_t padding = end - frame.offset - frame.length;
            if (padding && !read(frame.offset + frame.length, b, padding)) return false;
            for (uint32_t j = 0; j < padding; j++) if (b[j]) return false;
        }
        frames[i] = frame;
    }
    return end == size;
}
} // namespace HBoxUserImage
