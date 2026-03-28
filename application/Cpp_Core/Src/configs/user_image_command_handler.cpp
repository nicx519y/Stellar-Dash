#include "configs/user_image_command_handler.hpp"
#include "configs/websocket_server.hpp"
#include "qspi-w25q64.h"
#include "board_cfg.h"
#include "system_logger.h"
#include <cstring>
#include <cstdio>

static const uint8_t BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN = 0x30;
static const uint8_t BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK = 0x31;
static const uint8_t BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT = 0x32;
static const uint8_t BINARY_CMD_UPLOAD_USER_IMAGE_DELETE = 0x33;
static const uint8_t BINARY_CMD_GET_BG_IMAGE_INFO = 0x34;
static const uint8_t BINARY_CMD_READ_BG_IMAGE_CHUNK = 0x35;

static const uint8_t BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP = 0xB0;
static const uint8_t BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK_RESP = 0xB1;
static const uint8_t BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP = 0xB2;
static const uint8_t BINARY_CMD_UPLOAD_USER_IMAGE_DELETE_RESP = 0xB3;
static const uint8_t BINARY_CMD_GET_BG_IMAGE_INFO_RESP = 0xB4;
static const uint8_t BINARY_CMD_READ_BG_IMAGE_CHUNK_RESP = 0xB5;

static const uint32_t USER_IMAGE_INDEX_HEADER_SIZE = 4096;
static const uint32_t USER_IMAGE_FORMAT_RGB565LE = 1;
static const char* USER_IMAGE_ID = "USER_IMAGE";
static const uint32_t SYS_IMAGE_INDEX_HEADER_SIZE = 4096;
static const char* SYS_IMAGE_ID = "SYSTEM_DEFAULT";

#pragma pack(push, 1)
struct BinaryUserImageBeginHeader {
    uint8_t command;
    uint8_t reserved;
    uint32_t cid;
    uint16_t width;
    uint16_t height;
    uint32_t total_size;
};

struct BinaryUserImageBeginHeaderV2 {
    uint8_t command;
    uint8_t image_type; // 0=single, 1=sequence
    uint32_t cid;
    uint16_t width;
    uint16_t height;
    uint32_t total_size;
    uint8_t frame_count;
    uint8_t fps;
    uint16_t reserved2;
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

struct BinaryGetBgImageInfoHeader {
    uint8_t command;
    uint8_t reserved;
    uint32_t cid;
};

struct BinaryReadBgImageChunkHeader {
    uint8_t command;
    uint8_t target;   // 0=user, 1=system
    uint32_t cid;
    uint32_t offset;
    uint16_t chunk_size;
    uint16_t reserved2;
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
    uint8_t format; // 1=RGB565LE single, 2=RGB565LE sequence
    uint16_t width;
    uint16_t height;
    uint8_t frame_count;
    uint8_t fps;
    uint16_t reserved0;
    uint32_t frame_size;
    uint32_t frames_offset;
    uint32_t total_size;
    uint32_t frame_offsets[10];
    char id[16];
};

struct BinaryGetBgImageInfoResponse {
    uint8_t command;
    uint8_t success;
    uint32_t cid;
    uint8_t user_valid;
    uint8_t sys_valid;
    uint16_t user_width;
    uint16_t user_height;
    uint32_t user_size;
    uint8_t user_frame_count;
    uint8_t user_fps;
    uint8_t user_format;
    uint8_t user_reserved;
    uint16_t sys_width;
    uint16_t sys_height;
    uint32_t sys_size;
    uint8_t sys_frame_count;
    uint8_t sys_fps;
    uint8_t sys_format;
    uint8_t sys_reserved;
    char user_id[16];
    char sys_id[16];
};

struct BinaryReadBgImageChunkResponseHeader {
    uint8_t command;
    uint8_t success;
    uint8_t target; // 0=user, 1=system
    uint8_t format;
    uint32_t cid;
    uint16_t width;
    uint16_t height;
    uint32_t total;
    uint32_t offset;
    uint16_t chunk_size;
    uint8_t error_len;
    char error_msg[32];
};
#pragma pack(pop)

static struct {
    bool active;
    uint32_t cid;
    uint16_t width;
    uint16_t height;
    uint32_t total;
    uint32_t received;
    uint8_t frame_count;
    uint8_t fps;
    uint8_t format;
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
        int8_t r = QSPI_W25Qxx_WritePage((uint8_t*)(data + offset), (address + offset) & 0x00FFFFFF, (uint16_t)to_write);
        if (r != QSPI_W25Qxx_OK) return r;
        offset += to_write;
    }
    return QSPI_W25Qxx_OK;
}

static void clear_user_image_upload_session() {
    memset(&g_user_image_upload_session, 0, sizeof(g_user_image_upload_session));
}

struct QSPIXipGuard {
    bool was_xip;
    QSPIXipGuard() : was_xip(QSPI_W25Qxx_IsMemoryMappedMode()) {
        if (was_xip) {
            QSPI_W25Qxx_ExitMemoryMappedMode();
        }
    }
    ~QSPIXipGuard() {
        if (was_xip) {
            QSPI_W25Qxx_EnterMemoryMappedMode();
        }
    }
};

#pragma pack(push, 1)
struct UserImageIndexHeaderV1 {
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

static uint32_t align_up_u32(uint32_t v, uint32_t a) {
    return (v + (a - 1)) & ~(a - 1);
}

static bool read_index_header_at(uint32_t addr, UserImageIndexHeader& out) {
    QSPIXipGuard guard;
    UserImageIndexHeaderV1 v1 = {0};
    int8_t r = QSPI_W25Qxx_ReadBuffer((uint8_t*)&v1, addr, sizeof(v1));
    if (r != QSPI_W25Qxx_OK) return false;
    if (v1.magic != 0x474D4955) return false;
    if (v1.valid != 1) return false;
    if (v1.version == 1) {
        memset(&out, 0, sizeof(out));
        out.magic = v1.magic;
        out.version = v1.version;
        out.valid = v1.valid;
        out.format = USER_IMAGE_FORMAT_RGB565LE;
        out.width = v1.width;
        out.height = v1.height;
        out.frame_count = 1;
        out.fps = 0;
        out.frame_size = (uint32_t)out.width * (uint32_t)out.height * 2;
        out.frames_offset = v1.offset;
        out.total_size = v1.size;
        out.frame_offsets[0] = v1.offset;
        strncpy(out.id, v1.id, sizeof(out.id) - 1);
        return true;
    }
    r = QSPI_W25Qxx_ReadBuffer((uint8_t*)&out, addr, sizeof(out));
    if (r != QSPI_W25Qxx_OK) return false;
    if (out.magic != 0x474D4955) return false;
    if (out.valid != 1) return false;
    return true;
}

static bool is_sysbg_index_header(const UserImageIndexHeader& idx) {
    return std::strncmp(idx.id, SYS_IMAGE_ID, sizeof(idx.id)) == 0;
}

static bool is_user_index_header(const UserImageIndexHeader& idx) {
    return std::strncmp(idx.id, USER_IMAGE_ID, sizeof(idx.id)) == 0;
}

static const uint32_t SYSBG_MAX_FRAMES = 8;
static const uint32_t SYSBG_FRAME_W = 320;
static const uint32_t SYSBG_FRAME_H = 172;
static const uint32_t SYSBG_FRAME_SIZE = SYSBG_FRAME_W * SYSBG_FRAME_H * 2;
static const uint32_t SYSBG_RESERVED_SIZE = USER_IMAGE_INDEX_HEADER_SIZE + SYSBG_MAX_FRAMES * SYSBG_FRAME_SIZE;
static const uint32_t USER_IMAGE_BASE_ADDR = USER_IMAGE_RESOURCES_ADDR + SYSBG_RESERVED_SIZE;
static const uint32_t USER_IMAGE_AREA_SIZE = USER_IMAGE_RESOURCES_SIZE - SYSBG_RESERVED_SIZE;
static_assert(SYSBG_RESERVED_SIZE <= USER_IMAGE_RESOURCES_SIZE, "SYSBG reserved area exceeds USER_IMAGE_RESOURCES_SIZE");

static bool read_sysbg_index_header(UserImageIndexHeader& out) {
    if (!read_index_header_at(USER_IMAGE_RESOURCES_ADDR, out)) return false;
    if (!is_sysbg_index_header(out)) return false;
    return true;
}

static bool read_index_header(uint8_t target, UserImageIndexHeader& out) {
    if (target == 1) {
        return read_sysbg_index_header(out);
    }
    if (!read_index_header_at(USER_IMAGE_BASE_ADDR, out)) return false;
    if (!is_user_index_header(out)) return false;
    return true;
}

static void send_get_bg_info_response(WebSocketConnection* conn, uint32_t cid) {
    if (!conn) return;
    BinaryGetBgImageInfoResponse resp = {0};
    resp.command = BINARY_CMD_GET_BG_IMAGE_INFO_RESP;
    resp.success = 1;
    resp.cid = cid;

    UserImageIndexHeader userIdx = {0};
    if (read_index_header(0, userIdx)) {
        resp.user_valid = 1;
        resp.user_width = userIdx.width;
        resp.user_height = userIdx.height;
        resp.user_size = userIdx.total_size;
        resp.user_frame_count = userIdx.frame_count;
        resp.user_fps = userIdx.fps;
        resp.user_format = userIdx.format;
        strncpy(resp.user_id, userIdx.id, sizeof(resp.user_id) - 1);
    }

    UserImageIndexHeader sysIdx = {0};
    if (read_index_header(1, sysIdx)) {
        resp.sys_valid = 1;
        resp.sys_width = sysIdx.width;
        resp.sys_height = sysIdx.height;
        resp.sys_size = sysIdx.total_size;
        resp.sys_frame_count = sysIdx.frame_count;
        resp.sys_fps = sysIdx.fps;
        resp.sys_format = sysIdx.format;
        strncpy(resp.sys_id, sysIdx.id, sizeof(resp.sys_id) - 1);
    } else {
        resp.sys_valid = 0;
        resp.sys_width = 0;
        resp.sys_height = 0;
        resp.sys_size = 0;
        strncpy(resp.sys_id, SYS_IMAGE_ID, sizeof(resp.sys_id) - 1);
    }

    conn->send_binary((const uint8_t*)&resp, sizeof(resp));
}

static void send_read_chunk_response(WebSocketConnection* conn, const BinaryReadBgImageChunkHeader* req, const uint8_t* chunk, uint16_t chunk_size, uint8_t format, uint16_t width, uint16_t height, uint32_t total, const char* error_message) {
    if (!conn) return;
    BinaryReadBgImageChunkResponseHeader h = {0};
    h.command = BINARY_CMD_READ_BG_IMAGE_CHUNK_RESP;
    h.success = (error_message == nullptr) ? 1 : 0;
    h.target = req->target;
    h.format = format;
    h.cid = req->cid;
    h.width = width;
    h.height = height;
    h.total = total;
    h.offset = req->offset;
    h.chunk_size = (error_message == nullptr) ? chunk_size : 0;
    if (error_message) {
        size_t n = strlen(error_message);
        if (n > 31) n = 31;
        h.error_len = (uint8_t)n;
        memcpy(h.error_msg, error_message, n);
        h.error_msg[n] = '\0';
    }

    static uint8_t buffer[sizeof(BinaryReadBgImageChunkResponseHeader) + 4096];
    memcpy(buffer, &h, sizeof(h));
    if (!error_message && chunk && chunk_size > 0) {
        memcpy(buffer + sizeof(h), chunk, chunk_size);
        conn->send_binary(buffer, sizeof(h) + chunk_size);
    } else {
        conn->send_binary(buffer, sizeof(h));
    }
}

void UserImageCommandHandler::handleBinaryMessage(WebSocketConnection* conn, const uint8_t* data, size_t length) {
    if (!data || length < 1) {
        clear_user_image_upload_session();
        return;
    }
    uint8_t command = data[0];
    
    switch (command) {
        case BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN: {
            QSPIXipGuard guard;
            if (length < sizeof(BinaryUserImageBeginHeader)) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, false, 0, 0, 0, "Invalid begin length");
                break;
            }

            uint32_t cid = 0;
            uint16_t width = 0;
            uint16_t height = 0;
            uint32_t total_size = 0;
            uint8_t image_type = 0;
            uint8_t frame_count = 1;
            uint8_t fps = 0;

            if (length >= sizeof(BinaryUserImageBeginHeaderV2)) {
                const BinaryUserImageBeginHeaderV2* h = reinterpret_cast<const BinaryUserImageBeginHeaderV2*>(data);
                cid = h->cid;
                width = h->width;
                height = h->height;
                total_size = h->total_size;
                image_type = h->image_type;
                frame_count = h->frame_count;
                fps = h->fps;
                if (frame_count == 0) frame_count = 1;
                if (frame_count > 10) frame_count = 10;
                if (fps > 5) fps = 5;
            } else {
                const BinaryUserImageBeginHeader* h = reinterpret_cast<const BinaryUserImageBeginHeader*>(data);
                cid = h->cid;
                width = h->width;
                height = h->height;
                total_size = h->total_size;
                image_type = 0;
                frame_count = 1;
                fps = 0;
            }

            if (total_size == 0) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, false, cid, 0, 0, "Invalid size");
                break;
            }
            if (USER_IMAGE_AREA_SIZE <= USER_IMAGE_INDEX_HEADER_SIZE) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, false, cid, 0, total_size, "No space");
                break;
            }
            uint32_t max_payload = USER_IMAGE_AREA_SIZE - USER_IMAGE_INDEX_HEADER_SIZE;
            if (total_size > max_payload) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, false, cid, 0, total_size, "Size too large");
                break;
            }

            if (image_type == 1) {
                uint32_t frame_size = (uint32_t)width * (uint32_t)height * 2;
                uint32_t expected = frame_size * (uint32_t)frame_count;
                if (frame_size == 0 || expected != total_size) {
                    send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, false, cid, 0, total_size, "Size mismatch");
                    break;
                }
            }

            uint32_t erase_size = USER_IMAGE_INDEX_HEADER_SIZE + total_size;
            int8_t er = QSPI_W25Qxx_BufferErase(USER_IMAGE_BASE_ADDR, erase_size);
            if (er != QSPI_W25Qxx_OK) {
                char msg[64];
                std::snprintf(msg, sizeof(msg), "Erase failed:%d", (int)er);
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, false, cid, 0, total_size, msg);
                break;
            }
            g_user_image_upload_session.active = true;
            g_user_image_upload_session.cid = cid;
            g_user_image_upload_session.width = width;
            g_user_image_upload_session.height = height;
            g_user_image_upload_session.total = total_size;
            g_user_image_upload_session.received = 0;
            g_user_image_upload_session.frame_count = frame_count;
            g_user_image_upload_session.fps = fps;
            g_user_image_upload_session.format = (frame_count > 1) ? 2 : USER_IMAGE_FORMAT_RGB565LE;
            send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, true, cid, 0, total_size, nullptr);
            break;
        }
        case BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK: {
            QSPIXipGuard guard;
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
            int8_t wr = qspi_write_bytes(USER_IMAGE_BASE_ADDR + USER_IMAGE_INDEX_HEADER_SIZE + h->offset, payload, (uint32_t)payload_size);
            if (wr != QSPI_W25Qxx_OK) {
                char msg[64];
                std::snprintf(msg, sizeof(msg), "Write failed:%d", (int)wr);
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK_RESP, false, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, msg);
                break;
            }
            g_user_image_upload_session.received += (uint32_t)payload_size;
            send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK_RESP, true, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, nullptr);
            break;
        }
        case BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT: {
            QSPIXipGuard guard;
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
            idx.version = 2;
            idx.valid = 1;
            idx.format = g_user_image_upload_session.format;
            idx.width = g_user_image_upload_session.width;
            idx.height = g_user_image_upload_session.height;
            idx.frame_count = g_user_image_upload_session.frame_count == 0 ? 1 : g_user_image_upload_session.frame_count;
            idx.fps = g_user_image_upload_session.fps;
            idx.frame_size = (uint32_t)idx.width * (uint32_t)idx.height * 2;
            idx.frames_offset = USER_IMAGE_INDEX_HEADER_SIZE;
            idx.total_size = g_user_image_upload_session.total;
            for (uint32_t i = 0; i < 10; i++) {
                if (i < idx.frame_count) {
                    idx.frame_offsets[i] = idx.frames_offset + i * idx.frame_size;
                } else {
                    idx.frame_offsets[i] = 0;
                }
            }
            strncpy(idx.id, USER_IMAGE_ID, sizeof(idx.id) - 1);
            int8_t hr = qspi_write_bytes(USER_IMAGE_BASE_ADDR, (const uint8_t*)&idx, sizeof(idx));
            if (hr != QSPI_W25Qxx_OK) {
                char msg[64];
                std::snprintf(msg, sizeof(msg), "Header write failed:%d", (int)hr);
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP, false, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, msg);
                break;
            }
            send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP, true, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, nullptr);
            clear_user_image_upload_session();
            break;
        }
        case BINARY_CMD_UPLOAD_USER_IMAGE_DELETE: {
            QSPIXipGuard guard;
            if (length < sizeof(BinaryUserImageDeleteHeader)) {
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_DELETE_RESP, false, 0, 0, 0, "Invalid delete length");
                break;
            }
            const BinaryUserImageDeleteHeader* h = reinterpret_cast<const BinaryUserImageDeleteHeader*>(data);
            clear_user_image_upload_session();
            int8_t er = QSPI_W25Qxx_BufferErase(USER_IMAGE_BASE_ADDR, USER_IMAGE_AREA_SIZE);
            if (er != QSPI_W25Qxx_OK) {
                char msg[64];
                std::snprintf(msg, sizeof(msg), "Erase failed:%d", (int)er);
                send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_DELETE_RESP, false, h->cid, 0, 0, msg);
                break;
            }
            send_user_image_binary_response(conn, BINARY_CMD_UPLOAD_USER_IMAGE_DELETE_RESP, true, h->cid, 0, 0, nullptr);
            break;
        }
        case BINARY_CMD_GET_BG_IMAGE_INFO: {
            if (length < sizeof(BinaryGetBgImageInfoHeader)) {
                break;
            }
            const BinaryGetBgImageInfoHeader* h = reinterpret_cast<const BinaryGetBgImageInfoHeader*>(data);
            send_get_bg_info_response(conn, h->cid);
            break;
        }
        case BINARY_CMD_READ_BG_IMAGE_CHUNK: {
            if (length < sizeof(BinaryReadBgImageChunkHeader)) {
                break;
            }
            const BinaryReadBgImageChunkHeader* h = reinterpret_cast<const BinaryReadBgImageChunkHeader*>(data);

            UserImageIndexHeader idx = {0};
            bool hasHeader = read_index_header(h->target, idx);
            uint32_t base = (h->target == 1) ? USER_IMAGE_RESOURCES_ADDR : USER_IMAGE_BASE_ADDR;
            uint32_t payloadBase = base + (hasHeader ? idx.frames_offset : USER_IMAGE_INDEX_HEADER_SIZE);
            uint16_t width = hasHeader ? idx.width : 320;
            uint16_t height = hasHeader ? idx.height : 172;
            uint8_t format = hasHeader ? idx.format : USER_IMAGE_FORMAT_RGB565LE;
            uint32_t total = hasHeader ? idx.total_size : (uint32_t)width * (uint32_t)height * 2;

            if (h->offset >= total) {
                send_read_chunk_response(conn, h, nullptr, 0, format, width, height, total, "Out of range");
                break;
            }

            uint16_t want = h->chunk_size;
            if (want > 4096) want = 4096;
            uint32_t remain = total - h->offset;
            if (want > remain) want = (uint16_t)remain;

            uint8_t chunkBuf[4096];
            {
                QSPIXipGuard guard;
                int8_t r = QSPI_W25Qxx_ReadBuffer(chunkBuf, payloadBase + h->offset, want);
                if (r != QSPI_W25Qxx_OK) {
                    char msg[32];
                    std::snprintf(msg, sizeof(msg), "Read failed:%d", (int)r);
                    send_read_chunk_response(conn, h, nullptr, 0, format, width, height, total, msg);
                    break;
                }
            }
            send_read_chunk_response(conn, h, chunkBuf, want, format, width, height, total, nullptr);
            break;
        }
        default:
            break;
    }
}
