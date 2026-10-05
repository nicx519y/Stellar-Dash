#include "storagemanager.hpp"
#include "qspi-w25q64.h"
#include "lighting_resource_config.hpp"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>

// Real ConfigUtils and Storage; only the NOR peripheral is simulated.
static std::vector<uint8_t> flash(8u * 1024u * 1024u, 0xFF);
static bool mapped = true;
static int writesRemaining = -1;
// Resource-store initialization is independent of the configuration journal.
namespace LightingResources {
void initialize() {}
void migrate(Config &config) { LightingResourceConfig::migrate(config); }
bool resolve(const XoraResource::Ref &, XoraResource::Light &) { std::abort(); }
}
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

static void assertDefaultHotkeys(const Config &config) {
    const int32_t keys[] = {20, 19, 13, 12, 10, 9, 17, 16, 15, 14, 11};
    const GamepadHotkey actions[] = {
        HOTKEY_INPUT_MODE_WEBCONFIG, HOTKEY_INPUT_MODE_CALIBRATION,
        HOTKEY_LEDS_EFFECTSTYLE_NEXT, HOTKEY_LEDS_EFFECTSTYLE_PREV,
        HOTKEY_LEDS_BRIGHTNESS_UP, HOTKEY_LEDS_BRIGHTNESS_DOWN,
        HOTKEY_AMBIENT_LIGHT_EFFECTSTYLE_NEXT, HOTKEY_AMBIENT_LIGHT_EFFECTSTYLE_PREV,
        HOTKEY_AMBIENT_LIGHT_BRIGHTNESS_UP, HOTKEY_AMBIENT_LIGHT_BRIGHTNESS_DOWN,
        HOTKEY_LEDS_ENABLE_SWITCH
    };
    static_assert(sizeof(keys) / sizeof(keys[0]) == NUM_GAMEPAD_HOTKEYS);
    for (unsigned i = 0; i < NUM_GAMEPAD_HOTKEYS; ++i) {
        assert(config.hotkeys[i].virtualPin == keys[i]);
        assert(config.hotkeys[i].action == actions[i]);
        assert(config.hotkeys[i].isHold == (i < 2 || i == 10));
        assert(config.hotkeys[i].isLocked == (i < 2));
    }
}

static void assertDefaultProfileMappings(const Config &config) {
    const uint32_t expected[NUM_GAME_CONTROLLER_BUTTONS] = {
        0, (1u << 1) | (1u << 8), 1u << 6, 1u << 5, 1u << 7,
        1u << 9, 1u << 12, 1u << 10, 1u << 13,
        1u << 17, 1u << 15, 1u << 16, 1u << 14,
        1u << 19, 1u << 18, 1u << 0, 1u << 2, 1u << 20, 0, 1u << 21
    };
    assert(config.numProfilesMax == NUM_PROFILES);
    for (const auto &profile : config.profiles) {
        assert(profile.enabled);
        assert(!profile.isCompetitionProfile);
        assert(memcmp(profile.keysConfig.keyMapping, expected, sizeof(expected)) == 0);
        for (const auto &combo : profile.keysConfig.keyCombinations) {
            assert(combo.gameControllerButtonMask == 0 && combo.virtualPinMask == 0);
        }
        for (const auto &macro : profile.keysConfig.macros) {
            assert(macro.numSteps == 0 && macro.numTriggerKeys == 0);
        }
    }
}

int main() {
    static_assert(offsetof(ScreenControlConfig,reserved1)==offsetof(ScreenControlConfig,txIspReturnBootMode));
    auto& storage=STORAGE_MANAGER;
    storage.initConfig();
    assert(storage.config.version==CONFIG_VERSION);
    assertDefaultHotkeys(storage.config);
    assertDefaultProfileMappings(storage.config);
    // A blank new PCB commits defaults on first boot, before any manual save.
    storage.config = {};
    assert(ConfigUtils::fromStorage(storage.config));
    assertDefaultHotkeys(storage.config);
    assertDefaultProfileMappings(storage.config);
    storage.config = {}; storage.initConfig();
    assertDefaultHotkeys(storage.config);
    assertDefaultProfileMappings(storage.config);
    // Ordinary startup preserves an existing custom binding.
    storage.config.hotkeys[2] = {3, HOTKEY_SYSTEM_REBOOT, true, false};
    storage.config.profiles[7].keysConfig.keyMapping[GAME_CONTROLLER_BUTTON_B1] = 1u << 3;
    assert(storage.saveConfig());
    storage.config = {}; storage.initConfig();
    assert(storage.config.hotkeys[2].virtualPin == 3);
    assert(storage.config.hotkeys[2].action == HOTKEY_SYSTEM_REBOOT);
    assert(storage.config.hotkeys[2].isHold);
    assert(!storage.config.hotkeys[2].isLocked);
    assert(storage.config.profiles[7].keysConfig.keyMapping[GAME_CONTROLLER_BUTTON_B1] == (1u << 3));
    assert(storage.resetConfig());
    storage.config = {}; storage.initConfig();
    assertDefaultHotkeys(storage.config);
    assertDefaultProfileMappings(storage.config);
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
