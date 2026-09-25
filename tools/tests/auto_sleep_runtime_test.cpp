#include "auto_sleep_runtime_stubs.hpp"
#include <cassert>
#include <cstring>

static void step(unsigned ms, bool input = true, bool reset = false)
{
    for (unsigned i = 0; i < ms; ++i) {
        ++tick;
        SystemSleep_Tick1msFromISR();
        SystemSleep_Service(input, reset);
        SystemSleep_Idle();
        // The real input owner reports a fresh frame on resume.
        if (INPUT_STATE.running) (void)SystemSleep_FilterInput(0u);
    }
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    const char* scenario = argv[1];
    STORAGE_MANAGER.enabled = std::strcmp(scenario, "disabled") != 0;
    rcc.RSR = RCC_RSR_PORRSTF | RCC_RSR_PINRSTF | RCC_RSR_CPURSTF;
    if (!std::strcmp(scenario, "reset")) rcc.RSR = RCC_RSR_PINRSTF | RCC_RSR_CPURSTF;
    if (!std::strcmp(scenario, "software")) rcc.RSR = RCC_RSR_SFTRSTF | RCC_RSR_PINRSTF | RCC_RSR_CPURSTF;
    if (!std::strcmp(scenario, "fault")) rcc.RSR |= RCC_RSR_IWDG1RSTF | RCC_RSR_SFTRSTF;
    if (!std::strcmp(scenario, "debugger")) debug.DHCSR = 1u;
    if (!std::strcmp(scenario, "boot-key")) portC.high &= ~GPIO_BTN1_PIN;
    SystemSleep_CaptureBootFlags();
    assert((pwr.CPUCR & 7u) == 0u && (scb.SCR & 6u) == 0u);
    SystemSleep_ConfirmWakeHoldOrReturnStandby();
    SystemSleep_InitializeWakeKeys();
    portC.high = 0xffffu;
    if (!std::strcmp(scenario, "disabled")) {
        step(60000); assert(!SystemSleep_IsBusy() && idleCount == 0);
        STORAGE_MANAGER.enabled = true;
        step(9999); assert(!SystemSleep_IsBusy());
        step(2); assert(SystemSleep_IsBusy() && idleCount > 0);
        return 0;
    }
    if (!std::strcmp(scenario, "retime")) {
        step(25000); STORAGE_MANAGER.timeout = 30000;
        step(29999); assert(!SystemSleep_IsBusy());
        step(2); assert(SystemSleep_IsBusy()); return 0;
    }
    if (!std::strcmp(scenario, "exclusive")) {
        step(60000, false);
        assert(!SystemSleep_IsBusy() && idleCount == 0);
        step(9999);
        assert(!SystemSleep_IsBusy());
        step(2);
        assert(SystemSleep_IsBusy());
        return 0;
    }
    step(29900);
    assert(!SystemSleep_IsBusy());
    if (!std::strcmp(scenario, "reset-pending")) {
        step(60000, true, true);
        assert(!SystemSleep_IsBusy() && INPUT_STATE.neutrals == 0);
        return 0;
    }
    if (!std::strcmp(scenario, "cancel-before-pause")) {
        portC.high &= ~GPIO_BTN1_PIN;
        step(1000);
        assert(!SystemSleep_IsBusy() && INPUT_STATE.neutrals == 0);
        return 0;
    }
    if (!std::strcmp(scenario, "pause-failure")) {
        INPUT_STATE.pauseOk = false;
        step(200);
        assert(!SystemSleep_IsBusy() && INPUT_STATE.running);
        INPUT_STATE.pauseOk = true; step(60000);
        assert(!SystemSleep_IsBusy());
        return 0;
    }
    if (!std::strcmp(scenario, "prepare-timeout") || !std::strcmp(scenario, "disable-prepare")) bridgeIdle = false;
    step(200);
    if (!std::strcmp(scenario, "reset") || !std::strcmp(scenario, "fault") || !std::strcmp(scenario, "boot-key")) {
        step(60000);
        assert(!SystemSleep_IsBusy() && INPUT_STATE.neutrals == 0 && idleCount == 0);
        STORAGE_MANAGER.enabled = false; step(2);
        STORAGE_MANAGER.enabled = true; step(60000);
        assert(!SystemSleep_IsBusy() && idleCount == 0);
        return 0;
    }
    assert(SystemSleep_IsBusy() && !INPUT_STATE.running);
    if (!std::strcmp(scenario, "disable-prepare") || !std::strcmp(scenario, "disable-sleep") ||
        !std::strcmp(scenario, "disable-restore")) {
        if (!std::strcmp(scenario, "disable-prepare")) assert(idleCount == 0);
        if (!std::strcmp(scenario, "disable-restore")) { portC.high &= ~GPIO_BTN1_PIN; step(6); }
        STORAGE_MANAGER.enabled = false;
        const auto count = idleCount;
        step(40); assert(INPUT_STATE.running && !SystemSleep_IsBusy());
        assert(!SPIScreenManager::getInstance().suspended && idleCount == count);
        portC.high = 0xffffu; step(60000);
        assert(!SystemSleep_IsBusy() && idleCount == count); return 0;
    }
    if (!std::strcmp(scenario, "debugger")) {
        assert(idleCount == 0); debug.DHCSR = 0;
        scb.SCR = 6u; step(1); assert(idleCount == 1 && scb.SCR == 0u);
        irqMask = 1; SystemSleep_Idle(); assert(idleCount == 1); irqMask = 0;
        step(2, false); assert(idleCount == 1);
        step(2, true, true); assert(idleCount == 1); return 0;
    }
    if (!std::strcmp(scenario, "noise")) {
        portC.high &= ~GPIO_BTN1_PIN; step(2);
        portC.high = 0xffffu; step(6);
        assert(SystemSleep_IsBusy() && !INPUT_STATE.running && INPUT_STATE.resumes == 0);
        return 0;
    }
    if (!std::strcmp(scenario, "keepalive-failure")) {
        INPUT_STATE.neutralOk = false;
        step(100);
        assert(!SystemSleep_IsBusy());
        assert(INPUT_STATE.failures == 1);
        INPUT_STATE.neutralOk = true; INPUT_STATE.running = true;
        step(60000);
        assert(!SystemSleep_IsBusy() && INPUT_STATE.failures == 1);
        return 0;
    }
    if (!std::strcmp(scenario, "prepare-timeout")) {
        step(600);
        assert(!SystemSleep_IsBusy() && INPUT_STATE.running);
        bridgeIdle = true; step(60000);
        assert(!SystemSleep_IsBusy());
        return 0;
    }
    if (!std::strcmp(scenario, "mode-change")) {
        SystemSleep_CancelForModeChange();
        assert(!SystemSleep_IsBusy());
        assert(!SPIScreenManager::getInstance().suspended);
        return 0;
    }
    unsigned sent = INPUT_STATE.neutrals;
    step(100);
    assert(INPUT_STATE.neutrals >= sent + 9);
    SystemSleep_Idle(); assert(idleCount > 0 && irqMask == 0);
    if (!std::strcmp(scenario, "restore-failure")) INPUT_STATE.resumeOk = false;
    portC.high &= ~GPIO_BTN1_PIN;
    const auto beforeRestore = idleCount;
    step(35);
    assert(idleCount == beforeRestore);
    assert(!SystemSleep_IsBusy());
    assert(!SPIScreenManager::getInstance().suspended);
    if (!std::strcmp(scenario, "restore-failure")) {
        assert(INPUT_STATE.failures == 1);
        INPUT_STATE.resumeOk = true; INPUT_STATE.running = true;
        portC.high = 0xffffu; step(60000);
        assert(!SystemSleep_IsBusy() && INPUT_STATE.failures == 1);
        return 0;
    }
    assert(INPUT_STATE.running && INPUT_STATE.resumes == 1);
    portC.high = 0xffffu;
    step(10020);
    assert(SystemSleep_IsBusy());
    sent = INPUT_STATE.neutrals;
    step(100);
    assert(INPUT_STATE.neutrals >= sent + 9); // also on the SECOND sleep
    portA.high &= ~1u; step(35);
    assert(!SystemSleep_IsBusy() && INPUT_STATE.running && INPUT_STATE.resumes == 2);
    for (unsigned cycle = 2; cycle < 20; ++cycle) {
        portA.high = portC.high = 0xffffu; step(10020);
        assert(SystemSleep_IsBusy());
        if (cycle % 5 == 4) portA.high &= ~1u;
        else portC.high &= ~(GPIO_BTN1_PIN << (cycle % 5));
        step(35);
        assert(!SystemSleep_IsBusy() && INPUT_STATE.running);
    }
    assert(INPUT_STATE.resumes == 20);
}
