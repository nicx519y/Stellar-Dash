#include "ch585_handshake.h"
#include "ch585_release_state.hpp"
#include "board_cfg.h"
#include "stm32h7xx_hal.h"
#include "rf_bridge_port_internal.h"

alignas(32) volatile uint32_t g_ch585_release_fault[8] = {};
namespace {
Ch585LineOwner owner = CH585_LINE_NONE;
Ch585ReleaseState state;
uint32_t deadlineMs = 20, generation = 0;
uint32_t nextTicket = 0, activeTicket = 0;
class Critical {
    uint32_t saved = __get_PRIMASK();
public:
    Critical() { __disable_irq(); }
    ~Critical() { if (!saved) __enable_irq(); }
};
bool high() { return HAL_GPIO_ReadPin(CH585_IRQ_GPIO_PORT, CH585_IRQ_PIN) == GPIO_PIN_SET; }
void edges(bool reading) {
    // PR1 does not identify edge polarity. Enable only the expected polarity.
    CLEAR_BIT(EXTI_D1->IMR1, CH585_IRQ_PIN);
    CLEAR_BIT(EXTI->RTSR1, CH585_IRQ_PIN);
    CLEAR_BIT(EXTI->FTSR1, CH585_IRQ_PIN);
    __HAL_GPIO_EXTI_CLEAR_IT(CH585_IRQ_PIN);
    if (reading) SET_BIT(EXTI->RTSR1, CH585_IRQ_PIN);
    else if (owner == CH585_LINE_RF) SET_BIT(EXTI->FTSR1, CH585_IRQ_PIN);
    if (reading || owner == CH585_LINE_RF) SET_BIT(EXTI_D1->IMR1, CH585_IRQ_PIN);
    __DSB();
}
void capture() {
    if (__HAL_GPIO_EXTI_GET_IT(CH585_IRQ_PIN) != RESET) {
        __HAL_GPIO_EXTI_CLEAR_IT(CH585_IRQ_PIN);
        if (state.phase == Ch585ReleaseState::Phase::Reading ||
            state.phase == Ch585ReleaseState::Phase::Waiting) state.rising();
    }
}
void poll() {
    if (state.phase != Ch585ReleaseState::Phase::Waiting) return;
    capture();
    const auto before = state.phase;
    const bool lineHigh = high();
    state.poll(lineHigh, HAL_GetTick(), deadlineMs);
    if (state.phase == Ch585ReleaseState::Phase::Fault && before != state.phase) {
        g_ch585_release_fault[0] = 0x57524c31u;
        ++g_ch585_release_fault[1];
        g_ch585_release_fault[2] = HAL_GetTick();
        g_ch585_release_fault[3] = owner;
        g_ch585_release_fault[4] = generation;
        g_ch585_release_fault[5] = state.since;
        g_ch585_release_fault[6] = lineHigh;
        g_ch585_release_fault[7] = deadlineMs;
        SCB_CleanDCache_by_Addr((uint32_t*)g_ch585_release_fault, sizeof(g_ch585_release_fault));
        __DSB();
    }
    if (state.phase != before) edges(false);
}
}
void Ch585Handshake_Acquire(Ch585LineOwner next, uint32_t timeoutMs) {
    Critical lock;
    owner = next; ++generation; activeTicket = 0; state.reset(); deadlineMs = timeoutMs;
    // Configure the EXTI port mapping, then select only this owner's edge.
    GPIO_InitTypeDef gpio = {};
    gpio.Pin = CH585_IRQ_PIN; gpio.Mode = GPIO_MODE_IT_RISING;
    gpio.Pull = GPIO_PULLUP; gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(CH585_IRQ_GPIO_PORT, &gpio);
    edges(false);
    HAL_NVIC_SetPriority(RF_BRIDGE_IRQ_EXTI_IRQn, RF_BRIDGE_IRQ_EXTI_IRQn_PRIO, 0u);
    HAL_NVIC_EnableIRQ(RF_BRIDGE_IRQ_EXTI_IRQn);
}
void Ch585Handshake_Release(Ch585LineOwner expected) {
    Critical lock;
    if (owner != expected) return;
    owner = CH585_LINE_NONE; ++generation; activeTicket = 0; state.reset(); edges(false);
}
uint32_t Ch585Handshake_BeginRead(Ch585LineOwner expected) {
    Critical lock;
    if (owner != expected) return false;
    poll();
    if (state.phase != Ch585ReleaseState::Phase::Idle || high()) return false;
    // Arm before any read clocks; no old pending edge can satisfy this read.
    edges(true);
    if (!state.begin()) return 0;
    if (++nextTicket == 0) ++nextTicket;
    return activeTicket = nextTicket;
}
void Ch585Handshake_EndRead(Ch585LineOwner expected, uint32_t ticket) {
    Critical lock;
    if (owner != expected || ticket == 0 || ticket != activeTicket) return;
    capture(); state.finish(HAL_GetTick()); poll();
    activeTicket = 0;
}
bool Ch585Handshake_Ready(Ch585LineOwner expected) {
    Critical lock;
    if (owner != expected) return false;
    poll(); return state.phase == Ch585ReleaseState::Phase::Idle;
}
bool Ch585Handshake_Faulted(Ch585LineOwner expected) {
    Critical lock;
    if (owner != expected) return false;
    poll(); return state.phase == Ch585ReleaseState::Phase::Fault;
}
void Ch585Handshake_IRQHandler(void) {
    // The shared vector must not clear PR1 before this owner consumes it.
    const bool rfReady = owner == CH585_LINE_RF && state.phase == Ch585ReleaseState::Phase::Idle;
    capture();
    if (rfReady) RFBridgePort_IRQ_IRQHandler();
}
