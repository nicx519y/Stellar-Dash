#pragma once
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <vector>
#include <functional>

enum { HAL_OK, HAL_ERROR, HAL_BUSY };
using HAL_StatusTypeDef = int;
enum { HAL_TIM_STATE_RESET, HAL_TIM_STATE_READY };
enum { HAL_DMA_STATE_READY, HAL_DMA_STATE_BUSY, HAL_DMA_STATE_ERROR };
enum { HAL_TIM_CHANNEL_STATE_READY, HAL_TIM_CHANNEL_STATE_BUSY };
enum { TIM_CHANNEL_1 = 0, TIM_CHANNEL_2 = 4 };
enum { TIM_DMA_ID_CC1, TIM_DMA_ID_CC2 };
enum { HAL_TIM_ACTIVE_CHANNEL_CLEARED, HAL_TIM_ACTIVE_CHANNEL_1, HAL_TIM_ACTIVE_CHANNEL_2 };
enum { DMA_SxCR_EN = 1, DMA_IT_HT = 2, DMA_IT_TC = 4, DMA_IT_TE = 8,
       DMA_IT_DME = 16, DMA_IT_FE = 32, HAL_DMA_ERROR_NONE = 0 };
enum { TIM_DMA_CC1 = 2, TIM_DMA_CC2 = 4, TIM_CCER_CC1E = 1, TIM_CCER_CC2E = 16 };
enum { TIM_CR2_CCDS = 8 };
inline constexpr uint32_t DMA_SxCR_CIRC = 64u;
enum { DMA_NORMAL = 0, DMA_CIRCULAR = DMA_SxCR_CIRC };
#define MODIFY_REG(reg, clear, set) ((reg) = ((reg) & ~(clear)) | (set))
enum { RCC_D2CFGR_D2PPRE1 = 7, RCC_HCLK_DIV1 = 0 };
struct DMA_Stream_TypeDef { uint32_t CR = 0, NDTR = 0, FCR = 0; };
struct DMA_HandleTypeDef {
    DMA_Stream_TypeDef* Instance;
    struct { uint32_t Mode = DMA_NORMAL; } Init{};
    int State = HAL_DMA_STATE_READY;
    uint32_t ErrorCode = 0, flags = 0;
    const uint32_t* source = nullptr;
    unsigned size = 0, starts = 0, aborts = 0;
    bool failStart = false, failAbort = false;
    void (*XferCpltCallback)(DMA_HandleTypeDef*) = nullptr;
    void (*XferHalfCpltCallback)(DMA_HandleTypeDef*) = nullptr;
    void (*XferErrorCallback)(DMA_HandleTypeDef*) = nullptr;
};
struct TIM_TypeDef {
    uint32_t CCER = 0, DIER = 0, CNT = 137, EGR = 0, CR2 = 0;
    uint32_t CCR1 = 0, CCR2 = 0;
    uint32_t preload[2] = {}, active[2] = {};
    bool preloadEnabled[2] = {true, true}, enabled = false;
};
struct TIM_HandleTypeDef {
    TIM_TypeDef* Instance;
    DMA_HandleTypeDef* hdma[2];
    int State = HAL_TIM_STATE_RESET;
    int Channel = HAL_TIM_ACTIVE_CHANNEL_CLEARED;
    int channelState[2] = {};
};
inline TIM_TypeDef fakeTim;
inline DMA_Stream_TypeDef streams[2];
inline DMA_HandleTypeDef dmas[2] = {{&streams[0]}, {&streams[1]}};
inline TIM_HandleTypeDef htim4{&fakeTim, {&dmas[0], &dmas[1]}};
inline struct { uint32_t D2CFGR = 1; } fakeRcc;
#define RCC (&fakeRcc)
#define WS2812B_TIM_INSTANCE (&fakeTim)
#define WS2812B_KEYS_TIM_CHANNEL TIM_CHANNEL_1
#define WS2812B_AMBIENT_TIM_CHANNEL TIM_CHANNEL_2
#define WS2812B_TIM_PERIOD 307u
#define NUM_ADC_BUTTONS 18u
#define NUM_GPIO_BUTTONS 4u
#define NUM_LED_AROUND 40u
#define NUM_LEDs_PER_ADC_BUTTON 1u
#define APP_DBG(...) do { if (false) std::printf(__VA_ARGS__); } while (0)
#define APP_ERR(...) do { if (false) std::printf(__VA_ARGS__); } while (0)
inline uint32_t irqMask = 0;
inline std::function<void()> setupHook;
inline uint32_t __get_PRIMASK() { return irqMask; }
inline void __disable_irq() { irqMask = 1; }
inline void __enable_irq() { irqMask = 0; }
inline void __DMB() {}
inline uint32_t HAL_RCC_GetPCLK1Freq() { return 120000000; }
inline void MX_TIM4_Init() {
    htim4.State = HAL_TIM_STATE_READY;
    fakeTim.CR2 |= TIM_CR2_CCDS; // Matched by the production-init contract test.
}
inline bool rail[2] = {};
inline unsigned railWrites[2] = {};
inline void BoardPower_SetKeyLedEnabled(bool enabled) { rail[0] = enabled; ++railWrites[0]; }
inline void BoardPower_SetAmbientLedEnabled(bool enabled) { rail[1] = enabled; ++railWrites[1]; }
struct RGBColor { uint8_t r, g, b; };
inline uint32_t RGBToHex(uint8_t r, uint8_t g, uint8_t b) {
    return (uint32_t(g) << 16) | (uint32_t(r) << 8) | b;
}
#define TIM_CHANNEL_STATE_SET(h,c,s) ((h)->channelState[(c) / 4] = (s))
#define __HAL_DMA_GET_HT_FLAG_INDEX(h) DMA_IT_HT
#define __HAL_DMA_GET_TC_FLAG_INDEX(h) DMA_IT_TC
#define __HAL_DMA_GET_TE_FLAG_INDEX(h) DMA_IT_TE
#define __HAL_DMA_GET_DME_FLAG_INDEX(h) DMA_IT_DME
#define __HAL_DMA_GET_FE_FLAG_INDEX(h) DMA_IT_FE
#define __HAL_DMA_CLEAR_FLAG(h,f) ((h)->flags &= ~(f))
#define __HAL_DMA_GET_COUNTER(h) ((h)->Instance->NDTR)
inline void disableDmaInterrupt(DMA_HandleTypeDef* h, unsigned mask) {
    assert(mask == DMA_IT_FE || !(mask & DMA_IT_FE));
    if (mask == DMA_IT_FE) h->Instance->FCR &= ~mask;
    else h->Instance->CR &= ~mask;
}
#define __HAL_DMA_DISABLE_IT(h,i) disableDmaInterrupt(h,i)
#define __HAL_DMA_DISABLE(h) ((h)->Instance->CR &= ~DMA_SxCR_EN)
#define __HAL_TIM_DISABLE_DMA(h,r) ((h)->Instance->DIER &= ~(r))
inline void enableTimer(TIM_HandleTypeDef* h) {
    if (setupHook) setupHook();
    h->Instance->enabled = true;
}
#define __HAL_TIM_ENABLE(h) enableTimer(h)
#define __HAL_TIM_DISABLE(h) ((h)->Instance->enabled = false)
#define __HAL_TIM_DISABLE_OCxPRELOAD(h,c) ((h)->Instance->preloadEnabled[(c) / 4] = false)
#define __HAL_TIM_ENABLE_OCxPRELOAD(h,c) ((h)->Instance->preloadEnabled[(c) / 4] = true)
inline void setCompare(TIM_HandleTypeDef* h, unsigned channel, uint32_t value) {
    unsigned i = channel / 4;
    h->Instance->preload[i] = value;
    if (!h->Instance->preloadEnabled[i]) h->Instance->active[i] = value;
}
#define __HAL_TIM_SET_COMPARE(h,c,v) setCompare(h,c,v)
inline unsigned cleans = 0;
inline const uint32_t* lastClean = nullptr;
inline int32_t lastCleanBytes = 0;
inline std::function<void()> cleanHook;
inline std::vector<std::pair<const uint32_t*, int32_t>> cleanRanges;
inline void SCB_CleanDCache_by_Addr(uint32_t* address, int32_t size) {
    assert((uintptr_t(address) & 31u) == 0 && (size & 31) == 0);
    assert(irqMask == 0); // Encoding/cache work must remain preemptible.
    ++cleans; lastClean = address; lastCleanBytes = size;
    cleanRanges.emplace_back(address, size);
    if (cleanHook) cleanHook();
}
inline HAL_StatusTypeDef HAL_DMA_Start_IT(DMA_HandleTypeDef* handle, uintptr_t src,
                                         uintptr_t dst, uint32_t n) {
    assert(irqMask == 1);
    auto* h = &htim4;
    unsigned i = handle == &dmas[0] ? 0 : 1;
    auto* p = reinterpret_cast<const uint32_t*>(src);
    auto& d = *h->hdma[i];
    assert(dst == reinterpret_cast<uintptr_t>(i ? &fakeTim.CCR2 : &fakeTim.CCR1));
    assert((d.Instance->CR & DMA_SxCR_EN) == 0);
    assert(d.State == HAL_DMA_STATE_READY);
    assert(h->channelState[i] == HAL_TIM_CHANNEL_STATE_BUSY);
    bool clean = false;
    for (const auto& range : cleanRanges) {
        if (range.first == p && range.second == static_cast<int32_t>(n * 4)) clean = true;
    }
    assert(clean);
    assert(h->Instance->active[i] == 0 && h->Instance->preload[i] == 0);
    assert(d.flags == 0);
    assert(!(fakeTim.DIER & (i ? TIM_DMA_CC2 : TIM_DMA_CC1)));
    assert(d.XferCpltCallback && d.XferErrorCallback && !d.XferHalfCpltCallback);
    ++d.starts;
    if (setupHook) setupHook();
    if (d.failStart) return HAL_ERROR;
    d.source = p; d.size = n; d.State = HAL_DMA_STATE_BUSY;
    d.ErrorCode = HAL_DMA_ERROR_NONE;
    d.Instance->NDTR = n;
    d.Instance->CR = (d.Instance->CR & DMA_SxCR_CIRC) | DMA_SxCR_EN | DMA_IT_TC | DMA_IT_TE | DMA_IT_DME;
    return HAL_OK;
}
inline void enableTimerDma(TIM_HandleTypeDef* h, uint32_t request) {
    unsigned i = request == TIM_DMA_CC1 ? 0 : 1;
    assert(h->Instance->enabled);
    assert(h->Instance->CCER & (i ? TIM_CCER_CC2E : TIM_CCER_CC1E));
    assert(dmas[i].Instance->CR & DMA_SxCR_EN);
    assert(!(dmas[i].Instance->CR & DMA_IT_HT));
    assert(h->Instance->active[i] == 0 && h->Instance->preload[i] == 0);
    h->Instance->DIER |= request;
}
#define __HAL_TIM_ENABLE_DMA(h,r) enableTimerDma(h,r)
inline HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef* d) {
    assert(irqMask == 0);
    assert(d->State == HAL_DMA_STATE_BUSY); // READY is not a HAL abort success.
    ++d->aborts;
    if (d->failAbort) return HAL_ERROR;
    d->Instance->CR = 0;
    d->State = HAL_DMA_STATE_READY;
    d->flags = 0;
    return HAL_OK;
}
