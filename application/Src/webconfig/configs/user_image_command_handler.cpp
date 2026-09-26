#include "configs/user_image_command_handler.hpp"
#include "configs/user_image_format.hpp"
#include "qspi-w25q64.h"
#include "board_cfg.h"
#include "system_logger.h"
#include "config_transport_sink.hpp"
#include "stm32h7xx.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

extern "C" uint32_t HAL_GetTick(void);

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

struct BinaryUserImageBeginHeaderV3 {
    uint8_t command;
    uint8_t image_type; // 0=single, 1=sequence
    uint32_t cid;
    uint16_t width;
    uint16_t height;
    uint32_t total_size;
    uint8_t frame_count;
    uint8_t fps;
    uint8_t transfer_version;
    uint8_t reserved;
    uint32_t payload_crc32;
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

struct BinaryUserImageCommitResponseV2 {
    BinaryUserImageResponse legacy;
    uint32_t payload_crc32;
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

struct BinaryGetBgImageInfoResponseV2 {
    BinaryGetBgImageInfoResponse legacy;
    uint8_t catalog_version;
    uint8_t max_user_frames;
    uint8_t max_system_frames;
    uint8_t reserved;
    uint32_t user_crc32;
    uint32_t sys_crc32;
};

struct BinaryGetBgImageInfoResponseV3 {
    BinaryGetBgImageInfoResponseV2 v2;
    uint8_t image_transfer_version;
    uint8_t image_data_bytes_per_report;
    uint16_t image_transfer_flags;
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

static_assert(sizeof(BinaryGetBgImageInfoResponse) == 64u,
              "Legacy image catalog response ABI changed");
static_assert(sizeof(BinaryGetBgImageInfoResponseV2) == 76u,
              "Extended image catalog response ABI changed");
static_assert(sizeof(BinaryGetBgImageInfoResponseV3) == 80u,
              "Fast image catalog response ABI changed");
static_assert(sizeof(BinaryUserImageBeginHeaderV3) == 22u,
              "Fast image begin request ABI changed");
static_assert(sizeof(BinaryUserImageCommitResponseV2) == 83u,
              "Fast image commit response ABI changed");

static constexpr uint8_t IMAGE_TRANSFER_VERSION = 2u;
static constexpr uint8_t IMAGE_DATA_BYTES_PER_REPORT = 44u;
static constexpr uint16_t IMAGE_TRANSFER_FLAG_CONTINUOUS = 1u << 0;
static constexpr uint16_t IMAGE_TRANSFER_FLAG_TERMINAL_ACK_ONLY = 1u << 1;
static constexpr uint32_t IMAGE_UPLOAD_TIMEOUT_MS = 30000u;

static struct {
    bool active;
    bool last_received;
    bool write_failed;
    uint32_t cid;
    uint16_t width;
    uint16_t height;
    uint32_t total;
    uint32_t received;
    uint32_t persisted;
    uint32_t expected_crc32;
    uint32_t begin_started_ms;
    uint32_t last_activity_ms;
    uint32_t erase_ms;
    uint32_t page_write_ms;
    uint8_t frame_count;
    uint8_t fps;
    uint8_t format;
    uint16_t page_used;
    uint8_t page_buffer[W25Qxx_PageSize];
    char write_error[64];
} g_user_image_upload_session = {0};

static CRC32 g_user_image_upload_crc;

static void send_user_image_binary_response(uint8_t resp_cmd, bool success, uint32_t cid, uint32_t received, uint32_t total, const char* error_message, uint32_t payload_crc32 = 0u) {
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
    if (resp_cmd == BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP) {
        BinaryUserImageCommitResponseV2 extended = {};
        extended.legacy = response;
        extended.payload_crc32 = payload_crc32;
        ConfigTransport_ReplyBinary(
            reinterpret_cast<const uint8_t *>(&extended),
            sizeof(extended));
    } else {
        ConfigTransport_ReplyBinary(
            reinterpret_cast<const uint8_t *>(&response),
            sizeof(response));
    }
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
    g_user_image_upload_crc.reset();
}

void UserImageCommandHandler::resetUploadSession() {
    clear_user_image_upload_session();
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

static bool qspi_read_bytes(uint32_t address, void* destination, uint32_t length) {
    if (destination == nullptr || length == 0u) {
        return false;
    }

    const uint32_t flashOffset = address & 0x00FFFFFFu;
    if (length > W25Qxx_FlashSize ||
        flashOffset > W25Qxx_FlashSize - length) {
        return false;
    }

    if (QSPI_W25Qxx_IsMemoryMappedMode()) {
        const uint32_t mapped = W25Qxx_Mem_Addr + flashOffset;
        const uint32_t cacheStart = mapped & ~31u;
        const uint32_t cacheEnd = (mapped + length + 31u) & ~31u;
        SCB_InvalidateDCache_by_Addr(
            reinterpret_cast<uint32_t*>(cacheStart),
            static_cast<int32_t>(cacheEnd - cacheStart));
        __DSB();
        __ISB();
        memcpy(destination,
               reinterpret_cast<const void*>(mapped),
               length);
        return true;
    }

    return QSPI_W25Qxx_ReadBuffer(
               static_cast<uint8_t*>(destination),
               address,
               length) == QSPI_W25Qxx_OK;
}

using HBoxUserImage::HeaderV3;

static const uint32_t USER_IMAGE_FLASH_GUARD_SIZE = HBoxUserImage::STORAGE_GUARD_SIZE;
static const uint32_t USER_IMAGE_BASE_ADDR = USER_IMAGE_RESOURCES_ADDR + USER_IMAGE_FLASH_GUARD_SIZE;
static const uint32_t USER_IMAGE_AREA_SIZE = USER_IMAGE_RESOURCES_SIZE - USER_IMAGE_FLASH_GUARD_SIZE;
static const uint32_t LEGACY_USER_IMAGE_BASE_ADDR =
    USER_IMAGE_RESOURCES_ADDR + HBoxUserImage::LEGACY_USER_IMAGE_OFFSET;
static_assert(USER_IMAGE_FLASH_GUARD_SIZE < USER_IMAGE_RESOURCES_SIZE, "User image flash guard exceeds partition");
static_assert(USER_IMAGE_AREA_SIZE == 0x00180000u, "User image usable area must remain 0x180000 bytes");
static_assert(HBoxUserImage::LEGACY_USER_IMAGE_OFFSET == 0x000D8000u, "Legacy user image offset changed");
static_assert(
    HBoxUserImage::HEADER_SIZE +
        static_cast<uint32_t>(HBoxUserImage::MAX_USER_FRAMES) * HBoxUserImage::FULL_FRAME_SIZE <=
        USER_IMAGE_AREA_SIZE,
    "Configured user frame count exceeds USER_IMAGE_RESOURCES capacity");

static bool g_legacy_image_checked = false;

// The renderer is part of the application image, while the host-side QSPI
// contract test links this handler alone.  A weak notification keeps that
// test isolated and lets the real firmware invalidate its cached header.
extern void ScreenStandby_InvalidateImageCache(void) __attribute__((weak));

static void notify_screen_image_changed() {
    if (ScreenStandby_InvalidateImageCache) {
        ScreenStandby_InvalidateImageCache();
    }
}

static void remember_upload_write_error(int8_t status) {
    if (g_user_image_upload_session.write_failed) return;
    g_user_image_upload_session.write_failed = true;
    std::snprintf(g_user_image_upload_session.write_error,
                  sizeof(g_user_image_upload_session.write_error),
                  "Write failed:%d", static_cast<int>(status));
}

static bool flush_upload_page() {
    if (g_user_image_upload_session.page_used == 0u) {
        return !g_user_image_upload_session.write_failed;
    }
    if (g_user_image_upload_session.write_failed) {
        g_user_image_upload_session.page_used = 0u;
        return false;
    }

    QSPIXipGuard guard;
    const uint32_t address = USER_IMAGE_BASE_ADDR +
        HBoxUserImage::HEADER_SIZE + g_user_image_upload_session.persisted;
    const uint16_t length = g_user_image_upload_session.page_used;
    const uint32_t write_started_ms = HAL_GetTick();
    const int8_t result = QSPI_W25Qxx_WritePage(
        g_user_image_upload_session.page_buffer,
        address & 0x00FFFFFFu,
        length);
    g_user_image_upload_session.page_write_ms +=
        static_cast<uint32_t>(HAL_GetTick() - write_started_ms);
    if (result != QSPI_W25Qxx_OK) {
        remember_upload_write_error(result);
        g_user_image_upload_session.page_used = 0u;
        return false;
    }
    g_user_image_upload_session.persisted += length;
    g_user_image_upload_session.page_used = 0u;
    return true;
}

bool UserImageCommandHandler::consumeStreamData(
    const uint8_t* data,
    size_t length,
    bool last) {
    if (!g_user_image_upload_session.active || data == nullptr ||
        length == 0u || length > IMAGE_DATA_BYTES_PER_REPORT ||
        g_user_image_upload_session.last_received ||
        static_cast<uint64_t>(g_user_image_upload_session.received) + length >
            g_user_image_upload_session.total) {
        return false;
    }

    const uint32_t received_after =
        g_user_image_upload_session.received + static_cast<uint32_t>(length);
    if ((last && received_after != g_user_image_upload_session.total) ||
        (!last && received_after == g_user_image_upload_session.total)) {
        return false;
    }

    g_user_image_upload_crc.update(data, static_cast<uint16_t>(length));
    g_user_image_upload_session.received = received_after;
    g_user_image_upload_session.last_activity_ms = HAL_GetTick();

    size_t offset = 0u;
    while (offset < length) {
        const size_t capacity = W25Qxx_PageSize -
            g_user_image_upload_session.page_used;
        const size_t copy_length = std::min(capacity, length - offset);
        if (!g_user_image_upload_session.write_failed) {
            memcpy(g_user_image_upload_session.page_buffer +
                       g_user_image_upload_session.page_used,
                   data + offset,
                   copy_length);
            g_user_image_upload_session.page_used +=
                static_cast<uint16_t>(copy_length);
            if (g_user_image_upload_session.page_used == W25Qxx_PageSize) {
                (void)flush_upload_page();
            }
        }
        offset += copy_length;
    }

    if (last) {
        g_user_image_upload_session.last_received = true;
        (void)flush_upload_page();
    }
    return true;
}

void UserImageCommandHandler::pollUploadTimeout(uint32_t nowMs) {
    if (g_user_image_upload_session.active &&
        static_cast<uint32_t>(nowMs -
            g_user_image_upload_session.last_activity_ms) >=
            IMAGE_UPLOAD_TIMEOUT_MS) {
        clear_user_image_upload_session();
    }
}

bool UserImageCommandHandler::isUploadActive() {
    return g_user_image_upload_session.active;
}

static void invalidate_legacy_user_image_once() {
    if (g_legacy_image_checked) return;
    HeaderV3 legacy = {0};
    if (!qspi_read_bytes(LEGACY_USER_IMAGE_BASE_ADDR, &legacy, sizeof(legacy))) return;
    if (legacy.magic == HBoxUserImage::MAGIC && legacy.valid == 1u) {
        const int8_t result = QSPI_W25Qxx_BufferErase(
            LEGACY_USER_IMAGE_BASE_ADDR,
            HBoxUserImage::HEADER_SIZE);
        if (result != QSPI_W25Qxx_OK) {
            return;
        }
    }
    g_legacy_image_checked = true;
}

static bool calculate_qspi_crc(uint32_t address, uint32_t length, uint32_t& result) {
    if (length == 0u) return false;
    CRC32 crc;
    static uint8_t buffer[W25Qxx_PageSize];
    uint32_t offset = 0u;
    while (offset < length) {
        uint32_t chunk = length - offset;
        if (chunk > sizeof(buffer)) chunk = sizeof(buffer);
        if (!qspi_read_bytes(address + offset, buffer, chunk)) return false;
        crc.update(buffer, static_cast<uint16_t>(chunk));
        offset += chunk;
    }
    result = crc.finalize();
    return true;
}

static bool read_index_header_at(uint32_t address,
                                 uint32_t areaSize,
                                 const char* expectedId,
                                 uint8_t maxFrames,
                                 HeaderV3& out,
                                 bool verifyPayload = true) {
    memset(&out, 0, sizeof(out));
    if (!qspi_read_bytes(address, &out, sizeof(out))) return false;
    if (!HBoxUserImage::validateStructure(out, expectedId, areaSize, maxFrames)) {
        return false;
    }
    if (!verifyPayload) return true;
    uint32_t payloadCrc = 0u;
    return calculate_qspi_crc(address + out.frames_offset,
                              out.total_size,
                              payloadCrc) &&
           payloadCrc == out.payload_crc32;
}

static bool read_index_header(uint8_t target,
                              HeaderV3& out,
                              bool verifyPayload = true) {
    if (target != 0u) return false;
    return read_index_header_at(USER_IMAGE_BASE_ADDR,
                                USER_IMAGE_AREA_SIZE,
                                HBoxUserImage::USER_ID,
                                HBoxUserImage::MAX_USER_FRAMES,
                                out,
                                verifyPayload);
}

bool UserImageCommandHandler::isBackgroundImageAvailable(const char* imageId) {
    if (!imageId) return false;
    HeaderV3 header = {0};
    if (std::strcmp(imageId, HBoxUserImage::USER_ID) == 0) {
        return read_index_header(0u, header, true);
    }
    return false;
}

void UserImageCommandHandler::initializeStorageMigration() {
    QSPIXipGuard guard;
    invalidate_legacy_user_image_once();
}

static void send_get_bg_info_response(uint32_t cid, uint8_t requested_version) {
    BinaryGetBgImageInfoResponseV3 response = {0};
    BinaryGetBgImageInfoResponseV2& v2 = response.v2;
    BinaryGetBgImageInfoResponse& resp = v2.legacy;
    resp.command = BINARY_CMD_GET_BG_IMAGE_INFO_RESP;
    resp.success = 1u;
    resp.cid = cid;

    HeaderV3 userIdx = {0};
    if (read_index_header(0, userIdx)) {
        resp.user_valid = 1u;
        resp.user_width = userIdx.width;
        resp.user_height = userIdx.height;
        resp.user_size = userIdx.total_size;
        resp.user_frame_count = userIdx.frame_count;
        resp.user_fps = userIdx.fps;
        resp.user_format = userIdx.format;
        strncpy(resp.user_id, userIdx.id, sizeof(resp.user_id) - 1);
        v2.user_crc32 = userIdx.payload_crc32;
    }

    v2.catalog_version = requested_version >= 2u ? 3u : 2u;
    v2.max_user_frames = HBoxUserImage::MAX_USER_FRAMES;
    v2.max_system_frames = 0u;
    response.image_transfer_version = IMAGE_TRANSFER_VERSION;
    response.image_data_bytes_per_report = IMAGE_DATA_BYTES_PER_REPORT;
    response.image_transfer_flags =
        IMAGE_TRANSFER_FLAG_CONTINUOUS |
        IMAGE_TRANSFER_FLAG_TERMINAL_ACK_ONLY;
    const size_t response_size = requested_version >= 2u
        ? sizeof(response)
        : requested_version == 1u
            ? sizeof(v2)
            : sizeof(v2.legacy);
    ConfigTransport_ReplyBinary(
        reinterpret_cast<const uint8_t *>(&response),
        response_size);
}

static void send_read_chunk_response(const BinaryReadBgImageChunkHeader* req, const uint8_t* chunk, uint16_t chunk_size, uint8_t format, uint16_t width, uint16_t height, uint32_t total, const char* error_message) {
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
        ConfigTransport_ReplyBinary(
            buffer, sizeof(h) + chunk_size);
    } else {
        ConfigTransport_ReplyBinary(buffer, sizeof(h));
    }
}

void UserImageCommandHandler::handleBinaryMessage(const uint8_t* data, size_t length) {
    if (!data || length < 1) {
        clear_user_image_upload_session();
        return;
    }
    if (!g_legacy_image_checked) {
        QSPIXipGuard migrationGuard;
        invalidate_legacy_user_image_once();
    }
    uint8_t command = data[0];
    
    switch (command) {
        case BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN: {
            QSPIXipGuard guard;
            // A new BEGIN always supersedes any volatile transfer state.  It
            // does not erase the previous committed header until all metadata
            // has been validated and the actual erase starts below.
            clear_user_image_upload_session();
            if (length != sizeof(BinaryUserImageBeginHeaderV3)) {
                const uint32_t rejected_cid = length >= 6u
                    ? reinterpret_cast<const BinaryUserImageBeginHeader*>(data)->cid
                    : 0u;
                const uint32_t rejected_total = length >= sizeof(BinaryUserImageBeginHeader)
                    ? reinterpret_cast<const BinaryUserImageBeginHeader*>(data)->total_size
                    : 0u;
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, false, rejected_cid, 0, rejected_total, "Fast image transfer v2 required");
                break;
            }

            const BinaryUserImageBeginHeaderV3* h =
                reinterpret_cast<const BinaryUserImageBeginHeaderV3*>(data);
            const uint32_t cid = h->cid;
            const uint16_t width = h->width;
            const uint16_t height = h->height;
            const uint32_t total_size = h->total_size;
            const uint8_t image_type = h->image_type;
            const uint8_t frame_count = h->frame_count;
            const uint8_t fps = h->fps;

            if (h->transfer_version != IMAGE_TRANSFER_VERSION ||
                h->reserved != 0u) {
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, false, cid, 0, total_size, "Invalid fast transfer metadata");
                break;
            }

            if (width == 0u || width > HBoxUserImage::MAX_WIDTH ||
                height == 0u || height > HBoxUserImage::MAX_HEIGHT) {
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, false, cid, 0, total_size, "Invalid dimensions");
                break;
            }
            if (frame_count == 0u || frame_count > HBoxUserImage::MAX_USER_FRAMES) {
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, false, cid, 0, total_size, "Too many frames (max 6)");
                break;
            }
            if ((frame_count == 1u && (image_type != 0u || fps != 0u)) ||
                (frame_count > 1u && (image_type != 1u || fps != HBoxUserImage::ANIMATION_FPS))) {
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, false, cid, 0, total_size, "Invalid animation metadata");
                break;
            }
            const uint32_t frame_size = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 2u;
            const uint32_t expected_size = frame_size * static_cast<uint32_t>(frame_count);
            if (total_size == 0u || expected_size != total_size) {
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, false, cid, 0, total_size, "Size mismatch");
                break;
            }
            if (USER_IMAGE_AREA_SIZE <= HBoxUserImage::HEADER_SIZE) {
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, false, cid, 0, total_size, "No space");
                break;
            }
            uint32_t max_payload = USER_IMAGE_AREA_SIZE - HBoxUserImage::HEADER_SIZE;
            if (total_size > max_payload) {
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, false, cid, 0, total_size, "Size too large");
                break;
            }

            const uint32_t begin_started_ms = HAL_GetTick();
            uint32_t erase_size = HBoxUserImage::HEADER_SIZE + total_size;
            int8_t er = QSPI_W25Qxx_BufferErase(USER_IMAGE_BASE_ADDR, erase_size);
            const uint32_t erase_ms =
                static_cast<uint32_t>(HAL_GetTick() - begin_started_ms);
            if (er != QSPI_W25Qxx_OK) {
                char msg[64];
                std::snprintf(msg, sizeof(msg), "Erase failed:%d", (int)er);
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, false, cid, 0, total_size, msg);
                break;
            }
            notify_screen_image_changed();
            g_user_image_upload_session.active = true;
            g_user_image_upload_session.cid = cid;
            g_user_image_upload_session.width = width;
            g_user_image_upload_session.height = height;
            g_user_image_upload_session.total = total_size;
            g_user_image_upload_session.received = 0;
            g_user_image_upload_session.persisted = 0;
            g_user_image_upload_session.expected_crc32 = h->payload_crc32;
            g_user_image_upload_session.begin_started_ms = begin_started_ms;
            g_user_image_upload_session.last_activity_ms = HAL_GetTick();
            g_user_image_upload_session.erase_ms = erase_ms;
            g_user_image_upload_session.frame_count = frame_count;
            g_user_image_upload_session.fps = fps;
            g_user_image_upload_session.format = frame_count > 1u
                ? HBoxUserImage::FORMAT_RGB565LE_SEQUENCE
                : HBoxUserImage::FORMAT_RGB565LE_SINGLE;
            g_user_image_upload_crc.reset();
            send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_BEGIN_RESP, true, cid, 0, total_size, nullptr);
            break;
        }
        case BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK: {
            if (length < sizeof(BinaryUserImageChunkHeader)) {
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK_RESP, false, 0, 0, 0, "Invalid chunk length");
                break;
            }
            const BinaryUserImageChunkHeader* h = reinterpret_cast<const BinaryUserImageChunkHeader*>(data);
            send_user_image_binary_response(
                BINARY_CMD_UPLOAD_USER_IMAGE_CHUNK_RESP,
                false,
                h->cid,
                g_user_image_upload_session.received,
                g_user_image_upload_session.total,
                "Use IMAGE_DATA reports");
            break;
        }
        case BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT: {
            QSPIXipGuard guard;
            if (length < sizeof(BinaryUserImageCommitHeader)) {
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP, false, 0, 0, 0, "Invalid commit length");
                break;
            }
            const BinaryUserImageCommitHeader* h = reinterpret_cast<const BinaryUserImageCommitHeader*>(data);
            if (!g_user_image_upload_session.active || g_user_image_upload_session.cid != h->cid) {
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP, false, h->cid, 0, 0, "No active session");
                break;
            }
            if (g_user_image_upload_session.write_failed) {
                send_user_image_binary_response(
                    BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP,
                    false,
                    h->cid,
                    g_user_image_upload_session.persisted,
                    g_user_image_upload_session.total,
                    g_user_image_upload_session.write_error[0] != '\0'
                        ? g_user_image_upload_session.write_error
                        : "Payload write failed");
                clear_user_image_upload_session();
                break;
            }
            if (!g_user_image_upload_session.last_received ||
                g_user_image_upload_session.received != g_user_image_upload_session.total ||
                g_user_image_upload_session.persisted != g_user_image_upload_session.total ||
                g_user_image_upload_session.page_used != 0u) {
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP, false, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, "Incomplete upload");
                clear_user_image_upload_session();
                break;
            }
            const uint32_t expectedPayloadCrc =
                g_user_image_upload_session.expected_crc32;
            if (g_user_image_upload_crc.finalize() != expectedPayloadCrc) {
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP, false, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, "Stream CRC mismatch");
                clear_user_image_upload_session();
                break;
            }
            uint32_t persistedPayloadCrc = 0u;
            const uint32_t crc_started_ms = HAL_GetTick();
            if (!calculate_qspi_crc(
                    USER_IMAGE_BASE_ADDR + HBoxUserImage::HEADER_SIZE,
                    g_user_image_upload_session.total,
                    persistedPayloadCrc)) {
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP, false, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, "Payload readback failed");
                clear_user_image_upload_session();
                break;
            }
            const uint32_t crc_readback_ms =
                static_cast<uint32_t>(HAL_GetTick() - crc_started_ms);
            if (persistedPayloadCrc != expectedPayloadCrc) {
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP, false, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, "Payload CRC mismatch");
                clear_user_image_upload_session();
                break;
            }

            HeaderV3 idx = {0};
            idx.magic = HBoxUserImage::MAGIC;
            idx.version = HBoxUserImage::VERSION;
            idx.valid = 1u;
            idx.format = g_user_image_upload_session.format;
            idx.width = g_user_image_upload_session.width;
            idx.height = g_user_image_upload_session.height;
            idx.frame_count = g_user_image_upload_session.frame_count == 0 ? 1 : g_user_image_upload_session.frame_count;
            idx.fps = g_user_image_upload_session.fps;
            idx.frame_size = (uint32_t)idx.width * (uint32_t)idx.height * 2;
            idx.frames_offset = HBoxUserImage::HEADER_SIZE;
            idx.total_size = g_user_image_upload_session.total;
            for (uint32_t i = 0; i < HBoxUserImage::MAX_INDEXED_FRAMES; i++) {
                if (i < idx.frame_count) {
                    idx.frame_offsets[i] = idx.frames_offset + i * idx.frame_size;
                } else {
                    idx.frame_offsets[i] = 0;
                }
            }
            strncpy(idx.id, HBoxUserImage::USER_ID, sizeof(idx.id) - 1);
            idx.payload_crc32 = persistedPayloadCrc;
            idx.header_crc32 = HBoxUserImage::calculateHeaderCrc(idx);
            if (!HBoxUserImage::validateStructure(
                    idx,
                    HBoxUserImage::USER_ID,
                    USER_IMAGE_AREA_SIZE,
                    HBoxUserImage::MAX_USER_FRAMES)) {
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP, false, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, "Invalid generated header");
                clear_user_image_upload_session();
                break;
            }
            int8_t hr = qspi_write_bytes(USER_IMAGE_BASE_ADDR, (const uint8_t*)&idx, sizeof(idx));
            if (hr != QSPI_W25Qxx_OK) {
                char msg[64];
                std::snprintf(msg, sizeof(msg), "Header write failed:%d", (int)hr);
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP, false, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, msg);
                clear_user_image_upload_session();
                break;
            }

            HeaderV3 persistedHeader = {0};
            if (!qspi_read_bytes(USER_IMAGE_BASE_ADDR, &persistedHeader, sizeof(persistedHeader)) ||
                memcmp(&persistedHeader, &idx, sizeof(idx)) != 0 ||
                !HBoxUserImage::validateStructure(
                    persistedHeader,
                    HBoxUserImage::USER_ID,
                    USER_IMAGE_AREA_SIZE,
                    HBoxUserImage::MAX_USER_FRAMES)) {
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP, false, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, "Header readback failed");
                clear_user_image_upload_session();
                break;
            }
            send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_COMMIT_RESP, true, h->cid, g_user_image_upload_session.received, g_user_image_upload_session.total, nullptr, persistedPayloadCrc);
            LOG_INFO(
                "UserImage",
                "V2 upload bytes=%lu erase=%lums page-write=%lums crc-read=%lums total=%lums",
                static_cast<unsigned long>(g_user_image_upload_session.total),
                static_cast<unsigned long>(g_user_image_upload_session.erase_ms),
                static_cast<unsigned long>(g_user_image_upload_session.page_write_ms),
                static_cast<unsigned long>(crc_readback_ms),
                static_cast<unsigned long>(HAL_GetTick() -
                    g_user_image_upload_session.begin_started_ms));
            notify_screen_image_changed();
            clear_user_image_upload_session();
            break;
        }
        case BINARY_CMD_UPLOAD_USER_IMAGE_DELETE: {
            QSPIXipGuard guard;
            if (length < sizeof(BinaryUserImageDeleteHeader)) {
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_DELETE_RESP, false, 0, 0, 0, "Invalid delete length");
                break;
            }
            const BinaryUserImageDeleteHeader* h = reinterpret_cast<const BinaryUserImageDeleteHeader*>(data);
            clear_user_image_upload_session();
            int8_t er = QSPI_W25Qxx_BufferErase(USER_IMAGE_BASE_ADDR, USER_IMAGE_AREA_SIZE);
            if (er != QSPI_W25Qxx_OK) {
                char msg[64];
                std::snprintf(msg, sizeof(msg), "Erase failed:%d", (int)er);
                send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_DELETE_RESP, false, h->cid, 0, 0, msg);
                break;
            }
            notify_screen_image_changed();
            send_user_image_binary_response(BINARY_CMD_UPLOAD_USER_IMAGE_DELETE_RESP, true, h->cid, 0, 0, nullptr);
            break;
        }
        case BINARY_CMD_GET_BG_IMAGE_INFO: {
            if (length < sizeof(BinaryGetBgImageInfoHeader)) {
                break;
            }
            const BinaryGetBgImageInfoHeader* h = reinterpret_cast<const BinaryGetBgImageInfoHeader*>(data);
            send_get_bg_info_response(h->cid, h->reserved);
            break;
        }
        case BINARY_CMD_READ_BG_IMAGE_CHUNK: {
            if (length < sizeof(BinaryReadBgImageChunkHeader)) {
                break;
            }
            const BinaryReadBgImageChunkHeader* h = reinterpret_cast<const BinaryReadBgImageChunkHeader*>(data);

            HeaderV3 idx = {0};
            // The catalog path verifies the complete payload CRC before the
            // client starts reading.  Re-validating the whole image for each
            // 4 KiB chunk would turn a preview download into O(n^2) QSPI I/O;
            // the client verifies the catalog CRC again after assembly.
            bool hasHeader = read_index_header(h->target, idx, false);
            if (!hasHeader) {
                send_read_chunk_response(h, nullptr, 0, 0, 0, 0, 0, "Image unavailable");
                break;
            }
            uint32_t base = USER_IMAGE_BASE_ADDR;
            uint32_t payloadBase = base + idx.frames_offset;
            uint16_t width = idx.width;
            uint16_t height = idx.height;
            uint8_t format = idx.format;
            uint32_t total = idx.total_size;

            if (h->offset >= total) {
                send_read_chunk_response(h, nullptr, 0, format, width, height, total, "Out of range");
                break;
            }

            uint16_t want = h->chunk_size;
            if (want > 4096) want = 4096;
            uint32_t remain = total - h->offset;
            if (want > remain) want = (uint16_t)remain;

            uint8_t chunkBuf[4096];
            if (!qspi_read_bytes(
                    payloadBase + h->offset, chunkBuf, want)) {
                send_read_chunk_response(
                    h, nullptr, 0, format, width, height, total,
                    "Read failed");
                break;
            }
            send_read_chunk_response(h, chunkBuf, want, format, width, height, total, nullptr);
            break;
        }
        default:
            break;
    }
}
