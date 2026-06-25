#include "rf_bridge_port.hpp"

#include <string.h>

#include "board_cfg.h"
#include "rf_bridge_port_internal.h"
#include "system_logger.h"
#include "stm32h7xx_hal.h"

namespace {
#ifndef RF_BRIDGE_SPI_BAUD_PRESCALER
#define RF_BRIDGE_SPI_BAUD_PRESCALER SPI_BAUDRATEPRESCALER_8
#endif

#ifndef RF_BRIDGE_INPUT_DMA_FASTPATH
#define RF_BRIDGE_INPUT_DMA_FASTPATH 0
#endif

#ifndef RF_BRIDGE_EVENT_DRAIN_LIMIT
#define RF_BRIDGE_EVENT_DRAIN_LIMIT 4u
#endif

#ifndef RF_BRIDGE_EVENT_RX_CHUNK
#define RF_BRIDGE_EVENT_RX_CHUNK 4u
#endif

#ifndef RF_BRIDGE_EVENT_RX_GAP_MS
#define RF_BRIDGE_EVENT_RX_GAP_MS 1u
#endif

#ifndef RF_BRIDGE_IRQ_LOW_TIMEOUT_MS
#define RF_BRIDGE_IRQ_LOW_TIMEOUT_MS 20u
#endif

#ifndef RF_BRIDGE_MIN_CONTROL_TX_BYTES
#define RF_BRIDGE_MIN_CONTROL_TX_BYTES 14u
#endif

static SPI_HandleTypeDef s_rf_hspi = {};
static DMA_HandleTypeDef s_rf_dma_tx = {};
static bool s_rf_spi_ready = false;
static bool s_rf_dma_ready = false;
static volatile bool s_dma_busy = false;
static volatile bool s_dma_pending = false;
static volatile uint16_t s_dma_pending_len = 0u;
static volatile uint8_t s_irq_event_pending = 0u;
static uint8_t s_dma_active_buf[32] __attribute__((section(".DMA_Section"), aligned(32)));
static uint8_t s_dma_pending_buf[32] __attribute__((section(".DMA_Section"), aligned(32)));
static uint32_t s_diag_spi_init_fail = 0u;
static uint32_t s_diag_tx_fail = 0u;
static uint32_t s_diag_irq_timeout = 0u;
static uint32_t s_diag_rx_invalid = 0u;
static uint32_t s_diag_rx_io_fail = 0u;
static uint32_t s_diag_dma_start_fail = 0u;
static uint32_t s_diag_dma_overwrite = 0u;
static uint32_t s_diag_dma_done = 0u;
static uint32_t s_diag_input_blocking_done = 0u;
static uint32_t s_diag_dma_irq = 0u;
static uint32_t s_diag_spi_irq = 0u;
static uint32_t s_diag_exti_irq = 0u;
static uint32_t s_diag_spi_err = 0u;
static uint32_t s_stat_last_ms = 0u;
static uint32_t s_stat_tx_win = 0u;
static uint32_t s_stat_tx_ok_win = 0u;
static uint32_t s_stat_tx_fail_win = 0u;
static uint32_t s_stat_input_win = 0u;
static uint32_t s_stat_input_ok_win = 0u;
static uint32_t s_stat_input_fail_win = 0u;
static uint32_t s_stat_input_total = 0u;
static uint8_t s_stat_last_cmd = 0u;
static uint8_t s_stat_last_seq = 0u;

static void rf_enable_gpio_clock(GPIO_TypeDef* port) {
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

static void rf_cs_set(bool high) {
    HAL_GPIO_WritePin(RF_BRIDGE_SPI_GPIO_PORT, RF_BRIDGE_SPI_NSS_PIN, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static bool rf_wait_irq_high(uint32_t timeoutMs) {
    const uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeoutMs) {
        if (HAL_GPIO_ReadPin(RF_BRIDGE_IRQ_GPIO_PORT, RF_BRIDGE_IRQ_PIN) == GPIO_PIN_SET) {
            return true;
        }
    }
    return false;
}

static bool rf_wait_irq_low(uint32_t timeoutMs) {
    const uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeoutMs) {
        if (HAL_GPIO_ReadPin(RF_BRIDGE_IRQ_GPIO_PORT, RF_BRIDGE_IRQ_PIN) == GPIO_PIN_RESET) {
            return true;
        }
    }
    return HAL_GPIO_ReadPin(RF_BRIDGE_IRQ_GPIO_PORT, RF_BRIDGE_IRQ_PIN) == GPIO_PIN_RESET;
}

static bool rf_is_valid_evt(uint8_t evt) {
    return (evt == 0x81u) || (evt == 0x82u) || (evt == 0x83u) || (evt == 0x84u) || (evt == 0x85u);
}

static uint8_t rf_checksum8(const uint8_t* data, uint16_t len) {
    uint8_t s = 0u;
    for (uint16_t i = 0u; i < len; ++i) {
        s = static_cast<uint8_t>(s + data[i]);
    }
    return s;
}

static bool rf_has_pending_event_signal() {
    return HAL_GPIO_ReadPin(RF_BRIDGE_IRQ_GPIO_PORT, RF_BRIDGE_IRQ_PIN) == GPIO_PIN_SET;
}

static void rf_consume_irq_pending_marker() {
    __disable_irq();
    if (s_irq_event_pending != 0u) {
        s_irq_event_pending--;
    }
    __enable_irq();
}

static void rf_clean_dcache(const void* ptr, uint16_t len) {
    const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr) & ~static_cast<uintptr_t>(31u);
    const uintptr_t end = (reinterpret_cast<uintptr_t>(ptr) + len + 31u) & ~static_cast<uintptr_t>(31u);
    SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(addr), static_cast<int32_t>(end - addr));
}

static void rf_note_transfer(bool isInput, bool ok, uint8_t cmd, uint16_t txLen, uint8_t seq) {
    const uint32_t now = HAL_GetTick();
    if (s_stat_last_ms == 0u) {
        s_stat_last_ms = now;
    }

    s_stat_tx_win++;
    s_stat_last_cmd = cmd;
    if (ok) {
        s_stat_tx_ok_win++;
    } else {
        s_stat_tx_fail_win++;
    }

    if (isInput) {
        s_stat_input_win++;
        s_stat_last_seq = seq;
        if (ok) {
            s_stat_input_ok_win++;
            s_stat_input_total++;
        } else {
            s_stat_input_fail_win++;
        }
    }

    (void)txLen;
}

static bool rf_dma_init_once() {
    if (s_rf_dma_ready) {
        return true;
    }

    __HAL_RCC_DMA2_CLK_ENABLE();
#ifdef __HAL_RCC_DMAMUX1_CLK_ENABLE
    __HAL_RCC_DMAMUX1_CLK_ENABLE();
#endif
    memset(&s_rf_dma_tx, 0, sizeof(s_rf_dma_tx));
    s_rf_dma_tx.Instance = DMA2_Stream5;
    s_rf_dma_tx.Init.Request = DMA_REQUEST_SPI4_TX;
    s_rf_dma_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    s_rf_dma_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    s_rf_dma_tx.Init.MemInc = DMA_MINC_ENABLE;
    s_rf_dma_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    s_rf_dma_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    s_rf_dma_tx.Init.Mode = DMA_NORMAL;
    s_rf_dma_tx.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    s_rf_dma_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&s_rf_dma_tx) != HAL_OK) {
        return false;
    }

    __HAL_DMA_DISABLE_IT(&s_rf_dma_tx, DMA_IT_HT);
    __HAL_LINKDMA(&s_rf_hspi, hdmatx, s_rf_dma_tx);
    HAL_NVIC_SetPriority(DMA2_Stream5_IRQn, 1u, 0u);
    HAL_NVIC_EnableIRQ(DMA2_Stream5_IRQn);
    HAL_NVIC_SetPriority(SPI4_IRQn, 1u, 1u);
    HAL_NVIC_EnableIRQ(SPI4_IRQn);
    s_rf_dma_ready = true;
    return true;
}

static bool rf_spi_dma_start_locked(const uint8_t* tx, uint16_t txLen) {
    if ((txLen == 0u) || (txLen > sizeof(s_dma_active_buf))) {
        return false;
    }

    memcpy(s_dma_active_buf, tx, txLen);
    rf_clean_dcache(s_dma_active_buf, txLen);

    SPI_TypeDef* spi = s_rf_hspi.Instance;
    spi->IFCR = SPI_IFCR_EOTC | SPI_IFCR_TXTFC | SPI_IFCR_UDRC | SPI_IFCR_OVRC | SPI_IFCR_TIFREC | SPI_IFCR_MODFC;
    rf_cs_set(false);
    if (HAL_SPI_Transmit_DMA(&s_rf_hspi, s_dma_active_buf, txLen) != HAL_OK) {
        rf_cs_set(true);
        s_dma_busy = false;
        s_diag_dma_start_fail++;
        return false;
    }
    __HAL_DMA_DISABLE_IT(&s_rf_dma_tx, DMA_IT_HT);
    s_dma_busy = true;
    return true;
}

static bool rf_spi_dma_start_pending_from_isr() {
    uint8_t next_buf[sizeof(s_dma_active_buf)];
    uint16_t next_len;

    if (!s_dma_pending || (s_dma_pending_len == 0u) || (s_dma_pending_len > sizeof(next_buf))) {
        s_dma_busy = false;
        s_dma_pending = false;
        s_dma_pending_len = 0u;
        return false;
    }

    next_len = s_dma_pending_len;
    memcpy(next_buf, s_dma_pending_buf, next_len);
    s_dma_pending = false;
    s_dma_pending_len = 0u;
    if (!rf_spi_dma_start_locked(next_buf, next_len)) {
        s_dma_busy = false;
        return false;
    }
    return true;
}

static bool rf_spi_dma_wait_idle_and_drop_pending(uint32_t timeoutMs) {
    const uint32_t start = HAL_GetTick();

    __disable_irq();
    s_dma_pending = false;
    s_dma_pending_len = 0u;
    __enable_irq();

    while (s_dma_busy) {
        if ((HAL_GetTick() - start) >= timeoutMs) {
            return false;
        }
    }
    return true;
}

#if RF_BRIDGE_INPUT_DMA_FASTPATH
static bool rf_spi_dma_enqueue_latest(const uint8_t* tx, uint16_t txLen, uint8_t seq) {
    if ((tx == nullptr) || (txLen == 0u) || (txLen > sizeof(s_dma_pending_buf))) {
        return false;
    }
    if (!rf_dma_init_once()) {
        return false;
    }

    bool started = false;
    __disable_irq();
    if (!s_dma_busy) {
        s_dma_busy = true;
        if (s_dma_pending) {
            s_diag_dma_overwrite++;
        }
        s_dma_pending = false;
        s_dma_pending_len = 0u;
        __enable_irq();
        started = rf_spi_dma_start_locked(tx, txLen);
        if (!started) {
            __disable_irq();
            s_dma_busy = false;
            __enable_irq();
        }
        return started;
    }

    if (s_dma_pending) {
        s_diag_dma_overwrite++;
    }
    memcpy(s_dma_pending_buf, tx, txLen);
    s_dma_pending_len = txLen;
    s_dma_pending = true;
    __enable_irq();
    (void)seq;
    return true;
}
#endif

static bool rf_spi_dma_transmit_blocking(const uint8_t* tx, uint16_t txLen, uint32_t timeoutMs) {
    if ((tx == nullptr) || (txLen == 0u) || (txLen > sizeof(s_dma_active_buf))) {
        return false;
    }
    if (!rf_dma_init_once()) {
        return false;
    }

    __disable_irq();
    if (s_dma_busy) {
        __enable_irq();
        return false;
    }
    s_dma_pending = false;
    s_dma_pending_len = 0u;
    __enable_irq();

    if (!rf_spi_dma_start_locked(tx, txLen)) {
        return false;
    }

    const uint32_t start = HAL_GetTick();
    while (s_dma_busy) {
        if ((HAL_GetTick() - start) >= timeoutMs) {
            return false;
        }
    }
    return true;
}

static void rf_flush_stale_if_irq_high() {
    if (HAL_GPIO_ReadPin(RF_BRIDGE_IRQ_GPIO_PORT, RF_BRIDGE_IRQ_PIN) != GPIO_PIN_SET) {
        return;
    }
    uint8_t txDummy[32];
    uint8_t rxDummy[32];
    memset(txDummy, 0xFF, sizeof(txDummy));
    memset(rxDummy, 0x00, sizeof(rxDummy));
    rf_cs_set(false);
    (void)HAL_SPI_TransmitReceive(&s_rf_hspi, txDummy, rxDummy, sizeof(txDummy), RF_BRIDGE_SPI_TIMEOUT_MS);
    rf_cs_set(true);
    (void)rf_wait_irq_low(RF_BRIDGE_IRQ_LOW_TIMEOUT_MS);
    s_diag_dma_done++;
}

static bool rf_read_event_frame(uint8_t* rx, uint16_t* rxLen, uint8_t diagCmd) {
    static constexpr uint16_t kMinFrameLen = 4u;
    static constexpr uint16_t kPrefetchLen = 4u;
    static constexpr uint16_t kTailBufLen = 64u;
    (void)diagCmd;

    if ((rx == nullptr) || (rxLen == nullptr) || (*rxLen < kMinFrameLen)) {
        if (rxLen != nullptr) {
            *rxLen = 0u;
        }
        return false;
    }

    uint8_t preTx[kPrefetchLen] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
    uint8_t preRx[kPrefetchLen] = {0};
    uint8_t tailTx[kTailBufLen] = {0};
    uint8_t tailRx[kTailBufLen] = {0};

    memset(rx, 0, *rxLen);
    rf_cs_set(false);
    if (HAL_SPI_TransmitReceive(&s_rf_hspi, preTx, preRx, kPrefetchLen, RF_BRIDGE_SPI_TIMEOUT_MS) != HAL_OK) {
        rf_cs_set(true);
        s_diag_rx_io_fail++;
        *rxLen = 0u;
        return false;
    }

    uint16_t start = 0u;
    if ((preRx[0] == 0xA5u) && rf_is_valid_evt(preRx[1])) {
        start = 0u;
    } else if ((preRx[1] == 0xA5u) && rf_is_valid_evt(preRx[2])) {
        start = 1u;
    } else {
        rf_cs_set(true);
        s_diag_rx_invalid++;
        *rxLen = 0u;
        return false;
    }

    const uint8_t payloadLen = preRx[start + 2u];
    const uint16_t total = static_cast<uint16_t>(3u + payloadLen + 1u);
    if ((total < kMinFrameLen) || (total > *rxLen)) {
        rf_cs_set(true);
        s_diag_rx_invalid++;
        *rxLen = 0u;
        return false;
    }

    const uint16_t prefetched = static_cast<uint16_t>(kPrefetchLen - start);
    memcpy(rx, &preRx[start], prefetched);

    const uint16_t remain = static_cast<uint16_t>(total - prefetched);
    if (remain > 0u) {
        if (remain > kTailBufLen) {
            rf_cs_set(true);
            *rxLen = 0u;
            return false;
        }
        for (uint16_t i = 0u; i < remain; ++i) {
            tailTx[i] = 0xFFu;
        }
        uint16_t tailOffset = 0u;
        while (tailOffset < remain) {
            uint16_t chunk = static_cast<uint16_t>(remain - tailOffset);
            if (chunk > RF_BRIDGE_EVENT_RX_CHUNK) {
                chunk = RF_BRIDGE_EVENT_RX_CHUNK;
            }
            if (RF_BRIDGE_EVENT_RX_GAP_MS != 0u) {
                HAL_Delay(RF_BRIDGE_EVENT_RX_GAP_MS);
            }
            if (HAL_SPI_TransmitReceive(&s_rf_hspi,
                                        &tailTx[tailOffset],
                                        &tailRx[tailOffset],
                                        chunk,
                                        RF_BRIDGE_SPI_TIMEOUT_MS) != HAL_OK) {
                rf_cs_set(true);
                s_diag_rx_io_fail++;
                *rxLen = 0u;
                return false;
            }
            tailOffset = static_cast<uint16_t>(tailOffset + chunk);
        }
        memcpy(&rx[prefetched], tailRx, remain);
    }
    rf_cs_set(true);

    if (rf_checksum8(rx, static_cast<uint16_t>(total - 1u)) != rx[total - 1u]) {
        s_diag_rx_invalid++;
        *rxLen = 0u;
        return false;
    }

    *rxLen = total;
    return true;
}

static bool rf_spi_init_once() {
    if (s_rf_spi_ready) {
        return true;
    }

    rf_enable_gpio_clock(RF_BRIDGE_SPI_GPIO_PORT);
    __HAL_RCC_SPI4_CLK_ENABLE();

    RCC_PeriphCLKInitTypeDef clk = {};
    clk.PeriphClockSelection = RCC_PERIPHCLK_SPI45;
#ifdef RCC_SPI45CLKSOURCE_D2PCLK1
    clk.Spi45ClockSelection = RCC_SPI45CLKSOURCE_D2PCLK1;
#else
    clk.Spi45ClockSelection = RCC_SPI45CLKSOURCE_PCLK2;
#endif
    if (HAL_RCCEx_PeriphCLKConfig(&clk) != HAL_OK) {
        return false;
    }

    GPIO_InitTypeDef init = {};
    init.Mode = GPIO_MODE_AF_PP;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    init.Alternate = RF_BRIDGE_SPI_AF;
    init.Pin = RF_BRIDGE_SPI_MISO_PIN | RF_BRIDGE_SPI_SCK_PIN | RF_BRIDGE_SPI_MOSI_PIN;
    HAL_GPIO_Init(RF_BRIDGE_SPI_GPIO_PORT, &init);

    init.Mode = GPIO_MODE_OUTPUT_PP;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    init.Alternate = 0u;
    init.Pin = RF_BRIDGE_SPI_NSS_PIN;
    HAL_GPIO_Init(RF_BRIDGE_SPI_GPIO_PORT, &init);
    rf_cs_set(true);

    rf_enable_gpio_clock(RF_BRIDGE_IRQ_GPIO_PORT);
    init.Mode = GPIO_MODE_IT_RISING;
    init.Pull = GPIO_PULLDOWN;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    init.Alternate = 0u;
    init.Pin = RF_BRIDGE_IRQ_PIN;
    HAL_GPIO_Init(RF_BRIDGE_IRQ_GPIO_PORT, &init);
    __HAL_GPIO_EXTI_CLEAR_IT(RF_BRIDGE_IRQ_PIN);
    HAL_NVIC_SetPriority(RF_BRIDGE_IRQ_EXTI_IRQn, RF_BRIDGE_IRQ_EXTI_IRQn_PRIO, 0u);
    HAL_NVIC_EnableIRQ(RF_BRIDGE_IRQ_EXTI_IRQn);

    __HAL_RCC_SPI4_FORCE_RESET();
    __HAL_RCC_SPI4_RELEASE_RESET();

    memset(&s_rf_hspi, 0, sizeof(s_rf_hspi));
    s_rf_hspi.Instance = RF_BRIDGE_SPI_INSTANCE;
    s_rf_hspi.Init.Mode = SPI_MODE_MASTER;
    s_rf_hspi.Init.Direction = SPI_DIRECTION_2LINES;
    s_rf_hspi.Init.DataSize = SPI_DATASIZE_8BIT;
    s_rf_hspi.Init.CLKPolarity = SPI_POLARITY_LOW;
    s_rf_hspi.Init.CLKPhase = SPI_PHASE_1EDGE;
    s_rf_hspi.Init.NSS = SPI_NSS_SOFT;
    s_rf_hspi.Init.BaudRatePrescaler = RF_BRIDGE_SPI_BAUD_PRESCALER;
    s_rf_hspi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    s_rf_hspi.Init.TIMode = SPI_TIMODE_DISABLE;
    s_rf_hspi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    s_rf_hspi.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    s_rf_hspi.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
    s_rf_hspi.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
    s_rf_hspi.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    s_rf_hspi.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    s_rf_hspi.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
    s_rf_hspi.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    s_rf_hspi.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
    s_rf_hspi.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
    s_rf_hspi.Init.IOSwap = SPI_IO_SWAP_DISABLE;

    if (HAL_SPI_Init(&s_rf_hspi) != HAL_OK) {
        return false;
    }

    if (!rf_dma_init_once()) {
        return false;
    }

    s_rf_spi_ready = true;
    return true;
}
} // namespace

extern "C" void RFBridgePort_DMA_IRQHandler(void) {
    s_diag_dma_irq++;
    HAL_DMA_IRQHandler(&s_rf_dma_tx);
}

extern "C" void RFBridgePort_SPI_IRQHandler(void) {
    s_diag_spi_irq++;
    HAL_SPI_IRQHandler(&s_rf_hspi);
}

extern "C" void RFBridgePort_IRQ_IRQHandler(void) {
    s_diag_exti_irq++;
    if (s_irq_event_pending < RF_BRIDGE_EVENT_DRAIN_LIMIT) {
        s_irq_event_pending++;
    }
}

extern "C" void RFBridgePort_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {
    if ((hspi == nullptr) || (hspi->Instance != RF_BRIDGE_SPI_INSTANCE)) {
        return;
    }

    rf_cs_set(true);
    s_diag_dma_done++;

    if (!rf_spi_dma_start_pending_from_isr()) {
        __disable_irq();
        s_dma_busy = false;
        __enable_irq();
    }
}

extern "C" void RFBridgePort_SPI_ErrorCallback(SPI_HandleTypeDef *hspi) {
    if ((hspi == nullptr) || (hspi->Instance != RF_BRIDGE_SPI_INSTANCE)) {
        return;
    }
    rf_cs_set(true);
    s_diag_spi_err++;
    __disable_irq();
    s_dma_busy = false;
    s_dma_pending = false;
    s_dma_pending_len = 0u;
    __enable_irq();
}

bool RFBridgePort_IsReady(void) {
    return s_rf_spi_ready;
}

bool RFBridgePort_HasPendingEvent(void) {
    if (!s_rf_spi_ready) {
        return false;
    }
    return rf_has_pending_event_signal();
}

bool RFBridgePort_ReadEvent(uint8_t* rx, uint16_t* rxLen) {
    if ((rx == nullptr) || (rxLen == nullptr)) {
        return false;
    }

    if (!s_rf_spi_ready) {
        *rxLen = 0u;
        return false;
    }

    if (!rf_has_pending_event_signal()) {
        *rxLen = 0u;
        return true;
    }

    if (!rf_spi_dma_wait_idle_and_drop_pending(RF_BRIDGE_SPI_TIMEOUT_MS)) {
        *rxLen = 0u;
        return false;
    }

    rf_consume_irq_pending_marker();
    const bool ok = rf_read_event_frame(rx, rxLen, 0x00u);
    if (ok && (*rxLen > 0u)) {
        (void)rf_wait_irq_low(RF_BRIDGE_IRQ_LOW_TIMEOUT_MS);
    }
    return ok;
}

bool RFBridgePort_SendInputLatest(const uint8_t* tx, uint16_t txLen) {
    if ((tx == nullptr) || (txLen == 0u)) {
        return false;
    }

    if (!rf_spi_init_once()) {
        s_diag_spi_init_fail++;
        return false;
    }

    const uint8_t cmd = (txLen >= 2u) ? tx[1] : 0u;
    if ((txLen < 2u) || (tx[0] != 0xA5u) || (cmd != 0x06u)) {
        return false;
    }

    const uint8_t input_seq = (txLen >= 4u) ? tx[3] : 0u;

    bool tx_ok = false;
#if RF_BRIDGE_INPUT_DMA_FASTPATH
    tx_ok = rf_spi_dma_enqueue_latest(tx, txLen, input_seq);
#else
    if (!rf_spi_dma_wait_idle_and_drop_pending(RF_BRIDGE_SPI_TIMEOUT_MS)) {
        return false;
    }
    rf_flush_stale_if_irq_high();
    if (s_dma_busy) {
        return false;
    }
    rf_cs_set(false);
    const HAL_StatusTypeDef tx_st = HAL_SPI_Transmit(&s_rf_hspi,
                                                     const_cast<uint8_t*>(tx),
                                                     txLen,
                                                     RF_BRIDGE_SPI_TIMEOUT_MS);
    rf_cs_set(true);
    tx_ok = (tx_st == HAL_OK);
#endif

    if (!tx_ok) {
        s_diag_tx_fail++;
        rf_note_transfer(true, false, cmd, txLen, input_seq);
        return false;
    }

#if !RF_BRIDGE_INPUT_DMA_FASTPATH
    s_diag_input_blocking_done++;
#endif
    rf_note_transfer(true, true, cmd, txLen, input_seq);
    return true;
}

bool RFBridgePort_ControlTransferWithTimeout(const uint8_t* tx,
                                             uint16_t txLen,
                                             uint8_t* rx,
                                             uint16_t* rxLen,
                                             uint32_t ackTimeoutMs) {
    if ((tx == nullptr) || (txLen == 0u)) {
        if (rxLen != nullptr) *rxLen = 0u;
        return false;
    }

    if ((rx == nullptr) || (rxLen == nullptr) || (*rxLen < 4u)) {
        if (rxLen != nullptr) *rxLen = 0u;
        return false;
    }

    if (!rf_spi_init_once()) {
        s_diag_spi_init_fail++;
        *rxLen = 0u;
        return false;
    }

    const uint8_t cmd = (txLen >= 2u) ? tx[1] : 0u;
    uint8_t controlTxBuf[RF_BRIDGE_MIN_CONTROL_TX_BYTES] = {0};
    const uint8_t* busTx = tx;
    uint16_t busTxLen = txLen;

    if (txLen < RF_BRIDGE_MIN_CONTROL_TX_BYTES) {
        memset(controlTxBuf, 0xFF, sizeof(controlTxBuf));
        memcpy(controlTxBuf, tx, txLen);
        busTx = controlTxBuf;
        busTxLen = RF_BRIDGE_MIN_CONTROL_TX_BYTES;
    }

    if (!rf_spi_dma_wait_idle_and_drop_pending(RF_BRIDGE_SPI_TIMEOUT_MS)) {
        *rxLen = 0u;
        return false;
    }
    rf_flush_stale_if_irq_high();

    if (s_dma_busy) {
        *rxLen = 0u;
        return false;
    }

    bool tx_ok = rf_spi_dma_transmit_blocking(busTx, busTxLen, RF_BRIDGE_SPI_TIMEOUT_MS);
    if (!tx_ok) {
        s_diag_tx_fail++;
        rf_note_transfer(false, false, cmd, busTxLen, 0u);
        *rxLen = 0u;
        return false;
    }

    rf_note_transfer(false, true, cmd, busTxLen, 0u);

    const bool irq_ready = rf_wait_irq_high(ackTimeoutMs);
    if (!irq_ready) {
        s_diag_irq_timeout++;
        *rxLen = 0u;
        return false;
    }

    if (!rf_read_event_frame(rx, rxLen, cmd)) {
        *rxLen = 0u;
        return false;
    }
    rf_consume_irq_pending_marker();
    (void)rf_wait_irq_low(RF_BRIDGE_IRQ_LOW_TIMEOUT_MS);
    return true;
}

bool RFBridgePort_ControlTransfer(const uint8_t* tx, uint16_t txLen, uint8_t* rx, uint16_t* rxLen) {
    return RFBridgePort_ControlTransferWithTimeout(tx, txLen, rx, rxLen, 20u);
}

bool RFBridgePort_Transfer(const uint8_t* tx, uint16_t txLen, uint8_t* rx, uint16_t* rxLen) {
    const uint8_t cmd = (txLen >= 2u) ? tx[1] : 0u;

    if ((tx != nullptr) && (txLen >= 2u) && (tx[0] == 0xA5u) && (cmd == 0x06u) &&
        (rx == nullptr) && (rxLen == nullptr)) {
        return RFBridgePort_SendInputLatest(tx, txLen);
    }

    if ((rx == nullptr) || (rxLen == nullptr)) {
        return false;
    }

    return RFBridgePort_ControlTransfer(tx, txLen, rx, rxLen);
}
