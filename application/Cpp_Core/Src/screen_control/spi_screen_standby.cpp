#include "screen_control/spi_screen_standby.hpp"

#include <string.h>

#include "board_cfg.h"
#include "configs/user_image_format.hpp"
#include "qspi-w25q64.h"
#include "stm32h7xx.h"
#include "system_logger.h"

#ifndef SPI_SCREEN_STANDBY_TIMEOUT_MS
#define SPI_SCREEN_STANDBY_TIMEOUT_MS 5000u
#endif

extern "C" uint32_t HAL_GetTick(void);

static uint8_t g_display = 0;
static char g_bg_image_id[32] = {0};
static uint32_t g_bg = 0;
static uint32_t g_fg = 0xFFFFFFu;

static bool g_active = false;
static bool g_need_redraw = true;
static uint32_t g_last_activity_ms = 0;
static uint32_t g_last_input_mask = 0;
static bool g_image_source_ready = false;
static bool g_image_source_valid = false;
static uint8_t g_image_kind = 0;
static uint16_t g_image_w = 0;
static uint16_t g_image_h = 0;
static uint8_t g_anim_frame_count = 1;
static uint8_t g_anim_fps = 0;
static uint32_t g_anim_frame_size = 0;
static uint32_t g_anim_frame_offsets[10] = {0};
static uint32_t g_image_base_addr = 0;
static uint8_t g_anim_frame_index = 0;
static uint32_t g_anim_next_ms = 0;

enum : uint8_t {
    STANDBY_IMAGE_NONE = 0u,
    STANDBY_IMAGE_UIMG = 1u
};

static const uint32_t USER_IMAGE_FLASH_GUARD_SIZE = HBoxUserImage::STORAGE_GUARD_SIZE;
static const uint32_t USER_IMAGE_BASE_ADDR = USER_IMAGE_RESOURCES_ADDR + USER_IMAGE_FLASH_GUARD_SIZE;
static const uint32_t USER_IMAGE_AREA_SIZE = USER_IMAGE_RESOURCES_SIZE - USER_IMAGE_FLASH_GUARD_SIZE;

static bool tick_reached(uint32_t nowMs, uint32_t targetMs)
{
    return (int32_t)(nowMs - targetMs) >= 0;
}

static bool ensure_qspi_mmap(void)
{
    if (QSPI_W25Qxx_IsMemoryMappedMode()) return true;
    int8_t r = QSPI_W25Qxx_EnterMemoryMappedMode();
    if (r != QSPI_W25Qxx_OK) return false;
    return QSPI_W25Qxx_IsMemoryMappedMode();
}

static void reset_image_runtime(void)
{
    g_image_source_ready = false;
    g_image_source_valid = false;
    g_image_kind = STANDBY_IMAGE_NONE;
    g_image_w = 0;
    g_image_h = 0;
    g_anim_frame_count = 1u;
    g_anim_fps = 0u;
    g_anim_frame_size = 0u;
    g_image_base_addr = 0u;
    memset(g_anim_frame_offsets, 0, sizeof(g_anim_frame_offsets));
    g_anim_frame_index = 0u;
    g_anim_next_ms = 0u;
}

static bool validate_mapped_payload(uint32_t baseAddr,
                                    const HBoxUserImage::HeaderV3& header)
{
    const uint32_t payloadAddr = baseAddr + header.frames_offset;
    const uint32_t cacheStart = payloadAddr & ~31u;
    const uint32_t cacheEnd = (payloadAddr + header.total_size + 31u) & ~31u;
    SCB_InvalidateDCache_by_Addr(
        reinterpret_cast<uint32_t*>(cacheStart),
        static_cast<int32_t>(cacheEnd - cacheStart));
    __DSB();
    __ISB();

    CRC32 crc;
    const uint8_t* payload = reinterpret_cast<const uint8_t*>(payloadAddr);
    uint32_t offset = 0u;
    while (offset < header.total_size) {
        uint32_t chunk = header.total_size - offset;
        if (chunk > 4096u) chunk = 4096u;
        crc.update(payload + offset, static_cast<uint16_t>(chunk));
        offset += chunk;
    }
    return crc.finalize() == header.payload_crc32;
}

static void adopt_uimg_source(uint32_t baseAddr,
                              const HBoxUserImage::HeaderV3& header)
{
    g_image_kind = STANDBY_IMAGE_UIMG;
    g_image_base_addr = baseAddr;
    g_image_w = header.width;
    g_image_h = header.height;
    g_anim_frame_count = header.frame_count;
    g_anim_fps = header.fps;
    g_anim_frame_size = header.frame_size;
    memcpy(g_anim_frame_offsets, header.frame_offsets, sizeof(g_anim_frame_offsets));
}

static bool resolve_uimg_source(const char* imageId)
{
    if (!imageId || imageId[0] == '\0') return false;
    if (!QSPI_W25Qxx_IsMemoryMappedMode()) return false;

    uint32_t baseAddr = 0u;
    uint32_t areaSize = 0u;
    uint8_t maxFrames = 0u;
    const char* expectedId = nullptr;
    if (strncmp(imageId, HBoxUserImage::USER_ID, 16) == 0) {
        baseAddr = USER_IMAGE_BASE_ADDR;
        areaSize = USER_IMAGE_AREA_SIZE;
        maxFrames = HBoxUserImage::MAX_USER_FRAMES;
        expectedId = HBoxUserImage::USER_ID;
    } else {
        return false;
    }
    if (areaSize < sizeof(HBoxUserImage::HeaderV3)) return false;

    const uint8_t* base = (const uint8_t*)(uintptr_t)baseAddr;
    HBoxUserImage::HeaderV3 header = {0};
    memcpy(&header, base, sizeof(header));
    if (!HBoxUserImage::validateStructure(header, expectedId, areaSize, maxFrames) ||
        !validate_mapped_payload(baseAddr, header)) {
        LOG_WARN("ScreenStandby", "Rejected invalid UIMG v3 asset: %s", imageId);
        return false;
    }
    adopt_uimg_source(baseAddr, header);
    return true;
}

static void ensure_image_source(void)
{
    if (g_image_source_ready) return;
    g_image_source_ready = true;
    g_image_source_valid = false;
    g_image_kind = STANDBY_IMAGE_NONE;
    g_image_w = 0;
    g_image_h = 0;
    g_anim_frame_count = 1u;
    g_anim_fps = 0u;
    g_anim_frame_size = 0u;
    g_image_base_addr = 0u;
    memset(g_anim_frame_offsets, 0, sizeof(g_anim_frame_offsets));
    if (!ensure_qspi_mmap()) {
        return;
    }

    if (resolve_uimg_source(g_bg_image_id)) {
        g_image_source_valid = true;
        return;
    }
    LOG_WARN("ScreenStandby", "No valid background image is available");
}

static void draw_image_frame(ST7789_Handle* lcd, uint8_t frameIndex)
{
    uint16_t x = 0u;
    uint16_t y = 0u;
    if (g_image_w < ST7789_WIDTH) {
        x = (uint16_t)((ST7789_WIDTH - g_image_w) / 2u);
    }
    if (g_image_h < ST7789_HEIGHT) {
        y = (uint16_t)((ST7789_HEIGHT - g_image_h) / 2u);
    }

    if (g_image_kind == STANDBY_IMAGE_UIMG) {
        if (frameIndex >= g_anim_frame_count) frameIndex = 0u;
        uint32_t addr = g_image_base_addr + g_anim_frame_offsets[frameIndex];
        const uint8_t* pixels = (const uint8_t*)(uintptr_t)addr;
        ST7789_DrawBitmap(lcd, x, y, g_image_w, g_image_h, pixels, ST7789_BITMAP_RGB565_LE, (uint32_t)g_image_w * 2u);
    }
}


static void draw_button_layout(ST7789_Handle* lcd, uint32_t inputMask)
{
    const float scale = (float)ST7789_WIDTH / BOARD_WIDTH;
    ST7789_FillScreen(lcd, g_bg);
    for (uint32_t i = 0; i < (uint32_t)(NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS); i++) {
        float cx = HITBOX_BUTTON_POS_LIST[i].x * scale;
        float cy = HITBOX_BUTTON_POS_LIST[i].y * scale;
        float diameter = HITBOX_BUTTON_POS_LIST[i].r * scale;
        int x = (int)(cx + 0.5f);
        int y = (int)(cy + 0.5f);
        int r = (int)((diameter * 0.5f + 0.5f) * 9 / 10);
        if(r < 2) r = 2;
        bool pressed = (inputMask & (1u << i)) != 0u;
        if (pressed) {
            ST7789_FillCircle(lcd, x, y, r, g_fg);
        } else {
            ST7789_DrawCircle(lcd, x, y, r, g_fg);
            ST7789_DrawCircle(lcd, x, y, r - 1, g_fg);
        }
    }
}

void ScreenStandby_Init(uint32_t nowMs, uint32_t inputMask)
{
    g_active = false;
    g_need_redraw = true;
    g_last_activity_ms = nowMs;
    g_last_input_mask = inputMask;
    reset_image_runtime();
}

void ScreenStandby_Configure(uint8_t standbyDisplay, const char* backgroundImageId, uint32_t bgRgb888, uint32_t fgRgb888)
{
    if (g_display != standbyDisplay) {
        g_display = standbyDisplay;
        g_need_redraw = true;
    }
    if (strncmp(g_bg_image_id, backgroundImageId ? backgroundImageId : "", sizeof(g_bg_image_id)) != 0) {
        memset(g_bg_image_id, 0, sizeof(g_bg_image_id));
        if (backgroundImageId) {
            strncpy(g_bg_image_id, backgroundImageId, sizeof(g_bg_image_id) - 1u);
        }
        g_need_redraw = true;
        reset_image_runtime();
    }
    if (g_bg != bgRgb888 || g_fg != fgRgb888) {
        g_bg = bgRgb888;
        g_fg = fgRgb888;
        g_need_redraw = true;
    }
}

void ScreenStandby_InvalidateImageCache(void)
{
    reset_image_runtime();
    if (g_display == 1u) {
        g_active = false;
        g_last_activity_ms = HAL_GetTick();
    }
    g_need_redraw = true;
}

void ScreenStandby_NotifyInput(uint32_t nowMs, uint32_t inputMask, bool activityEvent, bool wakeEvent)
{
    bool activeInput = activityEvent;
    if (inputMask != g_last_input_mask) {
        g_last_input_mask = inputMask;
        activeInput = true;
        if (g_active && g_display == 2u) {
            g_need_redraw = true;
        }
    }
    if (activeInput) {
        g_last_activity_ms = nowMs;
    }
    if (g_active && wakeEvent) {
        g_active = false;
        g_need_redraw = true;
    }
}

void ScreenStandby_Tick(uint32_t nowMs)
{
    if (g_active) return;
    if (g_display == 0u) return;
    if ((uint32_t)(nowMs - g_last_activity_ms) < SPI_SCREEN_STANDBY_TIMEOUT_MS) return;
    if (g_display == 1u) {
        ensure_image_source();
        if (!g_image_source_valid) return;
    }
    g_active = true;
    g_need_redraw = true;
}

bool ScreenStandby_IsActive(void)
{
    return g_active;
}

bool ScreenStandby_Deactivate(void)
{
    if (!g_active) return false;
    g_active = false;
    g_need_redraw = true;
    g_anim_frame_index = 0u;
    g_anim_next_ms = 0u;
    return true;
}

void ScreenStandby_Render(ST7789_Handle* lcd, uint32_t inputMask)
{
    if (!lcd || !g_active) return;
    if (g_display == 1u) {
        ensure_image_source();
        bool animated = g_image_source_valid && g_image_kind == STANDBY_IMAGE_UIMG && g_anim_frame_count > 1u && g_anim_fps > 0u;
        uint32_t nowMs = HAL_GetTick();
        bool needFrame = g_need_redraw;
        if (!needFrame && animated && tick_reached(nowMs, g_anim_next_ms)) {
            needFrame = true;
            g_anim_frame_index = (uint8_t)((g_anim_frame_index + 1u) % g_anim_frame_count);
        }
        if (!needFrame) return;
        if (g_need_redraw) {
            g_anim_frame_index = 0u;
        }
        ST7789_FillScreen(lcd, g_bg);
        if (g_image_source_valid) {
            draw_image_frame(lcd, g_anim_frame_index);
        }
        if (animated) {
            uint32_t intervalMs = 1000u / (uint32_t)g_anim_fps;
            if (intervalMs == 0u) intervalMs = 1u;
            g_anim_next_ms = nowMs + intervalMs;
        }
    } else if (g_display == 2u) {
        if (!g_need_redraw) return;
        draw_button_layout(lcd, inputMask);
    } else {
        if (!g_need_redraw) return;
        ST7789_FillScreen(lcd, g_bg);
    }
    g_need_redraw = false;
}
