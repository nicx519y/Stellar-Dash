#include "spi-st7789.h"
#include <string.h>
#include "st7789.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_dma.h"
#include "stm32h7xx_hal_gpio.h"
#include "stm32h7xx_hal_gpio_ex.h"
#include "stm32h7xx_hal_rcc_ex.h"

static SPI_HandleTypeDef g_hspi;
static DMA_HandleTypeDef g_dma;
static TIM_HandleTypeDef g_bl_htim;

#define SPIST7789_DMA_CHUNK_BYTES 1024u
#define SPIST7789_Y_OFFSET 34u
static uint8_t g_fillbuf[SPIST7789_DMA_CHUNK_BYTES] __attribute__((section(".DMA_Section"), aligned(32)));

static volatile bool g_busy = false;
static volatile uint32_t g_remaining = 0;
static volatile uint8_t g_dma_irq_flag = 0;
static volatile uint8_t g_dma_done_flag = 0;
static volatile uint8_t g_dma_err_flag = 0;
static volatile bool g_spi_txc_flag = false;
static volatile uint8_t g_test_phase = 0;
static bool g_bl_tim_ready = false;
static uint16_t g_fill_color565 = 0xFFFFu;

static inline void gpio_write(GPIO_TypeDef* port, uint16_t pin, GPIO_PinState state)
{
    HAL_GPIO_WritePin(port, pin, state);
}

static inline void cs_low(void) { gpio_write(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_RESET); }
static inline void cs_high(void) { gpio_write(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_SET); }
static inline void dc_cmd(void) { gpio_write(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_RESET); }
static inline void dc_data(void) { gpio_write(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_SET); }

#define SPIST7789_BL_TIM_INSTANCE TIM12
#define SPIST7789_BL_TIM_CHANNEL TIM_CHANNEL_1
#define SPIST7789_BL_TIM_AF GPIO_AF2_TIM12

static void enable_gpio_clock(GPIO_TypeDef* port)
{
    if (port == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
    else if (port == GPIOE) __HAL_RCC_GPIOE_CLK_ENABLE();
    else if (port == GPIOF) __HAL_RCC_GPIOF_CLK_ENABLE();
    else if (port == GPIOG) __HAL_RCC_GPIOG_CLK_ENABLE();
    else if (port == GPIOH) __HAL_RCC_GPIOH_CLK_ENABLE();
    else if (port == GPIOI) __HAL_RCC_GPIOI_CLK_ENABLE();
    else if (port == GPIOJ) __HAL_RCC_GPIOJ_CLK_ENABLE();
    else if (port == GPIOK) __HAL_RCC_GPIOK_CLK_ENABLE();
}

static void spi_gpio_init(void)
{
    GPIO_InitTypeDef init = {0};
    enable_gpio_clock(ST7789_SCL_PORT);
    if (ST7789_SDA_PORT != ST7789_SCL_PORT) enable_gpio_clock(ST7789_SDA_PORT);
    enable_gpio_clock(ST7789_CS_PORT);
    if (ST7789_DC_PORT != ST7789_CS_PORT) enable_gpio_clock(ST7789_DC_PORT);
    if (ST7789_BL_PORT != ST7789_CS_PORT && ST7789_BL_PORT != ST7789_DC_PORT) enable_gpio_clock(ST7789_BL_PORT);

    init.Mode = GPIO_MODE_AF_PP;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    init.Alternate = ST7789_SPI_MOSI_AF;
    init.Pin = ST7789_SCL_PIN;
    HAL_GPIO_Init(ST7789_SCL_PORT, &init);
    init.Pin = ST7789_SDA_PIN;
    HAL_GPIO_Init(ST7789_SDA_PORT, &init);

    init.Mode = GPIO_MODE_OUTPUT_PP;
    init.Pin = ST7789_CS_PIN;
    HAL_GPIO_Init(ST7789_CS_PORT, &init);
    init.Pin = ST7789_DC_PIN;
    HAL_GPIO_Init(ST7789_DC_PORT, &init);
    init.Pin = ST7789_BL_PIN;
    HAL_GPIO_Init(ST7789_BL_PORT, &init);

    cs_high();
    dc_data();
    gpio_write(ST7789_BL_PORT, ST7789_BL_PIN, ST7789_BL_OFF_STATE);
}

static bool spi_clock_init(void)
{
    RCC_PeriphCLKInitTypeDef clk = {0};
    clk.PeriphClockSelection = RCC_PERIPHCLK_SPI5;
    clk.Spi45ClockSelection = RCC_SPI45CLKSOURCE_D2PCLK2;
    return (HAL_RCCEx_PeriphCLKConfig(&clk) == HAL_OK);
}

static bool spi_hw_init(void)
{
    __HAL_RCC_SPI5_CLK_ENABLE();
    if (!spi_clock_init()) return false;
    __HAL_RCC_SPI5_FORCE_RESET();
    __HAL_RCC_SPI5_RELEASE_RESET();

    memset(&g_hspi, 0, sizeof(g_hspi));
    g_hspi.Instance = ST7789_SPI_INSTANCE;
    g_hspi.Init.Mode = SPI_MODE_MASTER;
    g_hspi.Init.Direction = SPI_DIRECTION_2LINES_TXONLY;
    g_hspi.Init.DataSize = SPI_DATASIZE_8BIT;
    g_hspi.Init.CLKPolarity = SPI_POLARITY_HIGH;
    g_hspi.Init.CLKPhase = SPI_PHASE_2EDGE;
    g_hspi.Init.NSS = SPI_NSS_SOFT;
    g_hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
    g_hspi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    g_hspi.Init.TIMode = SPI_TIMODE_DISABLE;
    g_hspi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    g_hspi.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    g_hspi.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
    g_hspi.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
    g_hspi.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    g_hspi.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    g_hspi.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
    g_hspi.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    g_hspi.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
    g_hspi.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
    g_hspi.Init.IOSwap = SPI_IO_SWAP_DISABLE;
    return (HAL_SPI_Init(&g_hspi) == HAL_OK);
}

static void dcache_clean(const void* data, size_t len)
{
#if (__DCACHE_PRESENT == 1U)
    if (!data || len == 0) return;
    uintptr_t start = (uintptr_t)data & ~31U;
    uintptr_t end = ((uintptr_t)data + len + 31U) & ~31U;
    SCB_CleanDCache_by_Addr((uint32_t*)start, (int32_t)(end - start));
#else
    (void)data;
    (void)len;
#endif
}

static bool spi_wait_ready(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    while (g_hspi.State != HAL_SPI_STATE_READY) {
        if ((uint32_t)(HAL_GetTick() - t0) > timeout_ms) return false;
    }
    return true;
}

static bool spi_tx_blocking(const uint8_t* data, uint16_t len)
{
    if (!data || len == 0) return true;
    if (!spi_wait_ready(50)) return false;
    return (HAL_SPI_Transmit(&g_hspi, (uint8_t*)data, len, 50) == HAL_OK);
}

static bool write_cmd_data(uint8_t cmd, const uint8_t* data, uint16_t len)
{
    cs_low();
    dc_cmd();
    if (!spi_tx_blocking(&cmd, 1)) { cs_high(); return false; }
    if (len > 0 && data) {
        dc_data();
        if (!spi_tx_blocking(data, len)) { cs_high(); return false; }
    }
    cs_high();
    return true;
}

static bool set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t x0 = x, x1 = (uint16_t)(x + w - 1u);
    uint16_t y0 = (uint16_t)(SPIST7789_Y_OFFSET + y), y1 = (uint16_t)(SPIST7789_Y_OFFSET + y + h - 1u);
    uint8_t caset[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
    uint8_t raset[4] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
    if (!write_cmd_data(0x2A, caset, 4)) return false;
    if (!write_cmd_data(0x2B, raset, 4)) return false;
    cs_low();
    dc_cmd();
    {
        uint8_t cmd = 0x2C;
        if (!spi_tx_blocking(&cmd, 1)) { cs_high(); return false; }
    }
    dc_data();
    return true;
}

static bool start_dma_chunk(uint16_t len)
{
    if (HAL_SPI_Transmit_DMA(&g_hspi, g_fillbuf, len) != HAL_OK) return false;
    return true;
}

static void set_fill_color565(uint16_t color565)
{
    if (color565 == g_fill_color565) return;
    uint8_t hi = (uint8_t)(color565 >> 8);
    uint8_t lo = (uint8_t)(color565 & 0xFFu);
    for (size_t i = 0; i < sizeof(g_fillbuf); i += 2u) {
        g_fillbuf[i] = hi;
        g_fillbuf[i + 1u] = lo;
    }
    dcache_clean(g_fillbuf, sizeof(g_fillbuf));
    g_fill_color565 = color565;
}

static bool start_fill_async(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color565)
{
    if (w == 0u || h == 0u) return false;
    if (g_busy) return false;
    if (!spi_wait_ready(50)) return false;

    set_fill_color565(color565);

    if (!set_window(x, y, w, h)) {
        g_dma_err_flag = 1;
        return false;
    }

    g_remaining = (uint32_t)w * (uint32_t)h * 2u;
    {
        uint16_t first = (g_remaining > SPIST7789_DMA_CHUNK_BYTES) ? (uint16_t)SPIST7789_DMA_CHUNK_BYTES : (uint16_t)g_remaining;
        g_remaining -= (uint32_t)first;
        g_busy = true;
        g_spi_txc_flag = false;
        if (!start_dma_chunk(first)) {
            cs_high();
            g_busy = false;
            g_dma_err_flag = 1;
            return false;
        }
    }
    return true;
}

static bool backlight_tim12_init(void)
{
    if (g_bl_tim_ready) return true;

    enable_gpio_clock(ST7789_BL_PORT);
    __HAL_RCC_TIM12_CLK_ENABLE();

    GPIO_InitTypeDef init = {0};
    init.Pin = ST7789_BL_PIN;
    init.Mode = GPIO_MODE_AF_PP;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    init.Alternate = SPIST7789_BL_TIM_AF;
    HAL_GPIO_Init(ST7789_BL_PORT, &init);

    memset(&g_bl_htim, 0, sizeof(g_bl_htim));
    g_bl_htim.Instance = SPIST7789_BL_TIM_INSTANCE;
    g_bl_htim.Init.Prescaler = 239u;
    g_bl_htim.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_bl_htim.Init.Period = 999u;
    g_bl_htim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    g_bl_htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&g_bl_htim) != HAL_OK) return false;

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 0u;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&g_bl_htim, &oc, SPIST7789_BL_TIM_CHANNEL) != HAL_OK) return false;
    if (HAL_TIM_PWM_Start(&g_bl_htim, SPIST7789_BL_TIM_CHANNEL) != HAL_OK) return false;

    g_bl_tim_ready = true;
    return true;
}

void SPIST7789_DMA_IRQHandler(void)
{
    g_dma_irq_flag = 1;
    HAL_DMA_IRQHandler(&g_dma);
}

void SPIST7789_SPI_IRQHandler(void)
{
    HAL_SPI_IRQHandler(&g_hspi);
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef* hspi)
{
    if (hspi == &g_hspi) {
        g_spi_txc_flag = true;
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef* hspi)
{
    if (hspi != &g_hspi) return;
    cs_high();
    g_busy = false;
    g_dma_err_flag = 1;
}

void SPIST7789_Init(void)
{
    spi_gpio_init();
    if (!spi_hw_init()) {
        g_dma_err_flag = 1;
        return;
    }

    __HAL_RCC_DMA2_CLK_ENABLE();
#ifdef __HAL_RCC_DMAMUX1_CLK_ENABLE
    __HAL_RCC_DMAMUX1_CLK_ENABLE();
#endif

    memset(&g_dma, 0, sizeof(g_dma));
    g_dma.Instance = ST7789_SPI_DMA_STREAM;
    g_dma.Init.Request = ST7789_SPI_DMA_REQUEST;
    g_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    g_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    g_dma.Init.MemInc = DMA_MINC_ENABLE;
    g_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    g_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    g_dma.Init.Mode = DMA_NORMAL;
    g_dma.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    g_dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&g_dma) != HAL_OK) {
        g_dma_err_flag = 1;
        return;
    }
    __HAL_DMA_DISABLE_IT(&g_dma, DMA_IT_HT);
    __HAL_LINKDMA(&g_hspi, hdmatx, g_dma);

    HAL_NVIC_SetPriority(ST7789_SPI_DMA_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(ST7789_SPI_DMA_IRQn);
    HAL_NVIC_SetPriority(SPI5_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(SPI5_IRQn);

    (void)write_cmd_data(0x01, NULL, 0);
    HAL_Delay(150);
    (void)write_cmd_data(0x11, NULL, 0);
    HAL_Delay(120);
    {
        uint8_t colmod = 0x55u;
        (void)write_cmd_data(0x3A, &colmod, 1);
        uint8_t madctl = 0xA0u;
        (void)write_cmd_data(0x36, &madctl, 1);
        (void)write_cmd_data(0x21, NULL, 0);
    }
    (void)write_cmd_data(0x13, NULL, 0);
    (void)write_cmd_data(0x29, NULL, 0);
}

void SPIST7789_SetBacklight100(void)
{
    SPIST7789_SetBacklight(100u);
}

void SPIST7789_SetBacklight(uint8_t percent)
{
    if (percent > 100u) percent = 100u;
    if (!backlight_tim12_init()) {
        gpio_write(ST7789_BL_PORT, ST7789_BL_PIN, (percent == 0u) ? ST7789_BL_OFF_STATE : ST7789_BL_ON_STATE);
        return;
    }

    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&g_bl_htim);
    uint32_t pulse = ((arr + 1u) * (uint32_t)percent) / 100u;
    if (pulse > arr) pulse = arr;
    if (ST7789_BL_ON_STATE == GPIO_PIN_RESET) {
        pulse = arr - pulse;
    }
    __HAL_TIM_SET_COMPARE(&g_bl_htim, SPIST7789_BL_TIM_CHANNEL, pulse);
}

bool SPIST7789_IsBusy(void)
{
    return g_busy;
}

bool SPIST7789_FillBlueAsync(void)
{
    uint16_t color565 = 0x001Fu;
    if (g_test_phase == 0u) color565 = 0xFFFFu;
    else if (g_test_phase == 1u) color565 = 0xF800u;
    g_test_phase = (uint8_t)((g_test_phase + 1u) % 3u);
    return start_fill_async(0u, 0u, ST7789_WIDTH, ST7789_HEIGHT, color565);
}

bool SPIST7789_FillRectColor565Async(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color565)
{
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return false;
    if (w == 0u || h == 0u) return false;
    if ((uint32_t)x + (uint32_t)w > ST7789_WIDTH) w = (uint16_t)(ST7789_WIDTH - x);
    if ((uint32_t)y + (uint32_t)h > ST7789_HEIGHT) h = (uint16_t)(ST7789_HEIGHT - y);
    return start_fill_async(x, y, w, h, color565);
}

void SPIST7789_Service(void)
{
    if (!g_busy) return;
    if (!g_spi_txc_flag) return;

    g_spi_txc_flag = false;
    if (g_remaining > 0u) {
        uint16_t chunk = (g_remaining > SPIST7789_DMA_CHUNK_BYTES) ? (uint16_t)SPIST7789_DMA_CHUNK_BYTES : (uint16_t)g_remaining;
        g_remaining -= (uint32_t)chunk;
        if (!start_dma_chunk(chunk)) {
            cs_high();
            g_busy = false;
            g_dma_err_flag = 1;
        }
        return;
    }

    cs_high();
    g_busy = false;
    g_dma_done_flag = 1;
}

bool SPIST7789_WaitDone(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    while (SPIST7789_IsBusy()) {
        SPIST7789_Service();
        if ((uint32_t)(HAL_GetTick() - t0) > timeout_ms) {
            return false;
        }
    }
    return true;
}

bool SPIST7789_ConsumeDmaIrqFlag(void)
{
    if (!g_dma_irq_flag) return false;
    g_dma_irq_flag = 0;
    return true;
}

bool SPIST7789_ConsumeDmaDoneFlag(void)
{
    if (!g_dma_done_flag) return false;
    g_dma_done_flag = 0;
    return true;
}

bool SPIST7789_ConsumeDmaErrFlag(void)
{
    if (!g_dma_err_flag) return false;
    g_dma_err_flag = 0;
    return true;
}

void SPIST7789_OnSpiTxCplt(SPI_HandleTypeDef* hspi)
{
    if (hspi == &g_hspi) g_spi_txc_flag = true;
}

void SPIST7789_GetDebug(uint32_t* out_ndtr, uint32_t* out_dma_cr, uint32_t* out_dma_isr, uint32_t* out_spi_sr, uint32_t* out_spi_cfg1, uint32_t* out_spi_cr1, uint32_t* out_spi_cr2)
{
    DMA_Stream_TypeDef* s = (DMA_Stream_TypeDef*)g_dma.Instance;
    if (out_ndtr) *out_ndtr = s ? s->NDTR : 0;
    if (out_dma_cr) *out_dma_cr = s ? s->CR : 0;
    if (out_dma_isr) {
        if (!DMA2) *out_dma_isr = 0;
        else if (s == DMA2_Stream0 || s == DMA2_Stream1 || s == DMA2_Stream2 || s == DMA2_Stream3) *out_dma_isr = DMA2->LISR;
        else *out_dma_isr = DMA2->HISR;
    }
    if (out_spi_sr) *out_spi_sr = g_hspi.Instance ? g_hspi.Instance->SR : 0;
    if (out_spi_cfg1) *out_spi_cfg1 = g_hspi.Instance ? g_hspi.Instance->CFG1 : 0;
    if (out_spi_cr1) *out_spi_cr1 = g_hspi.Instance ? g_hspi.Instance->CR1 : 0;
    if (out_spi_cr2) *out_spi_cr2 = g_hspi.Instance ? g_hspi.Instance->CR2 : 0;
}

static int st7789_abs_i(int v) { return (v < 0) ? -v : v; }
static uint16_t st7789_fb[ST7789_WIDTH * ST7789_HEIGHT] __attribute__((section(".DMA_Section"), aligned(32)));

static uint16_t st7789_rgb888_to_565(uint32_t rgb888)
{
    uint8_t r = (uint8_t)((rgb888 >> 16) & 0xFFu);
    uint8_t g = (uint8_t)((rgb888 >> 8) & 0xFFu);
    uint8_t b = (uint8_t)(rgb888 & 0xFFu);
    return (uint16_t)(((uint16_t)(r & 0xF8u) << 8) | ((uint16_t)(g & 0xFCu) << 3) | (uint16_t)(b >> 3));
}

static inline uint32_t st7789_fb_index(uint16_t x, uint16_t y)
{
    return (uint32_t)y * (uint32_t)ST7789_WIDTH + (uint32_t)x;
}

static void st7789_mark_dirty_xy(ST7789_Handle* lcd, uint16_t x, uint16_t y)
{
    if (!lcd) return;
    if (!lcd->dirty_valid) {
        lcd->dirty_valid = true;
        lcd->dirty_x0 = x;
        lcd->dirty_y0 = y;
        lcd->dirty_x1 = x;
        lcd->dirty_y1 = y;
        return;
    }
    if (x < lcd->dirty_x0) lcd->dirty_x0 = x;
    if (y < lcd->dirty_y0) lcd->dirty_y0 = y;
    if (x > lcd->dirty_x1) lcd->dirty_x1 = x;
    if (y > lcd->dirty_y1) lcd->dirty_y1 = y;
}

static void st7789_fb_clear(ST7789_Handle* lcd, uint16_t c565)
{
    if (!lcd || !lcd->framebuffer_enabled || !lcd->fb_back) return;
    uint32_t pixels = (uint32_t)ST7789_WIDTH * (uint32_t)ST7789_HEIGHT;
    bool changed = false;
    for (uint32_t i = 0; i < pixels; i++) {
        if (lcd->fb_back[i] != c565) {
            lcd->fb_back[i] = c565;
            changed = true;
        }
    }
    if (changed) {
        lcd->dirty_valid = true;
        lcd->dirty_x0 = 0;
        lcd->dirty_y0 = 0;
        lcd->dirty_x1 = (uint16_t)(ST7789_WIDTH - 1u);
        lcd->dirty_y1 = (uint16_t)(ST7789_HEIGHT - 1u);
    }
}

static void st7789_fb_fill_rect(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c565)
{
    if (!lcd || !lcd->framebuffer_enabled || !lcd->fb_back) return;
    if (w == 0u || h == 0u) return;
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;
    uint16_t x1 = (uint16_t)(x + w - 1u);
    uint16_t y1 = (uint16_t)(y + h - 1u);
    if (x1 >= ST7789_WIDTH) x1 = (uint16_t)(ST7789_WIDTH - 1u);
    if (y1 >= ST7789_HEIGHT) y1 = (uint16_t)(ST7789_HEIGHT - 1u);
    for (uint16_t yy = y; yy <= y1; yy++) {
        uint32_t row = (uint32_t)yy * (uint32_t)ST7789_WIDTH;
        for (uint16_t xx = x; xx <= x1; xx++) {
            uint32_t idx = row + xx;
            if (lcd->fb_back[idx] != c565) {
                lcd->fb_back[idx] = c565;
                st7789_mark_dirty_xy(lcd, xx, yy);
            }
        }
    }
}

static void st7789_flush_rect(ST7789_Handle* lcd, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    if (!lcd || !lcd->fb_back) return;
    if (x0 > x1 || y0 > y1) return;
    if (x1 >= ST7789_WIDTH) x1 = (uint16_t)(ST7789_WIDTH - 1u);
    if (y1 >= ST7789_HEIGHT) y1 = (uint16_t)(ST7789_HEIGHT - 1u);
    uint16_t w = (uint16_t)(x1 - x0 + 1u);
    uint16_t h = (uint16_t)(y1 - y0 + 1u);
    if (!set_window(x0, y0, w, h)) return;
    uint8_t out[512];
    for (uint16_t y = y0; y <= y1; y++) {
        const uint16_t* row = lcd->fb_back + ((uint32_t)y * (uint32_t)ST7789_WIDTH + x0);
        uint16_t remaining = w;
        while (remaining) {
            uint16_t chunkPixels = remaining;
            if (chunkPixels > (uint16_t)(sizeof(out) / 2u)) chunkPixels = (uint16_t)(sizeof(out) / 2u);
            for (uint16_t i = 0; i < chunkPixels; i++) {
                uint16_t c = row[i];
                out[(uint32_t)i * 2u] = (uint8_t)(c >> 8);
                out[(uint32_t)i * 2u + 1u] = (uint8_t)(c & 0xFFu);
            }
            if (!spi_tx_blocking(out, (uint16_t)(chunkPixels * 2u))) {
                cs_high();
                return;
            }
            row += chunkPixels;
            remaining = (uint16_t)(remaining - chunkPixels);
        }
    }
    cs_high();
}

uint32_t ST7789_RGB(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void ST7789_Init(ST7789_Handle* lcd, const ST7789_Config* cfg)
{
    if (!lcd) return;
    memset(lcd, 0, sizeof(*lcd));
    lcd->cfg.width = ST7789_WIDTH;
    lcd->cfg.height = ST7789_HEIGHT;
    lcd->cfg.x_offset = 0;
    lcd->cfg.y_offset = SPIST7789_Y_OFFSET;
    lcd->cfg.color_mode = ST7789_COLOR_MODE_RGB565;
    lcd->cfg.rotation = ST7789_ROTATION_270;
    lcd->cfg.invert = true;
    lcd->cfg.fps = ST7789_DEFAULT_FPS;
    lcd->cfg.use_framebuffer = false;
    if (cfg) lcd->cfg = *cfg;
    lcd->framebuffer_enabled = lcd->cfg.use_framebuffer;
    lcd->dirty_valid = false;
    lcd->fb_front = NULL;
    lcd->fb_back = NULL;
    if (lcd->framebuffer_enabled) {
        lcd->fb_back = st7789_fb;
        memset(st7789_fb, 0, sizeof(st7789_fb));
    }
    SPIST7789_Init();
    lcd->inited = true;
}

bool ST7789_IsInited(const ST7789_Handle* lcd)
{
    return lcd && lcd->inited;
}

bool ST7789_IsFrameBlocked(const ST7789_Handle* lcd)
{
    return lcd ? lcd->frame_blocked : true;
}

bool ST7789_FrameBegin(ST7789_Handle* lcd)
{
    SPIST7789_Service();
    if (!lcd || !lcd->inited) return false;
    if (lcd->cfg.fps == 0) {
        lcd->frame_blocked = false;
        return true;
    }
    uint32_t intervalMs = 1000u / (uint32_t)lcd->cfg.fps;
    if (intervalMs == 0u) intervalMs = 1u;
    uint32_t nowMs = HAL_GetTick();
    if (lcd->last_frame_ms != 0u && (uint32_t)(nowMs - lcd->last_frame_ms) < intervalMs) {
        lcd->frame_blocked = true;
        return false;
    }
    lcd->last_frame_ms = nowMs;
    lcd->frame_blocked = false;
    return true;
}

void ST7789_FrameEnd(ST7789_Handle* lcd)
{
    if (!lcd || !lcd->inited) return;
    if (lcd->frame_blocked) return;
    if (!lcd->framebuffer_enabled) return;
    if (!lcd->dirty_valid) return;
    st7789_flush_rect(lcd, lcd->dirty_x0, lcd->dirty_y0, lcd->dirty_x1, lcd->dirty_y1);
    lcd->dirty_valid = false;
}

void ST7789_AttachBacklightPWM(ST7789_Handle* lcd, TIM_HandleTypeDef* htim, uint32_t channel)
{
    if (!lcd) return;
    lcd->cfg.bl_htim = htim;
    lcd->cfg.bl_tim_channel = channel;
}

void ST7789_SetRotation(ST7789_Handle* lcd, ST7789_Rotation rotation)
{
    if (!lcd) return;
    lcd->cfg.rotation = rotation;
}

void ST7789_SetOffset(ST7789_Handle* lcd, uint16_t x_offset, uint16_t y_offset)
{
    if (!lcd) return;
    lcd->cfg.x_offset = x_offset;
    lcd->cfg.y_offset = y_offset;
}

void ST7789_SetBacklight(ST7789_Handle* lcd, uint8_t percent)
{
    (void)lcd;
    SPIST7789_SetBacklight(percent);
}

bool ST7789_IsTransferBusy(const ST7789_Handle* lcd)
{
    (void)lcd;
    return SPIST7789_IsBusy();
}

bool ST7789_FillScreenAsync(ST7789_Handle* lcd, uint32_t rgb888)
{
    uint16_t c = st7789_rgb888_to_565(rgb888);
    if (lcd && lcd->framebuffer_enabled) {
        st7789_fb_clear(lcd, c);
        return true;
    }
    return SPIST7789_FillRectColor565Async(0u, 0u, ST7789_WIDTH, ST7789_HEIGHT, c);
}

void ST7789_FillScreen(ST7789_Handle* lcd, uint32_t rgb888)
{
    (void)ST7789_FillScreenAsync(lcd, rgb888);
    (void)SPIST7789_WaitDone(500u);
}

void ST7789_FillRect(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t rgb888)
{
    uint16_t c = st7789_rgb888_to_565(rgb888);
    if (lcd && lcd->framebuffer_enabled) {
        st7789_fb_fill_rect(lcd, x, y, w, h, c);
        return;
    }
    if (!SPIST7789_FillRectColor565Async(x, y, w, h, c)) return;
    (void)SPIST7789_WaitDone(200u);
}

void ST7789_DrawPixel(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint32_t rgb888)
{
    if (lcd && lcd->framebuffer_enabled) {
        if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;
        uint16_t c = st7789_rgb888_to_565(rgb888);
        uint32_t idx = st7789_fb_index(x, y);
        if (lcd->fb_back[idx] == c) return;
        lcd->fb_back[idx] = c;
        st7789_mark_dirty_xy(lcd, x, y);
        return;
    }
    ST7789_FillRect(lcd, x, y, 1u, 1u, rgb888);
}

void ST7789_DrawLine(ST7789_Handle* lcd, int x0, int y0, int x1, int y1, uint32_t rgb888)
{
    int dx = st7789_abs_i(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -st7789_abs_i(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    while (1)
    {
        if (x0 >= 0 && y0 >= 0 && x0 < (int)ST7789_WIDTH && y0 < (int)ST7789_HEIGHT) {
            ST7789_DrawPixel(lcd, (uint16_t)x0, (uint16_t)y0, rgb888);
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void ST7789_DrawRect(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t rgb888)
{
    if (w == 0u || h == 0u) return;
    ST7789_DrawLine(lcd, x, y, (int)(x + w - 1u), y, rgb888);
    ST7789_DrawLine(lcd, x, (int)(y + h - 1u), (int)(x + w - 1u), (int)(y + h - 1u), rgb888);
    ST7789_DrawLine(lcd, x, y, x, (int)(y + h - 1u), rgb888);
    ST7789_DrawLine(lcd, (int)(x + w - 1u), y, (int)(x + w - 1u), (int)(y + h - 1u), rgb888);
}

static const uint8_t st7789_font5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
    {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x10,0x08,0x08,0x10,0x08}
};

static uint16_t st7789_char_cell_w(uint8_t scale)
{
    if (scale == 0u) scale = 1u;
    if (scale == 3u) return 9u;
    return (uint16_t)(6u * scale);
}

static uint16_t st7789_char_cell_h(uint8_t scale)
{
    if (scale == 0u) scale = 1u;
    if (scale == 3u) return 12u;
    return (uint16_t)(8u * scale);
}

void ST7789_DrawChar(ST7789_Handle* lcd, uint16_t x, uint16_t y, char c, uint32_t fg_rgb888, uint32_t bg_rgb888, uint8_t scale)
{
    if (scale == 0u) scale = 1u;
    if (c < 32 || c > 126) c = '?';
    const uint8_t* glyph = st7789_font5x7[(uint8_t)(c - 32)];
    uint16_t w = st7789_char_cell_w(scale);
    uint16_t h = st7789_char_cell_h(scale);
    ST7789_FillRect(lcd, x, y, w, h, bg_rgb888);
    if (scale == 3u)
    {
        for (uint8_t dy = 0; dy < 12u; dy++) {
            uint8_t sy = (uint8_t)(((uint16_t)dy * 2u) / 3u);
            if (sy >= 7u) continue;
            for (uint8_t dx = 0; dx < 9u; dx++) {
                uint8_t sx = (uint8_t)(((uint16_t)dx * 2u) / 3u);
                if (sx >= 5u) continue;
                if (glyph[sx] & (uint8_t)(1u << sy)) ST7789_DrawPixel(lcd, (uint16_t)(x + dx), (uint16_t)(y + dy), fg_rgb888);
            }
        }
        return;
    }
    for (uint8_t col = 0; col < 5u; col++) {
        uint8_t line = glyph[col];
        for (uint8_t row = 0; row < 7u; row++) {
            if (line & 0x01u) ST7789_FillRect(lcd, (uint16_t)(x + col * scale), (uint16_t)(y + row * scale), scale, scale, fg_rgb888);
            line >>= 1u;
        }
    }
}

void ST7789_DrawString(ST7789_Handle* lcd, uint16_t x, uint16_t y, const char* s, uint32_t fg_rgb888, uint32_t bg_rgb888, uint8_t scale)
{
    if (!s) return;
    if (scale == 0u) scale = 1u;
    uint16_t cx = x;
    uint16_t cy = y;
    uint16_t step_x = st7789_char_cell_w(scale);
    uint16_t step_y = st7789_char_cell_h(scale);
    while (*s)
    {
        char c = *s++;
        if (c == '\n') { cx = x; cy = (uint16_t)(cy + step_y); continue; }
        if (c == '\r') continue;
        if (cx + step_x > ST7789_WIDTH) { cx = x; cy = (uint16_t)(cy + step_y); }
        if (cy + step_y > ST7789_HEIGHT) break;
        ST7789_DrawChar(lcd, cx, cy, c, fg_rgb888, bg_rgb888, scale);
        cx = (uint16_t)(cx + step_x);
    }
}

void ST7789_SPI_DMA_IRQHandler(void)
{
    SPIST7789_DMA_IRQHandler();
}

void ST7789_SPI_IRQHandler(void)
{
    SPIST7789_SPI_IRQHandler();
}

void ST7789_DrawCircle(ST7789_Handle* lcd, int x0, int y0, int r, uint32_t rgb888)
{
    if (r < 0) return;
    int x = -r;
    int y = 0;
    int err = 2 - 2 * r;
    do {
        ST7789_DrawPixel(lcd, (uint16_t)(x0 - x), (uint16_t)(y0 + y), rgb888);
        ST7789_DrawPixel(lcd, (uint16_t)(x0 - y), (uint16_t)(y0 - x), rgb888);
        ST7789_DrawPixel(lcd, (uint16_t)(x0 + x), (uint16_t)(y0 - y), rgb888);
        ST7789_DrawPixel(lcd, (uint16_t)(x0 + y), (uint16_t)(y0 + x), rgb888);
        int e2 = err;
        if (e2 <= y) {
            y++;
            err += y * 2 + 1;
            if (-x == y && e2 <= x) e2 = 0;
        }
        if (e2 > x) {
            x++;
            err += x * 2 + 1;
        }
    } while (x <= 0);
}

void ST7789_FillCircle(ST7789_Handle* lcd, int x0, int y0, int r, uint32_t rgb888)
{
    if (r < 0) return;
    int x = -r;
    int y = 0;
    int err = 2 - 2 * r;
    do {
        ST7789_DrawLine(lcd, x0 + x, y0 - y, x0 - x, y0 - y, rgb888);
        ST7789_DrawLine(lcd, x0 + x, y0 + y, x0 - x, y0 + y, rgb888);
        int e2 = err;
        if (e2 <= y) {
            y++;
            err += y * 2 + 1;
            if (-x == y && e2 <= x) e2 = 0;
        }
        if (e2 > x) {
            x++;
            err += x * 2 + 1;
        }
    } while (x <= 0);
}

void ST7789_DrawBitmap(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const void* pixels, ST7789_BitmapFormat format, uint32_t stride_bytes)
{
    if (!lcd || !lcd->inited || !pixels) return;
    if (w == 0u || h == 0u) return;
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;
    if (stride_bytes == 0u) {
        if (format == ST7789_BITMAP_RGB565_BE || format == ST7789_BITMAP_RGB565_LE) stride_bytes = (uint32_t)w * 2u;
        else if (format == ST7789_BITMAP_RGB888) stride_bytes = (uint32_t)w * 3u;
        else stride_bytes = (uint32_t)w * 4u;
    }
    uint16_t draw_w = w;
    uint16_t draw_h = h;
    if ((uint32_t)x + draw_w > ST7789_WIDTH) draw_w = (uint16_t)(ST7789_WIDTH - x);
    if ((uint32_t)y + draw_h > ST7789_HEIGHT) draw_h = (uint16_t)(ST7789_HEIGHT - y);

    const uint8_t* src = (const uint8_t*)pixels;
    for (uint16_t row = 0; row < draw_h; row++) {
        const uint8_t* row_ptr = src + (size_t)row * (size_t)stride_bytes;
        for (uint16_t col = 0; col < draw_w; col++) {
            uint32_t rgb = 0;
            if (format == ST7789_BITMAP_RGB565_BE) {
                const uint8_t* p = row_ptr + (size_t)col * 2u;
                uint16_t c = (uint16_t)((uint16_t)p[0] << 8 | (uint16_t)p[1]);
                uint8_t r5 = (uint8_t)((c >> 11) & 0x1Fu);
                uint8_t g6 = (uint8_t)((c >> 5) & 0x3Fu);
                uint8_t b5 = (uint8_t)(c & 0x1Fu);
                rgb = ST7789_RGB((uint8_t)((r5 << 3) | (r5 >> 2)), (uint8_t)((g6 << 2) | (g6 >> 4)), (uint8_t)((b5 << 3) | (b5 >> 2)));
            } else if (format == ST7789_BITMAP_RGB565_LE) {
                const uint8_t* p = row_ptr + (size_t)col * 2u;
                uint16_t c = (uint16_t)((uint16_t)p[1] << 8 | (uint16_t)p[0]);
                uint8_t r5 = (uint8_t)((c >> 11) & 0x1Fu);
                uint8_t g6 = (uint8_t)((c >> 5) & 0x3Fu);
                uint8_t b5 = (uint8_t)(c & 0x1Fu);
                rgb = ST7789_RGB((uint8_t)((r5 << 3) | (r5 >> 2)), (uint8_t)((g6 << 2) | (g6 >> 4)), (uint8_t)((b5 << 3) | (b5 >> 2)));
            } else if (format == ST7789_BITMAP_RGB888) {
                const uint8_t* p = row_ptr + (size_t)col * 3u;
                rgb = ST7789_RGB(p[0], p[1], p[2]);
            } else {
                const uint8_t* p = row_ptr + (size_t)col * 4u;
                if (p[0] == 0u) continue;
                rgb = ST7789_RGB(p[1], p[2], p[3]);
            }
            ST7789_DrawPixel(lcd, (uint16_t)(x + col), (uint16_t)(y + row), rgb);
        }
    }
}

void ST7789_DrawBitmap1BPP(ST7789_Handle* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint8_t* bits, uint32_t stride_bytes, uint32_t fg_rgb888, uint32_t bg_rgb888)
{
    if (!lcd || !lcd->inited || !bits) return;
    if (w == 0u || h == 0u) return;
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;
    if (stride_bytes == 0u) stride_bytes = (uint32_t)((w + 7u) >> 3);
    uint16_t draw_w = w;
    uint16_t draw_h = h;
    if ((uint32_t)x + draw_w > ST7789_WIDTH) draw_w = (uint16_t)(ST7789_WIDTH - x);
    if ((uint32_t)y + draw_h > ST7789_HEIGHT) draw_h = (uint16_t)(ST7789_HEIGHT - y);

    for (uint16_t row = 0; row < draw_h; row++) {
        const uint8_t* row_ptr = bits + (size_t)row * (size_t)stride_bytes;
        for (uint16_t col = 0; col < draw_w; col++) {
            uint8_t byte = row_ptr[col >> 3];
            uint8_t bit = (uint8_t)((byte >> (7u - (col & 7u))) & 0x01u);
            ST7789_DrawPixel(lcd, (uint16_t)(x + col), (uint16_t)(y + row), bit ? fg_rgb888 : bg_rgb888);
        }
    }
}

typedef struct
{
    const uint8_t* data;
    size_t len;
    size_t pos;
} st7789_gif_stream_t;

typedef struct
{
    bool transparent;
    uint8_t transparent_index;
    uint16_t delay_cs;
    uint8_t disposal;
} st7789_gif_gce_t;

typedef struct
{
    st7789_gif_stream_t* s;
    uint8_t subblock_remaining;
    bool terminator_seen;
    uint32_t datum;
    uint8_t bits;
} st7789_gif_lzw_reader_t;

static uint16_t st7789_gif_prefix[4096];
static uint8_t st7789_gif_suffix[4096];
static uint8_t st7789_gif_stack[4096];
static uint8_t st7789_gif_gct[256 * 3];
static uint8_t st7789_gif_lct[256 * 3];
static uint8_t st7789_gif_line_idx[ST7789_WIDTH];

static bool st7789_gif_read_u8(st7789_gif_stream_t* s, uint8_t* out)
{
    if (!s || !out || s->pos >= s->len) return false;
    *out = s->data[s->pos++];
    return true;
}

static bool st7789_gif_read_u16le(st7789_gif_stream_t* s, uint16_t* out)
{
    uint8_t lo = 0, hi = 0;
    if (!st7789_gif_read_u8(s, &lo)) return false;
    if (!st7789_gif_read_u8(s, &hi)) return false;
    *out = (uint16_t)((uint16_t)lo | ((uint16_t)hi << 8));
    return true;
}

static bool st7789_gif_skip(st7789_gif_stream_t* s, size_t n)
{
    if (!s) return false;
    if (s->pos + n > s->len) return false;
    s->pos += n;
    return true;
}

static bool st7789_gif_skip_subblocks(st7789_gif_stream_t* s)
{
    uint8_t sz = 0;
    while (1) {
        if (!st7789_gif_read_u8(s, &sz)) return false;
        if (sz == 0u) return true;
        if (!st7789_gif_skip(s, sz)) return false;
    }
}

static bool st7789_gif_read_color_table(st7789_gif_stream_t* s, uint8_t* pal, uint16_t count)
{
    if (!s || !pal || count > 256u) return false;
    for (uint16_t i = 0; i < count; i++) {
        if (!st7789_gif_read_u8(s, &pal[i * 3u + 0u])) return false;
        if (!st7789_gif_read_u8(s, &pal[i * 3u + 1u])) return false;
        if (!st7789_gif_read_u8(s, &pal[i * 3u + 2u])) return false;
    }
    return true;
}

static bool st7789_gif_read_gce(st7789_gif_stream_t* s, st7789_gif_gce_t* gce)
{
    uint8_t block_size = 0;
    if (!st7789_gif_read_u8(s, &block_size)) return false;
    if (block_size != 4u) return false;
    uint8_t packed = 0, trans = 0, terminator = 0;
    uint16_t delay_cs = 0;
    if (!st7789_gif_read_u8(s, &packed)) return false;
    if (!st7789_gif_read_u16le(s, &delay_cs)) return false;
    if (!st7789_gif_read_u8(s, &trans)) return false;
    if (!st7789_gif_read_u8(s, &terminator)) return false;
    if (terminator != 0u) return false;
    if (gce) {
        gce->transparent = (packed & 0x01u) ? true : false;
        gce->transparent_index = trans;
        gce->delay_cs = delay_cs;
        gce->disposal = (uint8_t)((packed >> 2) & 0x07u);
    }
    return true;
}

static bool st7789_gif_lzw_next_data_byte(st7789_gif_lzw_reader_t* r, uint8_t* out)
{
    if (!r || !out) return false;
    if (r->terminator_seen) return false;
    if (r->subblock_remaining == 0u) {
        uint8_t sz = 0;
        if (!st7789_gif_read_u8(r->s, &sz)) return false;
        if (sz == 0u) {
            r->terminator_seen = true;
            return false;
        }
        r->subblock_remaining = sz;
    }
    if (!st7789_gif_read_u8(r->s, out)) return false;
    r->subblock_remaining--;
    return true;
}

static bool st7789_gif_lzw_drain_to_terminator(st7789_gif_lzw_reader_t* r)
{
    if (!r) return false;
    if (r->terminator_seen) return true;
    if (r->subblock_remaining) {
        if (!st7789_gif_skip(r->s, r->subblock_remaining)) return false;
        r->subblock_remaining = 0;
    }
    if (!st7789_gif_skip_subblocks(r->s)) return false;
    r->terminator_seen = true;
    return true;
}

static int st7789_gif_lzw_read_code(st7789_gif_lzw_reader_t* r, int code_size)
{
    while (r->bits < (uint8_t)code_size) {
        uint8_t byte = 0;
        if (!st7789_gif_lzw_next_data_byte(r, &byte)) return -1;
        r->datum |= ((uint32_t)byte << r->bits);
        r->bits = (uint8_t)(r->bits + 8u);
    }
    int mask = (1 << code_size) - 1;
    int code = (int)(r->datum & (uint32_t)mask);
    r->datum >>= code_size;
    r->bits = (uint8_t)(r->bits - (uint8_t)code_size);
    return code;
}

static void st7789_gif_palette_color(const uint8_t* pal, uint16_t count, uint8_t index, uint8_t* r, uint8_t* g, uint8_t* b)
{
    if (!pal || !r || !g || !b || count == 0u || index >= count) {
        *r = 0u; *g = 0u; *b = 0u;
        return;
    }
    *r = pal[index * 3u + 0u];
    *g = pal[index * 3u + 1u];
    *b = pal[index * 3u + 2u];
}

static void st7789_gif_write_scanline(ST7789_Handle* lcd, uint16_t x, uint16_t y, const uint8_t* idx, uint16_t w, const uint8_t* pal, uint16_t pal_count, const st7789_gif_gce_t* gce, uint32_t bg_rgb888)
{
    if (!lcd || !idx || w == 0u) return;
    for (uint16_t i = 0; i < w; i++) {
        uint8_t px = idx[i];
        if (gce && gce->transparent && px == gce->transparent_index) {
            ST7789_DrawPixel(lcd, (uint16_t)(x + i), y, bg_rgb888);
        } else {
            uint8_t r = 0, g = 0, b = 0;
            st7789_gif_palette_color(pal, pal_count, px, &r, &g, &b);
            ST7789_DrawPixel(lcd, (uint16_t)(x + i), y, ST7789_RGB(r, g, b));
        }
    }
}

static bool st7789_gif_decode_image(ST7789_Handle* lcd, uint16_t base_x, uint16_t base_y, st7789_gif_stream_t* s, uint16_t left, uint16_t top, uint16_t w, uint16_t h, bool interlaced, const uint8_t* pal, uint16_t pal_count, const st7789_gif_gce_t* gce, uint32_t bg_rgb888, uint16_t* out_delay_ms, uint8_t* out_disposal)
{
    uint8_t lzw_min = 0;
    if (!st7789_gif_read_u8(s, &lzw_min)) return false;
    if (lzw_min < 2u || lzw_min > 8u) return false;
    if (w > ST7789_WIDTH) return false;

    st7789_gif_lzw_reader_t r = {0};
    r.s = s;
    int clear_code = 1 << lzw_min;
    int end_code = clear_code + 1;
    int code_size = lzw_min + 1;
    int next_code = end_code + 1;
    int old_code = -1;
    uint8_t first = 0;

    for (int i = 0; i < clear_code; i++) {
        st7789_gif_prefix[i] = 0xFFFFu;
        st7789_gif_suffix[i] = (uint8_t)i;
    }

    uint16_t row_actual = 0;
    uint8_t pass = 0;
    uint16_t interlace_row = 0;
    uint16_t col = 0;
    uint16_t rows_written = 0;

    while (rows_written < h) {
        int code = st7789_gif_lzw_read_code(&r, code_size);
        if (code < 0) break;
        if (code == clear_code) {
            code_size = lzw_min + 1;
            next_code = end_code + 1;
            old_code = -1;
            continue;
        }
        if (code == end_code) break;

        if (old_code == -1) {
            uint8_t px = (uint8_t)code;
            st7789_gif_line_idx[col++] = px;
            old_code = code;
            first = (uint8_t)code;
        } else {
            int in_code = code;
            int stack_top = 0;
            if (code >= next_code) {
                st7789_gif_stack[stack_top++] = first;
                code = old_code;
            }
            while (code >= clear_code) {
                st7789_gif_stack[stack_top++] = st7789_gif_suffix[code];
                code = st7789_gif_prefix[code];
            }
            first = (uint8_t)code;
            st7789_gif_stack[stack_top++] = first;
            if (next_code < 4096) {
                st7789_gif_prefix[next_code] = (uint16_t)old_code;
                st7789_gif_suffix[next_code] = first;
                next_code++;
                if (next_code == (1 << code_size) && code_size < 12) code_size++;
            }
            old_code = in_code;
            while (stack_top) {
                st7789_gif_line_idx[col++] = st7789_gif_stack[--stack_top];
                if (col == w) break;
            }
        }

        if (col == w) {
            uint16_t sx = (uint16_t)(base_x + left);
            uint16_t sy = (uint16_t)(base_y + top + row_actual);
            if (sx < ST7789_WIDTH && sy < ST7789_HEIGHT) {
                uint16_t vis_w = w;
                if ((uint32_t)sx + vis_w > ST7789_WIDTH) vis_w = (uint16_t)(ST7789_WIDTH - sx);
                st7789_gif_write_scanline(lcd, sx, sy, st7789_gif_line_idx, vis_w, pal, pal_count, gce, bg_rgb888);
            }
            col = 0;
            rows_written++;
            if (interlaced) {
                static const uint8_t start[4] = {0, 4, 2, 1};
                static const uint8_t step[4] = {8, 8, 4, 2};
                interlace_row = (uint16_t)(interlace_row + step[pass]);
                while (pass < 3u && interlace_row >= h) {
                    pass++;
                    interlace_row = start[pass];
                }
                row_actual = interlace_row;
            } else {
                row_actual++;
            }
        }
    }

    if (!st7789_gif_lzw_drain_to_terminator(&r)) return false;
    if (rows_written != h) return false;
    if (out_delay_ms) {
        uint16_t d = gce ? gce->delay_cs : 0u;
        uint16_t ms = (uint16_t)(d * 10u);
        if (ms == 0u) ms = 10u;
        *out_delay_ms = ms;
    }
    if (out_disposal) *out_disposal = gce ? gce->disposal : 0u;
    return true;
}

static bool st7789_gif_has_signature(const uint8_t* gif, size_t gif_len)
{
    if (!gif || gif_len < 6u) return false;
    if (gif[0] != 'G' || gif[1] != 'I' || gif[2] != 'F') return false;
    if (gif[3] != '8') return false;
    if (gif[4] != '7' && gif[4] != '9') return false;
    if (gif[5] != 'a') return false;
    return true;
}

bool ST7789_GIF_GetCanvasSize(const uint8_t* gif, size_t gif_len, uint16_t* out_w, uint16_t* out_h)
{
    if (!st7789_gif_has_signature(gif, gif_len)) return false;
    st7789_gif_stream_t s = {0};
    s.data = gif;
    s.len = gif_len;
    s.pos = 6;
    uint16_t w = 0, h = 0;
    uint8_t packed = 0, bg = 0, aspect = 0;
    if (!st7789_gif_read_u16le(&s, &w)) return false;
    if (!st7789_gif_read_u16le(&s, &h)) return false;
    if (!st7789_gif_read_u8(&s, &packed)) return false;
    if (!st7789_gif_read_u8(&s, &bg)) return false;
    if (!st7789_gif_read_u8(&s, &aspect)) return false;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return true;
}

bool ST7789_GIF_RenderFirstFrame(ST7789_Handle* lcd, uint16_t x, uint16_t y, const uint8_t* gif, size_t gif_len, uint32_t bg_rgb888)
{
    if (!lcd || !lcd->inited) return false;
    if (!st7789_gif_has_signature(gif, gif_len)) return false;
    st7789_gif_stream_t s = {0};
    s.data = gif;
    s.len = gif_len;
    s.pos = 6;

    uint16_t canvas_w = 0, canvas_h = 0;
    uint8_t packed = 0, bg_index = 0, aspect = 0;
    if (!st7789_gif_read_u16le(&s, &canvas_w)) return false;
    if (!st7789_gif_read_u16le(&s, &canvas_h)) return false;
    if (!st7789_gif_read_u8(&s, &packed)) return false;
    if (!st7789_gif_read_u8(&s, &bg_index)) return false;
    if (!st7789_gif_read_u8(&s, &aspect)) return false;

    bool gct_flag = (packed & 0x80u) ? true : false;
    uint16_t gct_count = 0;
    if (gct_flag) {
        uint8_t sz = (uint8_t)(packed & 0x07u);
        gct_count = (uint16_t)(1u << (sz + 1u));
        if (!st7789_gif_read_color_table(&s, st7789_gif_gct, gct_count)) return false;
    }

    st7789_gif_gce_t gce = {0};
    while (s.pos < s.len) {
        uint8_t sep = 0;
        if (!st7789_gif_read_u8(&s, &sep)) return false;
        if (sep == 0x3Bu) return false;
        if (sep == 0x21u) {
            uint8_t label = 0;
            if (!st7789_gif_read_u8(&s, &label)) return false;
            if (label == 0xF9u) {
                if (!st7789_gif_read_gce(&s, &gce)) return false;
            } else {
                if (!st7789_gif_skip_subblocks(&s)) return false;
            }
            continue;
        }
        if (sep == 0x2Cu) {
            uint16_t left = 0, top = 0, w = 0, h = 0;
            uint8_t ipacked = 0;
            if (!st7789_gif_read_u16le(&s, &left)) return false;
            if (!st7789_gif_read_u16le(&s, &top)) return false;
            if (!st7789_gif_read_u16le(&s, &w)) return false;
            if (!st7789_gif_read_u16le(&s, &h)) return false;
            if (!st7789_gif_read_u8(&s, &ipacked)) return false;
            bool lct_flag = (ipacked & 0x80u) ? true : false;
            bool interlaced = (ipacked & 0x40u) ? true : false;
            uint16_t lct_count = 0;
            const uint8_t* pal = st7789_gif_gct;
            uint16_t pal_count = gct_count;
            if (lct_flag) {
                uint8_t sz = (uint8_t)(ipacked & 0x07u);
                lct_count = (uint16_t)(1u << (sz + 1u));
                if (!st7789_gif_read_color_table(&s, st7789_gif_lct, lct_count)) return false;
                pal = st7789_gif_lct;
                pal_count = lct_count;
            }
            uint16_t delay_ms = 0;
            uint8_t disposal = 0;
            bool ok = st7789_gif_decode_image(lcd, x, y, &s, left, top, w, h, interlaced, pal, pal_count, &gce, bg_rgb888, &delay_ms, &disposal);
            (void)canvas_w; (void)canvas_h; (void)bg_index; (void)aspect; (void)delay_ms; (void)disposal;
            return ok;
        }
        return false;
    }
    return false;
}

bool ST7789_GIF_Play(ST7789_Handle* lcd, uint16_t x, uint16_t y, const uint8_t* gif, size_t gif_len, uint32_t bg_rgb888, uint16_t repeat)
{
    if (!lcd || !lcd->inited) return false;
    if (!st7789_gif_has_signature(gif, gif_len)) return false;
    if (repeat == 0u) repeat = 1u;

    for (uint16_t rep = 0; rep < repeat; rep++) {
        st7789_gif_stream_t s = {0};
        s.data = gif;
        s.len = gif_len;
        s.pos = 6;
        uint16_t canvas_w = 0, canvas_h = 0;
        uint8_t packed = 0, bg_index = 0, aspect = 0;
        if (!st7789_gif_read_u16le(&s, &canvas_w)) return false;
        if (!st7789_gif_read_u16le(&s, &canvas_h)) return false;
        if (!st7789_gif_read_u8(&s, &packed)) return false;
        if (!st7789_gif_read_u8(&s, &bg_index)) return false;
        if (!st7789_gif_read_u8(&s, &aspect)) return false;
        bool gct_flag = (packed & 0x80u) ? true : false;
        uint16_t gct_count = 0;
        if (gct_flag) {
            uint8_t sz = (uint8_t)(packed & 0x07u);
            gct_count = (uint16_t)(1u << (sz + 1u));
            if (!st7789_gif_read_color_table(&s, st7789_gif_gct, gct_count)) return false;
        }
        st7789_gif_gce_t gce = {0};
        while (s.pos < s.len) {
            uint8_t sep = 0;
            if (!st7789_gif_read_u8(&s, &sep)) return false;
            if (sep == 0x3Bu) break;
            if (sep == 0x21u) {
                uint8_t label = 0;
                if (!st7789_gif_read_u8(&s, &label)) return false;
                if (label == 0xF9u) {
                    if (!st7789_gif_read_gce(&s, &gce)) return false;
                } else {
                    if (!st7789_gif_skip_subblocks(&s)) return false;
                }
                continue;
            }
            if (sep == 0x2Cu) {
                uint16_t left = 0, top = 0, w = 0, h = 0;
                uint8_t ipacked = 0;
                if (!st7789_gif_read_u16le(&s, &left)) return false;
                if (!st7789_gif_read_u16le(&s, &top)) return false;
                if (!st7789_gif_read_u16le(&s, &w)) return false;
                if (!st7789_gif_read_u16le(&s, &h)) return false;
                if (!st7789_gif_read_u8(&s, &ipacked)) return false;
                bool lct_flag = (ipacked & 0x80u) ? true : false;
                bool interlaced = (ipacked & 0x40u) ? true : false;
                uint16_t lct_count = 0;
                const uint8_t* pal = st7789_gif_gct;
                uint16_t pal_count = gct_count;
                if (lct_flag) {
                    uint8_t sz = (uint8_t)(ipacked & 0x07u);
                    lct_count = (uint16_t)(1u << (sz + 1u));
                    if (!st7789_gif_read_color_table(&s, st7789_gif_lct, lct_count)) return false;
                    pal = st7789_gif_lct;
                    pal_count = lct_count;
                }
                uint16_t delay_ms = 0;
                uint8_t disposal = 0;
                if (!st7789_gif_decode_image(lcd, x, y, &s, left, top, w, h, interlaced, pal, pal_count, &gce, bg_rgb888, &delay_ms, &disposal)) return false;
                ST7789_FrameEnd(lcd);
                HAL_Delay(delay_ms);
                if (disposal == 2u) {
                    uint16_t fx = (uint16_t)(x + left);
                    uint16_t fy = (uint16_t)(y + top);
                    if (fx < ST7789_WIDTH && fy < ST7789_HEIGHT) {
                        uint16_t fw = w, fh = h;
                        if ((uint32_t)fx + fw > ST7789_WIDTH) fw = (uint16_t)(ST7789_WIDTH - fx);
                        if ((uint32_t)fy + fh > ST7789_HEIGHT) fh = (uint16_t)(ST7789_HEIGHT - fy);
                        ST7789_FillRect(lcd, fx, fy, fw, fh, bg_rgb888);
                    }
                }
                gce.transparent = false;
                gce.transparent_index = 0;
                gce.delay_cs = 0;
                gce.disposal = 0;
                (void)canvas_w; (void)canvas_h; (void)bg_index; (void)aspect;
                continue;
            }
            return false;
        }
    }
    return true;
}

static const uint8_t* st7789_assets_base = (const uint8_t*)0x905B0000u;

const void* ST7789_Assets_GetBaseAddress(void)
{
    return st7789_assets_base;
}

void ST7789_Assets_SetBaseAddress(const void* base)
{
    if (base) st7789_assets_base = (const uint8_t*)base;
}

static uint16_t st7789_u16le(const uint8_t* p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t st7789_u32le(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool st7789_assets_header_ok(const uint8_t* base, uint32_t* out_count, uint32_t* out_index_size)
{
    if (!base) return false;
    if (base[0] != 'H' || base[1] != 'I' || base[2] != 'M' || base[3] != 'G') return false;
    uint16_t ver = st7789_u16le(base + 4);
    if (ver != 1u) return false;
    uint32_t count = st7789_u32le(base + 16);
    uint32_t index_size = st7789_u32le(base + 20);
    if (count > 4096u) return false;
    if ((index_size & 0x0Fu) != 0u) return false;
    if (out_count) *out_count = count;
    if (out_index_size) *out_index_size = index_size;
    return true;
}

bool ST7789_Assets_Find(const char* name, ST7789_AssetInfo* out)
{
    if (!name || !out) return false;
    const uint8_t* base = st7789_assets_base;
    uint32_t count = 0, index_size = 0;
    if (!st7789_assets_header_ok(base, &count, &index_size)) return false;
    const uint8_t* index = base + 64;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* e = index + i * 64u;
        char nbuf[33];
        memcpy(nbuf, e, 32);
        nbuf[32] = '\0';
        for (int j = 0; j < 32; j++) {
            if (nbuf[j] == '\0') break;
            if ((unsigned char)nbuf[j] < 0x20u) { nbuf[j] = '\0'; break; }
        }
        if (strncmp(nbuf, name, 32) != 0) continue;
        out->type = (ST7789_AssetType)e[32];
        out->data = base + st7789_u32le(e + 36);
        out->size = st7789_u32le(e + 40);
        out->width = st7789_u16le(e + 44);
        out->height = st7789_u16le(e + 46);
        return true;
    }
    (void)index_size;
    return false;
}

bool ST7789_Assets_Draw(ST7789_Handle* lcd, uint16_t x, uint16_t y, const char* name, uint32_t bg_rgb888)
{
    if (!lcd || !lcd->inited) return false;
    ST7789_AssetInfo info = {0};
    if (!ST7789_Assets_Find(name, &info)) return false;
    if (info.type == ST7789_ASSET_TYPE_GIF) return ST7789_GIF_RenderFirstFrame(lcd, x, y, (const uint8_t*)info.data, (size_t)info.size, bg_rgb888);
    if (info.type == ST7789_ASSET_TYPE_RGB565LE) {
        ST7789_DrawBitmap(lcd, x, y, info.width, info.height, info.data, ST7789_BITMAP_RGB565_LE, (uint32_t)info.width * 2u);
        return true;
    }
    return false;
}

bool ST7789_Assets_Play(ST7789_Handle* lcd, uint16_t x, uint16_t y, const char* name, uint32_t bg_rgb888, uint16_t repeat)
{
    if (!lcd || !lcd->inited) return false;
    ST7789_AssetInfo info = {0};
    if (!ST7789_Assets_Find(name, &info)) return false;
    if (info.type == ST7789_ASSET_TYPE_GIF) return ST7789_GIF_Play(lcd, x, y, (const uint8_t*)info.data, (size_t)info.size, bg_rgb888, repeat);
    return ST7789_Assets_Draw(lcd, x, y, name, bg_rgb888);
}
