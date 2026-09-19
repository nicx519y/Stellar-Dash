#ifndef RF_LINK_CLOCK_H
#define RF_LINK_CLOCK_H
#include <stdint.h>

/* Private RF/SPI clock in 625 us ticks. The SDK TMOS getter mutates its
 * scheduler epoch and must never be reentered by an RF/SPI interrupt. */
typedef struct {
    uint32_t last_cycles, remainder, ticks, cycles_per_tick;
} rfh_cycle_clock_t;

static inline uint32_t rfh_cycle_clock_advance(rfh_cycle_clock_t *clock, uint32_t cycles)
{
    uint32_t elapsed = cycles - clock->last_cycles;
    uint32_t remainder = clock->remainder + elapsed % clock->cycles_per_tick;
    clock->last_cycles = cycles;
    clock->ticks += elapsed / clock->cycles_per_tick;
    if(remainder >= clock->cycles_per_tick) {
        remainder -= clock->cycles_per_tick;
        clock->ticks++;
    }
    clock->remainder = remainder;
    return clock->ticks;
}

uint32_t RF_LinkClockNow(void);

/* One implementation per firmware, emitted by RF_PHY.c. SysTick is already
 * free running at the system frequency in the RF_8K HAL; do not reconfigure it.
 * Main loops service this clock continuously, including RF Off (< one wrap).
 * The short critical section protects ONLY our state, never the SDK clock. */
#ifdef RF_LINK_CLOCK_IMPLEMENTATION
static rfh_cycle_clock_t g_rf_link_clock;
__HIGH_CODE
uint32_t RF_LinkClockNow(void)
{
    uint32_t irq_status, result;
    SYS_DisableAllIrq(&irq_status);
    if(!g_rf_link_clock.cycles_per_tick) {
        g_rf_link_clock.cycles_per_tick = GetSysClock() / 1600u;
        if(!g_rf_link_clock.cycles_per_tick) g_rf_link_clock.cycles_per_tick = 1u;
    }
    result = rfh_cycle_clock_advance(&g_rf_link_clock, SysTick->CNT);
    SYS_RecoverIrq(irq_status);
    return result;
}
#endif
#endif
