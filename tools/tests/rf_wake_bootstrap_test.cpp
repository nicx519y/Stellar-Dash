#include <cassert>
#include <cstdint>
#include <initializer_list>
#include "ch585_role_bootstrap.hpp"
#include "rf_boot_ready.hpp"

static uint32_t ticks, selections, handoffs;
static bool low, powered, portReady, portOk = true, handoffOk = true, roleOk = true;
uint32_t HAL_GetTick() { return ticks; }
// No HAL_Delay stub: the tested production wake functions must not block.
struct GPIO_InitTypeDef { unsigned Mode, Pull, Pin; };
#define GPIO_MODE_ANALOG 0
#define GPIO_NOPULL 0
#define CH585_SPI_NSS_PIN 1
#define CH585_SPI_SCK_PIN 2
#define CH585_SPI_MOSI_PIN 4
#define CH585_SPI_MISO_PIN 8
#define CH585_SPI_GPIO_PORT 0
#define CH585_IRQ_PIN 16
#define CH585_IRQ_GPIO_PORT 0
#define RF_BRIDGE_IRQ_GPIO_PORT 0
#define RF_BRIDGE_IRQ_PIN 16
#define RF_BRIDGE_IRQ_ASSERTED_STATE 0
void HAL_GPIO_Init(int, GPIO_InitTypeDef*) {}
int HAL_GPIO_ReadPin(int, int) { return low ? 0 : 1; }
void configureReadyInputPullup() {}
void publishStartupTrace(bool) {}
#define SleepDiagnostics_Record(...) ((void)0)
void USBBoardLinkPort_Shutdown() { portReady = false; }
bool USBBoardLinkPort_Init() { return portReady = portOk; }
bool USBBoardLinkPort_RequestApplicationHandoff() {
    assert(!selections); // Never use SELECT_ROLE to leave IAP.
    if (low || !handoffOk) return false;
    ++handoffs;
    return true;
}
struct Power {
    bool setUsbHostEnabled(bool) { return true; }
    void setCh585Enabled(bool enabled) {
        if (enabled) assert(portReady); // NSS initialized before power-on.
        powered = enabled;
    }
} BOARD_POWER;

#include "production.inc"

static bool selectRole(Ch585Role role) {
    assert(role == Ch585Role::Rf && powered && !low && s_readySeen);
    ++selections;
    return roleOk;
}
static auto& boot = Ch585RoleBootstrap::getInstance();
static Ch585ResumeResult advance(uint32_t ms) {
    auto result = Ch585ResumeResult::Pending;
    while (ms--) { ++ticks; result = boot.serviceRfSleepResume(); }
    return result;
}
static void begin(uint32_t now = 0) {
    boot.shutdown();
    ticks = now; selections = handoffs = 0; low = false;
    portOk = handoffOk = roleOk = true;
    boot.setSelector(selectRole);
    boot.beginRfSleepResume();
}
static void pulse() {
    low = true; advance(100); assert(!selections);
    low = false; advance(10); assert(!selections);
    assert(advance(1) == Ch585ResumeResult::Ready);
    assert(selections == 1 && boot.isLocked());
}
int main() {
    // Natural boot, repeated sleep and tick wrap: no handoff, no stale ready.
    for (uint32_t start : {0u, 10000u, UINT32_MAX - 600u}) {
        begin(start); advance(500); assert(!selections && !handoffs); pulse();
        assert(!handoffs);
    }
    // Slow IAP requires a separate handoff and a new genuine pulse.
    begin(); advance(740); assert(handoffs == 1 && !selections); pulse();
    // A pulse straddling the passive deadline must finish without handoff.
    begin(); advance(700); pulse(); assert(!handoffs);
    // Constant high cannot count as ready, even after the handoff deadline.
    begin(); assert(advance(1600) == Ch585ResumeResult::Failed);
    assert(handoffs == 1 && !selections);
    // Constant low cannot be clocked as an ACK or as an IAP handoff.
    begin(); advance(500); low = true;
    assert(advance(400) == Ch585ResumeResult::Failed);
    assert(!selections && !handoffs);
    // A short glitch is not an application-ready pulse.
    begin(); advance(500); low = true; advance(1); low = false; advance(100);
    assert(!selections && !handoffs);
    // Port initialization failure must not enable the power supply.
    begin(); portOk = false; assert(advance(20) == Ch585ResumeResult::Failed);
    assert(!powered && !selections);
    begin(); handoffOk = false; assert(advance(740) == Ch585ResumeResult::Failed);
    assert(!selections);
    // Cancel while waiting: no later handoff or selection, fresh next session.
    begin(); advance(500); boot.shutdown(); advance(2000);
    assert(!powered && !selections && !handoffs);
    begin(); advance(500); pulse();
}
