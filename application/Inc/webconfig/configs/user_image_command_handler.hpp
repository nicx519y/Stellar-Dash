#pragma once

#include <cstdint>
#include <cstddef>

class UserImageCommandHandler {
public:
    static void handleBinaryMessage(const uint8_t* data, size_t length);
    /* Consume one authenticated IMAGE_DATA report directly into a 256-byte
     * QSPI page buffer. No complete image or 4 KiB chunk is retained in RAM. */
    static bool consumeStreamData(const uint8_t* data, size_t length, bool last);
    /* Expire an abandoned transfer without making a partial image valid. */
    static void pollUploadTimeout(uint32_t nowMs);
    static bool isUploadActive();
    /* Drop volatile upload state when the owning WebHID session ends. */
    static void resetUploadSession();
    /* One-time boot migration: invalidate the former +0xD8000 image header. */
    static void initializeStorageMigration();
    /* Strict UIMG v3 structure/header/payload validation for config writes. */
    static bool isBackgroundImageAvailable(const char* imageId);
};
