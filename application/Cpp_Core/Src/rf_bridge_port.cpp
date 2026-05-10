#include "rf_bridge_port.hpp"

#include <string.h>

#include "board_cfg.h"
#include "system_logger.h"
#include "stm32h7xx_hal.h"

namespace {
static SPI_HandleTypeDef s_rf_hspi = {};
static bool s_rf_spi_ready = false;
static uint32_t s_diag_spi_init_fail = 0u;
static uint32_t s_diag_tx_fail = 0u;
static uint32_t s_diag_irq_timeout = 0u;
static uint32_t s_diag_rx_invalid = 0u;
static uint32_t s_diag_rx_io_fail = 0u;

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

static bool rf_wait_irq_rise(uint32_t timeoutMs) {
    const uint32_t start = HAL_GetTick();
    bool seen_low = false;
    while ((HAL_GetTick() - start) < timeoutMs) {
        GPIO_PinState s = HAL_GPIO_ReadPin(RF_BRIDGE_IRQ_GPIO_PORT, RF_BRIDGE_IRQ_PIN);
        if (s == GPIO_PIN_RESET) {
            seen_low = true;
            continue;
        }
        if (seen_low && s == GPIO_PIN_SET) {
            return true;
        }
    }
    return false;
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
    // APP_DBG("[RF_BRIDGE] stale flush before tx");
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
        // APP_DBG("[RF_BRIDGE] spi4 periph clock config failed");
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
    init.Mode = GPIO_MODE_INPUT;
    init.Pull = GPIO_PULLDOWN;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    init.Alternate = 0u;
    init.Pin = RF_BRIDGE_IRQ_PIN;
    HAL_GPIO_Init(RF_BRIDGE_IRQ_GPIO_PORT, &init);

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
    s_rf_hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
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
        // APP_DBG("[RF_BRIDGE] spi4 init failed");
        return false;
    }

    s_rf_spi_ready = true;
    // APP_DBG("[RF_BRIDGE] spi4 init ok (SCK=%u MISO=%u MOSI=%u NSS=%u)",
    //         (unsigned int)RF_BRIDGE_SPI_SCK_PIN,
    //         (unsigned int)RF_BRIDGE_SPI_MISO_PIN,
    //         (unsigned int)RF_BRIDGE_SPI_MOSI_PIN,
    //         (unsigned int)RF_BRIDGE_SPI_NSS_PIN);
    return true;
}
} // namespace

bool RFBridgePort_Transfer(const uint8_t* tx, uint16_t txLen, uint8_t* rx, uint16_t* rxLen) {
    if ((tx == nullptr) || (txLen == 0u)) {
        // APP_DBG("[RF_BRIDGE] transfer invalid tx args");
        if (rxLen != nullptr) *rxLen = 0u;
        return false;
    }

    if (!rf_spi_init_once()) {
        s_diag_spi_init_fail++;
        APP_DBG("[RF_BRIDGE][DIAG] spi_init_fail=%lu", s_diag_spi_init_fail);
        if (rxLen != nullptr) *rxLen = 0u;
        return false;
    }

    // if (txLen >= 2u && tx[0] == 0xA5u && tx[1] != 0x06u) {
    //     APP_DBG("[RF_BRIDGE] tx cmd=0x%02X len=%u", (unsigned int)tx[1], (unsigned int)txLen);
    // }

    rf_flush_stale_if_irq_high();

    rf_cs_set(false);
    const HAL_StatusTypeDef tx_st = HAL_SPI_Transmit(&s_rf_hspi, const_cast<uint8_t*>(tx), txLen, RF_BRIDGE_SPI_TIMEOUT_MS);
    rf_cs_set(true);
    if (tx_st != HAL_OK) {
        s_diag_tx_fail++;
        APP_DBG("[RF_BRIDGE][DIAG] tx_fail=%lu status=%u cmd=0x%02X len=%u",
                s_diag_tx_fail,
                (unsigned int)tx_st,
                (unsigned int)(txLen >= 2u ? tx[1] : 0u),
                (unsigned int)txLen);
        if (rxLen != nullptr) *rxLen = 0u;
        return false;
    }
    HAL_Delay(1u);

    const bool should_try_readback = (txLen >= 2u) &&
                                     ((tx[1] == 0x01u) || (tx[1] == 0x02u) || (tx[1] == 0x03u) ||
                                      (tx[1] == 0x04u) || (tx[1] == 0x05u));
    if (should_try_readback && (rx != nullptr) && (rxLen != nullptr) && (*rxLen != 0u)) {
        static constexpr uint16_t kMinFrameLen = 4u;
        static constexpr uint16_t kProbeLen = 40u;
        uint8_t txDummy[kProbeLen] = {0};
        uint8_t probe[kProbeLen] = {0};
        for (uint16_t i = 0u; i < kProbeLen; ++i) txDummy[i] = 0xFFu;

        if (*rxLen < kMinFrameLen) {
            // APP_DBG("[RF_BRIDGE] rx cap too small: %u", (unsigned int)(*rxLen));
            *rxLen = 0u;
            return false;
        }
        memset(rx, 0, *rxLen);

        const bool irq_before_tx = (HAL_GPIO_ReadPin(RF_BRIDGE_IRQ_GPIO_PORT, RF_BRIDGE_IRQ_PIN) == GPIO_PIN_SET);
        bool irq_ready = false;
        if (irq_before_tx) {
            irq_ready = rf_wait_irq_rise(20u);
        } else {
            irq_ready = rf_wait_irq_high(20u);
        }
        if (!irq_ready) {
            s_diag_irq_timeout++;
            APP_DBG("[RF_BRIDGE][DIAG] irq_timeout=%lu cmd=0x%02X irq_before=%u irq_now=%u",
                    s_diag_irq_timeout,
                    (unsigned int)tx[1],
                    (unsigned int)(irq_before_tx ? 1u : 0u),
                    (unsigned int)(HAL_GPIO_ReadPin(RF_BRIDGE_IRQ_GPIO_PORT, RF_BRIDGE_IRQ_PIN) == GPIO_PIN_SET ? 1u : 0u));
            *rxLen = 0u;
            return false;
        }

        rf_cs_set(false);
        if (HAL_SPI_TransmitReceive(&s_rf_hspi, txDummy, probe, kProbeLen, RF_BRIDGE_SPI_TIMEOUT_MS) != HAL_OK) {
            rf_cs_set(true);
            s_diag_rx_io_fail++;
            APP_DBG("[RF_BRIDGE][DIAG] rx_io_fail=%lu cmd=0x%02X", s_diag_rx_io_fail, (unsigned int)tx[1]);
            *rxLen = 0u;
            return false;
        }
        rf_cs_set(true);

        const uint16_t cap = *rxLen;
        bool found = false;
        for (uint16_t i = 0u; i + kMinFrameLen <= kProbeLen; ++i) {
            const uint8_t* f = &probe[i];
            const uint8_t payloadLen = f[2];
            const uint16_t total = static_cast<uint16_t>(3u + payloadLen + 1u);
            if (f[0] != 0xA5u || !rf_is_valid_evt(f[1])) continue;
            if (total < kMinFrameLen || total > cap || (uint16_t)(i + total) > kProbeLen) continue;
            if (rf_checksum8(f, static_cast<uint16_t>(total - 1u)) != f[total - 1u]) continue;
            memcpy(rx, f, total);
            *rxLen = total;
            found = true;
            break;
        }

        if (!found) {
            s_diag_rx_invalid++;
            APP_DBG("[RF_BRIDGE][DIAG] rx_invalid=%lu cmd=0x%02X head=%02X %02X %02X %02X",
                    s_diag_rx_invalid,
                    (unsigned int)tx[1],
                    (unsigned int)probe[0],
                    (unsigned int)probe[1],
                    (unsigned int)probe[2],
                    (unsigned int)probe[3]);
            return false;
        }

        // APP_DBG("[RF_BRIDGE] rx evt=0x%02X payload=%u total=%u",
        //         (unsigned int)rx[1], (unsigned int)rx[2], (unsigned int)(*rxLen));
    } else if (rxLen != nullptr) {
        *rxLen = 0u;
    }

    return true;
}
