#pragma GCC optimize("Os")
#include "battery_test.hpp"
#include "battery_test_recorder.hpp"
#include "power_manager.hpp"
#include "storagemanager.hpp"
#include "screen_control_config.hpp"
#include "system_sleep_manager.hpp"
#include "qspi-w25q64.h"
#include "firmware_metadata.h"
#include "stm32h7xx_hal.h"
#include <cstring>

namespace {
// Explicitly initialized in Init: D3 NOLOAD is not part of startup BSS.
BatteryTestRecorder recorder __attribute__((section(".BDMA_Section.battery"), aligned(32)));
bool initialized=false, requested=false, endRequested=false, owned=false, failed=false;
bool seen=false, wasProtected=false;
bool batchReadFailed=false;
bool requestedCharge=false;
bool clearRequested=false;
uint32_t written=0, configHash=0, sampleTime=0, lastCheck=0, requestedAt=0;
uint32_t quality=0, level=0, input=0, charge=0;
uint32_t sampleSleepMs=0;
const char *status="Not started";
constexpr uint32_t base=BATTERY_TEST_STORAGE_ADDR;
static_assert(base+BT_REGION_BYTES==CH585_FIRMWARE_STAGING_ADDR,"battery/staging boundary");
static_assert(XORA_IMAGE_DATA_ADDR+XORA_IMAGE_DATA_BYTES==base,"image/battery boundary");
static_assert(FAULT_LOG_STORAGE_ADDR+FAULT_LOG_STORAGE_SIZE<=XORA_IMAGE_DATA_ADDR,"fault isolation");
int regionRead(uint32_t off,void *data,uint32_t n) {
    if(batchReadFailed || off>BT_REGION_BYTES || n>BT_REGION_BYTES-off) return -1;
    const uint32_t addr=base+off;
    if(QSPI_W25Qxx_IsMemoryMappedMode()) { memcpy(data,reinterpret_cast<void *>(addr),n); return 0; }
    const int result=QSPI_W25Qxx_ReadBuffer(static_cast<uint8_t *>(data),addr,n);
    // The shared scanner visits every slot. After one transport error, return
    // immediately instead of paying a hardware timeout for each of 480 slots.
    if(result!=QSPI_W25Qxx_OK) batchReadFailed=true;
    return result;
}
int regionWrite(uint32_t off,const void *data,uint32_t n) {
    if(off>BT_REGION_BYTES || n>BT_REGION_BYTES-off || QSPI_W25Qxx_IsMemoryMappedMode()) return -1;
    // Page/sector primitives write QUADSPI_AR verbatim; unlike ReadBuffer,
    // they do not strip the CPU's mapped-aperture base from the address.
    return QSPI_W25Qxx_WritePage((uint8_t *)data,base-EXTERNAL_FLASH_BASE+off,n);
}
int read(uint32_t off,void *data,uint32_t n) {
    if(off>BT_DATA_BYTES || n>BT_DATA_BYTES-off) return -1;
    return regionRead(BT_HEADER_BYTES+off,data,n);
}
int write(uint32_t off,const void *data,uint32_t n) {
    if(off>BT_DATA_BYTES || n>BT_DATA_BYTES-off) return -1;
    return regionWrite(BT_HEADER_BYTES+off,data,n);
}
int erase(uint32_t) { return -1; } // append-only: protection can never erase
const fl_backend backend={read,write,erase,BT_DATA_BYTES};
bool reserveRegion() {
    uint8_t prefix[28];
    if(QSPI_W25Qxx_ReadBuffer(prefix,XORA_IMAGE_DATA_ADDR,sizeof(prefix))!=QSPI_W25Qxx_OK ||
       !bt_image_fits(prefix,XORA_IMAGE_DATA_BYTES)) return false;
    // Only inspect the old header if the current image header is empty: a
    // current image can contain arbitrary pixel bytes at the legacy address.
    if(fl_blank(prefix,sizeof(prefix))) {
        constexpr uint32_t legacy=USER_IMAGE_RESOURCES_ADDR+0xD8000u;
        if(QSPI_W25Qxx_ReadBuffer(prefix,legacy,sizeof(prefix))!=QSPI_W25Qxx_OK ||
           !bt_image_fits(prefix,base-legacy)) return false;
    }
    const fl_backend region={regionRead,regionWrite,erase,BT_REGION_BYTES};
    return bt_reserve_region(&region);
}
void snapshot(uint32_t *d) {
    const auto p=POWER_MANAGER.getSnapshot();
    d[0]=p.cell_mv; d[1]=p.soc_permille; d[2]=p.valid;
    d[3]=static_cast<uint32_t>(p.battery_level); d[4]=p.fault_bits; d[5]=p.sampled_at_ms;
}
void event(uint16_t e,uint32_t a,uint32_t b,bool important=false) {
    uint32_t d[8]={a,b}; snapshot(d+2);
    recorder.push(e,HAL_GetTick(),d,8,important);
}
void configuration(uint16_t e) {
    const auto &c=STORAGE_MANAGER.config;
    const auto *p=STORAGE_MANAGER.getDefaultGamepadProfile();
    uint32_t d[12]={static_cast<uint32_t>(c.connectionMode),
        static_cast<uint32_t>(c.inputMode),ConfigUtils::getWirelessReportRateHz(c.wirelessReportRate),
        c.screenControl.brightness,c.screenControl.standbyTimeoutSeconds,
        ScreenConfig::enabled(c.screenControl),c.power.autoSleepEnabled,c.power.autoStandbyMs,
        0,0,0,0};
    if(p) {
        const auto &l=p->ledsConfigs;
        d[8]=uint32_t(l.ledEnabled)|(uint32_t(l.ledBrightness)<<8)|(uint32_t(l.ledEffect)<<16);
        d[9]=uint32_t(l.aroundLedEnabled)|(uint32_t(l.aroundLedBrightness)<<8)|(uint32_t(l.aroundLedEffect)<<16);
        d[10]=fl_crc(&l,sizeof(l)); d[11]=fl_crc(p->id,sizeof(p->id));
    }
    const auto hash=fl_crc(d,sizeof(d));
    if(e==BAT_TEST_START || hash!=configHash) {
        configHash=hash; recorder.push(e,HAL_GetTick(),d,12);
    }
}
bool enterStorage(bool &mapped, QSPI_W25Qxx_WaitCallback &wait) {
    mapped=QSPI_W25Qxx_IsMemoryMappedMode();
    if(mapped && QSPI_W25Qxx_ExitMemoryMappedMode()!=QSPI_W25Qxx_OK) return false;
    wait=QSPI_W25Qxx_SetWaitCallback(nullptr);
    return true;
}
void leaveStorage(bool mapped, QSPI_W25Qxx_WaitCallback wait) {
    QSPI_W25Qxx_SetWaitCallback(wait);
    if(SCB->CCR&SCB_CCR_DC_Msk)
        SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t *>(base),BT_REGION_BYTES);
    if(mapped && QSPI_W25Qxx_EnterMemoryMappedMode()!=QSPI_W25Qxx_OK) XoraLog_Fatal(0x103u);
}
void fail() {
    failed=true; requested=false; recorder.incomplete=true;
    if(recorder.active) recorder.finish(HAL_GetTick(),BT_STORAGE_FAILED);
    status="Storage error / incomplete";
}

void chargeTick(uint32_t now) {
    const auto p=POWER_MANAGER.getSnapshot();
    auto &c=recorder.charging;
    const bool newSample=!seen || p.sampled_at_ms!=c.lastSample;
    const uint32_t age=now-p.sampled_at_ms;
    const uint32_t interval=p.sampled_at_ms-c.lastSample;
    const bool valid=p.valid && p.data_quality==PowerDataQuality::Fresh && age<=2500u;
    // Only battery-only waiting may use the slower ordinary-sleep cadence.
    // External power (including Full confirmation) requires continuous fresh telemetry.
    const uint32_t sleepAllowance=!c.previousInput && !p.vbus_present?sampleSleepMs:0u;
    const bool gap=seen && newSample && interval>2500u+sleepAllowance;
    const uint32_t q=uint32_t(valid)|(uint32_t(gap)<<1);
    if(!valid || gap) {
        ++recorder.missing; c.fullPending=false;
        if(gap) recorder.missing+=(interval-sleepAllowance)/1000u-1u;
    }
    if(!valid || !newSample) {
        if(!valid) c.previousValid=false;
        if(quality!=q) { quality=q; c.snapshot(recorder,CHG_QUALITY_CHANGED,now,q); }
        if(now-recorder.bucketAt>=BT_SUMMARY_MS) recorder.summary(now);
        status=valid?"Recording charge":"Telemetry unavailable";
        return;
    }
    const bool inputNow=p.vbus_present;
    const bool chargingNow=inputNow && p.charge_state==PowerChargeState::Charging && p.charge_block==PowerChargeBlock::None;
    if(c.previousValid && !gap) {
        if(c.previousInput && inputNow) c.inputMs+=interval;
        if(c.previousCharging && chargingNow) c.chargingMs+=interval;
    }
    const uint32_t state=static_cast<uint32_t>(p.charge_state)|(uint32_t(p.thermal_regulation)<<10)|
        (uint32_t(p.input_current_regulation)<<11)|(uint32_t(p.input_voltage_regulation)<<12);
    const uint32_t block=static_cast<uint32_t>(p.charge_block);
    const bool changed=!seen || state!=c.lastState || block!=c.lastBlock || p.fault_bits!=c.lastFault ||
        inputNow!=c.previousInput || (inputNow && ((p.vbus_mv>=7000u)!=(c.lastInput>=7000u)));
    c.lastSample=p.sampled_at_ms; c.lastCell=p.cell_mv; c.lastSoc=p.soc_permille;
    c.lastInput=inputNow?p.vbus_mv:0; c.lastCurrent=inputNow?p.charge_current_ma:0;
    c.lastState=state; c.lastBlock=block; c.lastFault=p.fault_bits;
    c.previousValid=true; c.previousInput=inputNow; c.previousCharging=chargingNow;
    recorder.sample(true,p.cell_mv,p.soc_permille);
    c.accumulate(p.cell_mv,c.lastInput,c.lastCurrent);
    if(quality!=q) { quality=q; c.snapshot(recorder,CHG_QUALITY_CHANGED,now,q); }
    if(inputNow && !c.inSegment) {
        ++c.segment; c.inSegment=true; c.milestoneMask=0; c.fullPending=false;
        c.snapshot(recorder,CHG_SEGMENT_START,now,!seen && c.midCharge?1u:0u,true);
        const uint16_t levels[]={200,500,800,900,1000};
        for(unsigned i=0;i<5;++i) if(p.soc_permille>levels[i]) c.milestoneMask|=1u<<i;
    } else if(!inputNow) c.close(recorder,now,CT_UNPLUGGED);
    if(changed) c.snapshot(recorder,CHG_STATE_CHANGED,now,0);
    if(c.inSegment) {
        const uint16_t levels[]={200,500,800,900,1000};
        for(unsigned i=0;i<5;++i) if(!(c.milestoneMask&(1u<<i)) && p.soc_permille>=levels[i]) {
            c.milestoneMask|=1u<<i; c.snapshot(recorder,CHG_MILESTONE,now,levels[i]);
        }
        if(p.charge_state==PowerChargeState::Full && p.charge_block==PowerChargeBlock::None) {
            if(!c.fullPending) { c.fullAt=p.sampled_at_ms; c.fullPending=true; }
            else if(p.sampled_at_ms-c.fullAt>=CT_FULL_CONFIRM_MS) {
                recorder.finish(now,BT_COMPLETED); status="Charge completed";
            }
        } else c.fullPending=false;
    }
    seen=true; sampleTime=p.sampled_at_ms; sampleSleepMs=0;
    if(recorder.active && now-recorder.bucketAt>=BT_SUMMARY_MS) recorder.summary(now);
    if(recorder.active) status=!inputNow?"Waiting for power":c.fullPending?"Confirming full (30s)":
        chargingNow?"Charging (24h max)":"Charge paused";
}
}

extern "C" void BatteryTest_Init() {
    batchReadFailed=false;
    recorder={}; initialized=true; requested=endRequested=owned=failed=seen=wasProtected=false;
    requestedCharge=false;
    clearRequested=false;
    written=configHash=sampleTime=lastCheck=quality=level=input=charge=0;
    sampleSleepMs=0;
    status="Not started";
    bt_header h;
    if(QSPI_W25Qxx_IsMemoryMappedMode()) memcpy(&h,reinterpret_cast<void *>(base),sizeof(h));
    else if(QSPI_W25Qxx_ReadBuffer(reinterpret_cast<uint8_t *>(&h),base,sizeof(h))!=QSPI_W25Qxx_OK) {
        failed=true; status="Storage read failed"; return;
    }
    if(fl_blank(&h,sizeof(h))) return;
    if(!bt_header_valid(&h)) {
        failed=true; // Dedicated storage: never reinterpret unknown bytes as free.
        status="Occupied / incomplete storage";
        return;
    }
    owned=true;
    const auto scan=fl_inspect(&backend); written=scan.count;
    if(scan.status) { failed=true; status="Incomplete storage"; return; }
    fl_record latest;
    const bool haveLatest=fl_find(&backend,UINT32_MAX,0,0,0,&latest)==FL_OK;
    if(haveLatest) {
        recorder.chargeMode=latest.data[0]==CT_VERSION;
        recorder.firmware=latest.firmware; recorder.session=latest.data[1];
        if(latest.event==BAT_TEST_END || latest.event==CHG_TEST_END) {
            recorder.elapsed=latest.data[3]; recorder.discharge=latest.data[4];
            recorder.incomplete=latest.words>=9 && latest.data[8]!=0u;
        }
    }
    if(!haveLatest || (latest.event!=BAT_TEST_END && latest.event!=CHG_TEST_END)) {
        fl_info info; XoraLog_Info(&info);
        recorder.begin(HAL_GetTick(),XoraLog_CurrentBoot(),info.firmware,haveLatest?latest.data[1]:0u);
        recorder.chargeMode=haveLatest && latest.data[0]==CT_VERSION;
        recorder.incomplete=true;
        const uint32_t d[]={BT_REBOOT,0,0,0,0,0,1};
        recorder.push(recorder.chargeMode?uint16_t(CHG_TEST_END):uint16_t(BAT_TEST_END),HAL_GetTick(),d,7,true); recorder.active=false;
        status=haveLatest?"Interrupted by reboot":"Interrupted preparation";
    } else status=written ? "Completed / view WebConfig" : "Reserved";
}
static void requestStart(bool charging) {
    if(!initialized || recorder.active || requested || clearRequested) return;
    if(failed) { status="Storage error / incomplete"; return; }
    if(owned) { status="History exists / clear first"; return; }
    if(POWER_MANAGER.batteryProtectionRequired() || !POWER_MANAGER.getSnapshot().valid) {
        status="Battery data unavailable"; return;
    }
    const auto p=POWER_MANAGER.getSnapshot();
    if(charging && (p.data_quality!=PowerDataQuality::Fresh || HAL_GetTick()-p.sampled_at_ms>2500u)) {
        status="Battery data unavailable"; return;
    }
    if(charging && p.vbus_present && p.charge_state==PowerChargeState::Full) {
        status="Already fully charged"; return;
    }
    requestedCharge=charging;
    requestedAt=HAL_GetTick(); requested=true; status="Preparing...";
}
extern "C" void BatteryTest_RequestStart() { requestStart(false); }
extern "C" void ChargeTest_RequestStart() { requestStart(true); }
extern "C" int BatteryTest_IsCharge() { return requested?requestedCharge:recorder.chargeMode; }
extern "C" void BatteryTest_RequestEnd() { if(recorder.active) endRequested=true; }
extern "C" void BatteryTest_RequestClear() {
    if(!initialized || clearRequested || __get_IPSR()) return;
    if(BatteryTest_Active()) { status="End test before clearing"; return; }
    if(POWER_MANAGER.batteryProtectionRequired()) { status="Battery data unavailable"; return; }
    clearRequested=true; requestedAt=HAL_GetTick(); status="Clearing records...";
}
extern "C" uint8_t BatteryTest_Clear() {
    if (!initialized || BatteryTest_Active() || clearRequested || __get_IPSR()) return FL_FULL;
    batchReadFailed=false;
    uint8_t prefix[28];
    const auto readPrefix=[&](uint32_t address) {
        if (QSPI_W25Qxx_IsMemoryMappedMode()) { memcpy(prefix,reinterpret_cast<void *>(address),sizeof(prefix)); return true; }
        return QSPI_W25Qxx_ReadBuffer(prefix,address,sizeof(prefix))==QSPI_W25Qxx_OK;
    };
    // Legacy images may extend into this partition: never erase their pixels.
    if (!readPrefix(XORA_IMAGE_DATA_ADDR) ||
        !bt_image_fits(prefix,XORA_IMAGE_DATA_BYTES)) return FL_IO;
    constexpr uint32_t legacy=USER_IMAGE_RESOURCES_ADDR+0xD8000u;
    if (fl_blank(prefix,sizeof(prefix)) &&
        (!readPrefix(legacy) || !bt_image_fits(prefix,base-legacy))) return FL_IO;
    bool mapped=false; QSPI_W25Qxx_WaitCallback wait=nullptr;
    if (!enterStorage(mapped,wait)) {
        if (mapped && QSPI_W25Qxx_EnterMemoryMappedMode()!=QSPI_W25Qxx_OK) XoraLog_Fatal(0x103u);
        return FL_IO;
    }
    const fl_backend clearBackend={regionRead,regionWrite,[](uint32_t off)->int {
        if (off%FL_SECTOR_BYTES || off>BT_REGION_BYTES-FL_SECTOR_BYTES) return -1;
        return QSPI_W25Qxx_SectorErase(base-EXTERNAL_FLASH_BASE+off);
    },BT_REGION_BYTES};
    const auto result=fl_clear(&clearBackend,1); // Header is erased only after all data sectors verify.
    leaveStorage(mapped,wait);
    if (result!=FL_OK) { fail(); return result; }
    BatteryTest_Init();
    return failed ? FL_IO : FL_OK;
}
extern "C" int BatteryTest_Active() { return recorder.active || requested; }
extern "C" int BatteryTest_NeedsStorage() { return clearRequested || (!failed && (requested || recorder.count)); }
extern "C" const char *BatteryTest_Status() { return status; }
extern "C" uint32_t BatteryTest_Elapsed() { return recorder.elapsed; }
extern "C" void BatteryTest_Event(uint16_t e,uint32_t a,uint32_t b) {
    if(!recorder.active) return;
    event(e,a,b,e==BAT_PROTECTION_ENTER || e==BAT_RECOVERY_ACTION || (e==BAT_LEVEL_CHANGED && a>=2u));
}
extern "C" void BatteryTest_Sleep(uint32_t dt,int protection,int attempted,int error) {
    if(!recorder.active) return;
    const bool protectedSleep=protection<0?POWER_MANAGER.batteryProtectionRequired():protection!=0;
    if(attempted) {
        ++recorder.stopCount; recorder.stopMs+=dt;
        if(protectedSleep) { ++recorder.protectionStops; recorder.protectionStopMs+=dt; }
        else {
            ++recorder.ordinaryStops; recorder.ordinaryStopMs+=dt;
            sampleSleepMs+=dt;
        }
    }
    if(error&256) {
        ++recorder.stopFailed;
        if(protectedSleep) ++recorder.protectionFailures; else ++recorder.ordinaryFailures;
    }
    recorder.stopGates|=uint32_t(error);
    // Counters are aggregated, never one queued event per maintenance wake.
}
extern "C" void BatteryTest_StoragePause(uint32_t elapsed) {
    if(owned) recorder.writeMs+=elapsed;
}
extern "C" void BatteryTest_Tick() {
    if((requested || clearRequested) && HAL_GetTick()-requestedAt>=10000u) {
        requested=clearRequested=false; status="Busy / retry in game mode";
    }
    if(!recorder.active) return;
    const uint32_t now=HAL_GetTick();
    recorder.advance(now);
    const auto p=POWER_MANAGER.getSnapshot();
    const bool sleeping=SystemSleep_IsSleeping();
    if(sleeping!=recorder.sleeping) {
        event(BAT_SLEEP_SUMMARY,sleeping,recorder.sleep);
        recorder.sleeping=sleeping;
    }
    if(endRequested || recorder.elapsed>=BT_DURATION_MS) {
        if(recorder.chargeMode && now-recorder.charging.lastSample>2500u) {
            recorder.charging.previousValid=false; ++recorder.missing;
        }
        recorder.finish(now,endRequested?BT_MANUAL:BT_TIMEOUT); endRequested=false;
        status="Ended / view WebConfig"; return;
    }
    if(now-lastCheck<1000u && seen) return;
    lastCheck=now;
    configuration(BAT_CONFIG_CHANGED);
    if(recorder.chargeMode) { chargeTick(now); return; }
    const bool fresh=p.valid && (!seen || p.sampled_at_ms!=sampleTime);
    const uint32_t interval=p.sampled_at_ms-sampleTime;
    // Ordinary STOP deliberately defers healthy battery sampling up to five
    // seconds. Account only observed sleep, without hiding awake bus stalls.
    const uint32_t gapLimit=2500u+sampleSleepMs;
    const bool gap=(seen && fresh && interval>gapLimit) || now-p.sampled_at_ms>gapLimit;
    if(seen && fresh && (quality&0x100u) && interval>gapLimit)
        recorder.missing+=(interval-sampleSleepMs)/1000u-1u;
    if(fresh) recorder.sample(true,p.cell_mv,p.soc_permille);
    else if(!p.valid) recorder.sample(false,0,0);
    const uint32_t q=uint32_t(p.data_quality)|(uint32_t(p.valid)<<8)|(uint32_t(gap)<<9);
    const uint32_t in=uint32_t(p.vbus_present)|(uint32_t(p.charge_block==PowerChargeBlock::None)<<8);
    const uint32_t ch=uint32_t(p.charge_state)|(uint32_t(p.charge_block)<<8)|(uint32_t(p.fault_bits)<<16);
    if(!seen || q!=quality) { event(BAT_DATA_QUALITY_CHANGED,q,now-p.sampled_at_ms); quality=q; }
    if(!seen || in!=input) { event(BAT_INPUT_POWER_CHANGED,in,p.vbus_mv); input=in; }
    if(!seen || ch!=charge) { event(BAT_CHARGE_CHANGED,ch,p.charge_current_ma); charge=ch; }
    recorder.onBattery=p.valid && !p.vbus_present;
    if(recorder.onBattery && !recorder.discharged) {
        recorder.discharged=true; event(BAT_DISCHARGE_START,p.cell_mv,p.soc_permille,true);
    }
    const uint32_t l=static_cast<uint32_t>(p.battery_level);
    if(!seen || l!=level) {
        if(!seen) event(BAT_LEVEL_CHANGED,l,l);
        level=l;
        if(l>=2u) wasProtected=true;
        else if(wasProtected) {
            event(BAT_RECOVERY_ACTION,3,0,true);
            recorder.finish(now,BT_COMPLETED); status="Completed / view WebConfig";
        }
    }
    sampleTime=p.sampled_at_ms; seen=true;
    if(fresh) sampleSleepMs=0;
    if(recorder.active && now-recorder.bucketAt>=BT_SUMMARY_MS) recorder.summary(now);
    if(recorder.active) status=!recorder.discharged?"Waiting for unplug":
        l>=2u?"Protection / recovery":"Recording (24h max)";
}
extern "C" void BatteryTest_Service(int safe) {
    if(!safe || !initialized || __get_IPSR()) return;
    if(clearRequested) {
        clearRequested=false;
        // Never erase during protection. The main loop has already stopped
        // input and waited for LCD DMA, and excluded upgrade/ISP transactions.
        if(POWER_MANAGER.batteryProtectionRequired()) { status="Battery data unavailable"; return; }
        const auto result=BatteryTest_Clear();
        status=result==FL_OK?"Records cleared":result==FL_FULL?"End test before clearing":"Clear failed / retry";
        return;
    }
    if(failed || (!requested && !recorder.count)) return;
    batchReadFailed=false;
    const uint32_t began=HAL_GetTick();
    bool mapped=false; QSPI_W25Qxx_WaitCallback wait=nullptr;
    if(!enterStorage(mapped,wait)) {
        if(mapped && QSPI_W25Qxx_EnterMemoryMappedMode()!=QSPI_W25Qxx_OK) XoraLog_Fatal(0x103u);
        fail(); return;
    }
    if(requested) {
        requested=false;
        if(POWER_MANAGER.batteryProtectionRequired() || !POWER_MANAGER.getSnapshot().valid) {
            status="Battery data unavailable"; leaveStorage(mapped,wait); return;
        }
        const auto p=POWER_MANAGER.getSnapshot();
        if(requestedCharge && (p.data_quality!=PowerDataQuality::Fresh || began-p.sampled_at_ms>2500u)) {
            status="Battery data unavailable"; leaveStorage(mapped,wait); return;
        }
        if(requestedCharge && p.vbus_present && p.charge_state==PowerChargeState::Full) {
            status="Already fully charged"; leaveStorage(mapped,wait); return;
        }
        // Checks every byte of the proposed tail. No history erasure.
        if(!reserveRegion()) { status="No capacity / keep history"; leaveStorage(mapped,wait); return; }
        owned=true;
        fl_info info; XoraLog_Info(&info);
        recorder.begin(began,XoraLog_CurrentBoot(),info.firmware,info.newest+1u);
        recorder.chargeMode=requestedCharge;
        if(requestedCharge) {
            quality=UINT32_MAX;
            recorder.charging.midCharge=p.vbus_present;
            recorder.charging.snapshot(recorder,CHG_TEST_START,began,p.vbus_present?1u:0u,true);
        }
        configuration(BAT_TEST_START);
        event(BAT_STORAGE_STATUS,BT_SLOTS,0,true);
        seen=wasProtected=false; status="Recording (24h max)";
    }
    // One atomic record per window; no erase, retry loop or unbounded flush.
    if(recorder.count) {
        auto &r=recorder.queue[recorder.head];
        if(written>=BT_SLOTS || fl_append(&backend,&r)!=FL_OK) fail();
        else { ++written; recorder.pop(); }
    }
    leaveStorage(mapped,wait);
}
extern "C" void BatteryTest_GetInfo(fl_info *i) {
    memset(i,0,sizeof(*i)); i->version=1;
    if(!owned) { i->status=failed?FL_IO:initialized?FL_OK:FL_UNSUPPORTED; return; }
    batchReadFailed=false;
    const auto s=fl_inspect(&backend);
    i->newest=s.newest; i->oldest=s.oldest; i->count=s.count;
    i->pending=recorder.count; i->dropped=recorder.dropped; i->firmware=recorder.firmware;
    i->status=(failed || recorder.incomplete)?FL_IO:s.status;
}
extern "C" uint8_t BatteryTest_Read(uint32_t upper,uint32_t before,uint8_t cat,uint8_t sev,uint8_t pending,fl_record *r) {
    if(!owned) return FL_UNSUPPORTED;
    if(pending) {
        if(pending>recorder.count) return FL_FULL;
        *r=recorder.queue[(recorder.head+pending-1)%16]; r->flags=FL_PENDING; return FL_OK;
    }
    batchReadFailed=false;
    const auto result=fl_find(&backend,upper,before,cat,sev,r);
    return batchReadFailed?FL_IO:result;
}
