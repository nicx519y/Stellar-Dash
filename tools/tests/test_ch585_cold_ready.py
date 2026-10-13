"""Exercise the production passive ready observer with delayed/missing pulses."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools.tests.test_ch585_usb_handshake import function

ROOT = Path(__file__).resolve().parents[2]


class ColdReadyTest(unittest.TestCase):
    def test_passive_observer(self):
        production = (ROOT/'application/Src/transport/rf/rf_boot_ready.cpp').read_text(encoding='utf-8')
        prefix = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include "ch585_startup_trace.hpp"
#define RF_BOOT_READY_STABLE_MS 2u
#define RF_BOOT_READY_SETTLE_MS 10u
#define RF_BOOT_READY_DEASSERT_TIMEOUT_MS 250u
#define RF_BRIDGE_IRQ_GPIO_PORT 0
#define RF_BRIDGE_IRQ_PIN 0
#define RF_BRIDGE_IRQ_ASSERTED_STATE 0
#define RF_BRIDGE_IRQ_DEASSERTED_STATE 1
uint32_t now=0, origin=0, pulseAt=900, releaseAt=1000;
uint32_t HAL_GetTick() { return now; }
void HAL_Delay(uint32_t ms) { now+=ms+1; }
void configureReadyInputPullup() {}
int HAL_GPIO_ReadPin(int,int) {
    const auto elapsed=now-origin;
    // A short power-up glitch must not satisfy the full ready observation.
    if(elapsed==20) return 0;
    return elapsed>=pulseAt && elapsed<releaseAt ? 0 : 1;
}
namespace RFBootReady {
bool s_readySeen=false;
'''
        main = r'''
}
int main(int argc,char** argv) {
    assert(argc==2);
    const bool shortBudget=!std::strcmp(argv[1],"old-budget");
    const bool missing=!std::strcmp(argv[1],"missing");
    const bool stuck=!std::strcmp(argv[1],"stuck-low");
    if(!std::strcmp(argv[1],"wrap")) now=origin=0xffffff80u;
    if(missing) pulseAt=releaseAt=3000;
    if(stuck) releaseAt=3000;
    Ch585StartupTrace::powerOn(now,1);
    const bool ready=RFBootReady::waitForModuleReady(shortBudget ? 700u : 2000u);
    assert(ready==(!shortBudget && !missing && !stuck));
    if(ready) {
        assert(now-origin>=1010 && now-origin<1050);
        const auto before=now;
        assert(RFBootReady::waitForModuleReady(2000) && now==before);
    } else if(shortBudget) assert(now-origin>=700 && now-origin<710);
    else if(missing) assert(now-origin>=2000 && now-origin<2010);
    else assert(now-origin>=1150 && now-origin<1200);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp); cpp=folder/'ready.cpp'; exe=folder/'ready.exe'
            cpp.write_text(prefix+function(production,'bool waitForModuleReady(')+main,encoding='utf-8')
            print('Compile/run passive ready observer: delayed, absent, stuck and tick-wrap',flush=True)
            subprocess.run([shutil.which('g++'),'-std=c++17','-Wall','-Wextra','-Werror',
                            '-I'+str(ROOT/'application/Inc/transport'),str(cpp),'-o',str(exe)],
                           check=True,timeout=60)
            for case in ('late-ready','old-budget','missing','stuck-low','wrap'):
                with self.subTest(case=case):
                    subprocess.run([str(exe),case],check=True,timeout=5)


if __name__=='__main__': unittest.main()
