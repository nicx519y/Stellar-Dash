#include "board_latest_ch585.h"

static volatile uint8_t s_usb_spi_owner;

static void board_disable_conflicting_remaps(void)
{
    /*
     * SPI0 must remain on PA12..PA15. UART0 remap also owns PA14/PA15,
     * so both alternate selections are explicitly disabled.
     */
    GPIOPinRemap(DISABLE, RB_PIN_SPI0);
    GPIOPinRemap(DISABLE, RB_PIN_UART0);
}

void rfm_board_latest_ch585_prepare_spi_pins(void)
{
    board_disable_conflicting_remaps();
    GPIOADigitalCfg(ENABLE,
                    (uint16_t)(RFM_BOARD_SPI_PIN_MASK | RFM_BOARD_W_INT_PIN));

    GPIOA_ModeCfg(RFM_BOARD_SPI_NSS_PIN |
                  RFM_BOARD_SPI_SCK_PIN |
                  RFM_BOARD_SPI_MOSI_PIN,
                  GPIO_ModeIN_PU);
    GPIOA_SetBits(RFM_BOARD_SPI_MISO_PIN);
    GPIOA_ModeCfg(RFM_BOARD_SPI_MISO_PIN, GPIO_ModeOut_PP_5mA);

    GPIOA_SetBits(RFM_BOARD_W_INT_PIN);
    GPIOA_ModeCfg(RFM_BOARD_W_INT_PIN, GPIO_ModeOut_PP_5mA);
}

void rfm_board_latest_ch585_prepare_sleep_pins(void)
{
    board_disable_conflicting_remaps();
    GPIOADigitalCfg(ENABLE,
                    (uint16_t)(RFM_BOARD_SPI_PIN_MASK | RFM_BOARD_W_INT_PIN));
    GPIOA_ModeCfg(RFM_BOARD_SPI_PIN_MASK, GPIO_ModeIN_PU);
    GPIOA_SetBits(RFM_BOARD_W_INT_PIN);
    GPIOA_ModeCfg(RFM_BOARD_W_INT_PIN, GPIO_ModeOut_PP_5mA);
}

void rfm_board_latest_ch585_stop_spi(void)
{
    PFIC_DisableIRQ(SPI0_IRQn);
    PFIC_DisableIRQ(GPIO_A_IRQn);
    R16_PA_INT_EN &= (uint16_t)~RFM_BOARD_SPI_NSS_PIN;
    GPIOA_ClearITFlagBit(RFM_BOARD_SPI_NSS_PIN);
    SPI0_ITCfg(DISABLE,
               SPI0_IT_CNT_END | SPI0_IT_DMA_END | SPI0_IT_FIFO_OV |
               SPI0_IT_FIFO_HF | SPI0_IT_BYTE_END | SPI0_IT_FST_BYTE);
    R8_SPI0_CTRL_CFG &= (uint8_t)~(RB_SPI_DMA_ENABLE | RB_SPI_DMA_LOOP);
    R8_SPI0_INT_FLAG = RB_SPI_IF_CNT_END | RB_SPI_IF_DMA_END |
                       RB_SPI_IF_FIFO_OV | RB_SPI_IF_FIFO_HF |
                       RB_SPI_IF_BYTE_END | RB_SPI_IF_FST_BYTE;
    R8_SPI0_CTRL_MOD = RB_SPI_ALL_CLEAR;
    rfm_board_latest_ch585_set_w_int(false);
}

bool rfm_board_latest_ch585_nss_high(void)
{
    return GPIOA_ReadPortPin(RFM_BOARD_SPI_NSS_PIN) != 0u;
}

bool rfm_board_latest_ch585_wake_high(void)
{
    return GPIOA_ReadPortPin(RFM_BOARD_SPI_MISO_PIN) != 0u;
}

void rfm_board_latest_ch585_set_w_int(bool asserted)
{
#if (RFM_BOARD_W_INT_ACTIVE_LOW != 0u)
    if(asserted)
    {
        GPIOA_ResetBits(RFM_BOARD_W_INT_PIN);
    }
    else
    {
        GPIOA_SetBits(RFM_BOARD_W_INT_PIN);
    }
#else
    if(asserted)
    {
        GPIOA_SetBits(RFM_BOARD_W_INT_PIN);
    }
    else
    {
        GPIOA_ResetBits(RFM_BOARD_W_INT_PIN);
    }
#endif
}

void rfm_board_latest_ch585_pulse_boot_ready(void)
{
    rfm_board_latest_ch585_set_w_int(true);
    DelayMs(RFM_BOARD_BOOT_READY_PULSE_MS);
    rfm_board_latest_ch585_set_w_int(false);
}

void rfm_board_latest_ch585_set_usb_spi_owner(bool enabled)
{
    s_usb_spi_owner = enabled ? 1u : 0u;
}

bool rfm_board_latest_ch585_usb_spi_owner(void)
{
    return s_usb_spi_owner != 0u;
}
