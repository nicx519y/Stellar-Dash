#include "battery_test_runtime_stubs.hpp"
#include "battery_test.hpp"
#include <cassert>
#include <cstdio>

static bool dmaBusy=false, dmaComplete=false, jpegActive=false;
static bool upgrade=false, isp=false, mounted=false, adcStopped=false;
static unsigned pauses=0, resumes=0, frames=0;
void SPIST7789_Service() { if(dmaComplete) { dmaBusy=false; dmaComplete=false; } }
bool SPIST7789_IsBusy() { return dmaBusy; }
bool ScreenJpeg_Active() { return jpegActive; }
struct SPIScreenManager {
    static SPIScreenManager& getInstance() { static SPIScreenManager s;return s; }
    void loop() {
        SPIST7789_Service();
        if(!dmaBusy) { dmaBusy=true; ++frames; } // continuous refresh, no idle gap
    }
};
enum class MainRuntimeState { Input,WebConfig,BatteryProtection,SafeRecovery,TxIsp };
struct MainStateMachine {
    bool interactiveRuntimeInitialized=true,resetPending=false;
    MainRuntimeState currentState=MainRuntimeState::Input;
    bool ispEntryBlocked() { return isp; }
    void serviceBatteryProtection() {}
    bool requestTransition(MainRuntimeState) { assert(false);return false; }
    void serviceSharedRuntime();
};
struct Connection { void loop() {} } CONNECTION_MANAGER;
struct Usb { bool isDeviceMounted() { return mounted; } } USB_BOARD_LINK;
struct FirmwareManager {
    static FirmwareManager* GetInstance() { static FirmwareManager f;return &f; }
    bool HasPendingUpgradeWrites() { return upgrade; }
};
struct Protection { bool storageReady() { return true; } } BATTERY_PROTECTION_STATE;
struct Input {
    bool suspendInputPipelineForStorage() { assert(!dmaBusy && !jpegActive);adcStopped=true;++pauses;return true; }
    bool resumeInputPipelineAfterStorage(bool running) { assert(running);adcStopped=false;++resumes;return true; }
    void serviceLeds() {}
} INPUT_STATE;
struct Adc { bool isSamplingHardwareStopped() { return adcStopped; } } ADC_MANAGER;
void SystemSleep_Service(bool,bool) {}
extern "C" void XoraLog_Tick(uint32_t) {}
extern "C" void XoraLog_Service(int) {}
extern "C" void XoraLog_Info(fl_info* i) { *i={};i->newest=7;i->firmware=0x010203; }
extern "C" uint32_t XoraLog_CurrentBoot() { return 42; }
extern "C" void XoraLog_Fatal(uint32_t) { assert(false); }
#define APP_STAGE(...) ((void)0)
#include "battery_dispatch_production.inc"

static void drain() { for(unsigned i=0;i<32 && BatteryTest_NeedsStorage();++i) BatteryTest_Service(1); }
static void history600() {
    memset(flashMemory,255,sizeof(flashMemory));fakePower={};tick=0;
    BatteryTest_Init();BatteryTest_RequestStart();drain();
    tick=600000;BatteryTest_RequestEnd();BatteryTest_Tick();drain();BatteryTest_Init();
    assert(BatteryTest_Elapsed()==600000 && !BatteryTest_Active());
    BatteryTest_RequestClear();eraseCalls=pauses=resumes=frames=0;
    dmaBusy=true;dmaComplete=false;jpegActive=false;upgrade=isp=mounted=adcStopped=false;
}
int main() {
    MainStateMachine main;
    history600();dmaComplete=true;
    main.serviceSharedRuntime();
    assert(BatteryTest_Elapsed()==0 && !strcmp(BatteryTest_Status(),"Records cleared"));
    assert(eraseCalls==16 && pauses==1 && resumes==1 && frames==1 && dmaBusy);
    // Real DMA is still running: do not erase until its completion is serviced.
    history600();main.serviceSharedRuntime();assert(eraseCalls==0 && pauses==0);
    dmaComplete=true;main.serviceSharedRuntime();assert(BatteryTest_Elapsed()==0);
    // An active decoder and external storage owners keep their safety gates.
    for(unsigned gate=0;gate<3;++gate) {
        history600();dmaComplete=true;
        if(gate==0) jpegActive=true;
        if(gate==1) upgrade=true;
        if(gate==2) isp=true;
        main.serviceSharedRuntime();assert(eraseCalls==0 && pauses==0);
        jpegActive=upgrade=isp=false;dmaComplete=true;
        main.serviceSharedRuntime();assert(BatteryTest_Elapsed()==0 && eraseCalls==16);
    }
    history600();main.currentState=MainRuntimeState::WebConfig;mounted=true;dmaComplete=true;
    main.serviceSharedRuntime();assert(eraseCalls==0 && pauses==0);
    main.currentState=MainRuntimeState::Input;mounted=false;dmaComplete=true;
    main.serviceSharedRuntime();assert(BatteryTest_Elapsed()==0);
    assert(!main.resetPending);
    puts("production dispatch: 600s history cleared during continuous frames, DMA/decoder/upgrade/ISP/USB gates retained, no reset");
}
