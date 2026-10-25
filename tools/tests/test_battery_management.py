"""Battery-only host simulation; no RF code or hardware access is executed."""
from pathlib import Path
import shutil
import tempfile
import unittest
from tools.tests.application_paths import application_include_flags, run_native
from tools.tests.test_ch585_usb_handshake import function

ROOT = Path(__file__).resolve().parents[2]


class BatteryManagementTests(unittest.TestCase):
    def test_update_abort_only_releases_uncommitted_receiver(self):
        source = (ROOT / "application/Src/firmware/ch585_firmware_update.cpp").read_text(encoding="utf-8")
        harness = r'''
#include <cassert>
#include <initializer_list>
#define private public
#include "ch585_firmware_update.hpp"
#undef private
'''
        harness += function(source, "void Ch585FirmwareUpdate::cancelReceiving()")
        harness += r'''
int main() {
    auto& update=CH585_FIRMWARE_UPDATE;
    for(auto status:{Ch585FirmwareUpdateStatus::Idle, Ch585FirmwareUpdateStatus::Ready,
        Ch585FirmwareUpdateStatus::Scheduled, Ch585FirmwareUpdateStatus::Programming,
        Ch585FirmwareUpdateStatus::Completed, Ch585FirmwareUpdateStatus::Failed}) {
        update.currentStatus=status; update.imageSize=100; update.received=50;
        update.cancelReceiving();
        assert(update.currentStatus==status && update.imageSize==100 && update.received==50);
    }
    update.currentStatus=Ch585FirmwareUpdateStatus::Receiving;
    update.cancelReceiving();
    assert(update.currentStatus==Ch585FirmwareUpdateStatus::Idle && !update.imageSize && !update.received);
}
'''
        with tempfile.TemporaryDirectory(prefix="xora-battery-update-") as folder:
            cpp, exe = Path(folder) / "cancel.cpp", Path(folder) / "cancel.exe"
            cpp.write_text(harness, encoding="utf-8")
            run_native([shutil.which("g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        *application_include_flags(), str(cpp), "-o", str(exe)], check=True)
            run_native([str(exe)], check=True)

    def test_calibration_cancel_does_not_save_or_notify_completion(self):
        source = (ROOT / "application/Src/system/states/calibration_state.cpp").read_text(encoding="utf-8")
        harness = r'''
#include <cassert>
#include <cstdint>
#define HAS_LED 0
#define LOG_DEBUG(...) ((void)0)
bool callbackArmed=true, hall=true;
unsigned callbackCalls=0, sampleStops=0;
struct Cal {
    void clearCallbacks() { callbackArmed=false; }
    int stopCalibration() { if(callbackArmed) ++callbackCalls; return 0; }
} ADC_CALIBRATION_MANAGER;
struct Adc { void forceStopAllSampling() { ++sampleStops; }
    bool isSamplingHardwareStopped() { return true; }
} ADC_MANAGER;
struct Scheduler { void stop() {} } REPORT_SCHEDULER;
struct Power { void setHallEnabled(bool enabled) { hall=enabled; } } BOARD_POWER;
struct CalibrationState { bool isRunning=true; uint32_t rebootTime=100; void exit(); };
'''
        harness += function(source, "void CalibrationState::exit()")
        harness += r'''
int main() {
    CalibrationState state; state.exit();
    assert(!state.isRunning && state.rebootTime==0 && !hall && sampleStops==1 && callbackCalls==0);
}
'''
        with tempfile.TemporaryDirectory(prefix="xora-battery-calibration-") as folder:
            cpp, exe = Path(folder) / "cancel.cpp", Path(folder) / "cancel.exe"
            cpp.write_text(harness, encoding="utf-8")
            run_native([shutil.which("g++"), "-std=c++17", "-Wall", "-Wextra",
                        "-Wno-unused-but-set-variable", "-Werror", str(cpp), "-o", str(exe)], check=True)
            run_native([str(exe)], check=True)

    def test_production_battery_icon_states(self):
        source = (ROOT / "application/Src/display/screen_control/spi_screen_manager.cpp").read_text(encoding="utf-8")
        harness = r'''
#include <cassert>
#include <cstdio>
#include <string>
#include "power_manager.hpp"
struct ST7789_Handle {};
constexpr unsigned SPI_SCREEN_BATTERY_BODY_H=14;
bool g_battUiLowBattery=false, g_battUiFastCharging=false;
uint16_t g_battUiCellMv=0;
bool (*BatteryTest_Active)()=nullptr;
bool (*BatteryTest_IsCharge)()=nullptr;
constexpr unsigned SCREEN_FONT_SMALL=1;
unsigned ScreenUI_CharCellH(unsigned) { return 12; }
uint8_t g_battUiSoc=50;
uint32_t g_battUiChargeAnimStartMs=0;
uint32_t g_battUiLastSampleMs=0;
PowerChargeState g_battUiChargeState=PowerChargeState::Discharging;
PowerDataQuality g_battUiQuality=PowerDataQuality::Fresh;
#undef POWER_MANAGER
struct FakePower { PowerSnapshot sample; PowerSnapshot getSnapshot() { return sample; } } fakePower;
#define POWER_MANAGER fakePower
unsigned textCalls=0, fill=0, borders=0;
void ST7789_DrawRect(ST7789_Handle*,unsigned,unsigned,unsigned,unsigned,unsigned) { ++borders; }
void ST7789_FillRect(ST7789_Handle*,unsigned,unsigned,unsigned width,unsigned,unsigned color) { if(color) fill=width; }
void ScreenUI_DrawStringCenteredInBox(ST7789_Handle*,unsigned x,unsigned y,unsigned w,unsigned h,const char*,unsigned,unsigned,unsigned) { assert(x+w<=36 && y+h<=172); ++textCalls; }
'''
        harness += function(source, "static void update_battery_ui_cache(")
        harness += function(source, "static uint8_t battery_animated_soc(")
        harness += function(source, "static void render_left_battery_icon(")
        harness += r'''
int main() {
    ST7789_Handle lcd;
    const auto render=[&](uint32_t now) {
        fill=borders=textCalls=0;
        render_left_battery_icon(&lcd,36,140,1,0,now);
        assert(textCalls==0);
    };
    render(0); assert(fill==11);
    g_battUiQuality=PowerDataQuality::Unknown; render(0); assert(fill==0);
    g_battUiQuality=PowerDataQuality::Stale; render(0); assert(fill==11);
    g_battUiQuality=PowerDataQuality::Fresh;
    g_battUiChargeState=PowerChargeState::Full; render(0); assert(fill==11);
    g_battUiChargeState=PowerChargeState::Fault; render(0); assert(fill==11);
    g_battUiChargeState=PowerChargeState::Charging; render(500); assert(fill>11);
    const auto normalFill=fill;
    g_battUiFastCharging=true; render(500); assert(fill>normalFill);
    g_battUiQuality=PowerDataQuality::Stale; render(500); assert(fill==11);
    g_battUiLowBattery=true; render(700); assert(borders==0);
    render(1100); assert(borders==2);
    g_battUiQuality=PowerDataQuality::Fresh;
    render(1100); assert(textCalls==0);
    fakePower.sample.soc_permille=420;
    fakePower.sample.data_quality=PowerDataQuality::Fresh;
    update_battery_ui_cache(2000); assert(g_battUiSoc==42);
    fakePower.sample.data_quality=PowerDataQuality::Unknown;
    update_battery_ui_cache(2001); render(2001);
    assert(g_battUiQuality==PowerDataQuality::Unknown && fill==0); // validity expiry must bypass the one-second UI cache
    g_battUiQuality=PowerDataQuality::Fresh;
    BatteryTest_Active=+[](){return true;};
    textCalls=0; render_left_battery_icon(&lcd,36,141,1,0,2100);
    assert(textCalls==1); // Only test voltage is drawn; it fits within the actual 172px screen.
}
'''
        with tempfile.TemporaryDirectory(prefix="xora-battery-icon-") as folder:
            temp = Path(folder)
            (temp / "stm32h7xx_hal.h").write_text(
                "#pragma once\n#include <stdint.h>\ntypedef struct {} I2C_HandleTypeDef;\n", encoding="utf-8")
            cpp, exe = temp / "icon.cpp", temp / "icon.exe"
            cpp.write_text(harness, encoding="utf-8")
            run_native([shutil.which("g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I", str(temp), *application_include_flags(), str(cpp), "-o", str(exe)], check=True)
            run_native([str(exe)], check=True)

    def test_board_load_inhibit_preserves_charge_and_display(self):
        source = (ROOT / "application/Src/power/board_power.cpp").read_text(encoding="utf-8")
        harness = r'''
#include <cassert>
#include "board_power.hpp"
constexpr unsigned CHARGE_EN_N_PORT=0, CHARGE_EN_N_PIN=0, HALL_VCC_EN_PORT=0, HALL_VCC_EN_PIN=1;
constexpr unsigned BOOST_5V_EN_PORT=0, BOOST_5V_EN_PIN=2, LED_EN_PORT=0, LED_EN_PIN=3;
constexpr unsigned AMBIENT_EN_PORT=0, AMBIENT_EN_PIN=4, LCD_EN_PORT=0, LCD_EN_PIN=5;
constexpr unsigned CH585_EN_PORT=0, CH585_EN_PIN=6, USB_HOST_EN_PORT=0, USB_HOST_EN_PIN=7;
constexpr unsigned BOARD_LED_5V_STABILIZE_MS=1;
bool pins[8]={};
void writePin(unsigned, unsigned pin, bool value) { pins[pin]=value; }
void HAL_Delay(unsigned) {}
'''
        for signature in ("void BoardPower::setChargeEnabled(", "void BoardPower::setHallEnabled(",
                          "void BoardPower::setKeyLedEnabled(", "void BoardPower::setAmbientLedEnabled(",
                          "void BoardPower::setLcdEnabled(", "void BoardPower::setCh585Enabled(",
                          "bool BoardPower::setUsbHostEnabled(", "void BoardPower::setLedBoostEnabled(",
                          "void BoardPower::refreshLedBoost("):
            harness += function(source, signature)
        harness += r'''
int main() {
    auto& p=BOARD_POWER;
    p.setCh585Enabled(true); assert(p.setUsbHostEnabled(true));
    p.setKeyLedEnabled(true); p.setAmbientLedEnabled(true); p.setHallEnabled(true);
    p.setBatteryLoadInhibited(true);
    // Latching alone never cuts a DMA owner's existing supply.
    assert(p.isCh585Enabled() && p.isHallEnabled() && pins[2]);
    p.setCh585Enabled(false); p.setHallEnabled(false);
    p.setKeyLedEnabled(false); p.setAmbientLedEnabled(false); assert(!pins[2]);
    p.setCh585Enabled(true); p.setHallEnabled(true);
    p.setKeyLedEnabled(true); p.setAmbientLedEnabled(true);
    assert(!p.setUsbHostEnabled(true));
    for(unsigned pin:{1u,2u,3u,4u,6u,7u}) assert(!pins[pin]);
    p.setChargeEnabled(true); p.setLcdEnabled(true);
    assert(p.isChargeEnabled() && !pins[0] && pins[5]);
    p.setBatteryLoadInhibited(false); p.setHallEnabled(true); assert(pins[1]);
}
'''
        # Only the selected production GPIO setters are linked, with fake pins.
        with tempfile.TemporaryDirectory(prefix="xora-battery-rails-") as folder:
            cpp, exe = Path(folder) / "rails.cpp", Path(folder) / "rails.exe"
            cpp.write_text("#include <initializer_list>\n" + harness, encoding="utf-8")
            run_native([shutil.which("g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        *application_include_flags(), str(cpp), "-o", str(exe)], check=True)
            run_native([str(exe)], check=True)

    def test_battery_and_notice_policies(self):
        with tempfile.TemporaryDirectory(prefix="xora-battery-policy-") as folder:
            exe = Path(folder) / "policy.exe"
            run_native([shutil.which("g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        *application_include_flags(),
                        str(ROOT / "tools/tests/battery_policy_test.cpp"), "-o", str(exe)], check=True)
            run_native([str(exe)], check=True)

    def test_production_power_manager(self):
        with tempfile.TemporaryDirectory(prefix="xora-battery-") as folder:
            temp = Path(folder)
            for name in ("board_cfg.h", "board_mode.hpp", "board_power.hpp",
                         "ch585_role_bootstrap.hpp", "connection_manager.hpp",
                         "storagemanager.hpp", "system_sleep_manager.hpp"):
                (temp / name).write_text("#pragma once\n", encoding="utf-8")
            (temp / "stm32h7xx_hal.h").write_text(
                '#pragma once\n#include <stdint.h>\n'
                'typedef struct {} I2C_HandleTypeDef;\n', encoding="utf-8")
            exe = temp / "battery.exe"
            run_native([shutil.which("g++"), "-std=c++17", "-Wall", "-Wextra",
                        "-Wno-missing-field-initializers", "-Werror",
                        "-I", str(temp), "-I", str(ROOT / "common"), *application_include_flags(),
                        str(ROOT / "tools/tests/battery_manager_test.cpp"), "-o", str(exe)],
                       check=True)
            run_native([str(exe)], check=True)

    def test_external_supply_load_policy(self):
        with tempfile.TemporaryDirectory(prefix="xora-power-load-") as folder:
            exe = Path(folder) / "load.exe"
            run_native([shutil.which("g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        *application_include_flags(),
                        str(ROOT / "tools/tests/power_load_policy_test.cpp"), "-o", str(exe)], check=True)
            run_native([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
