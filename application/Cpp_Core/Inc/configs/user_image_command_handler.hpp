#pragma once

#include <cstdint>
#include <cstddef>

class UserImageCommandHandler {
public:
    static void handleBinaryMessage(const uint8_t* data, size_t length);
    /* Drop volatile upload state when the owning WebHID session ends. */
    static void resetUploadSession();
};
