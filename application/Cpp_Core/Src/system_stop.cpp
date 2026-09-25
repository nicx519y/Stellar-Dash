#include "sleep_diagnostics.hpp"
#include "system_stop.hpp"
#include "board_cfg.h"
#include "stm32h7xx_hal.h"

extern "C" void PowerManager_NotifyChargerIrqFromISR(void);

namespace {
constexpr uint32_t keyPins = GPIO_BTN1_PIN | GPIO_BTN2_PIN | GPIO_BTN3_PIN | GPIO_BTN4_PIN;
constexpr uint32_t allPins = keyPins | ROTENC_BTN_PIN;
constexpr uint32_t faultMagic = 0x53544f50u;
// Deliberately outside .bss. No Flash, backup registers or protection bits.
__attribute__((section(".sleep_fault"), aligned(32), used)) volatile uint32_t recoveryFault[8];
volatile bool keyIrqOwned = false;
volatile uint32_t capturedPins = 0;
uint32_t fractionalTicks = 0;
uint32_t lastChargerCatchup = 0;

uint16_t counter()
{
    uint16_t a = static_cast<uint16_t>(LPTIM2->CNT);
    // Asynchronous clock: accept two equal consecutive reads, bounded even
    // when the timer/clock is faulty.
    for (unsigned i = 0; i < 16; ++i) {
        const uint16_t b = static_cast<uint16_t>(LPTIM2->CNT);
        if (a == b) return b;
        a = b;
    }
    return a;
}

bool waitBits(volatile uint32_t& reg, uint32_t mask, uint32_t value)
{
    __DSB();
    const uint16_t start = counter();
    // LSI timeout plus a finite iteration backstop if LSI itself fails.
    for (uint32_t spins = 0; spins < 1000000u; ++spins) {
        if ((reg & mask) == value) return true;
        if (static_cast<uint16_t>(counter() - start) >= LSI_VALUE / 20u) break;
    }
    return false;
}

void resetTimer()
{
    NVIC_DisableIRQ(LPTIM2_IRQn);
    // ES0392 2.17.1: never disable by clearing LPTIM_CR.ENABLE.
    __HAL_RCC_LPTIM2_FORCE_RESET();
    __HAL_RCC_LPTIM2_RELEASE_RESET();
    NVIC_ClearPendingIRQ(LPTIM2_IRQn);
}

bool startTimer(uint32_t intervalMs)
{
    SET_BIT(RCC->CSR, RCC_CSR_LSION);
    const uint32_t start = HAL_GetTick();
    while ((RCC->CSR & RCC_CSR_LSIRDY) == 0u) {
        if (HAL_GetTick() - start >= 20u) return false;
    }
    __HAL_RCC_LPTIM2_CLK_ENABLE();
    resetTimer();
    __HAL_RCC_LPTIM2_CONFIG(RCC_LPTIM2CLKSOURCE_LSI);
    __HAL_RCC_LPTIM2_CLK_SLEEP_ENABLE();
    SET_BIT(RCC->D3AMR, RCC_D3AMR_LPTIM2AMEN);
    LPTIM2->CFGR = 0u;
    LPTIM2->CR = LPTIM_CR_ENABLE;
    LPTIM2->ARR = 0xffffu;
    LPTIM2->CMP = (LSI_VALUE / 1000u) * intervalMs;
    // No flag clears here (ES0392 2.17.2). Only CMPM is cleared in its ISR.
    const uint32_t synced = LPTIM_ISR_ARROK | LPTIM_ISR_CMPOK;
    const uint32_t syncStart = HAL_GetTick();
    while ((LPTIM2->ISR & synced) != synced) {
        if (HAL_GetTick() - syncStart >= 10u) { resetTimer(); return false; }
    }
    LPTIM2->IER = LPTIM_IER_CMPMIE;
    NVIC_SetPriority(LPTIM2_IRQn, 6u);
    NVIC_ClearPendingIRQ(LPTIM2_IRQn);
    NVIC_EnableIRQ(LPTIM2_IRQn);
    LPTIM2->CR |= LPTIM_CR_CNTSTRT;
    // Prove that the counter advances before relying on it for STOP wakeup.
    const uint32_t runningAt = HAL_GetTick();
    while (counter() == 0u) {
        if (HAL_GetTick() - runningAt >= 10u) { resetTimer(); return false; }
    }
    return true;
}

uint32_t heldPins()
{
    return ((~GPIOC->IDR) & keyPins) | ((~ROTENC_BTN_PORT->IDR) & ROTENC_BTN_PIN);
}

struct ExtiSnapshot {
    uint32_t exticr0, exticr1, exticr2, imr, rising, falling;
    uint32_t irq0Enabled, irq0Priority, irqGroupEnabled, irqGroupPriority;
};

ExtiSnapshot armKeys()
{
    ExtiSnapshot saved{SYSCFG->EXTICR[0], SYSCFG->EXTICR[1], SYSCFG->EXTICR[2],
        EXTI->IMR1, EXTI->RTSR1, EXTI->FTSR1,
        NVIC_GetEnableIRQ(EXTI0_IRQn), NVIC_GetPriority(EXTI0_IRQn),
        NVIC_GetEnableIRQ(EXTI9_5_IRQn), NVIC_GetPriority(EXTI9_5_IRQn)};
    CLEAR_BIT(EXTI->IMR1, allPins);
    // Preserve charger notification before temporarily remapping PI8 to PC8.
    if (EXTI->PR1 & CHARGE_INT_PIN) PowerManager_NotifyChargerIrqFromISR();
    EXTI->PR1 = allPins;
    MODIFY_REG(SYSCFG->EXTICR[0], 0x000fu, 0x0000u); // PA0
    MODIFY_REG(SYSCFG->EXTICR[1], 0xff00u, 0x2200u); // PC6/7
    MODIFY_REG(SYSCFG->EXTICR[2], 0x00ffu, 0x0022u); // PC8/9
    CLEAR_BIT(EXTI->RTSR1, allPins);
    SET_BIT(EXTI->FTSR1, allPins);
    capturedPins = 0u;
    keyIrqOwned = true;
    SET_BIT(EXTI->IMR1, allPins);
    NVIC_SetPriority(EXTI0_IRQn, 6u);
    NVIC_SetPriority(EXTI9_5_IRQn, 6u);
    NVIC_EnableIRQ(EXTI0_IRQn);
    NVIC_EnableIRQ(EXTI9_5_IRQn);
    return saved;
}

void disarmKeys(const ExtiSnapshot& saved)
{
    CLEAR_BIT(EXTI->IMR1, allPins);
    capturedPins |= (EXTI->PR1 & allPins) | heldPins();
    EXTI->PR1 = allPins;
    MODIFY_REG(SYSCFG->EXTICR[0], 0x000fu, saved.exticr0 & 0x000fu);
    MODIFY_REG(SYSCFG->EXTICR[1], 0xff00u, saved.exticr1 & 0xff00u);
    MODIFY_REG(SYSCFG->EXTICR[2], 0x00ffu, saved.exticr2 & 0x00ffu);
    MODIFY_REG(EXTI->RTSR1, allPins, saved.rising & allPins);
    MODIFY_REG(EXTI->FTSR1, allPins, saved.falling & allPins);
    MODIFY_REG(EXTI->IMR1, allPins, saved.imr & allPins);
    keyIrqOwned = false;
    if (!saved.irq0Enabled) NVIC_DisableIRQ(EXTI0_IRQn);
    if (!saved.irqGroupEnabled) NVIC_DisableIRQ(EXTI9_5_IRQn);
    NVIC_ClearPendingIRQ(EXTI0_IRQn);
    // Do not clear the shared group's pending bit: PI8 may have asserted again.
    NVIC_SetPriority(EXTI0_IRQn, saved.irq0Priority);
    NVIC_SetPriority(EXTI9_5_IRQn, saved.irqGroupPriority);
    if (capturedPins || !(CHARGE_INT_PORT->IDR & CHARGE_INT_PIN) ||
        HAL_GetTick() - lastChargerCatchup >= 1000u) {
        PowerManager_NotifyChargerIrqFromISR();
        lastChargerCatchup = HAL_GetTick();
    }
}

[[noreturn]] void recoveryReset()
{
    recoveryFault[0] = faultMagic;
    recoveryFault[1] = ~faultMagic;
    SCB_CleanDCache_by_Addr(const_cast<uint32_t*>(recoveryFault), 32);
    __DSB();
    NVIC_SystemReset(); // ordinary reset, never changes option bytes
    for (;;) {}
}
} // namespace

extern "C" bool SystemStop_ConsumeRecoveryFault(bool coldReset, bool softwareReset)
{
    const bool fault = !coldReset && softwareReset &&
        recoveryFault[0] == faultMagic && recoveryFault[1] == ~faultMagic;
    recoveryFault[0] = recoveryFault[1] = 0u;
    return fault;
}

extern "C" bool SystemStop_HandleKeyIRQ(void)
{
    if (!keyIrqOwned) return false;
    const uint32_t pins = EXTI->PR1 & allPins;
    capturedPins |= pins;
    EXTI->PR1 = pins;
    return true;
}

extern "C" void EXTI0_IRQHandler(void) { (void)SystemStop_HandleKeyIRQ(); }
extern "C" void LPTIM2_IRQHandler(void)
{
    if (LPTIM2->ISR & LPTIM2->IER & LPTIM_ISR_CMPM) {
        LPTIM2->ICR = LPTIM_ICR_CMPMCF;
    }
}

extern "C" bool SystemStop_Enter(uint32_t intervalMs, uint32_t* wakePins)
{
    *wakePins = 0u;
    if (__get_PRIMASK() || __get_IPSR() ||
        (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk)) return true;
    // The power bus is synchronous in the main loop. Never stop mid-transfer.
    const bool i2cClocked = (RCC->APB1LENR & RCC_APB1LENR_I2C1EN) != 0u;
    if (i2cClocked && (I2C1->ISR & I2C_ISR_BUSY)) return true;
    if (intervalMs == 0u || intervalMs > 100u || !startTimer(intervalMs)) return false;
    const uint32_t oldCr = RCC->CR;
    const uint32_t oldCfgr = RCC->CFGR;
    const uint32_t oldPwr = PWR->CR1;
    const uint32_t oldVos = PWR->D3CR & PWR_D3CR_VOS;
    const uint32_t oldOverdrive = SYSCFG->PWRCR & SYSCFG_PWRCR_ODEN;
    const uint32_t i2cCr1 = i2cClocked ? I2C1->CR1 : 0u;
    SCB_CleanDCache();
    __disable_irq();
    // ES0392 2.19.2: disable I2C when STOP wakeup by I2C is not used.
    if (i2cClocked) CLEAR_BIT(I2C1->CR1, I2C_CR1_PE);
    const auto exti = armKeys();
    const uint32_t systick = SysTick->CTRL;
    const uint16_t started = counter();
    SysTick->CTRL = systick & ~SysTick_CTRL_ENABLE_Msk;
    // Account for a tick that arrived just before masking, then remove it so
    // it cannot turn every STOP attempt into an immediate WFI return.
    if (SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) {
        SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
        ++uwTick;
    }
    bool ready = true;
    SET_BIT(RCC->CR, RCC_CR_HSION);
    ready = waitBits(RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY);
    if (ready) {
        // VOS0 cannot enter low power: switch to HSI before removing overdrive.
        MODIFY_REG(RCC->CFGR, RCC_CFGR_SW, RCC_CFGR_SW_HSI);
        ready = waitBits(RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_HSI);
    }
    if (ready && oldOverdrive) {
        CLEAR_BIT(SYSCFG->PWRCR, SYSCFG_PWRCR_ODEN);
        ready = waitBits(PWR->CSR1, PWR_CSR1_ACTVOSRDY, PWR_CSR1_ACTVOSRDY);
    }
    if (ready) {
        CLEAR_BIT(RCC->CFGR, RCC_CFGR_STOPWUCK); // resume using HSI
        MODIFY_REG(PWR->CR1, PWR_CR1_SVOS | PWR_CR1_LPDS,
                   PWR_REGULATOR_SVOS_SCALE5 | PWR_CR1_LPDS);
        CLEAR_BIT(PWR->CPUCR, PWR_CPUCR_PDDS_D1 | PWR_CPUCR_PDDS_D2 |
                  PWR_CPUCR_PDDS_D3 | PWR_CPUCR_RUN_D3);
        CLEAR_BIT(SCB->SCR, SCB_SCR_SLEEPONEXIT_Msk);
        SET_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);
        // IRQs remain pending across WFI, but cannot execute with wrong clocks.
        // PRIMASK does not prevent an enabled NVIC interrupt waking WFI.
        // The compare may have fired before IRQ masking. Do not sleep until
        // the next 16-bit wrap if its ISR already consumed that notification.
        if (!heldPins() && !(EXTI->PR1 & allPins) && capturedPins == 0u &&
            counter() < LPTIM2->CMP) {
            ++g_sleepDiagnostics.stopEntries;
            g_sleepDiagnostics.stage = static_cast<uint32_t>(SleepStage::StopEnter);
            __DSB();
            __WFI();
            __ISB();
            ++g_sleepDiagnostics.stopReturns;
            g_sleepDiagnostics.stage = static_cast<uint32_t>(SleepStage::StopReturn);
        }
    }
    CLEAR_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk);
    SET_BIT(PWR->CPUCR, PWR_CPUCR_RUN_D3);
    PWR->CR1 = oldPwr;
    // RM0433 6.6.2: a real system STOP resets Run voltage scaling to VOS3.
    // VOSRDY alone only confirms that *VOS3* is ready. Restore the saved Run
    // scale on HSI before enabling VOS0 overdrive or any high-speed clock.
    MODIFY_REG(PWR->D3CR, PWR_D3CR_VOS, oldVos);
    if (!waitBits(PWR->D3CR, PWR_D3CR_VOS | PWR_D3CR_VOSRDY,
                  oldVos | PWR_D3CR_VOSRDY)) recoveryReset();
    const uint32_t actualVos = (oldVos >> PWR_D3CR_VOS_Pos) << PWR_CSR1_ACTVOS_Pos;
    if (!waitBits(PWR->CSR1, PWR_CSR1_ACTVOS | PWR_CSR1_ACTVOSRDY,
                  actualVos | PWR_CSR1_ACTVOSRDY)) recoveryReset();
    if (oldOverdrive) {
        SET_BIT(SYSCFG->PWRCR, SYSCFG_PWRCR_ODEN);
        if (!waitBits(PWR->D3CR, PWR_D3CR_VOSRDY, PWR_D3CR_VOSRDY)) recoveryReset();
        if (!waitBits(PWR->CSR1, PWR_CSR1_ACTVOSRDY, PWR_CSR1_ACTVOSRDY)) recoveryReset();
    }
    // PLL configuration and peripheral registers are retained in STOP. Restore
    // all previously enabled sources (ADC/LCD may use PLL2/3) before any ISR.
    const uint32_t oscillators = oldCr & (RCC_CR_HSEON | RCC_CR_CSION | RCC_CR_HSI48ON);
    SET_BIT(RCC->CR, oscillators);
    if ((oscillators & RCC_CR_HSEON) && !waitBits(RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY)) recoveryReset();
    if ((oscillators & RCC_CR_CSION) && !waitBits(RCC->CR, RCC_CR_CSIRDY, RCC_CR_CSIRDY)) recoveryReset();
    if ((oscillators & RCC_CR_HSI48ON) && !waitBits(RCC->CR, RCC_CR_HSI48RDY, RCC_CR_HSI48RDY)) recoveryReset();
    const uint32_t plls = oldCr & (RCC_CR_PLL1ON | RCC_CR_PLL2ON | RCC_CR_PLL3ON);
    SET_BIT(RCC->CR, plls);
    if (!waitBits(RCC->CR, plls << 1u, plls << 1u)) recoveryReset();
    MODIFY_REG(RCC->CFGR, RCC_CFGR_SW | RCC_CFGR_STOPWUCK,
               oldCfgr & (RCC_CFGR_SW | RCC_CFGR_STOPWUCK));
    if (!waitBits(RCC->CFGR, RCC_CFGR_SWS, (oldCfgr & RCC_CFGR_SW) << RCC_CFGR_SWS_Pos)) recoveryReset();
    __DSB();
    __ISB();
    if (i2cClocked) I2C1->CR1 = i2cCr1;
    // SystemCoreClock, bus divisors, QSPI mapping and flash latency never change.
    const uint32_t ticks = static_cast<uint16_t>(counter() - started);
    fractionalTicks += ticks * 1000u;
    uwTick += fractionalTicks / LSI_VALUE;
    fractionalTicks %= LSI_VALUE;
    disarmKeys(exti);
    *wakePins = capturedPins;
    resetTimer();
    CLEAR_BIT(RCC->D3AMR, RCC_D3AMR_LPTIM2AMEN);
    __HAL_RCC_LPTIM2_CLK_DISABLE();
    SysTick->CTRL = systick;
    __enable_irq();
    return ready;
}
