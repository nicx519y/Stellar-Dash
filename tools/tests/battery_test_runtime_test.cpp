#include "battery_test_runtime_stubs.hpp"
#include "battery_test.hpp"
#include <cassert>
#include <cstdio>
static int readWhole(uint32_t a,void *p,uint32_t n){return QSPI_W25Qxx_ReadBuffer((uint8_t*)p,BATTERY_TEST_STORAGE_ADDR+a,n);}
static int writeWhole(uint32_t a,const void *p,uint32_t n){return QSPI_W25Qxx_WritePage((uint8_t*)p,BATTERY_TEST_STORAGE_ADDR-EXTERNAL_FLASH_BASE+a,n);}
static int noErase(uint32_t){assert(false);return -1;}
static int reserveFixture() {
    const fl_backend b={readWhole,writeWhole,noErase,65536};
    return bt_reserve_region(&b);
}
extern "C" void XoraLog_Info(fl_info *i){*i={};i->newest=7;i->firmware=0x010203;}
extern "C" uint32_t XoraLog_CurrentBoot(){return 42;}
extern "C" void XoraLog_Fatal(uint32_t){assert(false);}
static void drain(){for(int i=0;i<32 && BatteryTest_NeedsStorage();i++)BatteryTest_Service(1);}
static void sample() {tick+=1001;fakePower.sample.sampled_at_ms=tick;BatteryTest_Tick();drain();}
static fl_record latest(){
    fl_record r;assert(BatteryTest_Read(UINT32_MAX,0,0,0,0,&r)==FL_OK);return r;
}
static unsigned eventCount(uint16_t event) {
    unsigned count=0;uint32_t before=0;fl_record r;
    while(BatteryTest_Read(UINT32_MAX,before,0,0,0,&r)==FL_OK) {
        before=r.sequence;if(r.event==event)++count;
        if(r.event==BAT_SAMPLE_SUMMARY)assert(r.data[9]==0);
    }
    return count;
}
static void reset(){
    memset(flashMemory,255,sizeof(flashMemory));writeFailure=readFailure=readCalls=0;fakePower={};fakeStorage={};tick=0;ordinarySleep=false;
    BatteryTest_Init();
}
int main(){
    reset();BatteryTest_RequestStart();assert(BatteryTest_Active());
    BatteryTest_Service(0);assert(flashMemory[(BATTERY_TEST_STORAGE_ADDR-EXTERNAL_FLASH_BASE)]==255);
    drain();sample();assert(latest().boot==42);
    assert(std::strstr(BatteryTest_Status(),"Waiting"));
    fakePower.sample.vbus_present=false;fakePower.sample.charge_block=PowerChargeBlock::NoInput;
    sample();assert(std::strstr(BatteryTest_Status(),"Recording"));
    tick+=BT_SUMMARY_MS;fakePower.sample.sampled_at_ms=tick;BatteryTest_Tick();drain();
    fakePower.sample.battery_level=BatteryLevel::Critical;sample();
    assert(BatteryTest_Active() && std::strstr(BatteryTest_Status(),"Protection"));
    fakePower.sample.vbus_present=true;fakePower.sample.battery_level=BatteryLevel::RecoveryReady;sample();
    assert(BatteryTest_Active());
    fakePower.sample.battery_level=BatteryLevel::Low;sample();
    assert(!BatteryTest_Active());assert(latest().event==BAT_TEST_END && latest().data[2]==BT_COMPLETED);
    BatteryTest_Init();fl_info info;BatteryTest_GetInfo(&info);assert(info.status==FL_OK && info.count>0);
    BatteryTest_RequestStart();assert(!BatteryTest_Active()); // don't delete a completed session
    reset();BatteryTest_RequestStart();drain();sample();
    ordinarySleep=true;BatteryTest_Tick();tick+=1000;BatteryTest_Sleep(1000,0,1,0);BatteryTest_Tick();
    ordinarySleep=false;BatteryTest_Tick();
    BatteryTest_Sleep(500,1,1,0);BatteryTest_Sleep(0,1,0,256);
    BatteryTest_RequestEnd();BatteryTest_Tick();drain();
    const fl_record end=latest();assert(end.data[5]==1000);
    fl_record sleep;assert(BatteryTest_Read(UINT32_MAX,end.sequence,0,0,0,&sleep)==FL_OK);
    assert(sleep.event==BAT_SLEEP_SUMMARY && sleep.words==14);
    assert(sleep.data[8]==1 && sleep.data[9]==1000 && sleep.data[10]==1 && sleep.data[11]==500);
    assert(sleep.data[12]==0 && sleep.data[13]==1);
    reset();BatteryTest_RequestStart();drain();sample();
    BatteryTest_Init();assert(!BatteryTest_Active());drain();
    assert(latest().event==BAT_TEST_END && latest().data[2]==BT_REBOOT);
    BatteryTest_GetInfo(&info);assert(info.status==FL_IO);
    reset();assert(reserveFixture());BatteryTest_Init();drain();
    assert(!BatteryTest_Active() && latest().event==BAT_TEST_END && latest().data[2]==BT_REBOOT);
    reset();flashMemory[(BATTERY_TEST_STORAGE_ADDR-EXTERNAL_FLASH_BASE)]=uint8_t(BT_MAGIC);
    BatteryTest_Init();BatteryTest_GetInfo(&info);
    assert(info.status==FL_IO && !BatteryTest_NeedsStorage());
    reset();BatteryTest_RequestStart();drain();sample();
    fakeStorage.p.ledsConfigs.ledBrightness=45;sample();
    assert(latest().event==BAT_CONFIG_CHANGED);
    writeFailure=1;BatteryTest_Event(BAT_PROTECTION_ENTER,1,0);drain();
    BatteryTest_GetInfo(&info);assert(info.status==FL_IO && !BatteryTest_Active());
    assert(info.pending>0); // evidence remains readable in RAM; failure never retries forever
    reset();flashMemory[(BATTERY_TEST_STORAGE_ADDR-EXTERNAL_FLASH_BASE)+1000]=0;
    BatteryTest_RequestStart();drain();assert(!BatteryTest_Active());
    assert(flashMemory[(BATTERY_TEST_STORAGE_ADDR-EXTERNAL_FLASH_BASE)+1000]==0);
    reset();BatteryTest_RequestStart();tick=10001;BatteryTest_Tick();
    assert(!BatteryTest_Active());assert(std::strstr(BatteryTest_Status(),"Busy"));
    reset();BatteryTest_RequestStart();fakePower.sample.battery_level=BatteryLevel::Critical;
    drain();assert(!BatteryTest_Active());assert(flashMemory[(BATTERY_TEST_STORAGE_ADDR-EXTERNAL_FLASH_BASE)]==255);
    reset();
    // Existing declared image extents protect even an erased-looking tail.
    uint8_t *image=flashMemory+XORA_IMAGE_DATA_ADDR-EXTERNAL_FLASH_BASE;
    const uint32_t magic=0x474d4955u, offset=4096, oversized=XORA_IMAGE_DATA_BYTES;
    memcpy(image,&magic,4);image[4]=5;image[5]=0;image[6]=1;
    memcpy(image+20,&offset,4);memcpy(image+24,&oversized,4);
    BatteryTest_RequestStart();drain();assert(!BatteryTest_Active());
    assert(flashMemory[BATTERY_TEST_STORAGE_ADDR-EXTERNAL_FLASH_BASE]==255);
    reset();memset(flashMemory+FAULT_LOG_STORAGE_ADDR-EXTERNAL_FLASH_BASE,0x5a,FAULT_LOG_STORAGE_SIZE);
    BatteryTest_RequestStart();drain();sample();BatteryTest_RequestEnd();BatteryTest_Tick();drain();
    for(unsigned i=0;i<FAULT_LOG_STORAGE_SIZE;i++)assert(flashMemory[FAULT_LOG_STORAGE_ADDR-EXTERNAL_FLASH_BASE+i]==0x5a);
    assert(flashMemory[CH585_FIRMWARE_STAGING_ADDR-EXTERNAL_FLASH_BASE]==255);
    // A normal five-second STOP maintenance interval is deliberate, not a
    // telemetry outage that should consume two quality events each wake.
    reset();BatteryTest_RequestStart();drain();sample();
    fakePower.sample.vbus_present=false;sample();
    ordinarySleep=true;BatteryTest_Tick();drain();
    const auto eventsBeforeSleep=eventCount(BAT_DATA_QUALITY_CHANGED);
    for(unsigned i=0;i<80;i++) {
        tick+=5000;BatteryTest_Sleep(5000,0,1,0);BatteryTest_Tick();drain();
        sample();
    }
    assert(eventCount(BAT_DATA_QUALITY_CHANGED)==eventsBeforeSleep);
    BatteryTest_GetInfo(&info);assert(info.dropped==0 && info.status==FL_OK);
    // An unaccounted awake sampling gap still produces an anomaly.
    ordinarySleep=false;BatteryTest_Tick();drain();
    tick+=4000;fakePower.sample.sampled_at_ms=tick;BatteryTest_Tick();drain();
    assert(latest().event==BAT_DATA_QUALITY_CHANGED && (latest().data[2] & 512u));
    // A failed journal scan must not repeat the driver's timeout for all 480
    // slots. Stop physical reads after the first error in this storage window.
    reset();BatteryTest_RequestStart();drain();sample();
    BatteryTest_Event(BAT_PROTECTION_ENTER,1,0);
    readFailure=1;readCalls=0;drain();
    assert(readCalls==1 && !BatteryTest_Active() && !BatteryTest_NeedsStorage());
    drain();assert(readCalls==1);
    BatteryTest_GetInfo(&info);assert(info.status==FL_IO && readCalls==2);
    fl_record unreadable;
    assert(BatteryTest_Read(UINT32_MAX,0,0,0,0,&unreadable)==FL_IO && readCalls==3);
    reset();eraseCalls=0;BatteryTest_RequestStart();drain();sample();
    assert(BatteryTest_Clear()==FL_FULL && eraseCalls==0);
    BatteryTest_RequestEnd();BatteryTest_Tick();drain();
    const unsigned begin=BATTERY_TEST_STORAGE_ADDR-EXTERNAL_FLASH_BASE;
    flashMemory[begin-1]=0x12;flashMemory[begin+BT_REGION_BYTES]=0x34;
    eraseFailure=1;assert(BatteryTest_Clear()==FL_IO);eraseFailure=0;
    assert(BatteryTest_Clear()==FL_OK && fl_blank(flashMemory+begin,BT_REGION_BYTES));
    assert(flashMemory[begin-1]==0x12 && flashMemory[begin+BT_REGION_BYTES]==0x34);
    BatteryTest_GetInfo(&info);assert(info.count==0 && info.pending==0 && info.dropped==0);
    BatteryTest_RequestStart();assert(BatteryTest_Active()); // explicit clear permits a new test
    reset();eraseCalls=0;
    flashMemory[XORA_IMAGE_DATA_ADDR-EXTERNAL_FLASH_BASE]=0; // unknown image extent must not be erased
    assert(BatteryTest_Clear()==FL_IO && eraseCalls==0);
    reset();eraseCalls=0;
    const uint32_t legacy=USER_IMAGE_RESOURCES_ADDR+0xD8000u;
    uint8_t *prefix=flashMemory+legacy-EXTERNAL_FLASH_BASE;
    const uint32_t legacyMagic=0x474d4955u,legacyOffset=4096u,oversize=BATTERY_TEST_STORAGE_ADDR-legacy;
    const uint16_t version=5;
    memcpy(prefix,&legacyMagic,4);memcpy(prefix+4,&version,2);prefix[6]=1;
    memcpy(prefix+20,&legacyOffset,4);memcpy(prefix+24,&oversize,4);
    assert(BatteryTest_Clear()==FL_IO && eraseCalls==0); // declared legacy extent crosses log boundary

    // Screen clear is deferred until the same safe storage window as recording.
    reset();eraseCalls=0;
    BatteryTest_RequestStart();drain();sample();
    BatteryTest_RequestClear();assert(eraseCalls==0 && BatteryTest_Active());
    BatteryTest_RequestEnd();BatteryTest_Tick();drain();
    flashMemory[begin-1]=0x12;flashMemory[begin+BT_REGION_BYTES]=0x34;
    BatteryTest_RequestClear();assert(eraseCalls==0 && BatteryTest_NeedsStorage());
    BatteryTest_RequestStart();ChargeTest_RequestStart();assert(!BatteryTest_Active());
    assert(BatteryTest_Clear()==FL_FULL); // competing synchronous clear is rejected
    BatteryTest_Service(0);assert(eraseCalls==0);
    BatteryTest_Service(1);
    assert(eraseCalls==16 && !BatteryTest_NeedsStorage());
    assert(fl_blank(flashMemory+begin,BT_REGION_BYTES) && BatteryTest_Elapsed()==0);
    assert(!strcmp(BatteryTest_Status(),"Records cleared"));
    assert(flashMemory[begin-1]==0x12 && flashMemory[begin+BT_REGION_BYTES]==0x34);
    BatteryTest_RequestStart();drain();assert(BatteryTest_Active());
    // Failed clears report failure, retain evidence, and require an explicit retry.
    BatteryTest_RequestEnd();BatteryTest_Tick();drain();
    BatteryTest_RequestClear();eraseFailure=1;BatteryTest_Service(1);eraseFailure=0;
    assert(!strcmp(BatteryTest_Status(),"Clear failed / retry"));
    const auto failedErases=eraseCalls;drain();assert(eraseCalls==failedErases);
    BatteryTest_RequestClear();BatteryTest_Service(1);
    assert(!strcmp(BatteryTest_Status(),"Records cleared"));
    // Protection at request time or before execution must never erase.
    reset();eraseCalls=0;fakePower.sample.battery_level=BatteryLevel::Critical;
    BatteryTest_RequestClear();assert(!BatteryTest_NeedsStorage());
    fakePower.sample.battery_level=BatteryLevel::Normal;BatteryTest_RequestClear();
    fakePower.sample.battery_level=BatteryLevel::Critical;BatteryTest_Service(1);
    assert(eraseCalls==0 && !BatteryTest_NeedsStorage());
    // No safe window: expire the queued request instead of erasing much later.
    reset();eraseCalls=0;BatteryTest_RequestClear();tick=10000;BatteryTest_Tick();
    BatteryTest_Service(1);assert(eraseCalls==0 && !BatteryTest_NeedsStorage());
    assert(std::strstr(BatteryTest_Status(),"Busy"));
    // Existing image overlap checks also apply to the screen entry.
    reset();eraseCalls=0;flashMemory[XORA_IMAGE_DATA_ADDR-EXTERNAL_FLASH_BASE]=0;
    BatteryTest_RequestClear();BatteryTest_Service(1);
    assert(eraseCalls==0 && !strcmp(BatteryTest_Status(),"Clear failed / retry"));

    // Charge: own entry, no automatic clear, active-mode mutual exclusion.
    reset();fakePower.sample.vbus_present=false;
    ChargeTest_RequestStart();drain();sample();assert(BatteryTest_IsCharge());
    BatteryTest_RequestStart();assert(BatteryTest_IsCharge());
    assert(eventCount(CHG_SEGMENT_START)==0);
    assert(BatteryTest_Clear()==FL_FULL);
    fakePower.sample.vbus_present=true;fakePower.sample.soc_permille=100;sample();
    assert(eventCount(CHG_SEGMENT_START)==1);
    for(int i=0;i<5;i++)sample();
    fakePower.sample.vbus_present=false;sample();assert(eventCount(CHG_SEGMENT_END)==1);
    for(int i=0;i<10;i++)sample();
    fakePower.sample.vbus_present=true;fakePower.sample.soc_permille=300;sample();
    assert(eventCount(CHG_SEGMENT_START)==2);
    fakePower.sample.soc_permille=1000;sample();assert(BatteryTest_Active()); // SOC is not Full
    // Recovery from low-battery protection must not end a charge session.
    fakePower.sample.battery_level=BatteryLevel::Critical;sample();
    fakePower.sample.battery_level=BatteryLevel::Normal;sample();assert(BatteryTest_Active());
    fakePower.sample.charge_state=PowerChargeState::Full;
    for(int i=0;i<20;i++) { sample(); }
    assert(BatteryTest_Active());
    fakePower.sample.charge_state=PowerChargeState::Charging;sample();
    fakePower.sample.charge_state=PowerChargeState::Full;
    for(int i=0;i<30;i++) { sample(); }
    assert(BatteryTest_Active());
    sample();assert(!BatteryTest_Active());
    assert(latest().event==CHG_TEST_END && latest().data[2]==BT_COMPLETED);
    assert(latest().data[4]<latest().data[3]); // unplugged waiting is excluded
    assert(latest().data[5]<latest().data[4]); // Full confirmation isn't charging
    assert(eventCount(CHG_SAMPLE_SUMMARY)==1); // partial final bucket
    assert(eventCount(BAT_DISCHARGE_START)==0);
    BatteryTest_Init();assert(BatteryTest_IsCharge() && !BatteryTest_Active());
    ChargeTest_RequestStart();assert(!BatteryTest_Active());
    assert(BatteryTest_Clear()==FL_OK);

    // Initial Full rejection and delayed preparation becoming Full consume no storage.
    reset();fakePower.sample.charge_state=PowerChargeState::Full;ChargeTest_RequestStart();
    assert(!BatteryTest_Active() && std::strstr(BatteryTest_Status(),"Already"));
    reset();ChargeTest_RequestStart();fakePower.sample.charge_state=PowerChargeState::Full;drain();
    assert(!BatteryTest_Active() && eventCount(CHG_TEST_START)==0);

    // A stale interval resets the entire continuous-Full confirmation window.
    reset();ChargeTest_RequestStart();drain();sample();
    fakePower.sample.charge_state=PowerChargeState::Full;
    for(int i=0;i<25;i++)sample();
    tick+=10000;BatteryTest_Tick();drain();assert(BatteryTest_Active());
    for(int i=0;i<30;i++) { sample(); }
    assert(BatteryTest_Active());
    sample();assert(!BatteryTest_Active());
    assert(eventCount(CHG_QUALITY_CHANGED)>=3);

    // Sleep time cannot excuse a missing interval during Full confirmation.
    reset();ChargeTest_RequestStart();drain();sample();
    fakePower.sample.charge_state=PowerChargeState::Full;sample();
    BatteryTest_Sleep(30000,0,1,0);tick+=30000;
    fakePower.sample.sampled_at_ms=tick;BatteryTest_Tick();drain();
    assert(BatteryTest_Active());
    for(int i=0;i<29;i++) { sample(); }
    assert(BatteryTest_Active());sample();assert(!BatteryTest_Active());

    // Reboot retains the v2 type and records an unknown interval; never resumes.
    reset();ChargeTest_RequestStart();drain();sample();BatteryTest_Init();drain();
    assert(!BatteryTest_Active() && BatteryTest_IsCharge());
    assert(latest().event==CHG_TEST_END && latest().data[2]==BT_REBOOT && latest().data[8]);
    reset();ChargeTest_RequestStart();drain();sample();BatteryTest_RequestEnd();BatteryTest_Tick();drain();
    assert(latest().event==CHG_TEST_END && latest().data[2]==BT_MANUAL);
    reset();ChargeTest_RequestStart();drain();fakePower.sample.valid=false;
    for(int i=0;i<60;i++) { sample(); }
    assert(eventCount(CHG_QUALITY_CHANGED)==1); // no repeated events before the first good sample
    BatteryTest_RequestEnd();BatteryTest_Tick();drain();
    assert(latest().data[7]==0); // no event-quota overflow
    reset();ChargeTest_RequestStart();drain();sample();tick=BT_DURATION_MS;BatteryTest_Tick();drain();
    assert(latest().event==CHG_TEST_END && latest().data[2]==BT_TIMEOUT);
    reset();ChargeTest_RequestStart();drain();writeFailure=1;sample();
    assert(!BatteryTest_Active() && std::strstr(BatteryTest_Status(),"Storage"));
    puts("battery runtime: safe windows, explicit clear isolation, active rejection and failure passed");
}
