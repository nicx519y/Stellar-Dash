#include "storagemanager.hpp"
#include "qspi-w25q64.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

// Real ConfigUtils and Storage; only the NOR peripheral is simulated.
static std::vector<uint8_t> flash(8u * 1024u * 1024u, 0xFF);
static bool mapped = true;
static int writesRemaining = -1;
bool QSPI_W25Qxx_IsMemoryMappedMode() { return mapped; }
int8_t QSPI_W25Qxx_ExitMemoryMappedMode() { mapped=false; return 0; }
int8_t QSPI_W25Qxx_EnterMemoryMappedMode() { mapped=true; return 0; }
int8_t QSPI_W25Qxx_ReadBuffer(uint8_t* data, uint32_t addr, uint32_t size) {
    assert(!mapped); addr &= 0xFFFFFF;
    assert(addr+size<=flash.size());
    std::copy_n(flash.data()+addr,size,data); return 0;
}
int8_t QSPI_W25Qxx_BufferErase(uint32_t addr, uint32_t size) {
    assert(!mapped); addr &= 0xFFFFFF;
    assert((addr==0x580000 || addr==0x590000) && size==65536);
    std::fill_n(flash.data()+addr,size,0xFF); return 0;
}
int8_t QSPI_W25Qxx_WritePage(uint8_t* data, uint32_t addr, uint16_t size) {
    assert(!mapped); addr &= 0xFFFFFF;
    assert(size>0 && size<=256 && (addr%256)+size<=256);
    if(writesRemaining==0) return -1;
    if(writesRemaining>0) --writesRemaining;
    for(unsigned i=0;i<size;++i) {
        assert((flash[addr+i]&data[i])==data[i]);
        flash[addr+i]&=data[i];
    }
    return 0;
}

int main() {
    static_assert(offsetof(ScreenControlConfig,reserved1)==offsetof(ScreenControlConfig,txIspReturnBootMode));
    auto& storage=STORAGE_MANAGER;
    storage.initConfig();
    assert(storage.config.version==CONFIG_VERSION);
    for(auto origin:{BootMode::BOOT_MODE_INPUT,BootMode::BOOT_MODE_WEB_CONFIG}) {
        storage.setBootMode(origin); assert(storage.saveConfig());
        storage.config.screenControl.txIspReturnBootMode=origin;
        storage.setBootMode(BootMode::BOOT_MODE_TX_ISP); assert(storage.saveConfig());
        // RAM cleared, full production load/migration/sanitization runs again.
        storage.config={}; storage.initConfig();
        assert(storage.getBootMode()==BootMode::BOOT_MODE_TX_ISP);
        assert(storage.config.screenControl.txIspReturnBootMode==origin);
        // Unrelated configuration saving must preserve the mode.
        storage.config.screenControl.brightness=78; assert(storage.saveConfig());
        storage.config={}; storage.initConfig();
        assert(storage.getBootMode()==BootMode::BOOT_MODE_TX_ISP);
        // Failed exit rolls RAM back from the actual committed NOR journal.
        writesRemaining=0; storage.setBootMode(origin);
        storage.config.screenControl.txIspReturnBootMode=0;
        assert(!storage.saveConfig());
        assert(storage.getBootMode()==BootMode::BOOT_MODE_TX_ISP);
        assert(storage.config.screenControl.txIspReturnBootMode==origin);
        writesRemaining=-1;
        storage.setBootMode(origin); storage.config.screenControl.txIspReturnBootMode=0;
        assert(storage.saveConfig()); storage.config={}; storage.initConfig();
        assert(storage.getBootMode()==origin && storage.config.screenControl.txIspReturnBootMode==0);
    }
    // Interrupt every write page of entry, including the final commit word.
    const auto baseline=flash;
    const int pages=2+(sizeof(Config)+64+255)/256;
    for(int failAfter=0;failAfter<=pages;++failAfter) {
        flash=baseline; writesRemaining=-1; storage.config={}; storage.initConfig();
        storage.config.screenControl.txIspReturnBootMode=storage.getBootMode();
        storage.setBootMode(BootMode::BOOT_MODE_TX_ISP);
        writesRemaining=failAfter;
        const bool saved=storage.saveConfig(); writesRemaining=-1;
        storage.config={}; storage.initConfig();
        assert(storage.getBootMode()==(saved ? BootMode::BOOT_MODE_TX_ISP : BootMode::BOOT_MODE_WEB_CONFIG));
    }
    std::cout<<"Real ConfigUtils/Storage boot-mode persistence passed; Config bytes="<<sizeof(Config)<<"\n";
}
