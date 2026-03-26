#pragma once

#include "configs/websocket_server.hpp"
#include <cstdint>
#include <cstddef>

class UserImageCommandHandler {
public:
    static void handleBinaryMessage(WebSocketConnection* conn, const uint8_t* data, size_t length);
};
