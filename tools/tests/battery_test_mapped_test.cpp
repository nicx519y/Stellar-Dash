// Exercise production pointer reads with a real host virtual-memory aperture.
// During indirect operations that aperture is inaccessible, catching accidental
// mapped reads that the ordinary byte-array backend cannot detect.
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "battery_test_runtime_stubs.hpp"
#include "battery_test.hpp"
#include <cassert>
#include <cstdio>

static void *aperture;
static unsigned invalidationsAtExit=0;
static void protect(bool readable) {
#ifdef _WIN32
    DWORD previous;
    assert(VirtualProtect(aperture,sizeof(flashMemory),readable?PAGE_READWRITE:PAGE_NOACCESS,&previous));
#else
    assert(mprotect(aperture,sizeof(flashMemory),readable?PROT_READ|PROT_WRITE:PROT_NONE)==0);
#endif
}
static void transition(bool mapped) {
    if(!mapped) { invalidationsAtExit=cacheInvalidations;protect(false);return; }
    assert(cacheInvalidations>invalidationsAtExit);
    protect(true);
    memcpy(aperture,flashMemory,sizeof(flashMemory));
}
static void wait() { assert(false); } // Rendering callbacks must stay disabled during erase.
extern "C" void XoraLog_Info(fl_info *i) { *i={};i->newest=7;i->firmware=0x010203; }
extern "C" uint32_t XoraLog_CurrentBoot() { return 42; }
extern "C" void XoraLog_Fatal(uint32_t) { assert(false); }
static void drain() { for(unsigned i=0;i<32 && BatteryTest_NeedsStorage();++i)BatteryTest_Service(1); }
static void history() {
    mappedMode=false;mappingTransition=nullptr;scb.CCR=0;
    memset(flashMemory,255,sizeof(flashMemory));fakePower={};tick=0;
    BatteryTest_Init();BatteryTest_RequestStart();drain();
    tick=600000;BatteryTest_RequestEnd();BatteryTest_Tick();drain();
    protect(true);memcpy(aperture,flashMemory,sizeof(flashMemory));
    mappedMode=true;mappingTransition=transition;scb.CCR=SCB_CCR_DC_Msk;
    BatteryTest_Init();assert(BatteryTest_Elapsed()==600000);
    waitCallback=wait;mappingExits=mappingEnters=eraseCalls=cacheInvalidations=0;
}
int main() {
    auto *address=reinterpret_cast<void *>(uintptr_t(EXTERNAL_FLASH_BASE));
#ifdef _WIN32
    aperture=VirtualAlloc(address,sizeof(flashMemory),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
#else
    aperture=mmap(address,sizeof(flashMemory),PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
#endif
    assert(aperture==address);
    history();BatteryTest_RequestClear();BatteryTest_Service(1);
    assert(!qspiAddressError && mappedMode && mappingExits==1 && mappingEnters==1);
    assert(eraseCalls==16 && cacheInvalidations==1 && waitCallback==wait);
    assert(BatteryTest_Elapsed()==0 && !strcmp(BatteryTest_Status(),"Records cleared"));
    for(unsigned i=0;i<BT_REGION_BYTES;++i)
        assert(static_cast<uint8_t *>(aperture)[BATTERY_TEST_STORAGE_ADDR-EXTERNAL_FLASH_BASE+i]==255);
    BatteryTest_Init();assert(BatteryTest_Elapsed()==0 && !strcmp(BatteryTest_Status(),"Not started"));
    // A recoverable erase failure restores mapping/callbacks and preserves history.
    history();eraseFailure=1;BatteryTest_RequestClear();BatteryTest_Service(1);eraseFailure=0;
    assert(mappedMode && waitCallback==wait && !qspiAddressError);
    assert(BatteryTest_Elapsed()==600000 && !strcmp(BatteryTest_Status(),"Clear failed / retry"));
    BatteryTest_RequestClear();BatteryTest_Service(1);
    assert(mappedMode && BatteryTest_Elapsed()==0 && !strcmp(BatteryTest_Status(),"Records cleared"));
    puts("mapped battery clear: chip offsets, inaccessible indirect aperture, cache/callback restoration, persisted zero and failure retry passed");
}
