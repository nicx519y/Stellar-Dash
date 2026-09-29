#include "boot_profile.h"
#if HBOX_BOOT_PROFILE
#include "stm32h7xx_hal.h"
#include "device_security_boot_context.h"

_Static_assert(sizeof(BootProfile) == 1024, "profile ABI must be 1 KiB");
_Static_assert(BP_ADDRESS + sizeof(BootProfile) == HBOX_BOOT_CONTEXT_ADDRESS,
               "profile must stop before authentication mailbox");
__attribute__((section(".boot_profile"), used, aligned(4)))
volatile BootProfile hbox_boot_profile;
#define P (&hbox_boot_profile)
static int app_detail_remaining;
static uint32_t app_detail_start;

static int recording(void)
{
    return P->magic == BP_MAGIC && P->version >= 1 && P->version <= 4 && P->status == 0;
}

void BootProfile_Mark(uint32_t tag)
{
    uint32_t cycles = DWT->CYCCNT;
    if (!recording()) return;
    uint32_t n = P->count;
    if (n >= BP_CAPACITY) { P->status = 2; return; }
    P->events[n].tag = tag | (P->domain << 16);
    P->events[n].tick = P->domain < 3 ? HAL_GetTick() : UINT32_MAX;
    P->events[n].cycles = cycles;
    __DMB();
    P->count = n + 1;
}

void BootProfile_Init(void)
{
    /* SRAM4 has no run-mode clock gate on STM32H750. */
    uint32_t sequence = P->magic == BP_MAGIC && P->version >= 1 && P->version <= 4
        ? P->reserved + 1u : 1u;
    P->magic = 0;
    volatile uint32_t *words = (volatile uint32_t *)P;
    for (unsigned i = 1; i < sizeof(*P) / 4; ++i) words[i] = 0;
    P->version = 1;
    P->reserved = sequence;
    P->initial_hz = SystemCoreClock;
    P->reset_flags = RCC->RSR;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR = 0xC5ACCE55;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    uint32_t before = DWT->CYCCNT;
    __DSB(); __ISB();
    if (DWT->CYCCNT == before) P->flags |= 1;
    P->magic = BP_MAGIC;
    BootProfile_Mark(BP_BASELINE);
}

void BootProfile_ClockReady(void)
{
    if (!recording()) return;
    P->boot_hz = SystemCoreClock;
    P->domain = 1;
    /* Measure the actual recorder at the stable boot clock. No delay/UART.
     * Discard calibration events; retain measured min/max in the header. */
    uint32_t count = P->count;
    P->overhead_min = UINT32_MAX;
    for (unsigned i = 0; i < 8; ++i) {
        uint32_t start = DWT->CYCCNT;
        BootProfile_Mark(BP_BASELINE);
        uint32_t elapsed = DWT->CYCCNT - start;
        if (elapsed < P->overhead_min) P->overhead_min = elapsed;
        if (elapsed > P->overhead_max) P->overhead_max = elapsed;
        P->count = count;
    }
    P->overhead_samples = 8;
}

void BootProfile_Handoff(void)
{
    if (!recording()) return;
    BootProfile_Mark(BP_TEARDOWN);
    P->domain = 3;
}

void BootProfile_Main(void)
{
    if (!recording()) return;
    /* SystemInit has reset D1CPRE to /1. Read the clock without modifying
     * SystemCoreClock or the application's existing initialization order. */
    P->domain = 4;
    BootProfile_Mark(BP_APP_MAIN);
    if ((RCC->D1CFGR & RCC_D1CFGR_D1CPRE) == 0)
        P->app_hz = HAL_RCC_GetSysClockFreq();
    else P->flags |= 4;
    /* v1 ended at main; v2+ continue to the loop; v4 focuses details on runtime entry.
     * Upgrade here, after the original bootloader/assembly records exist. */
    P->version = 4;
    app_detail_remaining = -1;
    __DSB();
}

static void publish_app_record(void)
{
    /* SWD reads SRAM, not the CPU cache. The reserved 1 KiB is cache-line
     * aligned and contains no other owner's data. Never touch the mailbox. */
    if (SCB->CCR & SCB_CCR_DC_Msk)
        SCB_CleanDCache_by_Addr((uint32_t *)(uintptr_t)BP_ADDRESS, sizeof(*P));
    __DSB();
}

void BootProfile_AppTick(uint32_t tag)
{
    if (!recording() || P->version < 2) return;
    P->domain = 2;
    BootProfile_Mark(tag);
    publish_app_record();
}

void BootProfile_AppComplete(void)
{
    if (!recording() || P->version < 2) return;
    BootProfile_AppTick(BP_APP_LOOP);
    if (P->status != 0) return; /* preserve overflow */
    __DMB();
    P->status = 1;
    publish_app_record();
}

void BootProfile_AppDetail(uint32_t tag)
{
    if (!recording() || P->version != 4) return;
    /* Called after board_done. Reserve the entire detail set plus loop_entry,
     * so a longer authenticated boot never loses its mandatory endpoint. */
    if (app_detail_remaining < 0) {
        app_detail_start = P->count;
        app_detail_remaining = P->count + BP_APP_DETAIL_EVENTS + 1 <= BP_CAPACITY
            ? BP_APP_DETAIL_EVENTS : 0;
    }
    if (app_detail_remaining == 0 || P->count >= BP_CAPACITY - 1) {
        /* Retries/fallbacks may exceed the planned eight pairs. Discard the
         * entire optional set, never publish misleading half-open spans, and
         * retain every mandatory marker plus space for loop_entry. */
        P->count = app_detail_start;
        app_detail_remaining = 0;
        P->flags |= 16;
        publish_app_record();
        return;
    }
    --app_detail_remaining;
    BootProfile_AppTick(tag);
}

static uint32_t log_tick_start;
uint32_t BootProfile_LogStart(void)
{
    log_tick_start = HAL_GetTick();
    return DWT->CYCCNT;
}
void BootProfile_LogEnd(uint32_t start)
{
    uint32_t elapsed = DWT->CYCCNT - start;
    if (!recording() || P->domain != 1) return;
    if ((uint64_t)(HAL_GetTick() - log_tick_start + 2u) * P->boot_hz >=
        ((uint64_t)1 << 32) * 1000u) P->flags |= 2;
    uint32_t previous = P->log_cycles;
    P->log_cycles = previous + elapsed;
    if (P->log_cycles < previous) P->flags |= 2;
    ++P->log_calls;
}

void BootProfile_AttestationResult(int success)
{
    if (recording() && !success) P->flags |= 8;
}
#endif
