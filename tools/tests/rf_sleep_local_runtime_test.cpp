#include "auto_sleep_runtime_stubs.hpp"
#include <cassert>

static void step(unsigned ms, uint32_t inputMask) {
    while (ms--) {
        ++tick;
        SystemSleep_Tick1msFromISR();
        SystemSleep_Service(true, false);
        SystemSleep_Idle();
        if (INPUT_STATE.running) (void)SystemSleep_FilterInput(inputMask);
    }
}

int main() {
    STORAGE_MANAGER.enabled = true;
    INPUT_STATE.rf = true;
    rcc.RSR = RCC_RSR_PORRSTF;
    SystemSleep_CaptureBootFlags();
    SystemSleep_ConfirmWakeHoldOrReturnStandby();
    SystemSleep_InitializeWakeKeys();
    step(31000, 0);
    assert(SystemSleep_IsBusy() && INPUT_STATE.transportOff);
    assert(lastStopInterval == 10);
    const uint32_t wake = 1u << GPIO_BTN3_VIRTUAL_PIN;
    simulatedWakePins = GPIO_BTN3_PIN;
    SystemSleep_Idle();
    portC.high &= ~GPIO_BTN3_PIN;
    step(40, wake);
    // Radio is still off indefinitely: real local manager must release ADC,
    // local busy state and screen recovery independently, without failure.
    assert(INPUT_STATE.running && !SystemSleep_IsBusy());
    assert(INPUT_STATE.transportOff && INPUT_STATE.failures == 0);
    assert(!SPIScreenManager::getInstance().suspended);
    assert(SystemSleep_FilterInput(wake) == 0);
    const uint32_t other = 1u << GPIO_BTN2_VIRTUAL_PIN;
    assert(SystemSleep_FilterInput(wake | other) == other);
    portC.high = 0xffff;
    step(30000, 0);
    assert(!SystemSleep_IsBusy() && INPUT_STATE.running && INPUT_STATE.transportOff);
    assert(SystemSleep_FilterInput(wake) == wake); // released first key is usable
    INPUT_STATE.transportOff = false;
    step(1, 0); step(9999, 0);
    assert(!SystemSleep_IsBusy());
    step(2, 0);
    assert(SystemSleep_IsBusy()); // recovery restarts the idle interval
}
