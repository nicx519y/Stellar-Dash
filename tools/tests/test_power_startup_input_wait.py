"""Exercise the production power initializer with scripted charger readings."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools.tests.test_ch585_usb_handshake import function

ROOT = Path(__file__).resolve().parents[2]


class PowerStartupInputWaitTest(unittest.TestCase):
    def test_input_absence_adc_wait_and_failures(self):
        source = (ROOT / 'application/Src/power/power_manager.cpp').read_text(encoding='utf-8')
        harness = r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include <algorithm>
using I2C_HandleTypeDef = int;
enum BQ25895_InputProfile { BQ25895_INPUT_PROFILE_5V_1P5A, BQ25895_INPUT_PROFILE_9V_1P5A };
struct Handle { bool online=true; };
struct BQ25895_State { bool vbus_good=false, power_good=false; uint8_t vbus_status=0; uint16_t vbus_mv=2600; };
constexpr uint32_t kInitialAdcTimeoutMs=1200, kInitialAdcPollMs=20;
constexpr uint16_t kInputProfile9vThresholdMv=7000;
constexpr uint8_t kGaugeAlertSocPercent=10;
uint32_t now=0, waited=0;
unsigned reads=0, configured=0, verified=0;
bool readOk=true, verifyOk=true, chargeEnabled=true;
BQ25895_InputProfile profile=BQ25895_INPUT_PROFILE_9V_1P5A;
std::vector<BQ25895_State> samples;
uint32_t HAL_GetTick() { return now; }
void HAL_Delay(uint32_t ms) { now+=ms; waited+=ms; }
I2C_HandleTypeDef* PowerI2C_GetHandle() { return nullptr; }
bool BQ25895_Init(Handle*,I2C_HandleTypeDef*) { return true; }
bool BQ25895_EnableContinuousAdc(Handle*) { return true; }
bool BQ25895_ReadState(Handle*,BQ25895_State* state) {
    assert(!chargeEnabled);
    *state=samples[std::min<size_t>(reads++,samples.size()-1)]; return readOk;
}
bool BQ25895_ConfigureSafeProfile(Handle*,BQ25895_InputProfile p) { ++configured; profile=p; return true; }
bool BQ25895_VerifySafeProfile(Handle*) { ++verified; return verifyOk; }
bool MAX17048_Init(Handle*,I2C_HandleTypeDef*) { return true; }
bool MAX17048_ConfigureAlert(Handle*,uint8_t) { return true; }
#define APP_STAGE(...) ((void)0)
struct PowerManager {
    Handle charger_, gauge_;
    bool profile_valid_=false;
    uint32_t last_profile_check_ms_=0;
    void setChargingEnabled(bool enabled) { chargeEnabled=enabled; }
    bool initializeDevices();
};
void reset(std::vector<BQ25895_State> next) {
    now=waited=reads=configured=verified=0; readOk=verifyOk=chargeEnabled=true; samples=next;
}
'''
        cases = r'''
int main() {
    const BQ25895_State absent{}, pending{true,false,0,2600}, five{true,true,3,5000}, nine{true,true,3,9000};
    PowerManager p;
    reset({absent}); assert(p.initializeDevices());
    assert(reads==1 && waited==0 && configured==1 && verified==1 && !chargeEnabled);
    assert(profile==BQ25895_INPUT_PROFILE_5V_1P5A);
    reset({pending,pending,nine}); assert(p.initializeDevices());
    assert(reads==3 && waited==40 && profile==BQ25895_INPUT_PROFILE_9V_1P5A && !chargeEnabled);
    reset({five}); assert(p.initializeDevices() && waited==0 && profile==BQ25895_INPUT_PROFILE_5V_1P5A);
    reset({pending}); assert(p.initializeDevices() && waited==1200 && !chargeEnabled);
    // Other presence indicators must also preserve the ADC warm-up budget.
    reset({{false,true,0,2600},five}); assert(p.initializeDevices() && waited==20);
    reset({{false,false,3,2600},nine}); assert(p.initializeDevices() && waited==20);
    reset({absent}); readOk=false; assert(!p.initializeDevices());
    assert(!p.profile_valid_ && configured==0 && !p.charger_.online && !chargeEnabled);
    reset({nine}); verifyOk=false; assert(!p.initializeDevices());
    assert(!p.profile_valid_ && verified==1 && !chargeEnabled);
    reset({pending}); now=UINT32_MAX-10; assert(p.initializeDevices() && waited==1200);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            cpp, exe = folder / 'power_wait.cpp', folder / 'power_wait.exe'
            cpp.write_text(harness + function(source, 'BQ25895_InputProfile inputProfileForVbus(')
                           + function(source, 'bool PowerManager::initializeDevices()') + cases,
                           encoding='utf-8')
            print('Compile/run power startup: absent input, ADC warmup, 5V/9V, I/O/profile failures, tick wrap', flush=True)
            subprocess.run([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            str(cpp), '-o', str(exe)], check=True, timeout=60)
            subprocess.run([str(exe)], check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
