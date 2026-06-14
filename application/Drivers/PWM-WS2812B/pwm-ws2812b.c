#include "pwm-ws2812b.h"
#include "board_cfg.h"

/* WS2812B-Mini-V3J data protocol (datasheet):
|-------------------------------------------|
|T0H | 220ns ~ 380ns
|T1H | 580ns ~ 1us
|T0L | 580ns ~ 1us
|T1L | 580ns ~ 1us
|RES | above 280us |
|-------------------------------------------
*/

/* 240MHz timer clock, period=300 ticks -> 1.25us.
 * Use center-ish values inside WS2812B-Mini-V3J timing window to increase margin.
 * T1H = 150/240MHz = 625ns, T1L = 675ns
 * T0H =  72/240MHz = 300ns, T0L = 950ns
 */
#define HIGH_CCR_CODE 150
#define LOW_CCR_CODE   72
#define WS2812B_DMA_BUFFER_LEN_FOR(count) (((((count) % 2u == 0u) ? ((count) + 10u) : ((count) + 11u)) * 24u) * (uint32_t)NUM_LEDs_PER_ADC_BUTTON)

#define LED_DEFAULT_BRIGHTNESS 128

enum {
    WS2812B_KEYS_LED_COUNT = (NUM_ADC_BUTTONS + NUM_GPIO_BUTTONS),
    WS2812B_AMBIENT_LED_COUNT = NUM_LED_AROUND
};

enum {
    WS2812B_KEYS_DMA_BUFFER_LEN = (int)WS2812B_DMA_BUFFER_LEN_FOR(WS2812B_KEYS_LED_COUNT),
    WS2812B_AMBIENT_DMA_BUFFER_LEN = (int)WS2812B_DMA_BUFFER_LEN_FOR(WS2812B_AMBIENT_LED_COUNT)
};

static bool g_power_gpio_initialized = false;
static bool g_keys_initialized = false;
static bool g_ambient_initialized = false;

static WS2812B_StateTypeDef g_keys_state = WS2812B_STOP;
static WS2812B_StateTypeDef g_ambient_state = WS2812B_STOP;

static uint32_t g_dma_half_irq_count_keys = 0;
static uint32_t g_dma_full_irq_count_keys = 0;
static uint32_t g_dma_half_irq_count_ambient = 0;
static uint32_t g_dma_full_irq_count_ambient = 0;

static uint32_t get_tim4_clock_hz(void)
{
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t ppre1 = (RCC->D2CFGR & RCC_D2CFGR_D2PPRE1);
    if (ppre1 != RCC_HCLK_DIV1) {
        return pclk1 * 2u;
    }
    return pclk1;
}

static uint8_t g_keys_colors[WS2812B_KEYS_LED_COUNT * 3u];
static uint8_t g_keys_brightness[WS2812B_KEYS_LED_COUNT];
/* Keep keys DMA buffer in .DMA_Section and retain legacy symbol name. */
static __attribute__((section(".DMA_Section"))) uint32_t DMA_LED_Buffer[WS2812B_KEYS_DMA_BUFFER_LEN];

static uint8_t g_ambient_colors[WS2812B_AMBIENT_LED_COUNT * 3u];
static uint8_t g_ambient_brightness[WS2812B_AMBIENT_LED_COUNT];
static __attribute__((section(".DMA_Section"))) uint32_t DMA_LED_Buffer_Ambient[WS2812B_AMBIENT_DMA_BUFFER_LEN];

void clearDCache(void *addr, uint32_t size)
{
    uint32_t alignedAddr = (uint32_t)addr & ~(32u - 1u);
    uint32_t alignedSize = ((size + 31u) & ~31u);

    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)alignedAddr, alignedSize);
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

static uint32_t strip_dma_buffer_len(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT) ? (uint32_t)WS2812B_AMBIENT_DMA_BUFFER_LEN : (uint32_t)WS2812B_KEYS_DMA_BUFFER_LEN;
}

static uint16_t strip_tim_channel(WS2812B_Strip strip)
{
    return (strip == WS2812B_STRIP_AMBIENT) ? (uint16_t)WS2812B_AMBIENT_TIM_CHANNEL : (uint16_t)WS2812B_KEYS_TIM_CHANNEL;
}

static void strip_power_write(WS2812B_Strip strip, GPIO_PinState state)
{
    if (strip == WS2812B_STRIP_AMBIENT) {
        HAL_GPIO_WritePin(WS2812B_AMBIENT_ENABLE_SWITCH_PORT, WS2812B_AMBIENT_ENABLE_SWITCH_PIN, state);
    } else {
        HAL_GPIO_WritePin(WS2812B_KEYS_ENABLE_SWITCH_PORT, WS2812B_KEYS_ENABLE_SWITCH_PIN, state);
    }
}

static void power_gpio_init_once(void)
{
    if (g_power_gpio_initialized) {
        return;
    }

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitStruct.Pin = WS2812B_KEYS_ENABLE_SWITCH_PIN | WS2812B_AMBIENT_ENABLE_SWITCH_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    HAL_GPIO_WritePin(WS2812B_KEYS_ENABLE_SWITCH_PORT, WS2812B_KEYS_ENABLE_SWITCH_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(WS2812B_AMBIENT_ENABLE_SWITCH_PORT, WS2812B_AMBIENT_ENABLE_SWITCH_PIN, GPIO_PIN_RESET);

    g_power_gpio_initialized = true;
}

static void led_data_to_dma_buffer(WS2812B_Strip strip, const uint16_t start, const uint16_t length)
{
    uint32_t* dma_buf = strip_dma_buffer(strip);
    uint16_t led_count = strip_led_count(strip);
    uint8_t* colors = strip_colors(strip);
    uint8_t* br = strip_brightness(strip);

    if (((uint32_t)dma_buf & 0x1Fu) != 0u) {
        APP_ERR("pwm-ws2812b: Error: DMA buffer not 32-byte aligned");
        return;
    }

    if (start > led_count) {
        return;
    }

    if ((uint32_t)start + (uint32_t)length > led_count) {
        return;
    }

    uint16_t end = (uint16_t)(start + length);

    for (uint16_t led = start; led < end; led++) {
        uint16_t cidx = (uint16_t)(led * 3u);
        double_t brightness = (double_t)br[led] / 255.0;
        uint32_t color = RGBToHex(
            (uint8_t)round(colors[cidx] * brightness),
            (uint8_t)round(colors[cidx + 1u] * brightness),
            (uint8_t)round(colors[cidx + 2u] * brightness)
        );

        for (uint16_t k = 0; k < (uint16_t)NUM_LEDs_PER_ADC_BUTTON; k++) {
            uint32_t base = ((uint32_t)led * (uint32_t)NUM_LEDs_PER_ADC_BUTTON + (uint32_t)k) * 24u;
            for (uint16_t i = 0; i < 24u; i++) {
                uint32_t dma_idx = base + (uint32_t)i;
                dma_buf[dma_idx] = ((0x800000u & (color << i)) != 0u) ? HIGH_CCR_CODE : LOW_CCR_CODE;
            }
        }
    }

    clearDCache(dma_buf, strip_dma_buffer_len(strip) * sizeof(uint32_t));
}

/* Backward-compatible legacy entrypoint, targeting keys strip. */
void LEDDataToDMABuffer(const uint16_t start, const uint16_t length)
{
    led_data_to_dma_buffer(WS2812B_STRIP_KEYS, start, length);
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

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if (!htim || htim->Instance != WS2812B_TIM_INSTANCE) {
        return;
    }

    WS2812B_Strip strip;
    uint32_t dma_len;
    uint16_t led_count;

    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
        strip = WS2812B_STRIP_KEYS;
        g_dma_full_irq_count_keys++;
        // if (g_dma_full_irq_count_keys <= 5u || (g_dma_full_irq_count_keys % 1000u) == 0u) {
        //     APP_DBG("WS2812B DMA FULL IRQ: KEYS count=%lu", (unsigned long)g_dma_full_irq_count_keys);
        // }
    } else if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2) {
        strip = WS2812B_STRIP_AMBIENT;
        g_dma_full_irq_count_ambient++;
    } else {
        return;
    }

    dma_len = strip_dma_buffer_len(strip);
    led_count = strip_led_count(strip);

    uint16_t start = (uint16_t)(dma_len / 2u / 24u / (uint32_t)NUM_LEDs_PER_ADC_BUTTON);
    uint16_t length = (start < led_count) ? (uint16_t)(led_count - start) : 0u;
    if (length > 0u) {
        led_data_to_dma_buffer(strip, start, length);
    }
}

void HAL_TIM_PWM_PulseFinishedHalfCpltCallback(TIM_HandleTypeDef *htim)
{
    if (!htim || htim->Instance != WS2812B_TIM_INSTANCE) {
        return;
    }

    WS2812B_Strip strip;
    uint32_t dma_len;
    uint16_t led_count;

    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
        strip = WS2812B_STRIP_KEYS;
        g_dma_half_irq_count_keys++;
        // if (g_dma_half_irq_count_keys <= 5u || (g_dma_half_irq_count_keys % 1000u) == 0u) {
        //     APP_DBG("WS2812B DMA HALF IRQ: KEYS count=%lu", (unsigned long)g_dma_half_irq_count_keys);
        // }
    } else if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2) {
        strip = WS2812B_STRIP_AMBIENT;
        g_dma_half_irq_count_ambient++;
    } else {
        return;
    }

    dma_len = strip_dma_buffer_len(strip);
    led_count = strip_led_count(strip);

    uint16_t length = (uint16_t)(dma_len / 2u / 24u / (uint32_t)NUM_LEDs_PER_ADC_BUTTON);
    if (length > led_count) {
        length = led_count;
    }
    led_data_to_dma_buffer(strip, 0u, length);
}

void HAL_TIM_ErrorCallback(TIM_HandleTypeDef *htim)
{
    (void)htim;
    APP_ERR("PWM-WS2812B-ErrorCallback...");
}

void WS2812B_InitStrip(WS2812B_Strip strip)
{
    bool* initialized = (strip == WS2812B_STRIP_AMBIENT) ? &g_ambient_initialized : &g_keys_initialized;
    uint8_t* br = strip_brightness(strip);
    uint32_t* dma_buf = strip_dma_buffer(strip);
    uint16_t led_count = strip_led_count(strip);
    uint32_t dma_len = strip_dma_buffer_len(strip);

    if (*initialized) {
        return;
    }

    power_gpio_init_once();
    strip_power_write(strip, GPIO_PIN_RESET);

    memset(dma_buf, 0, dma_len * sizeof(uint32_t));
    memset(br, LED_DEFAULT_BRIGHTNESS, led_count * sizeof(uint8_t));

    if (htim4.State == HAL_TIM_STATE_RESET) {
        MX_TIM4_Init();
    }

    led_data_to_dma_buffer(strip, 0u, led_count);

    APP_DBG("WS2812B_InitStrip success: strip=%u leds=%u dma=0x%08lX dma_len=%lu aligned32=%u",
            (unsigned)strip,
            (unsigned)led_count,
            (unsigned long)dma_buf,
            (unsigned long)dma_len,
            (unsigned)((((uint32_t)dma_buf & 0x1Fu) == 0u) ? 1u : 0u));
    uint32_t timClkHz = get_tim4_clock_hz();
    APP_DBG("WS2812B timing: tim4clk=%luHz period=%u T0H=%luns T1H=%luns",
            (unsigned long)timClkHz,
            (unsigned)(WS2812B_TIM_PERIOD + 1u),
            (unsigned long)((1000000000ull * (unsigned long long)LOW_CCR_CODE) / (unsigned long long)timClkHz),
            (unsigned long)((1000000000ull * (unsigned long long)HIGH_CCR_CODE) / (unsigned long long)timClkHz));

    *initialized = true;
}

WS2812B_StateTypeDef WS2812B_StartStrip(WS2812B_Strip strip)
{
    WS2812B_StateTypeDef* st = (strip == WS2812B_STRIP_AMBIENT) ? &g_ambient_state : &g_keys_state;

    if (*st != WS2812B_STOP) {
        return *st;
    }

    WS2812B_InitStrip(strip);
    strip_power_write(strip, GPIO_PIN_SET);

    uint16_t ch = strip_tim_channel(strip);
    uint32_t* dma_buf = strip_dma_buffer(strip);
    uint32_t dma_len = strip_dma_buffer_len(strip);

    HAL_StatusTypeDef state = HAL_TIM_PWM_Start_DMA(&htim4, ch, (uint32_t *)dma_buf, dma_len);
    *st = (state == HAL_OK) ? WS2812B_RUNNING : WS2812B_ERROR;
    return *st;
}

WS2812B_StateTypeDef WS2812B_StopStrip(WS2812B_Strip strip)
{
    WS2812B_StateTypeDef* st = (strip == WS2812B_STRIP_AMBIENT) ? &g_ambient_state : &g_keys_state;

    if (*st != WS2812B_RUNNING) {
        return *st;
    }

    uint16_t ch = strip_tim_channel(strip);
    HAL_StatusTypeDef state = HAL_TIM_PWM_Stop_DMA(&htim4, ch);
    strip_power_write(strip, GPIO_PIN_RESET);

    *st = (state == HAL_OK) ? WS2812B_STOP : WS2812B_ERROR;
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
    clearDCache(br, led_count * sizeof(uint8_t));
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

    clearDCache(colors, length * sizeof(uint8_t));
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
    clearDCache(&br[index], (uint32_t)actualLength * sizeof(uint8_t));
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
    clearDCache(&colors[idx], 3u * sizeof(uint8_t));
}

void WS2812B_RefreshStrip(WS2812B_Strip strip, const uint16_t start, const uint16_t length)
{
    led_data_to_dma_buffer(strip, start, length);
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

    clearDCache(g_keys_brightness, (uint32_t)WS2812B_KEYS_LED_COUNT * sizeof(uint8_t));
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

    clearDCache(g_keys_colors, (uint32_t)WS2812B_KEYS_LED_COUNT * 3u * sizeof(uint8_t));
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
