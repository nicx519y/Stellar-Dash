#include "platform_port.h"

#include "CH58x_common.h"

/* SPI wiring with STM32 host: CS/PB12, SCK/PB13, MOSI/PB14, MISO/PB15, IRQ/PB11. */
#define SPI_PINS        (GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15)
#define SPI_IRQ_PIN     (GPIO_Pin_11)

#define US_TICK_STEP      10u
#define TMR0_PERIOD_10US  (FREQ_SYS / 100000u)

static volatile uint32_t s_now_us;

void platform_clock_init(void)
{
    R32_SAFE_MODE_CTRL |= RB_XROM_312M_SEL;
    R8_SAFE_MODE_CTRL &= (uint8_t)(~RB_SAFE_AUTO_EN);
    sys_safe_access_enable();
    R32_MISC_CTRL |= 5u | (3u << 25);
    R8_PLL_CONFIG &= (uint8_t)(~(1u << 5));
    {
        uint8_t i;
        uint16_t clk_sys_cfg;
        uint8_t x32m_tune;

        x32m_tune = R8_XT32M_TUNE;
        R8_XT32M_TUNE = (uint8_t)(x32m_tune | 0x03u);
        R8_HFCK_PWR_CTRL |= RB_CLK_XT32M_PON;
        clk_sys_cfg = R16_CLK_SYS_CFG;
        R16_CLK_SYS_CFG = (uint16_t)(clk_sys_cfg | 0xC0u);
        for (i = 0u; i < 9u; ++i) {
            __nop();
        }
        R16_CLK_SYS_CFG = clk_sys_cfg;
        R8_XT32M_TUNE = x32m_tune;
    }
    R8_HFCK_PWR_CTRL |= RB_CLK_PLL_PON;
    R16_CLK_SYS_CFG = CLK_SOURCE_HSE_PLL_62_4MHz;
    R8_FLASH_SCK = (uint8_t)(R8_FLASH_SCK & (uint8_t)(~(1u << 4)));
    R8_FLASH_CFG = 0x01;
    R8_CK32K_CONFIG |= RB_CLK_INT32K_PON;
    R8_SAFE_MODE_CTRL |= RB_SAFE_AUTO_EN;
    sys_safe_access_disable();
}

void platform_gpio_init(void)
{
    GPIOPinRemap(ENABLE, RB_PIN_SPI0);
    GPIOADigitalCfg(ENABLE, (uint16_t)0xFFFFu);
    GPIOBDigitalCfg(ENABLE, SPI_PINS | SPI_IRQ_PIN);

    GPIOB_ModeCfg(SPI_IRQ_PIN, GPIO_ModeOut_PP_5mA);
    GPIOB_ResetBits(SPI_IRQ_PIN);

    /*
     * SPI0 slave IO mode:
     * - CS/SCK/MOSI input pull-up
     * - MISO push-pull output
     * The SPI peripheral owns these pins after SPI0_SlaveInit().
     */
    GPIOB_ModeCfg(GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14, GPIO_ModeIN_PU);
    GPIOB_ModeCfg(GPIO_Pin_15, GPIO_ModeOut_PP_5mA);
}

void platform_timer_init(void)
{
    s_now_us = 0u;

    TMR0_TimerInit(TMR0_PERIOD_10US);
    TMR0_ClearITFlag(TMR0_3_IT_CYC_END);
    TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    PFIC_EnableIRQ(TMR0_IRQn);
}

void platform_spi_init(void)
{
    SPI0_SlaveInit();
}

uint32_t platform_now_us(void)
{
    return s_now_us;
}

void platform_irq_line_set(bool asserted)
{
    if (asserted) {
        GPIOB_SetBits(SPI_IRQ_PIN);
    } else {
        GPIOB_ResetBits(SPI_IRQ_PIN);
    }
}

void platform_idle(void)
{
    /* Stub: optional low power entry can be inserted here. */
}

__INTERRUPT
__HIGH_CODE
void TMR0_IRQHandler(void)
{
    if (TMR0_GetITFlag(TMR0_3_IT_CYC_END)) {
        TMR0_ClearITFlag(TMR0_3_IT_CYC_END);
        s_now_us += US_TICK_STEP;
    }
}
