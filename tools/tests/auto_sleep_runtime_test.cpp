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
    const bool xinput = std::strncmp(scenario, "xinput-", 7) == 0;
    INPUT_STATE.keepalive = !xinput;
    STORAGE_MANAGER.enabled = std::strcmp(scenario, "disabled") != 0;
    rcc.RSR = RCC_RSR_PORRSTF | RCC_RSR_PINRSTF | RCC_RSR_CPURSTF;
    if (!std::strcmp(scenario, "reset")) rcc.RSR = RCC_RSR_PINRSTF | RCC_RSR_CPURSTF;
    if (!std::strcmp(scenario, "software")) rcc.RSR = RCC_RSR_SFTRSTF | RCC_RSR_PINRSTF | RCC_RSR_CPURSTF;
    if (!std::strcmp(scenario, "fault")) rcc.RSR |= RCC_RSR_IWDG1RSTF | RCC_RSR_SFTRSTF;
    if (!std::strcmp(scenario, "stop-fault")) recoveryStopFault = true;
    if (!std::strcmp(scenario, "debugger")) debug.DHCSR = 1u;
    if (!std::strcmp(scenario, "boot-key")) portC.high &= ~GPIO_BTN1_PIN;
    SystemSleep_CaptureBootFlags();
    assert((pwr.CPUCR & 7u) == 0u && (scb.SCR & 6u) == 0u);
    SystemSleep_ConfirmWakeHoldOrReturnStandby();
    SystemSleep_InitializeWakeKeys();
    portC.high = 0xffffu;
    if (!std::strncmp(scenario, "protection", 10)) {
        STORAGE_MANAGER.enabled=false;
#if HBOX_AUTO_SLEEP_ENABLED == 0
        assert(!SystemSleep_ProtectionStop() && idleCount==0);
        return 0;
#else
        assert(SystemSleep_ProtectionStop() && idleCount==1 && lastStopInterval==1000);
        debug.DHCSR=1; assert(!SystemSleep_ProtectionStop()); debug.DHCSR=0;
        POWER_MANAGER.polling=true; assert(!SystemSleep_ProtectionStop()); POWER_MANAGER.polling=false;
        portC.high &= ~GPIO_BTN1_PIN; assert(!SystemSleep_ProtectionStop()); portC.high=0xffffu;
        // Both GPIO and Hall wake presses must be released before gameplay.
        SystemSleep_SuppressHeldInputs();
        assert(SystemSleep_FilterInput(0x10u)==0u);
        tick+=30; assert(SystemSleep_FilterInput(0x10u)==0u);
        assert(SystemSleep_FilterInput(0u)==0u);
        assert(SystemSleep_FilterInput(0x10u)==0x10u);
        stopOk=false; assert(!SystemSleep_ProtectionStop());
        stopOk=true; assert(!SystemSleep_ProtectionStop()); // no boot retry loop
        assert(INPUT_STATE.running); // protection owner, never this API, manages loads
        return 0;
#endif
    }
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
    if (!std::strcmp(scenario, "xinput-neutral-failure")) {
        INPUT_STATE.neutralOk = false;
        step(200);
        assert(!SystemSleep_IsBusy() && INPUT_STATE.running && idleCount == 0);
        INPUT_STATE.neutralOk = true;
        step(60000);
        assert(!SystemSleep_IsBusy() && idleCount == 0);
        return 0;
    }
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
    if (!std::strcmp(scenario, "reset") || !std::strcmp(scenario, "fault") || !std::strcmp(scenario, "stop-fault") || !std::strcmp(scenario, "boot-key")) {
        assert(!SystemSleep_ProtectionStop());
        step(60000);
        assert(!SystemSleep_IsBusy() && INPUT_STATE.neutrals == 0 && idleCount == 0);
        STORAGE_MANAGER.enabled = false; step(2);
        STORAGE_MANAGER.enabled = true; step(60000);
        assert(!SystemSleep_IsBusy() && idleCount == 0);
        return 0;
    }
    assert(SystemSleep_IsBusy() && !INPUT_STATE.running);
    if (!std::strcmp(scenario, "xinput-idle")) {
        assert(INPUT_STATE.neutrals == 1 && lastStopInterval == 100);
        INPUT_STATE.neutralOk = false; // no repeated transfer should be attempted
        step(60000);
        assert(INPUT_STATE.neutrals == 1 && SystemSleep_IsBusy());
        assert(INPUT_STATE.failures == 0 && !INPUT_STATE.running);
        simulatedWakePins = GPIO_BTN3_PIN;
        SystemSleep_Idle(); step(40);
        assert(INPUT_STATE.running && !SystemSleep_IsBusy());
        assert(INPUT_STATE.neutrals == 1 && INPUT_STATE.failures == 0);
        return 0;
    }
    if (!std::strcmp(scenario, "stop-failure")) {
        stopOk = false; step(50);
        assert(INPUT_STATE.running && !SystemSleep_IsBusy());
        stopOk = true;
        const auto count = idleCount; step(60000);
        assert(!SystemSleep_IsBusy() && idleCount == count); return 0;
    }
    if (!std::strcmp(scenario, "stop-short-key")) {
        // Edge arrives and is released entirely while SysTick is suspended.
        simulatedWakePins = GPIO_BTN3_PIN;
        SystemSleep_Idle(); step(40);
        assert(!SystemSleep_IsBusy() && INPUT_STATE.running);
        assert(lastStopInterval <= 10u); return 0;
    }
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
    const auto gatedIdle = idleCount;
    POWER_MANAGER.polling = true;
    SystemSleep_Idle();
    assert(idleCount == gatedIdle);
    POWER_MANAGER.polling = false;
    SPIScreenManager::getInstance().notice = true;
    SystemSleep_Idle();
    assert(idleCount == gatedIdle);
    SPIScreenManager::getInstance().notice = false;
    step(100);
    assert(xinput ? INPUT_STATE.neutrals == sent : INPUT_STATE.neutrals >= sent + 9);
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
    assert(xinput ? INPUT_STATE.neutrals == sent : INPUT_STATE.neutrals >= sent + 9);
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
