#include "rf_boot_ready.hpp"
#include "ch585_startup_trace.hpp"

#include <stdio.h>

#include "board_cfg.h"
#include "stm32h7xx_hal.h"

#if !APP_LOG_VERBOSE
#define printf(...) ((void)0)
#endif

namespace {

#ifndef RF_BOOT_READY_STABLE_MS
#define RF_BOOT_READY_STABLE_MS 2u
#endif

#ifndef RF_BOOT_READY_SETTLE_MS
#define RF_BOOT_READY_SETTLE_MS 10u
#endif

#ifndef RF_BOOT_READY_DEASSERT_TIMEOUT_MS
#define RF_BOOT_READY_DEASSERT_TIMEOUT_MS 250u
#endif

static bool s_readySeen = false;
enum class ReadyPhase { Assert, Stable, Release, Settle, Ready, Failed };
static ReadyPhase s_phase = ReadyPhase::Failed;
static uint32_t s_waitStarted = 0u, s_phaseStarted = 0u;

static void enableGpioClock(GPIO_TypeDef* port) {
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

static void configureReadyInputPullup() {
    enableGpioClock(RF_BRIDGE_IRQ_GPIO_PORT);

    GPIO_InitTypeDef init = {};
    init.Mode = GPIO_MODE_INPUT;
    init.Pull = GPIO_PULLUP;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    init.Alternate = 0u;
    init.Pin = RF_BRIDGE_IRQ_PIN;
    HAL_GPIO_Init(RF_BRIDGE_IRQ_GPIO_PORT, &init);
}

}

namespace RFBootReady {

void reset() {
    s_readySeen = false;
    s_phase = ReadyPhase::Failed;
}

void beginWait() {
    configureReadyInputPullup();
    s_readySeen = false;
    s_phase = ReadyPhase::Assert;
    s_waitStarted = s_phaseStarted = HAL_GetTick();
}

// Main-loop control logic, not a timing-critical ISR. Keep its code footprint
// bounded in AXI SRAM, including builds with the formal trust configuration.
__attribute__((optimize("Os"))) WaitResult pollWait(uint32_t timeoutMs) {
    const uint32_t now = HAL_GetTick();
    const bool asserted = HAL_GPIO_ReadPin(RF_BRIDGE_IRQ_GPIO_PORT,
        RF_BRIDGE_IRQ_PIN) == RF_BRIDGE_IRQ_ASSERTED_STATE;
    switch (s_phase) {
    case ReadyPhase::Assert:
        if (asserted) { s_phase = ReadyPhase::Stable; s_phaseStarted = now; }
        break;
    case ReadyPhase::Stable:
        if (!asserted) s_phase = ReadyPhase::Assert;
        else if (now - s_phaseStarted >= RF_BOOT_READY_STABLE_MS) {
            s_phase = ReadyPhase::Release;
            s_phaseStarted = now;
        }
        break;
    case ReadyPhase::Release:
        if (!asserted) { s_phase = ReadyPhase::Settle; s_phaseStarted = now; }
        else if (now - s_phaseStarted >= RF_BOOT_READY_DEASSERT_TIMEOUT_MS)
            s_phase = ReadyPhase::Failed;
        break;
    case ReadyPhase::Settle:
        // A new low is not a role ACK: no role command has been sent yet.
        if (asserted) s_phase = ReadyPhase::Failed;
        else if (now - s_phaseStarted >= RF_BOOT_READY_SETTLE_MS) {
            s_phase = ReadyPhase::Ready;
            s_readySeen = true;
        }
        break;
    default: break;
    }
    // Once a stable pulse starts, allow its bounded release/settle even if
    // it straddles the passive-ready deadline. Never clock that low pulse.
    if ((s_phase == ReadyPhase::Assert || s_phase == ReadyPhase::Stable) &&
        now - s_waitStarted >= timeoutMs) s_phase = ReadyPhase::Failed;
    return s_phase == ReadyPhase::Ready ? WaitResult::Ready :
        (s_phase == ReadyPhase::Failed ? WaitResult::Timeout : WaitResult::Pending);
}

bool waitForModuleReady(uint32_t timeoutMs) {
    if (s_readySeen) {
        return true;
    }

    configureReadyInputPullup();

    const uint32_t start = HAL_GetTick();
    uint32_t assertedSince = 0u;

    while ((HAL_GetTick() - start) <= timeoutMs) {
        const auto level = HAL_GPIO_ReadPin(RF_BRIDGE_IRQ_GPIO_PORT, RF_BRIDGE_IRQ_PIN);
        Ch585StartupTrace::line(HAL_GetTick(), level == RF_BRIDGE_IRQ_DEASSERTED_STATE);
        if (level == RF_BRIDGE_IRQ_ASSERTED_STATE) {
            if (assertedSince == 0u) {
                assertedSince = HAL_GetTick();
            }
            if ((HAL_GetTick() - assertedSince) >= RF_BOOT_READY_STABLE_MS) {
                printf("[RF_BOOT][READY] W_INT asserted after %u ms\r\n",
                       (unsigned int)(HAL_GetTick() - start));
                const uint32_t deassertStart = HAL_GetTick();
                while ((HAL_GetTick() - deassertStart) <
                       RF_BOOT_READY_DEASSERT_TIMEOUT_MS) {
                    const auto released = HAL_GPIO_ReadPin(RF_BRIDGE_IRQ_GPIO_PORT,
                                                          RF_BRIDGE_IRQ_PIN);
                    Ch585StartupTrace::line(HAL_GetTick(), released == RF_BRIDGE_IRQ_DEASSERTED_STATE);
                    if (released == RF_BRIDGE_IRQ_DEASSERTED_STATE) {
                        HAL_Delay(RF_BOOT_READY_SETTLE_MS);
                        s_readySeen = true;
                        return true;
                    }
                    HAL_Delay(1u);
                }
                printf("[RF_BOOT][READY_TIMEOUT] W_INT did not deassert\r\n");
                return false;
            }
        } else {
            assertedSince = 0u;
        }
        HAL_Delay(1u);
    }

    printf("[RF_BOOT][READY_TIMEOUT] timeout=%u ms pin=%u\r\n",
           (unsigned int)timeoutMs,
           (unsigned int)HAL_GPIO_ReadPin(RF_BRIDGE_IRQ_GPIO_PORT, RF_BRIDGE_IRQ_PIN));
    return false;
}

}
