#include "screen_control/jpeg_player.hpp"
#include "stm32h7xx_hal_jpeg.h"
#include "stm32h7xx_hal_dma2d.h"
#include <cstring>

namespace {
JPEG_HandleTypeDef decoder{};
DMA2D_HandleTypeDef converter{};
alignas(32) uint8_t mcuRow[7680] __attribute__((section(".DMA_Section.Jpeg")));
ST7789_Handle* target = nullptr;
bool active = false, failed = false, decoded = false, converting = false;
uint32_t pendingBytes = 0, rowBytes = 0, rowHeight = 0, rowY = 0, started = 0;
void cacheClean(void* data, uint32_t size) {
    SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(data), int32_t((size + 31u) & ~31u));
    __DSB();
}
void cacheInvalidate(void* data, uint32_t size) {
    SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(data), int32_t((size + 31u) & ~31u));
    __DSB();
}
}
void ScreenJpeg_Cancel() {
    if (active) {
        __HAL_RCC_JPEG_FORCE_RESET(); __HAL_RCC_JPEG_RELEASE_RESET();
        __HAL_RCC_DMA2D_FORCE_RESET(); __HAL_RCC_DMA2D_RELEASE_RESET();
        if (target) target->dirty_valid = false;
    }
    active = false; target = nullptr;
}
bool ScreenJpeg_Active() { return active; }
bool ScreenJpeg_Begin(const uint8_t* jpeg, uint32_t length, ST7789_Handle* lcd) {
    ScreenJpeg_Cancel();
    if (!jpeg || (reinterpret_cast<uintptr_t>(jpeg) & 3u) || !length || (length & 3u) ||
        !lcd || !lcd->framebuffer_enabled || !lcd->fb_back) return false;
    __HAL_RCC_JPEG_CLK_ENABLE(); __HAL_RCC_DMA2D_CLK_ENABLE();
    __HAL_RCC_JPEG_FORCE_RESET(); __HAL_RCC_JPEG_RELEASE_RESET();
    __HAL_RCC_DMA2D_FORCE_RESET(); __HAL_RCC_DMA2D_RELEASE_RESET();
    // IRQs stay disabled: main-loop polling bounds FIFO service and avoids
    // high-rate JPEG interrupts delaying the controller's input work.
    HAL_NVIC_DisableIRQ(JPEG_IRQn); HAL_NVIC_ClearPendingIRQ(JPEG_IRQn);
    decoder = {}; decoder.Instance = JPEG;
    converter = {}; converter.Instance = DMA2D;
    converter.Init.Mode = DMA2D_M2M_PFC;
    converter.Init.ColorMode = DMA2D_OUTPUT_RGB565;
    converter.Init.OutputOffset = 0;
    if (HAL_JPEG_Init(&decoder) != HAL_OK || HAL_DMA2D_Init(&converter) != HAL_OK) return false;
    target = lcd; active = true; failed = decoded = converting = false;
    pendingBytes = rowBytes = rowHeight = rowY = 0;
    started = HAL_GetTick();
    cacheClean(lcd->fb_back, 320u * 172u * 2u);
    cacheInvalidate(lcd->fb_back, 320u * 172u * 2u);
    if (HAL_JPEG_Decode_IT(&decoder, const_cast<uint8_t*>(jpeg), length, mcuRow, sizeof(mcuRow)) != HAL_OK) {
        ScreenJpeg_Cancel(); return false;
    }
    return true;
}
int ScreenJpeg_Poll() {
    if (!active) return -1;
    if (failed || uint32_t(HAL_GetTick() - started) >= 150u) {
        ScreenJpeg_Cancel(); return -1;
    }
    if (converting) {
        if (DMA2D->ISR & (DMA2D_ISR_TEIF | DMA2D_ISR_CEIF)) { ScreenJpeg_Cancel(); return -1; }
        if (DMA2D->CR & DMA2D_CR_START) return 0;
        // Finalize the completed transfer to clear TC and release HAL's lock.
        // START is already clear, so this never waits for the peripheral.
        if (HAL_DMA2D_PollForTransfer(&converter, 0u) != HAL_OK) { ScreenJpeg_Cancel(); return -1; }
        const uint32_t lines = rowY + rowHeight > 172u ? 172u - rowY : rowHeight;
        cacheInvalidate(target->fb_back + rowY * 320u, lines * 640u);
        rowY += rowHeight; converting = false; pendingBytes = 0;
        if (!decoded) {
            HAL_JPEG_ConfigOutputBuffer(&decoder, mcuRow, rowBytes);
            if (HAL_JPEG_Resume(&decoder, JPEG_PAUSE_RESUME_OUTPUT) != HAL_OK) failed = true;
        }
    }
    // One call handles at most a small FIFO batch; callers regain control even
    // while the peripheral is starved or a corrupt stream never completes.
    for (unsigned batch = 0; batch < 32u && !pendingBytes && !decoded && !failed; batch++) {
        HAL_JPEG_IRQHandler(&decoder);
    }
    if (failed) { ScreenJpeg_Cancel(); return -1; }
    if (pendingBytes) {
        if (!rowBytes || pendingBytes != rowBytes || rowY >= 172u) { ScreenJpeg_Cancel(); return -1; }
        const uint32_t lines = rowY + rowHeight > 172u ? 172u - rowY : rowHeight;
        cacheClean(mcuRow, pendingBytes);
        if (HAL_DMA2D_Start(&converter, reinterpret_cast<uint32_t>(mcuRow),
                reinterpret_cast<uint32_t>(target->fb_back + rowY * 320u), 320u, lines) != HAL_OK) {
            ScreenJpeg_Cancel(); return -1;
        }
        converting = true;
        return 0;
    }
    if (decoded) {
        if (rowY < 172u) { ScreenJpeg_Cancel(); return -1; }
        target->dirty_valid = true;
        target->dirty_x0 = target->dirty_y0 = 0;
        target->dirty_x1 = 319; target->dirty_y1 = 171;
        active = false; target = nullptr;
        return 1;
    }
    return 0;
}
extern "C" void HAL_JPEG_InfoReadyCallback(JPEG_HandleTypeDef* handle, JPEG_ConfTypeDef* info) {
    if (handle != &decoder) return;
    if (info->ImageWidth != 320u || info->ImageHeight != 172u || info->ColorSpace != JPEG_YCBCR_COLORSPACE) { failed = true; return; }
    auto& layer = converter.LayerCfg[1];
    layer.InputColorMode = DMA2D_INPUT_YCBCR; layer.InputOffset = 0;
    layer.AlphaMode = DMA2D_NO_MODIF_ALPHA; layer.InputAlpha = 255;
    if (info->ChromaSubsampling == JPEG_420_SUBSAMPLING) {
        rowHeight = 16u; rowBytes = 7680u; layer.ChromaSubSampling = DMA2D_CSS_420;
    } else if (info->ChromaSubsampling == JPEG_422_SUBSAMPLING) {
        rowHeight = 8u; rowBytes = 5120u; layer.ChromaSubSampling = DMA2D_CSS_422;
    } else if (info->ChromaSubsampling == JPEG_444_SUBSAMPLING) {
        rowHeight = 8u; rowBytes = 7680u; layer.ChromaSubSampling = DMA2D_NO_CSS;
    } else { failed = true; return; }
    if (HAL_DMA2D_ConfigLayer(&converter, 1u) != HAL_OK) failed = true;
    HAL_JPEG_ConfigOutputBuffer(&decoder, mcuRow, rowBytes);
}
extern "C" void HAL_JPEG_DataReadyCallback(JPEG_HandleTypeDef* handle, uint8_t*, uint32_t length) {
    if (handle != &decoder) return;
    if (pendingBytes) { failed = true; return; }
    pendingBytes = length;
    if (HAL_JPEG_Pause(handle, JPEG_PAUSE_RESUME_OUTPUT) != HAL_OK) failed = true;
}
extern "C" void HAL_JPEG_GetDataCallback(JPEG_HandleTypeDef* handle, uint32_t) {
    if (handle == &decoder) HAL_JPEG_Pause(handle, JPEG_PAUSE_RESUME_INPUT);
}
extern "C" void HAL_JPEG_DecodeCpltCallback(JPEG_HandleTypeDef* handle) { if (handle == &decoder) decoded = true; }
extern "C" void HAL_JPEG_ErrorCallback(JPEG_HandleTypeDef* handle) { if (handle == &decoder) failed = true; }
