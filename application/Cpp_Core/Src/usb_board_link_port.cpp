#include "usb_board_link_port.hpp"

#include <string.h>

#include "board_cfg.h"
#include "rf_bridge_port.hpp"
#include "stm32h7xx_hal.h"
#include "usb_board_link_protocol.h"

#ifndef CH585_SPI_INSTANCE
#define CH585_SPI_INSTANCE RF_BRIDGE_SPI_INSTANCE
#define CH585_SPI_GPIO_PORT RF_BRIDGE_SPI_GPIO_PORT
#define CH585_SPI_MISO_PIN RF_BRIDGE_SPI_MISO_PIN
#define CH585_SPI_NSS_PIN RF_BRIDGE_SPI_NSS_PIN
#define CH585_SPI_SCK_PIN RF_BRIDGE_SPI_SCK_PIN
#define CH585_SPI_MOSI_PIN RF_BRIDGE_SPI_MOSI_PIN
#define CH585_SPI_AF RF_BRIDGE_SPI_AF
#define CH585_IRQ_GPIO_PORT RF_BRIDGE_IRQ_GPIO_PORT
#define CH585_IRQ_PIN RF_BRIDGE_IRQ_PIN
#endif

namespace {

static constexpr uint32_t kSpiTimeoutMs = 5u;
static constexpr uint32_t kReadGapMs = 1u;
static constexpr uint32_t kEventReleaseTimeoutMs = 2u;
/*
 * CH585 may switch its SPI0 DMA direction only while NSS is low but before
 * clocks start.  Fifty microseconds is still only 5% of the USB-mode 1-kHz
 * period and gives the 60-MHz CH585 ample time to observe NSS and restore RX
 * if a command write wins the arbitration race.
 */
/*
 * PA12 rising-edge handling on CH585 is the RX rearm boundary.  A concurrent
 * USBHS 512-byte ISR can delay that edge handler beyond 50 us, so every new
 * STM32 write holds NSS low without clocks for a conservative 200 us.  This
 * affects only the mutually-exclusive USB role (fixed 1 kHz), never RF 8K.
 */
static constexpr uint32_t kOwnershipGuardUs = 200u;
static SPI_HandleTypeDef s_hspi;
static bool s_ready;
static bool s_waitingEventRelease;

static void enableGpioClock(GPIO_TypeDef *port)
{
    if (port == GPIOA) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    } else if (port == GPIOB) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    } else if (port == GPIOC) {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    } else if (port == GPIOD) {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    } else if (port == GPIOE) {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    } else if (port == GPIOF) {
        __HAL_RCC_GPIOF_CLK_ENABLE();
    } else if (port == GPIOG) {
        __HAL_RCC_GPIOG_CLK_ENABLE();
    } else if (port == GPIOH) {
        __HAL_RCC_GPIOH_CLK_ENABLE();
    } else if (port == GPIOI) {
        __HAL_RCC_GPIOI_CLK_ENABLE();
    }
}

static void chipSelect(bool high)
{
    HAL_GPIO_WritePin(CH585_SPI_GPIO_PORT,
                      CH585_SPI_NSS_PIN,
                      high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void ownershipGuardDelay()
{
    /*
     * Give the CH585 polling port enough time to observe NSS low before the
     * first clock.  The second W_INT sample then resolves a concurrent event
     * claim without adding a millisecond-scale delay to the 1 kHz USB path.
     */
    const uint32_t loops =
        ((SystemCoreClock / 1000000u) * kOwnershipGuardUs) / 6u + 1u;
    for (volatile uint32_t index = 0u; index < loops; ++index) {
        __NOP();
    }
    __DSB();
}

static bool eventLineIsHigh()
{
    return HAL_GPIO_ReadPin(CH585_IRQ_GPIO_PORT, CH585_IRQ_PIN) ==
           GPIO_PIN_SET;
}

static bool refreshEventRelease()
{
    if (s_waitingEventRelease && eventLineIsHigh()) {
        s_waitingEventRelease = false;
    }
    return !s_waitingEventRelease;
}

static bool waitEventHigh(uint32_t timeoutMs)
{
    const uint32_t started = HAL_GetTick();
    do {
        if (eventLineIsHigh()) {
            s_waitingEventRelease = false;
            return true;
        }
    } while ((HAL_GetTick() - started) < timeoutMs);
    return false;
}

static bool waitEventLow(uint32_t timeoutMs)
{
    if (!refreshEventRelease()) {
        return false;
    }
    const uint32_t started = HAL_GetTick();
    do {
        if (HAL_GPIO_ReadPin(CH585_IRQ_GPIO_PORT, CH585_IRQ_PIN) == GPIO_PIN_RESET) {
            return true;
        }
    } while ((HAL_GetTick() - started) < timeoutMs);
    return false;
}

static bool readFrame(uint8_t *response,
                      uint8_t responseCapacity,
                      uint8_t *responseLength)
{
    uint8_t byte = 0xFFu;
    uint8_t raw[USB_BOARD_LINK_MAX_FRAME_BYTES + 8u] = {};
    uint8_t rawLength = 0u;
    uint8_t start = 0u;
    uint8_t total = 0u;
    bool found = false;

    if ((response == nullptr) || (responseLength == nullptr) ||
        (responseCapacity < 4u) || !s_ready) {
        return false;
    }
    *responseLength = 0u;

    chipSelect(false);
    while (rawLength < sizeof(raw)) {
        if (HAL_SPI_TransmitReceive(&s_hspi,
                                    &byte,
                                    &raw[rawLength],
                                    1u,
                                    kSpiTimeoutMs) != HAL_OK) {
            chipSelect(true);
            s_waitingEventRelease = true;
            (void)waitEventHigh(kEventReleaseTimeoutMs);
            return false;
        }
        ++rawLength;

        if (!found && (rawLength >= USB_BOARD_LINK_HEADER_BYTES)) {
            for (uint8_t index = 0u; (index + 2u) < rawLength; ++index) {
                if (raw[index] != USB_BOARD_LINK_SYNC) {
                    continue;
                }
                const uint8_t payloadLength = raw[index + 2u];
                const uint8_t candidateLength =
                    (uint8_t)(USB_BOARD_LINK_HEADER_BYTES + payloadLength +
                              USB_BOARD_LINK_CHECKSUM_BYTES);
                if ((payloadLength > USB_BOARD_LINK_MAX_PAYLOAD_BYTES) ||
                    (candidateLength > responseCapacity) ||
                    ((uint16_t)index + candidateLength > sizeof(raw))) {
                    continue;
                }
                start = index;
                total = candidateLength;
                found = true;
                break;
            }
        }
        if (found && (rawLength >= (uint8_t)(start + total))) {
            break;
        }
    }
    chipSelect(true);
    s_waitingEventRelease = true;
    (void)waitEventHigh(kEventReleaseTimeoutMs);

    if (!found || (rawLength < (uint8_t)(start + total)) ||
        (usb_board_link_checksum(&raw[start], (uint16_t)(total - 1u)) !=
         raw[start + total - 1u])) {
        return false;
    }
    memcpy(response, &raw[start], total);
    *responseLength = total;
    return true;
}

} // namespace

bool USBBoardLinkPort_Init()
{
    if (s_ready) {
        return true;
    }

    RFBridgePort_Shutdown();
    enableGpioClock(CH585_SPI_GPIO_PORT);
    enableGpioClock(CH585_IRQ_GPIO_PORT);
    __HAL_RCC_SPI4_CLK_ENABLE();

    RCC_PeriphCLKInitTypeDef clock = {};
    clock.PeriphClockSelection = RCC_PERIPHCLK_SPI45;
#ifdef RCC_SPI45CLKSOURCE_D2PCLK1
    clock.Spi45ClockSelection = RCC_SPI45CLKSOURCE_D2PCLK1;
#else
    clock.Spi45ClockSelection = RCC_SPI45CLKSOURCE_PCLK2;
#endif
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) {
        return false;
    }

    GPIO_InitTypeDef gpio = {};
    gpio.Pin = CH585_SPI_MISO_PIN | CH585_SPI_SCK_PIN | CH585_SPI_MOSI_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = CH585_SPI_AF;
    HAL_GPIO_Init(CH585_SPI_GPIO_PORT, &gpio);

    gpio.Pin = CH585_SPI_NSS_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Alternate = 0u;
    HAL_GPIO_Init(CH585_SPI_GPIO_PORT, &gpio);
    chipSelect(true);

    gpio.Pin = CH585_IRQ_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(CH585_IRQ_GPIO_PORT, &gpio);

    __HAL_RCC_SPI4_FORCE_RESET();
    __HAL_RCC_SPI4_RELEASE_RESET();
    memset(&s_hspi, 0, sizeof(s_hspi));
    s_hspi.Instance = CH585_SPI_INSTANCE;
    s_hspi.Init.Mode = SPI_MODE_MASTER;
    s_hspi.Init.Direction = SPI_DIRECTION_2LINES;
    s_hspi.Init.DataSize = SPI_DATASIZE_8BIT;
    s_hspi.Init.CLKPolarity = SPI_POLARITY_LOW;
    s_hspi.Init.CLKPhase = SPI_PHASE_1EDGE;
    s_hspi.Init.NSS = SPI_NSS_SOFT;
    s_hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    s_hspi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    s_hspi.Init.TIMode = SPI_TIMODE_DISABLE;
    s_hspi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    s_hspi.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    s_hspi.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
    s_hspi.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
    s_hspi.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    s_hspi.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    s_hspi.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
    s_hspi.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    s_hspi.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
    s_hspi.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
    s_hspi.Init.IOSwap = SPI_IO_SWAP_DISABLE;

    if (HAL_SPI_Init(&s_hspi) != HAL_OK) {
        return false;
    }
    s_waitingEventRelease = false;
    s_ready = true;
    return true;
}

void USBBoardLinkPort_Shutdown()
{
    if (!s_ready) {
        return;
    }
    chipSelect(true);
    (void)HAL_SPI_DeInit(&s_hspi);
    s_waitingEventRelease = false;
    s_ready = false;
}

bool USBBoardLinkPort_Send(const uint8_t *frame, uint8_t frameLength)
{
    if ((frame == nullptr) || (frameLength < 4u) ||
        (frameLength > USB_BOARD_LINK_MAX_FRAME_BYTES) ||
        (!USBBoardLinkPort_Init())) {
        return false;
    }
    /*
     * W_INT low reserves the next NSS assertion for an event read.  Refuse a
     * write at the final port boundary as well as at the protocol layer.
     */
    if (!refreshEventRelease() || !eventLineIsHigh()) {
        return false;
    }
    chipSelect(false);
    ownershipGuardDelay();
    if (!eventLineIsHigh()) {
        chipSelect(true);
        return false;
    }
    const HAL_StatusTypeDef result =
        HAL_SPI_Transmit(&s_hspi,
                         const_cast<uint8_t *>(frame),
                         frameLength,
                         kSpiTimeoutMs);
    chipSelect(true);
    return result == HAL_OK;
}

bool USBBoardLinkPort_Transact(const uint8_t *frame,
                               uint8_t frameLength,
                               uint8_t *response,
                               uint8_t responseCapacity,
                               uint8_t *responseLength,
                               uint32_t timeoutMs)
{
    if (responseLength != nullptr) {
        *responseLength = 0u;
    }
    if (!USBBoardLinkPort_Send(frame, frameLength) ||
        !waitEventLow(timeoutMs)) {
        return false;
    }
    HAL_Delay(kReadGapMs);
    return readFrame(response, responseCapacity, responseLength);
}

bool USBBoardLinkPort_HasEvent()
{
    return s_ready && refreshEventRelease() && !eventLineIsHigh();
}

bool USBBoardLinkPort_ReadEvent(uint8_t *response,
                                uint8_t responseCapacity,
                                uint8_t *responseLength)
{
    if (!USBBoardLinkPort_HasEvent()) {
        return false;
    }
    return readFrame(response, responseCapacity, responseLength);
}
