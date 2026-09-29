#include "pwm-ws2812b.h"
#include "board_cfg.h"
#include "board_power.hpp"

/* WS2812B-Mini-V3J data protocol (datasheet):
|-------------------------------------------|
|T0H | 220ns ~ 380ns
|T1H | 580ns ~ 1us
|T0L | 580ns ~ 1us
|T1L | 580ns ~ 1us
|RES | above 280us |
|-------------------------------------------
*/

/* 240MHz timer clock, period=308 ticks -> 1.283us (779.22kHz).
 *
 * Both key-module schematics populate WS2812B-MINI-V3J.  Keep the high and
 * low portions away from the datasheet limits; 160 ticks previously left
 * only about 3ns of T1L margin.  The centered values also leave more time for
 * the next CCR DMA write before the following PWM period (TIM4 now requests
 * CC DMA on update via CCDS, giving a full period independent of duty):
 *   T1H = 150/240MHz = 625ns, T1L = 658ns
 *   T0H =  72/240MHz = 300ns, T0L = 983ns
 *
 * The ambient path already uses and has verified the same centered timing.
 */
#define KEYS_HIGH_CCR_CODE       150u
#define KEYS_LOW_CCR_CODE         72u
#define AMBIENT_HIGH_CCR_CODE    150u
#define AMBIENT_LOW_CCR_CODE      72u

/* One reset slot is 24 zero-duty PWM periods, about 30.8us at 779.22kHz.
 * Ten slots produce about 308us, the shortest whole-slot value that remains
 * above the WS2812B-MINI-V3J datasheet reset/latch minimum of 280us. */
#define WS2812B_KEYS_RESET_SLOT_COUNT       10u
#define WS2812B_AMBIENT_RESET_SLOT_COUNT    10u
#define WS2812B_FRAME_BUFFER_LEN_FOR(count, resetSlots) \
    ((((uint32_t)(count) + (uint32_t)(resetSlots)) * 24u) * \
     (uint32_t)NUM_LEDs_PER_ADC_BUTTON)

#define LED_DEFAULT_BRIGHTNESS 128

enum {
    WS2812B_KEYS_LED_COUNT = (NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS),
    WS2812B_AMBIENT_LED_COUNT = NUM_LED_AROUND
};

enum {
    WS2812B_KEYS_DMA_BUFFER_LEN = (int)WS2812B_FRAME_BUFFER_LEN_FOR(
        WS2812B_KEYS_LED_COUNT, WS2812B_KEYS_RESET_SLOT_COUNT),
    WS2812B_AMBIENT_DMA_BUFFER_LEN = (int)WS2812B_FRAME_BUFFER_LEN_FOR(
        WS2812B_AMBIENT_LED_COUNT, WS2812B_AMBIENT_RESET_SLOT_COUNT)
};

static bool g_keys_initialized = false;
static bool g_ambient_initialized = false;

static volatile WS2812B_StateTypeDef g_keys_state = WS2812B_STOP;
static volatile WS2812B_StateTypeDef g_ambient_state = WS2812B_STOP;

static volatile bool g_keys_dirty = false;
static volatile bool g_ambient_dirty = false;

typedef enum {
    WS2812B_UPDATE_IDLE = 0,
    WS2812B_UPDATE_ENCODING,
    WS2812B_UPDATE_TRANSMITTING
} WS2812B_UpdatePhase;

static volatile WS2812B_UpdatePhase g_keys_update_phase = WS2812B_UPDATE_IDLE;
static volatile WS2812B_UpdatePhase g_ambient_update_phase = WS2812B_UPDATE_IDLE;
static volatile uint32_t g_keys_tc_count = 0u;
static volatile uint32_t g_ambient_tc_count = 0u;
static volatile uint32_t g_keys_published_generation = 0u;
static volatile uint32_t g_keys_in_flight_generation = 0u;
static volatile uint32_t g_keys_applied_generation = 0u;
static volatile uint32_t g_ambient_published_generation = 0u;
static volatile uint32_t g_ambient_in_flight_generation = 0u;
static volatile uint32_t g_ambient_applied_generation = 0u;

static uint32_t get_tim4_clock_hz(void)
{
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t ppre1 = (RCC->D2CFGR & RCC_D2CFGR_D2PPRE1);
    if (ppre1 != RCC_HCLK_DIV1) {
        return pclk1 * 2u;
    }
    return pclk1;
}

/* Setters and lifecycle APIs are main-loop owned. Only completion/error
 * callbacks run in interrupt context. The edit frame may change at any time;
 * the submitted snapshot and DMA frame are exclusively owned by ENCODING or
 * TRANSMITTING until the NORMAL transfer has completed (including reset tail).
 * Delayed interrupts retain ownership; no circular half needs refilling. */
static volatile WS2812B_TxDiagnostics g_tx_diagnostics[2];

static uint8_t g_keys_colors[WS2812B_KEYS_LED_COUNT * 3u];
static uint8_t g_keys_brightness[WS2812B_KEYS_LED_COUNT];
static uint8_t g_keys_submitted_colors[WS2812B_KEYS_LED_COUNT * 3u];
static uint8_t g_keys_submitted_brightness[WS2812B_KEYS_LED_COUNT];
/* Keep keys DMA buffer in .DMA_Section and retain legacy symbol name. */
static __attribute__((section(".DMA_Section"), aligned(32))) uint32_t DMA_LED_Buffer[WS2812B_KEYS_DMA_BUFFER_LEN];

static uint8_t g_ambient_colors[WS2812B_AMBIENT_LED_COUNT * 3u];
static uint8_t g_ambient_brightness[WS2812B_AMBIENT_LED_COUNT];
static uint8_t g_ambient_submitted_colors[WS2812B_AMBIENT_LED_COUNT * 3u];
static uint8_t g_ambient_submitted_brightness[WS2812B_AMBIENT_LED_COUNT];
static __attribute__((section(".DMA_Section"), aligned(32))) uint32_t DMA_LED_Buffer_Ambient[WS2812B_AMBIENT_DMA_BUFFER_LEN];

static void cleanDCache(const void *addr, uint32_t size)
{
    if (addr == NULL || size == 0u) {
        return;
    }

    const uintptr_t address = (uintptr_t)addr;
    const uintptr_t alignedAddress = address & ~(uintptr_t)(32u - 1u);
    const uint32_t leadingBytes = (uint32_t)(address - alignedAddress);
    const uint32_t alignedSize =
        (leadingBytes + size + 31u) & ~(32u - 1u);

    /* DMA only reads these buffers: clean modified cache lines, do not
     * invalidate CPU-owned data that may share a cache line. */
    SCB_CleanDCache_by_Addr((uint32_t *)alignedAddress, (int32_t)alignedSize);
}

static uint16_t strip_led_count(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT) ? (uint16_t)WS2812B_AMBIENT_LED_COUNT : (uint16_t)WS2812B_KEYS_LED_COUNT;
}

static uint8_t* strip_colors(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT) ? g_ambient_colors : g_keys_colors;
}

static uint8_t* strip_brightness(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT) ? g_ambient_brightness : g_keys_brightness;
}

static uint32_t* strip_dma_buffer(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT) ? DMA_LED_Buffer_Ambient : DMA_LED_Buffer;
}

static uint32_t strip_high_ccr_code(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT)
        ? AMBIENT_HIGH_CCR_CODE : KEYS_HIGH_CCR_CODE;
}

static uint32_t strip_low_ccr_code(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT)
        ? AMBIENT_LOW_CCR_CODE : KEYS_LOW_CCR_CODE;
}

static uint32_t strip_reset_slot_count(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT)
        ? WS2812B_AMBIENT_RESET_SLOT_COUNT
        : WS2812B_KEYS_RESET_SLOT_COUNT;
}

static uint8_t* strip_submitted_colors(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT)
        ? g_ambient_submitted_colors
        : g_keys_submitted_colors;
}

static uint8_t* strip_submitted_brightness(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT)
        ? g_ambient_submitted_brightness
        : g_keys_submitted_brightness;
}

static uint32_t strip_dma_buffer_len(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT) ? (uint32_t)WS2812B_AMBIENT_DMA_BUFFER_LEN : (uint32_t)WS2812B_KEYS_DMA_BUFFER_LEN;
}

static uint16_t strip_tim_channel(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT) ? (uint16_t)WS2812B_AMBIENT_TIM_CHANNEL : (uint16_t)WS2812B_KEYS_TIM_CHANNEL;
}

static DMA_HandleTypeDef* strip_dma_handle(WS2812B_Strip strip)
{
    return htim4.hdma[(strip == WS2812B_STRIP_AMBIENT)
        ? TIM_DMA_ID_CC2
        : TIM_DMA_ID_CC1];
}

static void strip_power_write(WS2812B_Strip strip, bool enabled)
{
    if (strip == WS2812B_STRIP_AMBIENT) {
        BoardPower_SetAmbientLedEnabled(enabled);
    } else {
        BoardPower_SetKeyLedEnabled(enabled);
    }
}

static volatile bool* strip_dirty_flag(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT)
        ? &g_ambient_dirty
        : &g_keys_dirty;
}

static volatile WS2812B_UpdatePhase* strip_update_phase(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT)
        ? &g_ambient_update_phase
        : &g_keys_update_phase;
}

static volatile uint32_t* strip_published_generation(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT)
        ? &g_ambient_published_generation
        : &g_keys_published_generation;
}

static volatile uint32_t* strip_in_flight_generation(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT)
        ? &g_ambient_in_flight_generation
        : &g_keys_in_flight_generation;
}

static volatile uint32_t* strip_applied_generation(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT)
        ? &g_ambient_applied_generation
        : &g_keys_applied_generation;
}

static void mark_strip_dirty(WS2812B_Strip strip)
{
    volatile bool* dirty = strip_dirty_flag(strip);
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    *dirty = true;
    if (primask == 0u) {
        __enable_irq();
    }
}

static void led_data_to_buffer(WS2812B_Strip strip,
                               uint32_t* dma_buf,
                               const uint16_t start,
                               const uint16_t length,
                               const bool clean_for_dma)
{
    uint16_t led_count = strip_led_count(strip);
    uint8_t* colors = strip_submitted_colors(strip);
    uint8_t* br = strip_submitted_brightness(strip);
    const uint32_t highCode = strip_high_ccr_code(strip);
    const uint32_t lowCode = strip_low_ccr_code(strip);

    if (((uintptr_t)dma_buf & 0x1Fu) != 0u) {
        APP_ERR("pwm-ws2812b: Error: DMA buffer not 32-byte aligned");
        return;
    }

    if (start >= led_count) {
        return;
    }

    uint16_t actualLength = length;
    if ((uint32_t)start + (uint32_t)actualLength > led_count) {
        actualLength = (uint16_t)(led_count - start);
    }

    uint16_t end = (uint16_t)(start + actualLength);

    for (uint16_t led = start; led < end; led++) {
        uint16_t cidx = (uint16_t)(led * 3u);
        const uint16_t brightness = br[led];
        uint32_t color = RGBToHex(
            (uint8_t)(((uint16_t)colors[cidx] * brightness + 127u) / 255u),
            (uint8_t)(((uint16_t)colors[cidx + 1u] * brightness + 127u) / 255u),
            (uint8_t)(((uint16_t)colors[cidx + 2u] * brightness + 127u) / 255u)
        );

        for (uint16_t k = 0; k < (uint16_t)NUM_LEDs_PER_ADC_BUTTON; k++) {
            uint32_t base = ((uint32_t)led * (uint32_t)NUM_LEDs_PER_ADC_BUTTON + (uint32_t)k) * 24u;
            for (uint16_t i = 0; i < 24u; i++) {
                uint32_t dma_idx = base + (uint32_t)i;
                dma_buf[dma_idx] = ((0x800000u & (color << i)) != 0u)
                    ? highCode : lowCode;
            }
        }
    }

    const uint32_t firstDmaWord =
        (uint32_t)start * (uint32_t)NUM_LEDs_PER_ADC_BUTTON * 24u;
    const uint32_t dmaWordCount =
        (uint32_t)actualLength * (uint32_t)NUM_LEDs_PER_ADC_BUTTON * 24u;
    if (clean_for_dma) {
        cleanDCache(&dma_buf[firstDmaWord], dmaWordCount * sizeof(uint32_t));
    }
}

static void encode_submitted_frame(WS2812B_Strip strip)
{
    uint32_t* buffer = strip_dma_buffer(strip);
    const uint32_t payload = (uint32_t)strip_led_count(strip) *
        NUM_LEDs_PER_ADC_BUTTON * 24u;
    led_data_to_buffer(strip, buffer, 0u, strip_led_count(strip), false);
    const uint32_t resetWords = strip_reset_slot_count(strip) *
        NUM_LEDs_PER_ADC_BUTTON * 24u;
    memset(&buffer[payload], 0, resetWords * sizeof(uint32_t));
    cleanDCache(buffer, strip_dma_buffer_len(strip) * sizeof(uint32_t));
}

/* Legacy callers must obey the same ownership as normal submissions. */
void LEDDataToDMABuffer(const uint16_t start, const uint16_t length)
{
    WS2812B_RefreshStrip(WS2812B_STRIP_KEYS, start, length);
}

static volatile WS2812B_StateTypeDef* strip_state(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT) ? &g_ambient_state : &g_keys_state;
}

static uint32_t strip_dma_request(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT) ? TIM_DMA_CC2 : TIM_DMA_CC1;
}

static void disable_strip_output(WS2812B_Strip strip)
{
    htim4.Instance->CCER &= (strip == WS2812B_STRIP_AMBIENT)
        ? ~TIM_CCER_CC2E : ~TIM_CCER_CC1E;
}

static void clear_strip_dma_flags(DMA_HandleTypeDef* hdma)
{
    __HAL_DMA_CLEAR_FLAG(hdma,
        __HAL_DMA_GET_HT_FLAG_INDEX(hdma) | __HAL_DMA_GET_TC_FLAG_INDEX(hdma) |
        __HAL_DMA_GET_TE_FLAG_INDEX(hdma) | __HAL_DMA_GET_DME_FLAG_INDEX(hdma) |
        __HAL_DMA_GET_FE_FLAG_INDEX(hdma));
}

/* Called only with this channel disabled and its DMA stream stopped.
 * Clear both active CCR and preload without generating a shared TIM4 UG or
 * resetting CNT: CH2 may be transmitting while CH1 is being restarted. */
static void reset_strip_compare(WS2812B_Strip strip)
{
    const uint32_t channel = strip_tim_channel(strip);
    __HAL_TIM_DISABLE_OCxPRELOAD(&htim4, channel);
    __HAL_TIM_SET_COMPARE(&htim4, channel, 0u);
    __HAL_TIM_ENABLE_OCxPRELOAD(&htim4, channel);
    __HAL_TIM_SET_COMPARE(&htim4, channel, 0u);
}

static bool callback_strip(TIM_HandleTypeDef* htim, WS2812B_Strip* strip)
{
    if (htim != &htim4 || htim->Instance != WS2812B_TIM_INSTANCE) {
        return false;
    }
    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
        *strip = WS2812B_STRIP_KEYS;
    } else if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2) {
        *strip = WS2812B_STRIP_AMBIENT;
    } else {
        return false;
    }
    return true;
}

/* No polling, printing or automatic retry in interrupt context. A failed
 * stream remains owned until explicit Stop verifies it has stopped. */
static void fail_strip(WS2812B_Strip strip)
{
    *strip_state(strip) = WS2812B_ERROR;
    *strip_dirty_flag(strip) = true;
    __HAL_TIM_DISABLE_DMA(&htim4, strip_dma_request(strip));
    disable_strip_output(strip);
    DMA_HandleTypeDef* hdma = strip_dma_handle(strip);
    if (hdma != NULL) {
        __HAL_DMA_DISABLE_IT(hdma, DMA_IT_HT | DMA_IT_TC | DMA_IT_TE | DMA_IT_DME);
        /* FE is in FCR, unlike the other interrupt enables in CR. */
        __HAL_DMA_DISABLE_IT(hdma, DMA_IT_FE);
        __HAL_DMA_DISABLE(hdma);
    }
    strip_power_write(strip, false);
}

static bool map_global_index(uint16_t globalIndex, WS2812B_Strip* strip, uint16_t* localIndex)
{
    if (globalIndex < (uint16_t)WS2812B_KEYS_LED_COUNT) {
        *strip = WS2812B_STRIP_KEYS;
        *localIndex = globalIndex;
        return true;
    }

    uint16_t ambientGlobalBase = (uint16_t)WS2812B_KEYS_LED_COUNT;
    uint16_t ambientGlobalEnd = (uint16_t)(ambientGlobalBase + WS2812B_AMBIENT_LED_COUNT);

    if (globalIndex < ambientGlobalEnd) {
        *strip = WS2812B_STRIP_AMBIENT;
        *localIndex = (uint16_t)(globalIndex - ambientGlobalBase);
        return true;
    }

    return false;
}

static void complete_strip(WS2812B_Strip strip)
{
    DMA_HandleTypeDef* hdma = strip_dma_handle(strip);
    volatile WS2812B_UpdatePhase* phase = strip_update_phase(strip);
#if HBOX_LED_DMA_DIAGNOSTIC
    if (hdma != NULL && hdma->Init.Mode == DMA_CIRCULAR &&
        *strip_state(strip) == WS2812B_RUNNING &&
        *phase == WS2812B_UPDATE_TRANSMITTING &&
        hdma->ErrorCode == HAL_DMA_ERROR_NONE) {
        /* The diagnostic circular frame is immutable for its whole stage.
         * Count callbacks only; never release ownership or copy in the ISR. */
        if (strip == WS2812B_STRIP_KEYS) ++g_keys_tc_count;
        else ++g_ambient_tc_count;
        return;
    }
#endif
    if (hdma == NULL || *strip_state(strip) != WS2812B_RUNNING ||
        *phase != WS2812B_UPDATE_TRANSMITTING ||
        hdma->ErrorCode != HAL_DMA_ERROR_NONE ||
        (((DMA_Stream_TypeDef*)hdma->Instance)->CR & DMA_SxCR_EN) != 0u ||
        __HAL_DMA_GET_COUNTER(hdma) != 0u) {
        return;
    }

    /* NORMAL DMA stopped on its final zero. The 240-period reset tail has
     * already held the output low for >280us even allowing CCR preload's
     * one-period pipeline. Keep PWM enabled at CCR=0 between submissions. */
    __HAL_TIM_DISABLE_DMA(&htim4, strip_dma_request(strip));
    TIM_CHANNEL_STATE_SET(&htim4, strip_tim_channel(strip), HAL_TIM_CHANNEL_STATE_READY);
    if (strip == WS2812B_STRIP_AMBIENT) {
        ++g_ambient_tc_count;
    } else {
        ++g_keys_tc_count;
    }
    *strip_applied_generation(strip) = *strip_in_flight_generation(strip);
    *strip_in_flight_generation(strip) = 0u;
    __DMB();
    *phase = WS2812B_UPDATE_IDLE;
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    WS2812B_Strip strip;
    if (callback_strip(htim, &strip)) {
        complete_strip(strip);
    }
}

/* Direct DMA callbacks identify the strip by its handle, without changing the
 * shared TIM handle's Channel field. HAL_DMA_IRQHandler owns the DMA state. */
static void strip_dma_complete(DMA_HandleTypeDef* hdma)
{
    if (hdma == strip_dma_handle(WS2812B_STRIP_KEYS)) {
        complete_strip(WS2812B_STRIP_KEYS);
    } else if (hdma == strip_dma_handle(WS2812B_STRIP_AMBIENT)) {
        complete_strip(WS2812B_STRIP_AMBIENT);
    }
}

static void strip_dma_error(DMA_HandleTypeDef* hdma)
{
    WS2812B_Strip strip;
    if (hdma == strip_dma_handle(WS2812B_STRIP_KEYS)) {
        strip = WS2812B_STRIP_KEYS;
    } else if (hdma == strip_dma_handle(WS2812B_STRIP_AMBIENT)) {
        strip = WS2812B_STRIP_AMBIENT;
    } else {
        return;
    }
    if (*strip_state(strip) == WS2812B_RUNNING) {
        ++g_tx_diagnostics[strip].dmaErrors;
        fail_strip(strip);
    }
}

void HAL_TIM_PWM_PulseFinishedHalfCpltCallback(TIM_HandleTypeDef *htim)
{
    /* HT is disabled. A stale/foreign callback must never write a frame. */
    (void)htim;
}

void HAL_TIM_ErrorCallback(TIM_HandleTypeDef *htim)
{
    WS2812B_Strip strip;
    if (callback_strip(htim, &strip) &&
        *strip_state(strip) == WS2812B_RUNNING) {
        ++g_tx_diagnostics[strip].dmaErrors;
        fail_strip(strip);
    }
}

void WS2812B_InitStrip(WS2812B_Strip strip)
{
    bool* initialized = (strip == WS2812B_STRIP_AMBIENT) ? &g_ambient_initialized : &g_keys_initialized;
    uint8_t* br = strip_brightness(strip);
    uint8_t* colors = strip_colors(strip);
    uint8_t* submitted_br = strip_submitted_brightness(strip);
    uint8_t* submitted_colors = strip_submitted_colors(strip);
    uint32_t* dma_buf = strip_dma_buffer(strip);
    uint16_t led_count = strip_led_count(strip);
    uint32_t dma_len = strip_dma_buffer_len(strip);

    if (*initialized) {
        return;
    }

    strip_power_write(strip, false);

    memset(dma_buf, 0, dma_len * sizeof(uint32_t));
    memset(colors, 0, (uint32_t)led_count * 3u);
    memset(br, LED_DEFAULT_BRIGHTNESS, led_count * sizeof(uint8_t));
    memset(submitted_colors, 0, (uint32_t)led_count * 3u);
    memset(submitted_br, LED_DEFAULT_BRIGHTNESS, led_count * sizeof(uint8_t));

    if (htim4.State == HAL_TIM_STATE_RESET) {
        MX_TIM4_Init();
    }

    encode_submitted_frame(strip);

    APP_DBG("WS2812B_InitStrip success: strip=%u leds=%u dma=%p dma_len=%lu aligned32=%u",
            (unsigned)strip,
            (unsigned)led_count,
            (void*)dma_buf,
            (unsigned long)dma_len,
            (unsigned)((((uintptr_t)dma_buf & 0x1Fu) == 0u) ? 1u : 0u));
    uint32_t timClkHz = get_tim4_clock_hz();
    APP_DBG("WS2812B timing: strip=%u tim4clk=%luHz bitrate=%luHz period=%u T0H=%luns T1H=%luns reset=%luus",
            (unsigned)strip,
            (unsigned long)timClkHz,
            (unsigned long)(timClkHz / (WS2812B_TIM_PERIOD + 1u)),
            (unsigned)(WS2812B_TIM_PERIOD + 1u),
            (unsigned long)((1000000000ull * (unsigned long long)strip_low_ccr_code(strip)) / (unsigned long long)timClkHz),
            (unsigned long)((1000000000ull * (unsigned long long)strip_high_ccr_code(strip)) / (unsigned long long)timClkHz),
            (unsigned long)((1000000ull * (unsigned long long)strip_reset_slot_count(strip) * 24ull *
                             (unsigned long long)(WS2812B_TIM_PERIOD + 1u)) /
                            (unsigned long long)timClkHz));
    (void)timClkHz;

    *initialized = true;
}

WS2812B_StateTypeDef WS2812B_StartStrip(WS2812B_Strip strip)
{
    volatile WS2812B_StateTypeDef* st = strip_state(strip);
    if (*st != WS2812B_STOP) {
        /* ERROR requires explicit Stop before retrying. */
        return *st;
    }
    WS2812B_InitStrip(strip);
    DMA_HandleTypeDef* hdma = strip_dma_handle(strip);
    if (hdma == NULL || hdma->State != HAL_DMA_STATE_READY ||
        (((DMA_Stream_TypeDef*)hdma->Instance)->CR & DMA_SxCR_EN) != 0u) {
        ++g_tx_diagnostics[strip].startFailures;
        fail_strip(strip);
        return *st;
    }
    __HAL_TIM_DISABLE_DMA(&htim4, strip_dma_request(strip));
    disable_strip_output(strip);
    reset_strip_compare(strip);
    clear_strip_dma_flags(hdma);
    TIM_CHANNEL_STATE_SET(&htim4, strip_tim_channel(strip), HAL_TIM_CHANNEL_STATE_READY);
    *strip_update_phase(strip) = WS2812B_UPDATE_IDLE;
    *strip_dirty_flag(strip) = true;
    *st = WS2812B_RUNNING;
    strip_power_write(strip, true);
    (void)WS2812B_SubmitStrip(strip);
    return *st;
}

WS2812B_StateTypeDef WS2812B_StopStrip(WS2812B_Strip strip)
{
    volatile WS2812B_StateTypeDef* st = strip_state(strip);
    if (!((strip == WS2812B_STRIP_AMBIENT) ? g_ambient_initialized : g_keys_initialized)) {
        strip_power_write(strip, false);
        return *st;
    }
    DMA_HandleTypeDef* hdma = strip_dma_handle(strip);
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    /* Prevent late callbacks from publishing success during teardown. */
    *st = WS2812B_STOP;
    __HAL_TIM_DISABLE_DMA(&htim4, strip_dma_request(strip));
    disable_strip_output(strip);
    if (hdma != NULL) {
        __HAL_DMA_DISABLE_IT(hdma, DMA_IT_HT | DMA_IT_TC | DMA_IT_TE | DMA_IT_DME);
        __HAL_DMA_DISABLE_IT(hdma, DMA_IT_FE);
    }
    if (primask == 0u) {
        __enable_irq();
    }
    strip_power_write(strip, false);

    /* Abort may poll: never hold a global IRQ lock while waiting. NORMAL
     * completion makes READY/no-transfer a successful stop, not an error. */
    if (hdma != NULL) {
        if ((hdma->State == HAL_DMA_STATE_BUSY && HAL_DMA_Abort(hdma) != HAL_OK) ||
            (((DMA_Stream_TypeDef*)hdma->Instance)->CR & DMA_SxCR_EN) != 0u) {
            *st = WS2812B_ERROR;
            return *st;
        }
        clear_strip_dma_flags(hdma);
        hdma->State = HAL_DMA_STATE_READY;
        hdma->ErrorCode = HAL_DMA_ERROR_NONE;
    }
    reset_strip_compare(strip);
    TIM_CHANNEL_STATE_SET(&htim4, strip_tim_channel(strip), HAL_TIM_CHANNEL_STATE_READY);
    *strip_update_phase(strip) = WS2812B_UPDATE_IDLE;
    *strip_in_flight_generation(strip) = 0u;
    *strip_dirty_flag(strip) = true;
    const WS2812B_StateTypeDef otherState =
        (strip == WS2812B_STRIP_AMBIENT) ? g_keys_state : g_ambient_state;
    if (otherState == WS2812B_RUNNING) {
        __HAL_TIM_ENABLE(&htim4);
    } else {
        __HAL_TIM_DISABLE(&htim4);
    }
    return *st;
}

WS2812B_StateTypeDef WS2812B_GetStateStrip(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT) ? g_ambient_state : g_keys_state;
}

void WS2812B_SetAllLEDBrightnessStrip(WS2812B_Strip strip, const uint8_t brightness)
{
    uint8_t* br = strip_brightness(strip);
    uint16_t led_count = strip_led_count(strip);
    memset(br, brightness, led_count * sizeof(uint8_t));
    mark_strip_dirty(strip);
}

void WS2812B_SetAllLEDColorStrip(WS2812B_Strip strip, const uint8_t r, const uint8_t g, const uint8_t b)
{
    uint8_t* colors = strip_colors(strip);
    uint16_t led_count = strip_led_count(strip);
    uint32_t length = (uint32_t)led_count * 3u;

    for (uint32_t i = 0; i < length; i += 3u) {
        colors[i] = r;
        colors[i + 1u] = g;
        colors[i + 2u] = b;
    }

    mark_strip_dirty(strip);

}

void WS2812B_SetLEDBrightnessStrip(WS2812B_Strip strip, const uint8_t brightness, const uint16_t index, const uint16_t length)
{
    uint8_t* br = strip_brightness(strip);
    uint16_t led_count = strip_led_count(strip);

    if (index >= led_count) {
        return;
    }

    uint16_t actualLength = length;
    if ((uint32_t)index + (uint32_t)actualLength > led_count) {
        actualLength = (uint16_t)(led_count - index);
    }

    memset(&br[index], brightness, (uint32_t)actualLength * sizeof(uint8_t));
    mark_strip_dirty(strip);
}

void WS2812B_SetLEDColorStrip(WS2812B_Strip strip, const uint8_t r, const uint8_t g, const uint8_t b, const uint16_t index)
{
    uint8_t* colors = strip_colors(strip);
    uint16_t led_count = strip_led_count(strip);

    if (index >= led_count) {
        return;
    }

    uint16_t idx = (uint16_t)(index * 3u);
    colors[idx] = r;
    colors[idx + 1u] = g;
    colors[idx + 2u] = b;
    mark_strip_dirty(strip);
}

void WS2812B_RefreshStrip(WS2812B_Strip strip, const uint16_t start, const uint16_t length)
{
    if (start >= strip_led_count(strip) || length == 0u) {
        return;
    }
    /* Even legacy partial refresh requests publish one coherent full frame. */
    mark_strip_dirty(strip);
    (void)WS2812B_SubmitStrip(strip);
}

static bool start_encoded_frame(WS2812B_Strip strip)
{
    DMA_HandleTypeDef* hdma = strip_dma_handle(strip);
    volatile WS2812B_UpdatePhase* phase = strip_update_phase(strip);
    /* Keep only the bounded HAL register setup atomic with completion/error
     * callbacks; no frame copies or encoding take place in this section. */
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t next = *strip_published_generation(strip) + 1u;
    if (next == 0u) {
        next = 1u;
    }
    *strip_published_generation(strip) = next;
    *strip_in_flight_generation(strip) = next;
    clear_strip_dma_flags(hdma);
    *phase = WS2812B_UPDATE_TRANSMITTING;
    __DMB();
    /* HAL_TIM_PWM_Start_DMA enables requests BEFORE TIM_CCxChannelCmd clears
     * and re-enables CCxE. On an already-running shared timer, that allows DMA
     * to reach payload while the pin is being gated. Arm the DMA directly;
     * leave the pin enabled between frames and release requests LAST. */
    __HAL_TIM_DISABLE_DMA(&htim4, strip_dma_request(strip));
    TIM_CHANNEL_STATE_SET(&htim4, strip_tim_channel(strip), HAL_TIM_CHANNEL_STATE_BUSY);
    hdma->XferCpltCallback = strip_dma_complete;
    hdma->XferHalfCpltCallback = NULL;
    hdma->XferErrorCallback = strip_dma_error;
    volatile uint32_t* compare = (strip == WS2812B_STRIP_AMBIENT)
        ? &htim4.Instance->CCR2 : &htim4.Instance->CCR1;
    const HAL_StatusTypeDef status = HAL_DMA_Start_IT(
        hdma, (uintptr_t)strip_dma_buffer(strip), (uintptr_t)compare,
        strip_dma_buffer_len(strip));
    __HAL_DMA_DISABLE_IT(hdma, DMA_IT_HT);
    if (status != HAL_OK) {
        ++g_tx_diagnostics[strip].startFailures;
        fail_strip(strip);
    } else {
        /* Active CCR and preload are zero from reset/previous frame's tail.
         * CCDS defers the first transfer to update, and preload defers its
         * first pulse to the following update. Never touch CNT or EGR. */
        htim4.Instance->CCER |= (strip == WS2812B_STRIP_AMBIENT)
            ? TIM_CCER_CC2E : TIM_CCER_CC1E;
        __HAL_TIM_ENABLE(&htim4);
        __DMB();
        __HAL_TIM_ENABLE_DMA(&htim4, strip_dma_request(strip));
    }
    if (primask == 0u) {
        __enable_irq();
    }
    return status == HAL_OK;
}

bool WS2812B_SubmitStrip(WS2812B_Strip strip)
{
    volatile bool* dirty = strip_dirty_flag(strip);
    volatile WS2812B_UpdatePhase* phase = strip_update_phase(strip);
    if (*strip_state(strip) == WS2812B_ERROR) {
        return false;
    }
    if (*phase != WS2812B_UPDATE_IDLE) {
        ++g_tx_diagnostics[strip].busyDeferrals;
        return false;
    }
    if (!*dirty) {
        return true;
    }
    WS2812B_InitStrip(strip);
    DMA_HandleTypeDef* hdma = strip_dma_handle(strip);
    if (hdma == NULL || (((DMA_Stream_TypeDef*)hdma->Instance)->CR & DMA_SxCR_EN) != 0u) {
        ++g_tx_diagnostics[strip].startFailures;
        fail_strip(strip);
        return false;
    }

    /* API calls/setters are main-loop owned. ISR only releases TRANSMITTING,
     * so snapshot/encoding require no global IRQ lock. */
    *phase = WS2812B_UPDATE_ENCODING;
    const uint16_t count = strip_led_count(strip);
    memcpy(strip_submitted_colors(strip), strip_colors(strip), (uint32_t)count * 3u);
    memcpy(strip_submitted_brightness(strip), strip_brightness(strip), count);
    *dirty = false;
    encode_submitted_frame(strip);
    if (*strip_state(strip) == WS2812B_STOP) {
        *phase = WS2812B_UPDATE_IDLE;
        return true;
    }

    return start_encoded_frame(strip);
}

void WS2812B_ServiceStrip(WS2812B_Strip strip)
{
    (void)WS2812B_SubmitStrip(strip);
}

void WS2812B_GetUpdateStats(WS2812B_Strip strip,
                            uint32_t* halfCount,
                            uint32_t* completeCount)
{
    if (halfCount != NULL) {
        *halfCount = 0u; /* NORMAL transfers do not service HT. */
    }
    if (completeCount != NULL) {
        *completeCount = (strip == WS2812B_STRIP_AMBIENT)
            ? g_ambient_tc_count : g_keys_tc_count;
    }
}

void WS2812B_GetTxDiagnostics(WS2812B_Strip strip, WS2812B_TxDiagnostics* stats)
{
    if (stats == NULL) {
        return;
    }
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    stats->startFailures = g_tx_diagnostics[strip].startFailures;
    stats->dmaErrors = g_tx_diagnostics[strip].dmaErrors;
    stats->busyDeferrals = g_tx_diagnostics[strip].busyDeferrals;
    if (primask == 0u) {
        __enable_irq();
    }
}

void WS2812B_Init(void)
{
    WS2812B_InitStrip(WS2812B_STRIP_KEYS);
    WS2812B_InitStrip(WS2812B_STRIP_AMBIENT);
}

void WS2812B_SetAllLEDBrightness(const uint8_t brightness)
{
    WS2812B_SetAllLEDBrightnessStrip(WS2812B_STRIP_KEYS, brightness);
    WS2812B_SetAllLEDBrightnessStrip(WS2812B_STRIP_AMBIENT, brightness);
}

void WS2812B_SetAllLEDColor(const uint8_t r, const uint8_t g, const uint8_t b)
{
    WS2812B_SetAllLEDColorStrip(WS2812B_STRIP_KEYS, r, g, b);
    WS2812B_SetAllLEDColorStrip(WS2812B_STRIP_AMBIENT, r, g, b);
}

void WS2812B_SetLEDBrightness(const uint8_t brightness, const uint16_t index, const uint8_t length)
{
    uint16_t end = (uint16_t)(index + length);
    for (uint16_t i = index; i < end; i++) {
        WS2812B_Strip strip;
        uint16_t localIndex;
        if (!map_global_index(i, &strip, &localIndex)) {
            continue;
        }
        WS2812B_SetLEDBrightnessStrip(strip, brightness, localIndex, 1u);
    }
}

void WS2812B_SetLEDColor(const uint8_t r, const uint8_t g, const uint8_t b, const uint16_t index)
{
    WS2812B_Strip strip;
    uint16_t localIndex;
    if (!map_global_index(index, &strip, &localIndex)) {
        return;
    }
    WS2812B_SetLEDColorStrip(strip, r, g, b, localIndex);
}

void WS2812B_SetLEDBrightnessByMask(const uint8_t fontBrightness, const uint8_t backgroundBrightness, const uint32_t mask)
{
    uint16_t len = (WS2812B_KEYS_LED_COUNT > 32u) ? 32u : (uint16_t)WS2812B_KEYS_LED_COUNT;

    for (uint16_t i = 0; i < len; i++) {
        if (((mask >> i) & 1u) == 1u) {
            g_keys_brightness[i] = fontBrightness;
        } else {
            g_keys_brightness[i] = backgroundBrightness;
        }
    }

    mark_strip_dirty(WS2812B_STRIP_KEYS);

}

void WS2812B_SetLEDColorByMask(const struct RGBColor frontColor, const struct RGBColor backgroundColor, const uint32_t mask)
{
    uint16_t len = (WS2812B_KEYS_LED_COUNT > 32u) ? 32u : (uint16_t)WS2812B_KEYS_LED_COUNT;

    for (uint16_t i = 0; i < len; i++) {
        uint16_t idx = (uint16_t)(i * 3u);
        if (((mask >> i) & 1u) == 1u) {
            g_keys_colors[idx] = frontColor.r;
            g_keys_colors[idx + 1u] = frontColor.g;
            g_keys_colors[idx + 2u] = frontColor.b;
        } else {
            g_keys_colors[idx] = backgroundColor.r;
            g_keys_colors[idx + 1u] = backgroundColor.g;
            g_keys_colors[idx + 2u] = backgroundColor.b;
        }
    }

    mark_strip_dirty(WS2812B_STRIP_KEYS);

}

WS2812B_StateTypeDef WS2812B_Start(void)
{
    WS2812B_StateTypeDef keys = WS2812B_StartStrip(WS2812B_STRIP_KEYS);
    WS2812B_StateTypeDef ambient = WS2812B_StartStrip(WS2812B_STRIP_AMBIENT);

    if (keys == WS2812B_ERROR || ambient == WS2812B_ERROR) {
        return WS2812B_ERROR;
    }
    return WS2812B_RUNNING;
}

WS2812B_StateTypeDef WS2812B_Stop(void)
{
    WS2812B_StateTypeDef keys = WS2812B_StopStrip(WS2812B_STRIP_KEYS);
    WS2812B_StateTypeDef ambient = WS2812B_StopStrip(WS2812B_STRIP_AMBIENT);

    if (keys == WS2812B_ERROR || ambient == WS2812B_ERROR) {
        return WS2812B_ERROR;
    }
    return WS2812B_STOP;
}

WS2812B_StateTypeDef WS2812B_GetState(void)
{
    if (g_keys_state == WS2812B_ERROR || g_ambient_state == WS2812B_ERROR) {
        return WS2812B_ERROR;
    }
    if (g_keys_state == WS2812B_RUNNING || g_ambient_state == WS2812B_RUNNING) {
        return WS2812B_RUNNING;
    }
    return WS2812B_STOP;
}

void WS2812B_Test(void)
{
    uint8_t r = 171;
    uint8_t g = 21;
    uint8_t b = 176;

    WS2812B_Init();
    WS2812B_SetAllLEDBrightness(80);
    WS2812B_SetAllLEDColor(r, g, b);
    WS2812B_Start();

    APP_DBG("Hex: %x", RGBToHex(r, g, b));
}

#if HBOX_LED_DMA_DIAGNOSTIC
#include "pwm-ws2812b-diagnostic.inc"
#endif
