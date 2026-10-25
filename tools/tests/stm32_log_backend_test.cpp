#include "firmware_metadata.h"
#include "fault_log.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(test) do { if (!(test)) { std::fprintf(stderr,"Failed: %s at %d\n",#test,__LINE__); std::exit(1); } } while (0)
static uint8_t storage[FAULT_LOG_STORAGE_SIZE];
static bool mapped;
static unsigned calls, erases;
static uint32_t lastAddress;
static constexpr uint32_t chipBase = FAULT_LOG_STORAGE_ADDR - EXTERNAL_FLASH_BASE;
static bool QSPI_W25Qxx_IsMemoryMappedMode() { return mapped; }
static uint32_t chipOffset(uint32_t address, uint32_t size) {
    ++calls; lastAddress = address;
    CHECK(!mapped && address >= chipBase && address <= chipBase + sizeof(storage));
    CHECK(size <= chipBase + sizeof(storage) - address);
    return address - chipBase;
}
static int QSPI_W25Qxx_ReadBuffer(uint8_t *data,uint32_t address,uint32_t n) {
    memcpy(data,storage+chipOffset(address,n),n); return 0;
}
static int QSPI_W25Qxx_WritePage(uint8_t *data,uint32_t address,uint16_t n) {
    auto off=chipOffset(address,n);
    CHECK(n && n<=256 && (address%256)+n<=256);
    for(uint32_t i=0;i<n;++i) { CHECK((storage[off+i]&data[i])==data[i]); storage[off+i]&=data[i]; }
    return 0;
}
static int QSPI_W25Qxx_SectorErase(uint32_t address) {
    auto off=chipOffset(address,FL_SECTOR_BYTES); CHECK(!(off%FL_SECTOR_BYTES));
    memset(storage+off,255,FL_SECTOR_BYTES); ++erases; return 0;
}
static void *mappedCopy(void *dest,const void *src,size_t n) {
    const auto address=reinterpret_cast<uintptr_t>(src);
    CHECK(mapped && address>=FAULT_LOG_STORAGE_ADDR && address<=FAULT_LOG_STORAGE_ADDR+sizeof(storage));
    CHECK(n<=FAULT_LOG_STORAGE_ADDR+sizeof(storage)-address);
    return memcpy(dest,storage+address-FAULT_LOG_STORAGE_ADDR,n);
}
#define memcpy mappedCopy
#include "stm32_backend_under_test.inc"
#undef memcpy
int main() {
    memset(storage,255,sizeof(storage));
    uint8_t data[92]={}, out[92];
    // Exact transfer observed in the five-second startup stall.
    CHECK(writeLog(0x180,data,sizeof(data))==0 && lastAddress==0x560180);
    CHECK(readLog(0x180,out,sizeof(out))==0 && !memcmp(data,out,sizeof(data)));
    CHECK(eraseLog(0)==0 && lastAddress==0x560000);
    CHECK(writeLog(sizeof(storage)-1,data,1)==0 && lastAddress==0x56ffff);
    CHECK(eraseLog(sizeof(storage)-FL_SECTOR_BYTES)==0 && lastAddress==0x56f000);
    unsigned before=calls;
    CHECK(writeLog(sizeof(storage)-1,data,2)!=0);
    CHECK(readLog(UINT32_MAX,out,2)!=0);
    CHECK(eraseLog(1)!=0 && eraseLog(sizeof(storage))!=0 && calls==before);
    mapped=true;
    CHECK(readLog(0,out,sizeof(out))==0 && out[0]==255);
    CHECK(writeLog(0,data,1)!=0 && eraseLog(0)!=0 && calls==before);
    mapped=false;
    const fl_backend backend={readLog,writeLog,eraseLog,sizeof(storage)};
    fl_record record={}; record.source=FL_SOURCE_STM32; record.event=FL_EVENT_CPU; record.repeat=1;
    CHECK(fl_append(&backend,&record)==FL_OK);
    CHECK(fl_inspect(&backend).count==1);
    unsigned previousErases=erases;
    // Simulated reboot scan and append preserve existing evidence.
    record.sequence=0; CHECK(fl_append(&backend,&record)==FL_OK);
    mapped=true; CHECK(fl_inspect(&backend).count==2 && erases==previousErases);
    puts("STM32 QSPI addresses, partition bounds, mapped reads and journal persistence passed");
}
