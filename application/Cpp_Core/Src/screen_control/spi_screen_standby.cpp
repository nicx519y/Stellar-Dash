#include "screen_control/spi_screen_standby.hpp"

#include <string.h>

#include "board_cfg.h"
#include "qspi-w25q64.h"

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
static const uint8_t* g_image_pixels = nullptr;
static uint16_t g_image_w = 0;
static uint16_t g_image_h = 0;
static uint8_t g_anim_frame_count = 1;
static uint8_t g_anim_fps = 0;
static uint32_t g_anim_frame_size = 0;
static uint32_t g_anim_frame_offsets[10] = {0};
static uint8_t g_anim_frame_index = 0;
static uint32_t g_anim_next_ms = 0;

enum : uint8_t {
    STANDBY_IMAGE_NONE = 0u,
    STANDBY_IMAGE_HIMG_RGB565 = 1u,
    STANDBY_IMAGE_UIMG = 2u
};

static const uint32_t UIMG_MAGIC = 0x474D4955u;
static const uint8_t UIMG_FORMAT_RGB565_SINGLE = 1u;
static const uint8_t UIMG_FORMAT_RGB565_SEQUENCE = 2u;
static const uint32_t UIMG_HEADER_SIZE = 4096u;
static const uint32_t SYSBG_MAX_FRAMES = 8u;
static const uint32_t SYSBG_FRAME_W = 320u;
static const uint32_t SYSBG_FRAME_H = 172u;
static const uint32_t SYSBG_FRAME_SIZE = SYSBG_FRAME_W * SYSBG_FRAME_H * 2u;
static const uint32_t SYSBG_RESERVED_SIZE = UIMG_HEADER_SIZE + SYSBG_MAX_FRAMES * SYSBG_FRAME_SIZE;
static const uint32_t USER_IMAGE_BASE_ADDR = USER_IMAGE_RESOURCES_ADDR + SYSBG_RESERVED_SIZE;
static const uint32_t USER_IMAGE_AREA_SIZE = USER_IMAGE_RESOURCES_SIZE - SYSBG_RESERVED_SIZE;

static const char* USER_IMAGE_ID = "USER_IMAGE";
static const char* SYSTEM_IMAGE_ID = "SYSTEM_DEFAULT";

#pragma pack(push, 1)
typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint8_t valid;
    uint8_t format;
    uint16_t width;
    uint16_t height;
    uint32_t size;
    uint32_t offset;
    char id[16];
} StandbyUimgHeaderV1;

typedef struct
{
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
    uint32_t frame_offsets[10];
    char id[16];
} StandbyUimgHeaderV2;
#pragma pack(pop)

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
    g_image_pixels = nullptr;
    g_image_w = 0;
    g_image_h = 0;
    g_anim_frame_count = 1u;
    g_anim_fps = 0u;
    g_anim_frame_size = 0u;
    memset(g_anim_frame_offsets, 0, sizeof(g_anim_frame_offsets));
    g_anim_frame_index = 0u;
    g_anim_next_ms = 0u;
}

static bool parse_uimg_v1(const StandbyUimgHeaderV1* v1, uint32_t maxSize)
{
    if (!v1) return false;
    if (v1->magic != UIMG_MAGIC || v1->valid != 1u) return false;
    uint32_t frameSize = (uint32_t)v1->width * (uint32_t)v1->height * 2u;
    if (frameSize == 0u) return false;
    if (v1->size != frameSize) return false;
    if (v1->offset < UIMG_HEADER_SIZE) return false;
    if ((uint64_t)v1->offset + (uint64_t)frameSize > (uint64_t)maxSize) return false;

    g_image_kind = STANDBY_IMAGE_UIMG;
    g_image_w = v1->width;
    g_image_h = v1->height;
    g_anim_frame_count = 1u;
    g_anim_fps = 0u;
    g_anim_frame_size = frameSize;
    g_anim_frame_offsets[0] = v1->offset;
    return true;
}

static bool parse_uimg_v2(const StandbyUimgHeaderV2* v2, uint32_t maxSize)
{
    if (!v2) return false;
    if (v2->magic != UIMG_MAGIC || v2->valid != 1u) return false;

    uint8_t frameCount = v2->frame_count;
    if (frameCount == 0u) frameCount = 1u;
    if (frameCount > 10u) frameCount = 10u;
    uint8_t fps = v2->fps;
    if (fps > 5u) fps = 5u;
    uint32_t frameSize = (uint32_t)v2->width * (uint32_t)v2->height * 2u;
    if (frameSize == 0u) return false;
    if (v2->frame_size != frameSize) return false;
    if (v2->frames_offset < UIMG_HEADER_SIZE) return false;
    if ((uint64_t)v2->frames_offset + (uint64_t)frameSize > (uint64_t)maxSize) return false;

    if (v2->format == UIMG_FORMAT_RGB565_SEQUENCE) {
        uint32_t expected = frameSize * (uint32_t)frameCount;
        if (v2->total_size != expected) return false;
    } else if (v2->format == UIMG_FORMAT_RGB565_SINGLE) {
        frameCount = 1u;
        fps = 0u;
        if (v2->total_size != frameSize) return false;
    } else {
        return false;
    }

    for (uint8_t i = 0; i < frameCount; i++) {
        uint32_t off = v2->frame_offsets[i];
        if (off == 0u) {
            off = v2->frames_offset + (uint32_t)i * frameSize;
        }
        if (off < UIMG_HEADER_SIZE) return false;
        if ((uint64_t)off + (uint64_t)frameSize > (uint64_t)maxSize) return false;
        g_anim_frame_offsets[i] = off;
    }

    g_image_kind = STANDBY_IMAGE_UIMG;
    g_image_w = v2->width;
    g_image_h = v2->height;
    g_anim_frame_count = frameCount;
    g_anim_fps = fps;
    g_anim_frame_size = frameSize;
    return true;
}

static bool resolve_uimg_source(const char* imageId)
{
    if (!imageId || imageId[0] == '\0') return false;
    if (!QSPI_W25Qxx_IsMemoryMappedMode()) return false;

    uint32_t baseAddr = 0u;
    uint32_t areaSize = 0u;
    if (strncmp(imageId, SYSTEM_IMAGE_ID, 16) == 0) {
        baseAddr = USER_IMAGE_RESOURCES_ADDR;
        areaSize = SYSBG_RESERVED_SIZE;
    } else if (strncmp(imageId, USER_IMAGE_ID, 16) == 0) {
        baseAddr = USER_IMAGE_BASE_ADDR;
        areaSize = USER_IMAGE_AREA_SIZE;
    } else {
        return false;
    }
    if (areaSize < sizeof(StandbyUimgHeaderV1)) return false;

    const uint8_t* base = (const uint8_t*)(uintptr_t)baseAddr;
    StandbyUimgHeaderV1 v1 = {0};
    memcpy(&v1, base, sizeof(v1));
    if (v1.magic != UIMG_MAGIC || v1.valid != 1u) return false;

    memset(g_anim_frame_offsets, 0, sizeof(g_anim_frame_offsets));
    if (v1.version == 1u) {
        return parse_uimg_v1(&v1, areaSize);
    }
    if (areaSize < sizeof(StandbyUimgHeaderV2)) return false;
    StandbyUimgHeaderV2 v2 = {0};
    memcpy(&v2, base, sizeof(v2));
    return parse_uimg_v2(&v2, areaSize);
}

static bool resolve_himg_source(const char* imageId)
{
    if (!imageId || imageId[0] == '\0') return false;
    if (!QSPI_W25Qxx_IsMemoryMappedMode()) return false;

    ST7789_AssetInfo info;
    memset(&info, 0, sizeof(info));
    if (!ST7789_Assets_Find(imageId, &info)) return false;
    if (info.type != ST7789_ASSET_TYPE_RGB565LE) return false;
    if (!info.data || info.width == 0u || info.height == 0u) return false;

    g_image_kind = STANDBY_IMAGE_HIMG_RGB565;
    g_image_pixels = (const uint8_t*)info.data;
    g_image_w = info.width;
    g_image_h = info.height;
    g_anim_frame_count = 1u;
    g_anim_fps = 0u;
    g_anim_frame_size = (uint32_t)info.width * 2u * (uint32_t)info.height;
    return true;
}

static void ensure_image_source(void)
{
    if (g_image_source_ready) return;
    g_image_source_ready = true;
    g_image_source_valid = false;
    g_image_kind = STANDBY_IMAGE_NONE;
    g_image_pixels = nullptr;
    g_image_w = 0;
    g_image_h = 0;
    g_anim_frame_count = 1u;
    g_anim_fps = 0u;
    g_anim_frame_size = 0u;
    memset(g_anim_frame_offsets, 0, sizeof(g_anim_frame_offsets));
    if (!ensure_qspi_mmap()) {
        return;
    }

    if (resolve_uimg_source(g_bg_image_id)) {
        g_image_source_valid = true;
        return;
    }
    if (resolve_himg_source(g_bg_image_id)) {
        g_image_source_valid = true;
    }
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

    if (g_image_kind == STANDBY_IMAGE_HIMG_RGB565) {
        if (!g_image_pixels) return;
        ST7789_DrawBitmap(lcd, x, y, g_image_w, g_image_h, g_image_pixels, ST7789_BITMAP_RGB565_LE, (uint32_t)g_image_w * 2u);
        return;
    }
    if (g_image_kind == STANDBY_IMAGE_UIMG) {
        if (frameIndex >= g_anim_frame_count) frameIndex = 0u;
        uint32_t addr = 0u;
        if (strncmp(g_bg_image_id, SYSTEM_IMAGE_ID, 16) == 0) {
            addr = USER_IMAGE_RESOURCES_ADDR + g_anim_frame_offsets[frameIndex];
        } else {
            addr = USER_IMAGE_BASE_ADDR + g_anim_frame_offsets[frameIndex];
        }
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
