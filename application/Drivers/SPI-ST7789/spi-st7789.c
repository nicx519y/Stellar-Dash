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

static inline void gpio_write(GPIO_TypeDef* port, uint16_t pin, GPIO_PinState state)
{
    HAL_GPIO_WritePin(port, pin, state);
}

static inline void cs_low(void) { gpio_write(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_RESET); }
static inline void cs_high(void) { gpio_write(ST7789_CS_PORT, ST7789_CS_PIN, GPIO_PIN_SET); }
static inline void dc_cmd(void) { gpio_write(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_RESET); }
static inline void dc_data(void) { gpio_write(ST7789_DC_PORT, ST7789_DC_PIN, GPIO_PIN_SET); }

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

void SPIST7789_DMA_IRQHandler(void)
{
    g_dma_irq_flag = 1;
    HAL_DMA_IRQHandler(&g_dma);
}

void SPIST7789_SPI_IRQHandler(void)
{
    HAL_SPI_IRQHandler(&g_hspi);
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
    gpio_write(ST7789_BL_PORT, ST7789_BL_PIN, ST7789_BL_ON_STATE);
}

bool SPIST7789_IsBusy(void)
{
    return g_busy;
}

bool SPIST7789_FillBlueAsync(void)
{
    if (g_busy) return false;
    if (!spi_wait_ready(50)) return false;

    uint16_t color565 = 0x001Fu;
    if (g_test_phase == 0u) color565 = 0xFFFFu;
    else if (g_test_phase == 1u) color565 = 0xF800u;
    g_test_phase = (uint8_t)((g_test_phase + 1u) % 3u);
    uint8_t hi = (uint8_t)(color565 >> 8);
    uint8_t lo = (uint8_t)(color565 & 0xFFu);
    for (size_t i = 0; i < sizeof(g_fillbuf); i += 2) {
        g_fillbuf[i] = hi;
        g_fillbuf[i + 1] = lo;
    }
    dcache_clean(g_fillbuf, sizeof(g_fillbuf));

    const uint16_t w = ST7789_WIDTH;
    const uint16_t h = ST7789_HEIGHT;
    if (!set_window(0u, 0u, w, h)) {
        g_dma_err_flag = 1;
        return false;
    }

    g_remaining = (uint32_t)w * (uint32_t)h * 2u;
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
    return true;
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
