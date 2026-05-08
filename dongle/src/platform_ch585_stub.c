#include "platform_port.h"

#include <stdbool.h>
#include <stdint.h>

#include "CH58x_common.h"

/* PA10 = LED_EN, active low */
#define LED_EN_PIN GPIO_Pin_10
#define US_TICK_STEP   10u
#define TMR0_PERIOD_10US (FREQ_SYS / 100000u)

static volatile uint32_t s_now_us;

void platform_clock_init(void)
{
    /*
     * Configure external crystal capacitance, then apply manual clock switch
     * sequence compatible with generic riscv-none-embed-gcc toolchain.
     */
    HSECFG_Capacitance(HSECap_18p);
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
    /*
     * Configure PA10 (LED_EN) as digital output.
     * Default state is high (pull-up equivalent) => LED off.
     */
    GPIOADigitalCfg(ENABLE, LED_EN_PIN);
    GPIOA_ModeCfg(LED_EN_PIN, GPIO_ModeOut_PP_5mA);
    GPIOA_SetBits(LED_EN_PIN);
}

void platform_timer_init(void)
{
    s_now_us = 0u;

    /*
     * Use TMR0 cycle interrupt as a hardware time base.
     * Tick period = 10 us.
     */
    TMR0_TimerInit(TMR0_PERIOD_10US);
    TMR0_ClearITFlag(TMR0_3_IT_CYC_END);
    TMR0_ITCfg(ENABLE, TMR0_3_IT_CYC_END);
    PFIC_EnableIRQ(TMR0_IRQn);
    PFIC_EnableAllIRQ();
}

void platform_irq_ensure_enabled(void)
{
    /* Keep timer/global interrupt gate enabled in case RF ROM code toggles it. */
    PFIC_EnableIRQ(TMR0_IRQn);
    PFIC_EnableAllIRQ();
}

uint32_t platform_now_us(void)
{
    return s_now_us;
}

void platform_led_set(bool on)
{
    if (on) {
        GPIOA_ResetBits(LED_EN_PIN); /* active low */
    } else {
        GPIOA_SetBits(LED_EN_PIN);
    }
}

void platform_idle(void)
{
    /* TODO: Optional low-power wait-for-interrupt. */
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
