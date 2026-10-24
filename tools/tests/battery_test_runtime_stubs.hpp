#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include "battery_policy.hpp"
#include "battery_test_log.h"
#define EXTERNAL_FLASH_BASE 0x90000000u
#define USER_IMAGE_RESOURCES_ADDR 0x905F0000u
#define CH585_FIRMWARE_STAGING_ADDR 0x90780000u
#define FAULT_LOG_STORAGE_ADDR 0x90560000u
#define FAULT_LOG_STORAGE_SIZE 65536u
#define QSPI_W25Qxx_OK 0
#define SCB_CCR_DC_Msk 1u
struct FakeScb {uint32_t CCR=0;};
inline FakeScb scb;
#define SCB (&scb)
inline unsigned cacheInvalidations=0;
inline void SCB_InvalidateDCache_by_Addr(uint32_t *p,uint32_t n) {
    if(reinterpret_cast<uintptr_t>(p)!=BATTERY_TEST_STORAGE_ADDR || n!=BT_REGION_BYTES)abort();
    ++cacheInvalidations;
}
inline unsigned __get_IPSR() {return 0;}
inline uint32_t tick;
inline uint32_t HAL_GetTick() {return tick;}
enum class PowerDataQuality { Unknown,Fresh,Stale };
enum class PowerChargeBlock {None,NoInput,Telemetry};
enum class PowerChargeState {Discharging,Charging,Full,Fault,Unknown};
struct Sample {
    uint16_t cell_mv=4000,soc_permille=900,vbus_mv=5000,fault_bits=0,charge_current_ma=500;
    uint32_t sampled_at_ms=0;
    bool valid=true,vbus_present=true;
    bool thermal_regulation=false,input_current_regulation=false,input_voltage_regulation=false;
    BatteryLevel battery_level=BatteryLevel::Normal;
    PowerDataQuality data_quality=PowerDataQuality::Fresh;
    PowerChargeBlock charge_block=PowerChargeBlock::None;
    PowerChargeState charge_state=PowerChargeState::Charging;
};
struct FakePower {
    Sample sample;
    void loop() {}
    Sample getSnapshot() const {return sample;}
    bool batteryProtectionRequired() const {return sample.battery_level>=BatteryLevel::Critical;}
};
inline FakePower fakePower;
#define POWER_MANAGER fakePower
struct FakeLeds {
    bool ledEnabled=true,aroundLedEnabled=true;
    uint8_t ledBrightness=30,aroundLedBrightness=50,ledEffect=0,aroundLedEffect=0;
};
struct FakeProfile {FakeLeds ledsConfigs;char id[16]="profile";};
struct FakeConfig {
    uint32_t connectionMode=1,inputMode=0,wirelessReportRate=8000;
    struct {uint32_t brightness=60,standbyTimeoutSeconds=30;} screenControl;
    struct {uint32_t autoSleepEnabled=0,autoStandbyMs=300000;} power;
};
struct FakeStorage {FakeConfig config;FakeProfile p;FakeProfile *getDefaultGamepadProfile(){return &p;}};
inline FakeStorage fakeStorage;
#define STORAGE_MANAGER fakeStorage
namespace ScreenConfig {template<class T> bool enabled(const T&){return true;}}
namespace ConfigUtils {inline uint32_t getWirelessReportRateHz(uint32_t n){return n;}}
inline bool ordinarySleep=false;
inline bool SystemSleep_IsSleeping(){return ordinarySleep;}
using QSPI_W25Qxx_WaitCallback=void (*)();
inline QSPI_W25Qxx_WaitCallback waitCallback=nullptr;
inline QSPI_W25Qxx_WaitCallback QSPI_W25Qxx_SetWaitCallback(QSPI_W25Qxx_WaitCallback w){auto previous=waitCallback;waitCallback=w;return previous;}
inline bool mappedMode=false, qspiAddressError=false;
inline unsigned mappingExits=0, mappingEnters=0;
inline void (*mappingTransition)(bool)=nullptr;
inline bool QSPI_W25Qxx_IsMemoryMappedMode(){return mappedMode;}
inline int QSPI_W25Qxx_ExitMemoryMappedMode(){mappedMode=false;++mappingExits;if(mappingTransition)mappingTransition(false);return 0;}
inline int QSPI_W25Qxx_EnterMemoryMappedMode(){if(qspiAddressError)return -1;mappedMode=true;++mappingEnters;if(mappingTransition)mappingTransition(true);return 0;}
inline uint8_t flashMemory[0x800000];
inline int writeFailure=0;
inline int readFailure=0, readCalls=0;
inline int eraseFailure=0, eraseCalls=0;
inline int QSPI_W25Qxx_SectorErase(uint32_t addr) {
    // Like the production primitive, this accepts a chip offset, not a CPU
    // memory-mapped address. ReadBuffer alone normalizes mapped addresses.
    if(addr<BATTERY_TEST_STORAGE_ADDR-EXTERNAL_FLASH_BASE ||
       addr>=BATTERY_TEST_STORAGE_ADDR-EXTERNAL_FLASH_BASE+BT_REGION_BYTES || addr%4096){qspiAddressError=true;return -1;}
    if(waitCallback)waitCallback();
    ++eraseCalls;if(eraseFailure)return -1;
    memset(flashMemory+addr,255,4096);return 0;
}
inline int QSPI_W25Qxx_ReadBuffer(uint8_t *p,uint32_t addr,uint32_t n){
    ++readCalls;if(readFailure)return -1;
    if(addr<EXTERNAL_FLASH_BASE || addr-EXTERNAL_FLASH_BASE+n>sizeof(flashMemory))abort();
    memcpy(p,flashMemory+addr-EXTERNAL_FLASH_BASE,n);return 0;
}
inline int QSPI_W25Qxx_WritePage(uint8_t *p,uint32_t addr,uint32_t n){
    if(addr>=sizeof(flashMemory) || n>sizeof(flashMemory)-addr){qspiAddressError=true;return -1;}
    if(writeFailure)return -1;
    for(uint32_t i=0;i<n;i++)flashMemory[addr+i]&=p[i];
    return 0;
}
