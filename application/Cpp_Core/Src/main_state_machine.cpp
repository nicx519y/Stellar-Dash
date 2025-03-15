#include "main_state_machine.hpp"

MainStateMachine::MainStateMachine()
{

}

void MainStateMachine::setup()
{
    APP_DBG("MainStateMachine::setup");
    STORAGE_MANAGER.initConfig();
    APP_DBG("Storage initConfig success.");

    // BootMode bootMode = Storage::getInstance().config.bootMode;
    BootMode bootMode = BootMode::BOOT_MODE_WEB_CONFIG;
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
