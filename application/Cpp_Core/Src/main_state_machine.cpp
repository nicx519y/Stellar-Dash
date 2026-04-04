#include "main_state_machine.hpp"
#include "states/calibration_state.hpp"
#include "system_logger.h"
#include "adc_btns/adc_manager.hpp"
#include "screen_control/spi_screen_manager.hpp"
#include "tusb.h"

void MainStateMachine::setup()
{
    APP_DBG("MainStateMachine::setup");
    STORAGE_MANAGER.initConfig();
    APP_DBG("Storage initConfig success.");

    BootMode bootMode = STORAGE_MANAGER.getBootMode();
    // BootMode bootMode = BOOT_MODE_INPUT;
    // BootMode bootMode = BOOT_MODE_WEB_CONFIG;
    // LOG_INFO("MAIN_STATE_MACHINE", "BootMode: %d", bootMode);

    switch(bootMode) {
    case BootMode::BOOT_MODE_WEB_CONFIG:
            
            
            state = &WEB_CONFIG_STATE;
            LOG_INFO("MAIN_STATE_MACHINE", "Entering WEB_CONFIG_STATE");
            break;
        case BootMode::BOOT_MODE_INPUT:
            // 切换到低延迟模式 (SOF触发)
            ADCManager::getInstance().setADCMode(ADC_MODE_LOW_LATENCY);
            
            state = &INPUT_STATE;
            LOG_INFO("MAIN_STATE_MACHINE", "Entering INPUT_STATE");
            break;
        case BootMode::BOOT_MODE_CALIBRATION:

            state = &CALIBRATION_STATE;
            LOG_INFO("MAIN_STATE_MACHINE", "Entering CALIBRATION_STATE");
            break;
    }

    state->setup();
    APP_DBG("MainStateMachine::setup.");
    SPIScreenManager::getInstance().setup();
    APP_DBG("MainStateMachine::setup done.");

    while(1) {
        state->loop();
        SPIScreenManager::getInstance().loop();
    }

}
