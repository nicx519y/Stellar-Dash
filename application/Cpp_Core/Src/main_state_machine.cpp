#include "main_state_machine.hpp"
#include "states/calibration_state.hpp"
#include "system_logger.h"

void MainStateMachine::setup()
{
    APP_DBG("MainStateMachine::setup");
    STORAGE_MANAGER.initConfig();
    APP_DBG("Storage initConfig success.");

    BootMode bootMode = STORAGE_MANAGER.getBootMode();
    // BootMode bootMode = BOOT_MODE_INPUT;
    // BootMode bootMode = BOOT_MODE_WEB_CONFIG;
    LOG_INFO("MAIN_STATE_MACHINE", "BootMode: %d", bootMode);

    switch(bootMode) {
    case BootMode::BOOT_MODE_WEB_CONFIG:
            state = &WEB_CONFIG_STATE;
            LOG_INFO("MAIN_STATE_MACHINE", "Entering WEB_CONFIG_STATE");
            break;
        case BootMode::BOOT_MODE_INPUT:
            state = &INPUT_STATE;
            LOG_INFO("MAIN_STATE_MACHINE", "Entering INPUT_STATE");
            break;
        case BootMode::BOOT_MODE_CALIBRATION:
            state = &CALIBRATION_STATE;
            STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT); // 设置为输入模式，保证下次启动时进入输入模式
            STORAGE_MANAGER.saveConfig();
            LOG_INFO("MAIN_STATE_MACHINE", "Entering CALIBRATION_STATE");
            break;
    }

    state->setup();

    while(1) {
        // 执行状态机循环
        state->loop();
    }

}
