"""Host tests of production USB bootstrap/physical-mode code; no device access."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
INC = ROOT / 'application/Inc'
SRC = ROOT / 'application/Src'


class UsbStartupOverlapTest(unittest.TestCase):
    def test_production_usb_startup(self):
        compiler = shutil.which("g++")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            stubs = {
                "board_cfg.h": r'''
#pragma once
#define CH585_POWER_OFF_MIN_MS 20u
#define CH585_POWER_ON_SETTLE_MS 20u
#define CH585_READY_HINT_TIMEOUT_MS 700u
#define CH585_ROLE_SELECT_RETRY_MS 5u
#define CH585_ROLE_SELECT_TIMEOUT_MS 1200u
#define BOARD_MODE_DEBOUNCE_MS 20u
#define MODE_USB_N_PORT GPIOI
#define MODE_RF_N_PORT GPIOI
#define MODE_USB_N_PIN 1u
#define MODE_RF_N_PIN 2u
#define APP_STAGE(...) ((void)0)
#define APP_STAGE_ERROR(...) ((void)0)
''',
                "stm32h7xx_hal.h": r'''
#pragma once
#include <cstdint>
struct GPIO_TypeDef {};
extern GPIO_TypeDef gpioi;
#define GPIOI (&gpioi)
struct GPIO_InitTypeDef { uint32_t Pin, Mode, Pull, Speed; };
#define GPIO_MODE_INPUT 0
#define GPIO_PULLUP 0
#define GPIO_SPEED_FREQ_LOW 0
#define GPIO_PIN_SET 1
#define __HAL_RCC_GPIOI_CLK_ENABLE() ((void)0)
uint32_t HAL_GetTick();
void HAL_Delay(uint32_t);
int HAL_GPIO_ReadPin(GPIO_TypeDef*, uint32_t);
inline void HAL_GPIO_Init(GPIO_TypeDef*, GPIO_InitTypeDef*) {}
''',
                "board_power.hpp": r'''
#pragma once
struct TestPower {
    bool initialized = true, on = false, host = false;
    bool isInitialized() const { return initialized; }
    bool isCh585Enabled() const { return on; }
    bool setUsbHostEnabled(bool v) { host=v; return true; }
    void setCh585Enabled(bool v);
};
extern TestPower power;
#define BOARD_POWER power
''',
                "usb_board_link_port.hpp": "#pragma once\nbool USBBoardLinkPort_Init();\n",
                "system_logger.h": "#pragma once\n",
            }
            for name, body in stubs.items():
                (folder / name).write_text(body, encoding="utf-8")
            (folder / "test.cpp").write_text(r'''
#include <cassert>
#include <cstring>
#include <vector>
#include "board_cfg.h"
#include "stm32h7xx_hal.h"
#include "board_power.hpp"
#include "board_mode.hpp"
#include "ch585_role_bootstrap.hpp"
GPIO_TypeDef gpioi;
TestPower power;
uint32_t now, powerOnAt, readyAfter=0xFFFFFFFFu;
unsigned powerOns, selectors, succeedOnPower=1, portInits;
bool portOk=true;
int rawUsb=0, rawRf=1;
std::vector<uint32_t> budgets, selectorTimes;
uint32_t HAL_GetTick() { return now; }
void HAL_Delay(uint32_t ms) { now += ms+1; }
int HAL_GPIO_ReadPin(GPIO_TypeDef*, uint32_t pin) { return pin==1 ? rawUsb : rawRf; }
void TestPower::setCh585Enabled(bool v) {
    on=v;
    if (v) { ++powerOns; powerOnAt=now; }
}
bool USBBoardLinkPort_Init() { assert(!power.on); ++portInits; return portOk; }
namespace RFBootReady {
void reset() {}
bool waitForModuleReady(uint32_t timeout) {
    budgets.push_back(timeout);
    const uint32_t elapsed=now-powerOnAt;
    assert(elapsed>=CH585_POWER_ON_SETTLE_MS);
    if (readyAfter!=0xFFFFFFFFu && elapsed<=readyAfter && readyAfter-elapsed<=timeout) {
        now += readyAfter-elapsed;
        return true;
    }
    now += timeout+2; // model the HAL-tick polling timeout
    return false;
}
}
bool selectRole(Ch585Role) {
    ++selectors;
    selectorTimes.push_back(now-powerOnAt);
    assert(power.on && !power.host);
    now += 20; // bounded command transaction, including failures
    return powerOns>=succeedOnPower;
}
int main(int argc, char** argv) {
    assert(argc==2);
    auto& b=CH585_ROLE_BOOTSTRAP;
    b.setSelector(selectRole);
    const char* test=argv[1];
    if (!std::strcmp(test,"mode_switch")) {
        assert(!BOARD_MODE.isUsbStartupSafe());
        BOARD_MODE.setup();
        assert(BOARD_MODE.isUsbStartupSafe());
        rawUsb=1; // stable cache still says USB; raw check must reject immediately
        assert(BOARD_MODE.current()==BoardMode::Usb && !BOARD_MODE.isUsbStartupSafe());
        BOARD_MODE.setup();
        assert(BOARD_MODE.current()==BoardMode::CenterOff && !BOARD_MODE.isUsbStartupSafe());
        return 0;
    }
    if (!std::strcmp(test,"uninitialized")) {
        power.initialized=false;
        assert(!b.prepareUsbStartup() && powerOns==0 && selectors==0);
        return 0;
    }
    if (!std::strcmp(test,"port_failure")) {
        portOk=false;
        assert(!b.prepareUsbStartup() && !power.on && !b.hasPreparedUsbStartup());
        return 0;
    }
    if (!std::strcmp(test,"cold")) {
        assert(b.start(Ch585Role::Usb));
        assert(powerOns==1 && budgets.size()==1 && budgets[0]==700);
        assert(selectorTimes[0]>=720);
        return 0;
    }
    if (!std::strcmp(test,"rollover")) now=0xFFFFFF80u;
    assert(b.prepareUsbStartup());
    assert(powerOns==1 && portInits==1 && selectors==0 && !power.host);
    assert(b.role()==Ch585Role::SafeIdle && b.state()==Ch585BootstrapState::Booting);
    assert(b.prepareUsbStartup() && powerOns==1 && portInits==1);
    if (!std::strcmp(test,"cancel")) {
        b.shutdown();
        assert(!power.on && !b.hasPreparedUsbStartup());
        assert(b.start(Ch585Role::Usb) && powerOns==2 && budgets[0]==700);
        return 0;
    }
    if (!std::strcmp(test,"missing_selector")) {
        b.setSelector(nullptr);
        assert(!b.start(Ch585Role::Usb) && !power.on && !b.hasPreparedUsbStartup());
        return 0;
    }
    if (!std::strcmp(test,"safe_idle")) {
        assert(!b.start(Ch585Role::SafeIdle) && !power.on && !b.hasPreparedUsbStartup());
        return 0;
    }
    if (!std::strcmp(test,"expired")) now+=800;
    else if (std::strcmp(test,"immediate")) now+=380;
    if (!std::strcmp(test,"rail_lost")) power.on=false;
    if (!std::strcmp(test,"ready")) readyAfter=600;
    if (!std::strcmp(test,"retry")) succeedOnPower=2;
    if (!std::strcmp(test,"fail")) succeedOnPower=100;
    const auto role=!std::strcmp(test,"role_change") ? Ch585Role::Maintenance : Ch585Role::Usb;
    const bool ok=b.start(role);
    assert(!b.hasPreparedUsbStartup());
    if (!std::strcmp(test,"fail")) {
        assert(!ok && !power.on && !power.host && powerOns==2);
        assert(b.state()==Ch585BootstrapState::Failed && budgets.size()==2 && budgets[1]==700);
        return 0;
    }
    assert(ok && b.isLocked() && b.role()==role && !power.host);
    if (!std::strcmp(test,"expired") || !std::strcmp(test,"rail_lost") ||
        !std::strcmp(test,"role_change")) {
        assert(powerOns==2 && budgets[0]==700);
    } else {
        assert(budgets[0]<=700);
        assert(selectorTimes[0]>=(!std::strcmp(test,"ready") ? 600u : 720u));
        if (std::strcmp(test,"immediate")) assert(budgets[0]==340);
        if (!std::strcmp(test,"retry")) assert(powerOns==2 && budgets.size()==2 && budgets[1]==700);
        else assert(powerOns==1);
    }
    unsigned before=selectors;
    assert(b.start(role) && selectors==before); // locked start remains idempotent
    assert(!b.prepareUsbStartup() && b.isLocked());
}
''', encoding="utf-8")
            exe = folder / "test.exe"
            command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                       "-I"+str(folder), "-I"+str(INC), "-I"+str(ROOT/"common"),
                       str(ROOT / 'application/Src/transport/ch585_role_bootstrap.cpp'), str(ROOT / 'application/Src/system/board_mode.cpp'),
                       str(folder/"test.cpp"), "-o", str(exe)]
            print("USB startup overlap: compile production bootstrap/mode code", flush=True)
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            for case in ("cold", "overlap", "immediate", "rollover", "ready", "retry", "fail",
                         "expired", "rail_lost", "cancel", "missing_selector", "safe_idle",
                         "role_change", "uninitialized", "port_failure", "mode_switch"):
                with self.subTest(case=case):
                    result = subprocess.run([str(exe), case], capture_output=True, text=True, timeout=5)
                    self.assertEqual(result.returncode, 0, result.stderr)
            print("USB startup overlap: 16 scenarios completed", flush=True)


if __name__ == "__main__":
    unittest.main()
