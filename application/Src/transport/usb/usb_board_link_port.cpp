#include "usb_board_link_port.hpp"
#include "usb_role_ready_wait.hpp"
#include "ch585_handshake.h"

#include <string.h>

#include "board_cfg.h"
#include "rf_bridge_port.hpp"
#include "stm32h7xx_hal.h"
#include "usb_board_link_protocol.h"
#include "ch585_iap_protocol.h"
#include "usb_board_link_codec.h"
#include "webhid_fast_link.h"
#include "webhid_benchmark.h"
#include "states/webconfig_state.hpp"
#if HBOX_SECURE_BOOT_REQUIRED == 0
extern "C" webhid_benchmark_snapshot_t g_webhid_benchmark;
#endif

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
/* This is a deadline, not pacing: return as soon as RX-ready is observed.
 * DMA direction changes during PREPARE must finish before another command. */
static constexpr uint32_t kEventReleaseTimeoutMs = 20u;
/*
 * PA12 rising-edge handling on CH585 is the RX rearm boundary.  A concurrent
 * USBHS ISR can delay that edge handler, so bootstrap, CAPS, and IAP retain
 * the proven 200-us ownership guard.  After CAPS explicitly advertises the
 * fast-data-plane contract, Application input frames use a fixed 20-us guard;
 * CH585 still performs its second NSS sample before the DMA direction switch.
 */
static constexpr uint32_t kOwnershipGuardSlowUs = 200u;
static constexpr uint32_t kOwnershipGuardFastUs = 20u;
static constexpr uint32_t kExpectedSpiClockHz = 120000000u;
static constexpr uint32_t kFastSpiPrescaler = 8u;
static constexpr uint32_t kInputFrameBytes =
    USB_BOARD_INPUT_V1_BYTES + USB_BOARD_LINK_HEADER_BYTES +
    USB_BOARD_LINK_CHECKSUM_BYTES;
static constexpr uint32_t kInputFrameWireUs =
    ((kInputFrameBytes * 8u * kFastSpiPrescaler * 1000000u) +
     kExpectedSpiClockHz - 1u) / kExpectedSpiClockHz;
static_assert(kOwnershipGuardFastUs + kInputFrameWireUs <= 50u,
              "fast BoardLink input frame exceeds the 50-us NSS budget");
static_assert(kOwnershipGuardFastUs + kInputFrameWireUs < 125u,
              "fast BoardLink input frame cannot fit an 8-kHz period");
static SPI_HandleTypeDef s_hspi;
static bool s_ready;
static bool s_roleRequestSent;
static bool s_fastApplication;
static bool s_fastWebHid;
static bool s_fastIap;
static DMA_HandleTypeDef s_hsTxDma, s_hsRxDma;
__attribute__((section(".DMA_Section"), aligned(32))) static uint8_t s_hsTx[4096];
__attribute__((section(".DMA_Section"), aligned(32))) static uint8_t s_hsRx[4096];
static uint8_t s_hsRead[WHF_BLOCK_BYTES];

static bool hsDma(const uint8_t *tx, uint8_t *rx, uint16_t size)
{
    const uint32_t cyclesStarted=DWT->CYCCNT;
    if((!s_fastWebHid && !s_fastIap) || size==0u || size>sizeof(s_hsTx)) return false;
    if(tx) memcpy(s_hsTx,tx,size); else memset(s_hsTx,0xFF,size);
    const int32_t cacheSize=static_cast<int32_t>((size+31u)&~31u);
    SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t *>(s_hsTx),cacheSize);
    SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t *>(s_hsRx),cacheSize);
    __DSB();
    if(HAL_SPI_TransmitReceive_DMA(&s_hspi,s_hsTx,s_hsRx,size)!=HAL_OK) return false;
    const uint32_t start=HAL_GetTick();
    /* Dedicated streams are polled here; handlers execute HAL completion
     * bookkeeping without taking over the RF-owned SPI4 interrupt vector. */
    /* RX completion can enable SPI EOT after the TX handler has already
     * polled this iteration. EOT makes SPI READY while TX's completion flag
     * is still pending, leaving that DMA handle BUSY/locked. Retire both
     * streams before allowing the next header/body transaction to start. */
    while(HAL_SPI_GetState(&s_hspi)!=HAL_SPI_STATE_READY ||
          HAL_DMA_GetState(&s_hsTxDma)!=HAL_DMA_STATE_READY ||
          HAL_DMA_GetState(&s_hsRxDma)!=HAL_DMA_STATE_READY) {
        HAL_DMA_IRQHandler(&s_hsTxDma); HAL_DMA_IRQHandler(&s_hsRxDma);
        HAL_SPI_IRQHandler(&s_hspi);
        if(HAL_GetTick()-start>=10u) { (void)HAL_SPI_Abort(&s_hspi); return false; }
    }
    __DSB();
    SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t *>(s_hsRx),cacheSize);
    if(rx) memcpy(rx,s_hsRx,size);
    const bool ok=HAL_SPI_GetError(&s_hspi)==HAL_SPI_ERROR_NONE;
#if HBOX_SECURE_BOOT_REQUIRED == 0
    if(g_webhid_benchmark.magic==WEBHID_BENCHMARK_MAGIC && g_webhid_benchmark.state==1u) {
        g_webhid_benchmark.dma_us+=(DWT->CYCCNT-cyclesStarted)/(SystemCoreClock/1000000u);
        ++g_webhid_benchmark.dma_blocks; if(!ok) ++g_webhid_benchmark.dma_errors;
    }
#else
    (void)cyclesStarted;
#endif
    return ok;
}

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
     * claim before the first SPI clock.
     */
    const uint32_t guardUs = (s_fastApplication || s_fastWebHid)
        ? kOwnershipGuardFastUs
        : kOwnershipGuardSlowUs;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    const uint32_t cyclesPerUs = SystemCoreClock / 1000000u;
    const uint32_t waitCycles = cyclesPerUs * guardUs;
    const uint32_t startedAt = DWT->CYCCNT;
    while (static_cast<uint32_t>(DWT->CYCCNT - startedAt) < waitCycles) {
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
    return Ch585Handshake_Ready(CH585_LINE_USB);
}

static bool waitEventHigh(uint32_t timeoutMs)
{
    const uint32_t started = HAL_GetTick();
    do {
        if (refreshEventRelease()) return true;
        if (Ch585Handshake_Faulted(CH585_LINE_USB)) return false;
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

    Ch585ReadGuard read(CH585_LINE_USB);
    if (!read) return false;
    chipSelect(false);
    while (rawLength < sizeof(raw)) {
        if (HAL_SPI_TransmitReceive(&s_hspi,
                                    &byte,
                                    &raw[rawLength],
                                    1u,
                                    kSpiTimeoutMs) != HAL_OK) {
            chipSelect(true);
            read.finish();
            WebConfig_RecordStartupFrame(raw, rawLength, 1u);
            return false;
        }
        ++rawLength;

        if(s_fastWebHid && !found && rawLength>=4u && raw[rawLength-4u]==WHF_SYNC) {
            const uint8_t at=rawLength-4u;
            const uint16_t blockSize=whf_u16(raw+at+2u);
            bool ok=raw[at+1u]==2u && blockSize>=WHF_HEADER_BYTES && blockSize<=WHF_BLOCK_BYTES;
            if(ok) {
                memcpy(s_hsRead,raw+at,4u);
                ok=hsDma(nullptr,s_hsRead+4u,static_cast<uint16_t>(blockSize-4u));
            }
            chipSelect(true); read.finish();
            if(!ok) { USBBoardLink_HsTransportFault(); return false; }
            USBBoardLink_HsAcceptBlock(s_hsRead,blockSize);
            /* Preserve the small control-event API while delivering large
             * reports through its separate, bounded data-plane receiver. */
            return usb_board_link_encode(0xFEu,nullptr,0u,response,responseCapacity,responseLength);
        }

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
    read.finish();
    (void)waitEventHigh(kEventReleaseTimeoutMs);

    if (!found || (rawLength < (uint8_t)(start + total)) ||
        (usb_board_link_checksum(&raw[start], (uint16_t)(total - 1u)) !=
         raw[start + total - 1u])) {
        WebConfig_RecordStartupFrame(raw, rawLength, 2u);
        return false;
    }
    WebConfig_RecordStartupFrame(raw, rawLength, 0u);
    memcpy(response, &raw[start], total);
    *responseLength = total;
    return true;
}

} // namespace

void USBBoardLinkPort_WaitApplicationReady()
{
    (void)UsbRoleReady::wait(s_ready && waitEventHigh(kEventReleaseTimeoutMs),
                             [] { return HAL_GetTick(); },
                             [] { return eventLineIsHigh(); },
                             [](uint32_t ms) { HAL_Delay(ms); });
}

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
    /*
     * SELECT_ROLE, CAPS and IAP are compatibility-plane transactions.  They
     * always start at the already-validated /256 rate. Only an explicit
     * FAST_INPUT_V2 acknowledgement may switch Application to /8.
     */
    s_hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
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
    Ch585Handshake_Acquire(CH585_LINE_USB, kEventReleaseTimeoutMs);
    s_roleRequestSent = false;
    s_fastApplication = false;
    s_ready = true;
    return true;
}

bool USBBoardLinkPort_InitIap()
{
    if (!USBBoardLinkPort_Init()) {
        return false;
    }
    /*
     * The 4 KiB loader drains its eight-byte SPI FIFO from a polling loop.
     * Keep IAP packets at the same conservative clock used during local
     * bring-up even after the WebConfig test override is removed.
     */
    s_fastApplication = false;
    s_fastWebHid = false;
    s_fastIap = false;
    if (s_hspi.Init.BaudRatePrescaler == SPI_BAUDRATEPRESCALER_256) {
        return true;
    }
    if (HAL_SPI_DeInit(&s_hspi) != HAL_OK) {
        s_ready = false;
        return false;
    }
    s_hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
    s_hspi.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    if (HAL_SPI_Init(&s_hspi) != HAL_OK) {
        s_ready = false;
        return false;
    }
    return true;
}

bool USBBoardLinkPort_EnableFastApplication()
{
    if (!USBBoardLinkPort_Init()) {
        return false;
    }
    if (s_fastApplication &&
        s_hspi.Init.BaudRatePrescaler == SPI_BAUDRATEPRESCALER_8) {
        return true;
    }
    if (!refreshEventRelease() || !eventLineIsHigh()) {
        return false;
    }
    if (USBBoardLinkPort_ClockHz() != kExpectedSpiClockHz) {
        return false;
    }

    chipSelect(true);
    if (HAL_SPI_DeInit(&s_hspi) != HAL_OK) {
        s_ready = false;
        return false;
    }
    s_hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
    s_hspi.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    if (HAL_SPI_Init(&s_hspi) != HAL_OK) {
        /* Keep the compatible 1-kHz path available if fast mode cannot arm. */
        s_hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
        s_ready = HAL_SPI_Init(&s_hspi) == HAL_OK;
        s_fastApplication = false;
        return false;
    }
    s_fastApplication = true;
    return true;
}

bool USBBoardLinkPort_DisableFastApplication()
{
    s_fastWebHid = false;
    s_fastIap = false;
    if (!USBBoardLinkPort_Init()) {
        return false;
    }
    if (!s_fastApplication &&
        s_hspi.Init.BaudRatePrescaler == SPI_BAUDRATEPRESCALER_256) {
        return true;
    }

    chipSelect(true);
    if (HAL_SPI_DeInit(&s_hspi) != HAL_OK) {
        s_ready = false;
        s_fastApplication = false;
        return false;
    }
    s_hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
    s_hspi.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    if (HAL_SPI_Init(&s_hspi) != HAL_OK) {
        s_ready = false;
        s_fastApplication = false;
        return false;
    }
    s_fastApplication = false;
    return true;
}

bool USBBoardLinkPort_EnableWebHid(uint32_t spiHz)
{
    if(!USBBoardLinkPort_Init() || s_fastApplication ||
       USBBoardLinkPort_ClockHz()!=kExpectedSpiClockHz ||
       (spiHz!=15000000u && spiHz!=7500000u) ||
       !refreshEventRelease() || !eventLineIsHigh()) return false;
    s_fastWebHid=false;
    s_fastIap=false;
    chipSelect(true);
    if(HAL_SPI_DeInit(&s_hspi)!=HAL_OK) return false;
    s_hspi.Init.BaudRatePrescaler=spiHz==15000000u ? SPI_BAUDRATEPRESCALER_8 : SPI_BAUDRATEPRESCALER_16;
    /* Continuous 15 MHz DMA clocks exposed first-bit corruption on CH585
     * MISO. Keep SCK at 15 MHz and allow one SCK idle cycle between bytes
     * for the slave's next-byte output setup. Bootstrap/input are separate
     * modes and retain their existing inter-data timing. */
    s_hspi.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_01CYCLE;
    if(HAL_SPI_Init(&s_hspi)!=HAL_OK) return false;
    __HAL_RCC_DMA2_CLK_ENABLE();
    DMA_HandleTypeDef *handles[2]={&s_hsTxDma,&s_hsRxDma};
    for(unsigned i=0;i<2;++i) {
        DMA_HandleTypeDef &dma=*handles[i]; memset(&dma,0,sizeof(dma));
        dma.Instance=i==0 ? DMA2_Stream6 : DMA2_Stream7;
        dma.Init.Request=i==0 ? DMA_REQUEST_SPI4_TX : DMA_REQUEST_SPI4_RX;
        dma.Init.Direction=i==0 ? DMA_MEMORY_TO_PERIPH : DMA_PERIPH_TO_MEMORY;
        dma.Init.PeriphInc=DMA_PINC_DISABLE; dma.Init.MemInc=DMA_MINC_ENABLE;
        dma.Init.PeriphDataAlignment=DMA_PDATAALIGN_BYTE; dma.Init.MemDataAlignment=DMA_MDATAALIGN_BYTE;
        dma.Init.Mode=DMA_NORMAL; dma.Init.Priority=DMA_PRIORITY_HIGH; dma.Init.FIFOMode=DMA_FIFOMODE_DISABLE;
        if(HAL_DMA_Init(&dma)!=HAL_OK) return false;
    }
    __HAL_LINKDMA(&s_hspi,hdmatx,s_hsTxDma);
    __HAL_LINKDMA(&s_hspi,hdmarx,s_hsRxDma);
    s_fastWebHid=true;
    return true;
}

bool USBBoardLinkPort_EnableIapDma()
{
    // Reuse the dedicated SPI4 DMA streams/cache-safe D2 buffers. Keep the
    // application WebHID mode separate so raw IAP data is never framed as HID.
    if (!USBBoardLinkPort_EnableWebHid(7500000u)) return false;
    s_fastWebHid = false;
    s_fastIap = true;
    return true;
}

bool USBBoardLinkPort_SendWebHidBlock(const uint8_t *data, uint16_t length)
{
    if(!s_fastWebHid || !data || length<WHF_HEADER_BYTES || length>WHF_BLOCK_BYTES ||
       !refreshEventRelease() || !eventLineIsHigh()) return false;
    chipSelect(false); ownershipGuardDelay();
    if(!eventLineIsHigh()) { chipSelect(true); return false; }
    const bool ok=hsDma(data,nullptr,length);
    chipSelect(true);
    if(!ok) USBBoardLink_HsTransportFault();
    return ok;
}

bool USBBoardLinkPort_IsFastApplication()
{
    return s_ready && s_fastApplication &&
           s_hspi.Init.BaudRatePrescaler == SPI_BAUDRATEPRESCALER_8;
}

uint32_t USBBoardLinkPort_ClockHz()
{
    return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI45);
}

bool USBBoardLinkPort_InitApplication()
{
    if (!USBBoardLinkPort_Init()) {
        return false;
    }
    /*
     * Keep the Application control plane at the same /256 rate already
     * proven by SELECT_ROLE and IAP.  Reinitializing SPI4 to /16 at the
     * selector-to-DMA hand-off made the first GET_CAPS transaction disappear
     * on the current PCB.  Throughput tuning belongs after CAPS/WebConfig
     * reliability is established, not inside the bootstrap boundary.
     */
    s_fastApplication = false;
    s_fastWebHid = false;
    s_fastIap = false;
    if (s_hspi.Init.BaudRatePrescaler == SPI_BAUDRATEPRESCALER_256) {
        return true;
    }
    chipSelect(true);
    if (HAL_SPI_DeInit(&s_hspi) != HAL_OK) {
        s_ready = false;
        return false;
    }
    s_hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
    s_hspi.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    if (HAL_SPI_Init(&s_hspi) != HAL_OK) {
        s_ready = false;
        return false;
    }
    return true;
}

bool USBBoardLinkPort_TryShutdown()
{
    if (!s_ready) {
        s_fastIap = false;
        s_roleRequestSent = false;
        Ch585Handshake_Release(CH585_LINE_USB);
        return true;
    }
    chipSelect(true);
    if (HAL_SPI_DeInit(&s_hspi) != HAL_OK) return false;
    s_roleRequestSent = false;
    Ch585Handshake_Release(CH585_LINE_USB);
    s_fastApplication = false;
    s_ready = false;
    s_fastWebHid = false;
    s_fastIap = false;
    return true;
}

void USBBoardLinkPort_Shutdown() { (void)USBBoardLinkPort_TryShutdown(); }

bool USBBoardLinkPort_SelectRfRoleOnce()
{
    if (!USBBoardLinkPort_Init()) return false;
    // A late boot-ready pulse can outlive the passive startup hint window.
    // Until SELECT_ROLE was sent, a low line must never be clocked as an ACK.
    if (!s_roleRequestSent && USBBoardLinkPort_HasEvent()) return false;
    const uint32_t started = HAL_GetTick();
    constexpr uint32_t budget = 20u;
    const uint8_t role = USB_BOARD_ROLE_RF;
    uint8_t request[5] = {};
    uint8_t requestLength = 0;
    if (!usb_board_link_encode(USB_BOARD_CMD_SELECT_ROLE, &role, 1u,
                               request, sizeof(request), &requestLength)) return false;
    // A late real ACK from this same cold-start attempt is valid. Never use
    // cached UsbBoardLink role/capability state as evidence for wake.
    if (!USBBoardLinkPort_HasEvent() && !USBBoardLinkPort_Send(request, requestLength)) return false;
    while (HAL_GetTick() - started < budget) {
        if (!USBBoardLinkPort_HasEvent()) { HAL_Delay(1u); continue; }
        // ROLE_SELECTED is six bytes. Allow the existing small scan prefix,
        // but reject oversized/unrelated replies without an unbounded drain.
        uint8_t raw[14] = {};
        uint8_t count = 0, start = 0, total = 0;
        bool valid = false;
        Ch585ReadGuard read(CH585_LINE_USB);
        if (!read) return false;
        chipSelect(false);
        while (count < sizeof(raw) && HAL_GetTick() - started < budget) {
            uint8_t fill = 0xffu;
            const uint32_t used = HAL_GetTick() - started;
            if (used >= budget) break;
            const uint32_t remaining = budget - used;
            if (HAL_SPI_TransmitReceive(&s_hspi, &fill, &raw[count], 1u,
                                       remaining < kSpiTimeoutMs ? remaining : kSpiTimeoutMs) != HAL_OK) break;
            ++count;
            if (!total && count >= 3u) {
                const uint8_t at = count - 3u;
                if (raw[at] == USB_BOARD_LINK_SYNC && raw[at + 1u] == USB_BOARD_EVT_ROLE_SELECTED &&
                    raw[at + 2u] == sizeof(usb_board_role_selected_v1_t)) {
                    start = at;
                    total = 4u + sizeof(usb_board_role_selected_v1_t);
                }
            }
            if (total && count == start + total) {
                usb_board_link_frame_t reply = {};
                valid = usb_board_link_decode(raw + start, total, &reply) &&
                    reply.payload[0] == USB_BOARD_ROLE_RF && reply.payload[1] == USB_BOARD_STATUS_OK;
                break;
            }
        }
        chipSelect(true);
        read.finish();
        const uint32_t elapsed = HAL_GetTick() - started;
        if (elapsed < budget) {
            const uint32_t left = budget - elapsed;
            (void)waitEventHigh(left < kEventReleaseTimeoutMs ? left : kEventReleaseTimeoutMs);
        }
        return valid;
    }
    return false;
}

static uint32_t monitorSpiStart, monitorSpiEnd;
static bool monitorSpiValid;
bool USBBoardLinkPort_LastMonitorTiming(uint32_t *start,uint32_t *end) {
    *start=monitorSpiStart;*end=monitorSpiEnd;return monitorSpiValid;
}

bool USBBoardLinkPort_Send(const uint8_t *frame, uint8_t frameLength)
{
    monitorSpiValid=false;
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
    uint8_t paddedCapsFrame[USB_BOARD_LINK_MAX_FRAME_BYTES];
    uint8_t *wireFrame = const_cast<uint8_t *>(frame);
    uint8_t wireLength = frameLength;
    if (frame[1] == USB_BOARD_CMD_GET_CAPS) {
        /*
         * Four clocks can remain entirely in the CH585 hardware FIFO on this
         * PCB, whose PA12 peripheral-NSS edge is not always retained.  Extend
         * GET_CAPS to 64 physical bytes so RX DMA advances; CH585 deliberately
         * uses a 65-byte DMA count, hence this legal maximum frame still ends
         * by NSS and never races its DMA-full interrupt.  SELECT_ROLE remains
         * its exact five-byte polling-selector transaction.
         */
        memset(paddedCapsFrame, 0xFF, sizeof(paddedCapsFrame));
        memcpy(paddedCapsFrame, frame, frameLength);
        wireFrame = paddedCapsFrame;
        wireLength = sizeof(paddedCapsFrame);
    }
    monitorSpiStart=DWT->CYCCNT;
    const HAL_StatusTypeDef result =
        HAL_SPI_Transmit(&s_hspi, wireFrame, wireLength, kSpiTimeoutMs);
    monitorSpiEnd=DWT->CYCCNT;monitorSpiValid=result==HAL_OK;
    if ((result == HAL_OK) &&
        (frame[1] == USB_BOARD_CMD_SELECT_ROLE)) {
        /*
         * The CH585 cold-boot selector polls its RX FIFO every microsecond and
         * not enable the steady-state DMA/NSS interrupt path until after the
         * role is locked.  Keep NSS asserted after this short five-byte frame
         * so the selector can drain and parse it before the transaction ends.
         * Steady-state USB/WebHID frames do not pay this bring-up-only delay.
         */
        ownershipGuardDelay();
    }
    chipSelect(true);
    if (result == HAL_OK && frame[1] == USB_BOARD_CMD_SELECT_ROLE)
        s_roleRequestSent = true;
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

bool USBBoardLinkPort_RoleRequestSent() { return s_ready && s_roleRequestSent; }

bool USBBoardLinkPort_HasReleaseFault()
{
    return Ch585Handshake_Faulted(CH585_LINE_USB);
}

bool USBBoardLinkPort_WaitEventRelease(uint32_t timeoutMs)
{
    return s_ready && waitEventHigh(timeoutMs);
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

bool USBBoardLinkPort_RawTransact(const uint8_t *request,
                                  uint16_t requestLength,
                                  uint8_t *response,
                                  uint16_t responseLength,
                                  uint32_t timeoutMs)
{
    if (request == nullptr || requestLength == 0u ||
        response == nullptr || responseLength == 0u ||
        requestLength > (s_fastIap ? CH585_IAP_DMA_PACKET_SIZE : USB_BOARD_LINK_MAX_FRAME_BYTES) ||
        !USBBoardLinkPort_Init() ||
        !refreshEventRelease() || !eventLineIsHigh()) {
        return false;
    }

    chipSelect(false);
    ownershipGuardDelay();
    if (!eventLineIsHigh()) {
        chipSelect(true);
        return false;
    }
    HAL_StatusTypeDef result = s_fastIap
        ? (hsDma(request, nullptr, requestLength) ? HAL_OK : HAL_ERROR)
        : HAL_SPI_Transmit(
        &s_hspi,
        const_cast<uint8_t *>(request),
        requestLength,
        timeoutMs);
    chipSelect(true);
    if (result != HAL_OK || !waitEventLow(timeoutMs)) {
        return false;
    }

    HAL_Delay(kReadGapMs);
    uint8_t fill[16];
    memset(fill, 0xFF, sizeof(fill));
    if (responseLength > sizeof(fill)) {
        return false;
    }
    Ch585ReadGuard read(CH585_LINE_USB);
    if (!read) return false;
    chipSelect(false);
    result = HAL_SPI_TransmitReceive(&s_hspi,
                                     fill,
                                     response,
                                     responseLength,
                                     timeoutMs);
    chipSelect(true);
    read.finish();
    // Preserve a received ACK even if the bus subsequently fails to release.
    // The next command must consult HasReleaseFault before considering retry.
    (void)waitEventHigh(kEventReleaseTimeoutMs);
    return result == HAL_OK;
}

bool USBBoardLinkPort_RawDiscardPendingResponse(uint16_t responseLength,
                                                uint32_t timeoutMs)
{
    if (responseLength == 0u || responseLength > 16u ||
        !USBBoardLinkPort_Init() || !refreshEventRelease()) {
        return false;
    }
    if (eventLineIsHigh() && !waitEventLow(timeoutMs)) {
        return true; /* no late response arrived */
    }

    uint8_t fill[16];
    uint8_t discard[16];
    memset(fill, 0xFF, sizeof(fill));
    Ch585ReadGuard read(CH585_LINE_USB);
    if (!read) return false;
    chipSelect(false);
    const HAL_StatusTypeDef result = HAL_SPI_TransmitReceive(
        &s_hspi, fill, discard, responseLength, timeoutMs);
    chipSelect(true);
    read.finish();
    (void)waitEventHigh(kEventReleaseTimeoutMs);
    return result == HAL_OK;
}
