#include "platform_port.h"

#include <stdbool.h>
#include <stdint.h>

#include "CH58x_common.h"
#include "dongle_config.h"

/* PA10 = LED_EN, active low */
#define LED_EN_PIN GPIO_Pin_10
#define US_TICK_STEP   10u
#define TMR0_PERIOD_10US (FREQ_SYS / 100000u)

static volatile uint32_t s_now_us;

void platform_clock_init(void)
{
    HSECFG_Capacitance(HSECap_18p);
    SetSysClock(SYSCLK_FREQ);
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
#if DONGLE_USE_USBHS_BACKEND
    PFIC_EnableIRQ(USB2_DEVICE_IRQn);
#else
    PFIC_EnableIRQ(USB_IRQn);
#endif
    PFIC_EnableAllIRQ();
}

uint32_t platform_now_us(void)
{
    return s_now_us;
}

void platform_wdt_disable(void)
{
    sys_safe_access_enable();
    R8_RST_WDOG_CTRL &= (uint8_t)(~(RB_WDOG_RST_EN | RB_WDOG_INT_EN));
    R8_SAFE_LRST_CTRL &= (uint8_t)(~RB_IWDG_RST_EN);
    R8_WDOG_COUNT = 0u;
    sys_safe_access_disable();
}

void platform_led_set(bool on)
{
    GPIOADigitalCfg(ENABLE, LED_EN_PIN);
    GPIOA_ModeCfg(LED_EN_PIN, GPIO_ModeOut_PP_5mA);
    if (on) {
        GPIOA_ResetBits(LED_EN_PIN); /* active low */
    } else {
        GPIOA_SetBits(LED_EN_PIN);
    }
}

void platform_led_blink(void)
{
    static bool s_inited = false;
    static bool s_led_on = false;
    static uint32_t s_next_toggle_us = 0u;
    uint32_t now_us;

    now_us = platform_now_us();
    if (!s_inited) {
        s_inited = true;
        s_led_on = false;
        platform_led_set(s_led_on);
        s_next_toggle_us = now_us + 300000u;
        return;
    }

    if ((int32_t)(now_us - s_next_toggle_us) >= 0) {
        s_led_on = !s_led_on;
        platform_led_set(s_led_on);
        s_next_toggle_us += 300000u;
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
