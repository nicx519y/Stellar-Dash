#include "main_state_machine.hpp"
#include "states/calibration_state.hpp"
void MainStateMachine::setup()
{
    APP_DBG("MainStateMachine::setup");
    STORAGE_MANAGER.initConfig();
    APP_DBG("Storage initConfig success.");

    BootMode bootMode = STORAGE_MANAGER.getBootMode();
    // BootMode bootMode = BOOT_MODE_INPUT;
    // BootMode bootMode = BOOT_MODE_WEB_CONFIG;
    
    APP_DBG("BootMode: %d", bootMode);

    switch(bootMode) {
    case BootMode::BOOT_MODE_WEB_CONFIG:
            state = &WEB_CONFIG_STATE;
            break;
        case BootMode::BOOT_MODE_INPUT:
            state = &INPUT_STATE;
            break;
        case BootMode::BOOT_MODE_CALIBRATION:
            state = &CALIBRATION_STATE;
            STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT); // 设置为输入模式，保证下次启动时进入输入模式
            STORAGE_MANAGER.saveConfig();
            break;
    }

    state->setup();

    while(1) {
        state->loop();
    }

}
