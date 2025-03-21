#include "main_state_machine.hpp"

void MainStateMachine::setup()
{
    APP_DBG("MainStateMachine::setup");
    STORAGE_MANAGER.initConfig();
    APP_DBG("Storage initConfig success.");

    // BootMode bootMode = STORAGE_MANAGER.getBootMode();
    BootMode bootMode = BOOT_MODE_INPUT;
    APP_DBG("BootMode: %d", bootMode);

    switch(bootMode) {
        case BootMode::BOOT_MODE_WEB_CONFIG:
            state = &WEB_CONFIG_STATE;
            break;
        case BootMode::BOOT_MODE_INPUT:
            state = &INPUT_STATE;
            break;
    }

    state->setup();

    while(1) {
        state->loop();
    }

}
