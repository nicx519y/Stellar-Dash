#include <tx_isp_runtime_stubs.hpp>
#define private public
#include "main_state_machine.hpp"
#undef private
#include "main_runtime_control.hpp"
#include "states/tx_isp_state.hpp"
#include "screen_control/spi_screen_detail_entries.hpp"
#include "screen_control/spi_screen_main_list.hpp"
#include <cassert>
#include <cstring>
#include <iostream>

bool boot_mode_to_detail_menu(BootMode mode,uint8_t* menu);

int main() {
    auto& machine=MAIN_STATE_MACHINE;
    // Screen setup precedes state entry: persisted ISP must already select its
    // own page even with the physical USB/RF gate closed.
    uint8_t startupMenu=0;
    BOARD_MODE.allowed=false;
    assert(!MainRuntime_IsTxIspActive());
    assert(boot_mode_to_detail_menu(BootMode::BOOT_MODE_TX_ISP,&startupMenu));
    assert(startupMenu==SCREEN_MENU_TX_ISP);
    assert(!boot_mode_to_detail_menu(BootMode::BOOT_MODE_WEB_CONFIG,&startupMenu));
    assert(!boot_mode_to_detail_menu(BootMode::BOOT_MODE_TX_ISP,nullptr));
    BOARD_MODE.allowed=true;
    assert(machine.requestTransition(MainRuntimeState::Input));
    // Entry is deferred: the encoder callback cannot tear down the active LCD frame.
    ScreenDetailTxIsp_InitIndex();
    assert(!ScreenDetailTxIsp_OnConfirm(0));
    assert(!MainRuntime_IsTxIspActive() && INPUT_STATE.exits==0);
    assert(!MainRuntime_RequestTxIsp(true)); // already queued
    machine.servicePendingTransition();
    assert(MainRuntime_IsTxIspActive() && INPUT_STATE.exits==1);
    assert(STORAGE_MANAGER.committedBoot==BootMode::BOOT_MODE_TX_ISP);
    assert(STORAGE_MANAGER.committedReturnMode==BootMode::BOOT_MODE_INPUT);
    assert(BOARD_POWER.tx && BOARD_POWER.main && BOARD_POWER.lcd && !BOARD_POWER.host);
    assert(nowMs==20 && gpioAnalog==(CH585_SPI_SCK_PIN|CH585_SPI_MOSI_PIN|CH585_SPI_MISO_PIN));
    assert(gpioNss==CH585_SPI_NSS_PIN && nssLatch==CH585_SPI_NSS_PIN);
    assert(SPIScreenManager::getInstance().ispPages==1);

    const auto writes=txPower.size(); const int oldConn=CONNECTION_MANAGER.loops;
    BOARD_MODE.mode=BoardMode::Rf;
    for(int i=0;i<100;++i) { nowMs+=10000; TX_ISP_STATE.tick(); machine.serviceSharedRuntime(); }
    assert(BOARD_POWER.tx && txPower.size()==writes && CONNECTION_MANAGER.loops==oldConn);
    assert(!sleepInput && POWER_MANAGER.loops==100);
    machine.requestReset(); assert(!machine.resetPending && resets==0);
    ST7789_Handle lcd; ScreenUiStyle style={};
    ScreenDetailTxIsp_Render(&lcd,0,style); assert(rendered.find("power held on")!=std::string::npos);
    // Back/Finish only opens the confirmation, and Back cancels it without power loss.
    assert(!ScreenDetailTxIsp_OnBack()); assert(std::strcmp(ScreenDetailTxIsp_ConfirmLabel(),"Exit")==0);
    assert(!ScreenDetailTxIsp_OnBack()); assert(std::strcmp(ScreenDetailTxIsp_ConfirmLabel(),"Finish")==0);
    assert(BOARD_POWER.tx);
    assert(!ScreenDetailTxIsp_OnConfirm(0));
    ScreenDetailTxIsp_Render(&lcd,0,style); assert(rendered.find("Remove PB22")!=std::string::npos);
    assert(ScreenDetailTxIsp_OnConfirm(0)); assert(BOARD_POWER.tx); // exit also deferred
    machine.servicePendingTransition();
    assert(machine.current()==MainRuntimeState::Input && INPUT_STATE.entries==2 && !BOARD_POWER.tx);
    assert(STORAGE_MANAGER.committedBoot==BootMode::BOOT_MODE_INPUT && STORAGE_MANAGER.committedReturnMode==0);

    // Failed entry commit retains Input, resumes sampling and never cycles TX.
    STORAGE_MANAGER.saveOk=false;
    const auto failedWrites=txPower.size();
    assert(MainRuntime_RequestTxIsp(true)); machine.servicePendingTransition();
    assert(MainRuntime_TxIspTransitionFailed() && machine.current()==MainRuntimeState::Input);
    assert(txPower.size()==failedWrites && !INPUT_STATE.suspended);
    assert(STORAGE_MANAGER.committedBoot==BootMode::BOOT_MODE_INPUT);
    ScreenDetailTxIsp_Render(&lcd,0,style); assert(rendered.find("Try again")!=std::string::npos);
    STORAGE_MANAGER.saveOk=true;

    // Refuse every update owner before touching either the old state or TX power.
    RELEASE_INSTALLER.owned=true; const int exits=INPUT_STATE.exits;
    assert(!MainRuntime_RequestTxIsp(true)); RELEASE_INSTALLER.owned=false;
    CH585_FIRMWARE_UPDATE.pending=true; assert(!MainRuntime_RequestTxIsp(true));
    CH585_FIRMWARE_UPDATE.pending=false;
    for(auto value:{Ch585FirmwareUpdateStatus::Receiving,Ch585FirmwareUpdateStatus::Scheduled,Ch585FirmwareUpdateStatus::Programming}) {
        CH585_FIRMWARE_UPDATE.value=value; assert(!MainRuntime_RequestTxIsp(true));
    }
    CH585_FIRMWARE_UPDATE.value=Ch585FirmwareUpdateStatus::Idle;
    assert(INPUT_STATE.exits==exits);
    assert(MainRuntime_RequestTxIsp(true)); RELEASE_INSTALLER.owned=true;
    const int savesBeforeBlocked=STORAGE_MANAGER.saves;
    machine.servicePendingTransition(); assert(machine.current()==MainRuntimeState::Input && INPUT_STATE.exits==exits);
    assert(STORAGE_MANAGER.saves==savesBeforeBlocked && MainRuntime_TxIspTransitionFailed());
    RELEASE_INSTALLER.owned=false;
    // Preserve a WebConfig origin, including its persisted boot mode.
    assert(machine.requestTransition(MainRuntimeState::WebConfig));
    STORAGE_MANAGER.boot=BootMode::BOOT_MODE_WEB_CONFIG;
    assert(MainRuntime_RequestTxIsp(true)); machine.servicePendingTransition();
    assert(MainRuntime_RequestTxIsp(false)); machine.servicePendingTransition();
    assert(machine.current()==MainRuntimeState::WebConfig && STORAGE_MANAGER.boot==BootMode::BOOT_MODE_WEB_CONFIG);

    // Cold boot restores ISP and its return mode before normal TX pre-start.
    for(auto origin:{MainRuntimeState::Input,MainRuntimeState::WebConfig,MainRuntimeState::SafeRecovery}) {
        assert(machine.requestTransition(origin));
        assert(MainRuntime_RequestTxIsp(true)); machine.servicePendingTransition();
        assert(machine.current()==MainRuntimeState::TxIsp);
        assert(STORAGE_MANAGER.committedBoot==BootMode::BOOT_MODE_TX_ISP);
        const uint16_t committed=STORAGE_MANAGER.committedReturnMode;
        const auto activeWrites=txPower.size();
        STORAGE_MANAGER.saveOk=false;
        assert(MainRuntime_RequestTxIsp(false)); machine.servicePendingTransition();
        assert(MainRuntime_TxIspTransitionFailed() && MainRuntime_IsTxIspActive());
        assert(BOARD_POWER.tx && txPower.size()==activeWrites && STORAGE_MANAGER.committedReturnMode==committed);
        assert(STORAGE_MANAGER.boot==BootMode::BOOT_MODE_TX_ISP && STORAGE_MANAGER.committedBoot==BootMode::BOOT_MODE_TX_ISP);
        // Simulate reset/power-on with no in-memory dispatcher/return state.
        machine.state=nullptr; machine.currentState=MainRuntimeState::SafeRecovery;
        machine.interactiveRuntimeInitialized=false; machine.ispReturnState=MainRuntimeState::Input;
        const int prepares=CH585_ROLE_BOOTSTRAP.prepares;
        machine.initializeInteractiveRuntime(true);
        assert(machine.resolveNormalStartupState()==MainRuntimeState::TxIsp);
        assert(CH585_ROLE_BOOTSTRAP.prepares==prepares);
        assert(machine.enterState(machine.resolveNormalStartupState()));
        assert(MainRuntime_IsTxIspActive() && BOARD_POWER.tx && machine.ispReturnState==origin);
        STORAGE_MANAGER.saveOk=true;
        assert(MainRuntime_RequestTxIsp(false)); machine.servicePendingTransition();
        assert(machine.current()==origin && STORAGE_MANAGER.committedReturnMode==0);
        assert(STORAGE_MANAGER.committedBoot!=BootMode::BOOT_MODE_TX_ISP);
        // Persisted exit must survive the next configuration load too.
        STORAGE_MANAGER.initConfig(); assert(machine.resolveNormalStartupState()!=MainRuntimeState::TxIsp);
    }
    assert(machine.requestTransition(MainRuntimeState::WebConfig));
    // Port stop failure never turns TX back on; normal recovery owns the fallback.
    shutdownOk=false; assert(MainRuntime_RequestTxIsp(true)); machine.servicePendingTransition();
    assert(machine.current()==MainRuntimeState::SafeRecovery && !BOARD_POWER.tx);
    shutdownOk=true;
    assert(machine.requestTransition(MainRuntimeState::Calibration));
    assert(!MainRuntime_RequestTxIsp(true));
    assert(machine.requestTransition(MainRuntimeState::Ch585BridgeUpdate));
    assert(!MainRuntime_RequestTxIsp(true));

    // Maintenance entry is hidden; normal menu order and USB gating stay intact.
    ScreenControlConfig config; for(int i=0;i<12;++i)config.featuresOrder[i]=i;
    uint8_t ids[16]={}; BOARD_MODE.allowed=true;
    assert(ScreenMain_RebuildMenuIds(config,ids,16)==12);
    for(unsigned i=0;i<12;++i) assert(ids[i]==i && ids[i]!=SCREEN_MENU_TX_ISP);
    BOARD_MODE.allowed=false; config.featuresMask=1u<<1;
    assert(ScreenMain_RebuildMenuIds(config,ids,16)==1 && ids[0]==1);
    config.featuresMask=0;
    const auto count=ScreenMain_RebuildMenuIds(config,ids,16);
    assert(count==11);
    for(unsigned i=0;i<count;++i) assert(ids[i]!=9 && ids[i]!=SCREEN_MENU_TX_ISP);
    uint8_t bounded[2]={0,0xA5};
    assert(ScreenMain_RebuildMenuIds(config,bounded,1)==1 && bounded[1]==0xA5);
    assert(ScreenMain_RebuildMenuIds(config,nullptr,16)==0);
    std::cout << "TX ISP runtime/menu tests passed\n";
}
