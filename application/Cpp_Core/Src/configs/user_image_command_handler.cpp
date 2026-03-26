#include "configs/user_image_command_handler.hpp"
#include "configs/websocket_server.hpp"
#include "qspi-w25q64.h"
#include "board_cfg.h"
#include "system_logger.h"
#include <cstring>

static const uint8_t BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN = 0x30;
static const uint8_t BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK = 0x31;
static const uint8_t BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT = 0x32;
static const uint8_t BINARY_CMD_UPLOAD_USER_IMAGE_DELETE = 0x33;

static const uint8_t BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP = 0xB0;
static const uint8_t BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK_RESP = 0xB1;
static const uint8_t BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP = 0xB2;
static const uint8_t BINARY_CMD_UPLOAD_USER_IMAGE_DELETE_RESP = 0xB3;

static const uint32_t USER_IMAGE_INDEX_HEADER_SIZE = 4096;
static const uint32_t USER_IMAGE_PAYLOAD_ADDR = USER_IMAGE_RESOURCES_ADDR + USER_IMAGE_INDEX_HEADER_SIZE;
static const uint32_t USER_IMAGE_FORMAT_RGB565LE = 1;
static const char* USER_IMAGE_ID = "USER_IMAGE";

#pragma pack(push, 1)
struct BinaryUserImageBeginHeader {
    uint8_t command;
    uint8_t reserved;
    uint32_t cid;
    uint16_t width;
    uint16_t height;
    uint32_t total_size;
};

struct BinaryUserImageChunkHeader {
    uint8_t command;
    uint8_t reserved;
    uint32_t cid;
    uint32_t offset;
    uint16_t chunk_size;
    uint16_t reserved2;
};

struct BinaryUserImageCommitHeader {
    uint8_t command;
    uint8_t reserved;
    uint32_t cid;
};

struct BinaryUserImageDeleteHeader {
    uint8_t command;
    uint8_t reserved;
    uint32_t cid;
};

struct BinaryUserImageResponse {
    uint8_t command;
    uint8_t success;
    uint32_t cid;
    uint32_t received;
    uint32_t total;
    uint8_t error_len;
    char error_msg[64];
};

struct UserImageIndexHeader {
    uint32_t magic;
    uint16_t version;
    uint8_t valid;
    uint8_t format;
    uint16_t width;
    uint16_t height;
    uint32_t size;
    uint32_t offset;
    char id[16];
};
#pragma pack(pop)

static struct {
    bool active;
    uint32_t cid;
    uint16_t width;
    uint16_t height;
    uint32_t total;
    uint32_t received;
} g_user_image_upload_session = {0};

static void send_user_image_binary_response(WebSocketConnection* connection, uint8_t resp_cmd, bool success, uint32_t cid, uint32_t received, uint32_t total, const char* error_message) {
    if (!connection) return;
    BinaryUserImageResponse response = {0};
    response.command = resp_cmd;
    response.success = success ? 1 : 0;
    response.cid = cid;
    response.received = received;
    response.total = total;
    if (!success && error_message) {
        size_t n = strlen(error_message);
        if (n > 63) n = 63;
        response.error_len = (uint8_t)n;
        memcpy(response.error_msg, error_message, n);
        response.error_msg[n] = '\0';
    }
    connection->send_binary((const uint8_t*)&response, sizeof(response));
}

static int8_t qspi_write_bytes(uint32_t address, const uint8_t* data, uint32_t length) {
    uint32_t offset = 0;
    while (offset < length) {
        uint32_t remaining = length - offset;
        uint32_t page_offset = (address + offset) & (W25Qxx_PageSize - 1);
        uint32_t to_write = W25Qxx_PageSize - page_offset;
        if (to_write > remaining) to_write = remaining;
        int8_t r = QSPI_W25Qxx_WritePage((uint8_t*)(data + offset), address + offset, (uint16_t)to_write);
        if (r != QSPI_W25Qxx_OK) return r;
        offset += to_write;
    }
    return QSPI_W25Qxx_OK;
}

static void clear_user_image_upload_session() {
    memset(&g_user_image_upload_session, 0, sizeof(g_user_image_upload_session));
}

void UserImageCommandHandler::handleBinaryMessage(WebSocketConnection* conn, const uint8_t* data, size_t length) {
    if (!data || length < 1) {
        clear_user_image_upload_session();
        return;
    }
    uint8_t command = data[0];
    
    switch (command) {
        case BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN: {
            if (length < sizeof(BinaryUserImageBeginHeader)) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, false, 0, 0, 0, "Invalid begin length");
                break;
            }
            const BinaryUserImageBeginHeader* h = reinterpret_cast<const BinaryUserImageBeginHeader*>(data);
            if (h->total_size == 0) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, false, h->cid, 0, 0, "Invalid size");
                break;
            }
            uint32_t max_payload = USER_IMAGE_RESOURCES_SIZE - USER_IMAGE_INDEX_HEADER_SIZE;
            if (h->total_size > max_payload) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, false, h->cid, 0, h->total_size, "Size too large");
                break;
            }
            uint32_t erase_size = USER_IMAGE_INDEX_HEADER_SIZE + h->total_size;
            int8_t er = QSPI_W25Qxx_BufferErase(USER_IMAGE_RESOURCES_ADDR, erase_size);
            if (er != QSPI_W25Qxx_OK) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, false, h->cid, 0, h->total_size, "Erase failed");
                break;
            }
            g_user_image_upload_session.active = true;
            g_user_image_upload_session.cid = h->cid;
            g_user_image_upload_session.width = h->width;
            g_user_image_upload_session.height = h->height;
            g_user_image_upload_session.total = h->total_size;
            g_user_image_upload_session.received = 0;
            send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, true, h->cid, 0, h->total_size, nullptr);
            break;
        }
        case BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK: {
            if (length < sizeof(BinaryUserImageChunkHeader)) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK_RESP, false, 0, 0, 0, "Invalid chunk length");
                break;
            }
            const BinaryUserImageChunkHeader* h = reinterpret_cast<const BinaryUserImageChunkHeader*>(data);
            if (!g_user_image_upload_session.active || g_user_image_upload_session.cid != h->cid) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK_RESP, false, h->cid, 0, 0, "No active session");
                break;
            }
            size_t payload_offset = sizeof(BinaryUserImageChunkHeader);
            size_t payload_size = length - payload_offset;
            if (payload_size != h->chunk_size) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK_RESP, false, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, "Chunk size mismatch");
                break;
            }
            if (h->offset != g_user_image_upload_session.received) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK_RESP, false, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, "Unexpected offset");
                break;
            }
            if ((uint64_t)h->offset + (uint64_t)h->chunk_size > (uint64_t)g_user_image_upload_session.total) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK_RESP, false, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, "Out of range");
                break;
            }
            const uint8_t* payload = data + payload_offset;
            int8_t wr = qspi_write_bytes(USER_IMAGE_PAYLOAD_ADDR + h->offset, payload, (uint32_t)payload_size);
            if (wr != QSPI_W25Qxx_OK) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK_RESP, false, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, "Write failed");
                break;
            }
            g_user_image_upload_session.received += (uint32_t)payload_size;
            send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK_RESP, true, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, nullptr);
            break;
        }
        case BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT: {
            if (length < sizeof(BinaryUserImageCommitHeader)) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP, false, 0, 0, 0, "Invalid commit length");
                break;
            }
            const BinaryUserImageCommitHeader* h = reinterpret_cast<const BinaryUserImageCommitHeader*>(data);
            if (!g_user_image_upload_session.active || g_user_image_upload_session.cid != h->cid) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP, false, h->cid, 0, 0, "No active session");
                break;
            }
            if (g_user_image_upload_session.received != g_user_image_upload_session.total) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP, false, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, "Incomplete upload");
                break;
            }
            UserImageIndexHeader idx = {0};
            idx.magic = 0x474D4955;
            idx.version = 1;
            idx.valid = 1;
            idx.format = USER_IMAGE_FORMAT_RGB565LE;
            idx.width = g_user_image_upload_session.width;
            idx.height = g_user_image_upload_session.height;
            idx.size = g_user_image_upload_session.total;
            idx.offset = USER_IMAGE_INDEX_HEADER_SIZE;
            strncpy(idx.id, USER_IMAGE_ID, sizeof(idx.id) - 1);
            int8_t hr = qspi_write_bytes(USER_IMAGE_RESOURCES_ADDR, (const uint8_t*)&idx, sizeof(idx));
            if (hr != QSPI_W25Qxx_OK) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP, false, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, "Header write failed");
                break;
            }
            send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP, true, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, nullptr);
            clear_user_image_upload_session();
            break;
        }
        case BINARY_CMD_UPLOAD_USER_IMAGE_DELETE: {
            if (length < sizeof(BinaryUserImageDeleteHeader)) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_DELETE_RESP, false, 0, 0, 0, "Invalid delete length");
                break;
            }
            const BinaryUserImageDeleteHeader* h = reinterpret_cast<const BinaryUserImageDeleteHeader*>(data);
            clear_user_image_upload_session();
            int8_t er = QSPI_W25Qxx_BufferErase(USER_IMAGE_RESOURCES_ADDR, USER_IMAGE_RESOURCES_SIZE);
            if (er != QSPI_W25Qxx_OK) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_DELETE_RESP, false, h->cid, 0, 0, "Erase failed");
                break;
            }
            send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_DELETE_RESP, true, h->cid, 0, 0, nullptr);
            break;
        }
        default:
            break;
    }
}
