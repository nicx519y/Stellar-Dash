#include "connection_mode_config.hpp"
#include "board_mode.hpp"
#include "storagemanager.hpp"
#include "screen_control/spi_screen_detail_entries.hpp"
#include "screen_control/spi_screen_detail_render_helpers.hpp"
#include "main_runtime_control.hpp"
#include <cassert>
#include <cstring>
#include <string>

std::string title;
void ScreenDetailRender_List(ST7789_Handle*, const char* text, const char* const*, uint8_t count,
    uint8_t index, uint8_t selected, const ScreenUiStyle&, uint16_t, bool, int, uint32_t, uint32_t, const bool*) {
    assert(count == 2 && index < 2 && selected < 2); title=text;
}
extern "C" void MainRuntime_RequestReset() { ++resets; }
extern "C" bool MainRuntime_IsTxIspActive() { return isp; }

int main() {
    Config config = {};
    config.connectionMode=CONNECTION_MODE_RF24G;
    config.screenControl.reservedStyle[0]=ScreenConfig::kPreviousSchema;
    config.screenControl.standbyDisplay=2;
    config.screenControl.reservedStyle[1]=0; // Explicitly disabled in A1.
    config.screenControl.standbyTimeoutSeconds=120;
    config.screenControl.featuresMask=SCREEN_FEATURE_LED_SETTING;
    config.profiles[0].enabled=true;
    std::strcpy(config.profiles[0].name, "Preserved");
    const auto profile=config.profiles[0];
    const auto refs=config.lightResources[0];
    assert(ConnectionConfig::normalize(config));
    ScreenConfig::normalize(config.screenControl);
    assert(config.connectionMode==CONNECTION_MODE_USB);
    assert(config.screenControl.reservedStyle[0]==ScreenConfig::kSchema);
    assert(!ScreenConfig::enabled(config.screenControl));
    assert(config.screenControl.standbyDisplay==2 && config.screenControl.standbyTimeoutSeconds==120);
    assert(config.screenControl.featuresMask & SCREEN_FEATURE_LED_SETTING);
    assert(config.screenControl.featuresMask & SCREEN_FEATURE_TRANSPORT_MODE_SWITCH);
    assert(!std::memcmp(&profile,&config.profiles[0],sizeof(profile)));
    assert(!std::memcmp(&refs,&config.lightResources[0],sizeof(refs)));
    config.connectionMode=CONNECTION_MODE_RF24G;
    assert(!ConnectionConfig::normalize(config));
    config.connectionMode=static_cast<ConnectionMode>(255);
    assert(ConnectionConfig::normalize(config) && config.connectionMode==CONNECTION_MODE_USB);
    // No MCU headers or GPIO implementation are linked: input pin levels
    // cannot participate in software selection or introduce a debounce delay.
    BOARD_MODE.setup(BoardMode::Rf);
    assert(BOARD_MODE.isWebConfigAllowed() && !BOARD_MODE.isUsbStartupSafe());
    assert(BOARD_MODE.current()==BoardMode::Rf);
    BOARD_MODE.setup(BoardMode::Usb);
    assert(BOARD_MODE.isUsbStartupSafe());
    BOARD_MODE.setup(BoardMode::Fault);
    assert(!BOARD_MODE.isStable() && !BOARD_MODE.isWebConfigAllowed());
    BOARD_MODE.setup(BoardMode::Usb);

    STORAGE_MANAGER.config=config;
    STORAGE_MANAGER.config.bootMode=BootMode::BOOT_MODE_INPUT;
    uint8_t index=ScreenDetailConnection_InitIndex(); assert(index==0);
    ScreenDetailConnection_Rotate(&index,-128); assert(index==0);
    ScreenDetailConnection_Rotate(&index,127); assert(index==1);
    saveOk=false;
    assert(!ScreenDetailConnection_OnConfirm(index));
    assert(running && resets==0 && STORAGE_MANAGER.config.connectionMode==CONNECTION_MODE_USB);
    ST7789_Handle lcd={}; ScreenUiStyle style={};
    ScreenDetailConnection_Render(&lcd,index,style); assert(title=="Save Failed");
    saveOk=true;
    assert(ScreenDetailConnection_OnConfirm(index));
    assert(!running && resets==1 && STORAGE_MANAGER.committed.connectionMode==CONNECTION_MODE_RF24G);
    assert(BOARD_MODE.current()==BoardMode::Usb); // Not switched before reset.
    STORAGE_MANAGER.config=STORAGE_MANAGER.committed;
    assert(ScreenDetailConnection_InitIndex()==1);
    const int before=saves;
    assert(ScreenDetailConnection_OnConfirm(1) && saves==before && resets==1);
    assert(!ScreenDetailConnection_OnConfirm(2) && saves==before);
    isp=true;
    assert(!ScreenDetailConnection_OnConfirm(0) && saves==before);
}
