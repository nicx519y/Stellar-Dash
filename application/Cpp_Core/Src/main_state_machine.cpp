#include "main_state_machine.hpp"
#include "states/calibration_state.hpp"
#include "system_logger.h"
#include "adc_btns/adc_manager.hpp"
#include "screen_control/spi_screen_manager.hpp"
#include "tusb.h"
#include "power_manager.hpp"
#include "board_cfg.h"
#include "connection_manager.hpp"

void MainStateMachine::setup()
{
    APP_DBG("MainStateMachine::setup");
    STORAGE_MANAGER.initConfig();
    APP_DBG("Storage initConfig success.");

    BootMode bootMode = STORAGE_MANAGER.getBootMode();
#if RF24G_SPI_TEST_FORCE_RF24G
    bootMode = BootMode::BOOT_MODE_INPUT;
    APP_DBG("[RF_SPI_TEST] force boot mode INPUT");
#endif
    // BootMode bootMode = BOOT_MODE_INPUT;
    // BootMode bootMode = BOOT_MODE_WEB_CONFIG;
    // LOG_INFO("MAIN_STATE_MACHINE", "BootMode: %d", bootMode);

    switch(bootMode) {
    case BootMode::BOOT_MODE_WEB_CONFIG:
            state = &WEB_CONFIG_STATE;
            LOG_INFO("MAIN_STATE_MACHINE", "Entering WEB_CONFIG_STATE");
            break;
        case BootMode::BOOT_MODE_INPUT:
            // INPUT uses continuous circular DMA; report ticks read the latest DMA data.
            ADCManager::getInstance().setADCMode(ADC_MODE_INPUT_CONTINUOUS);
            
            state = &INPUT_STATE;
            LOG_INFO("MAIN_STATE_MACHINE", "Entering INPUT_STATE");
            break;
        case BootMode::BOOT_MODE_CALIBRATION:

            state = &CALIBRATION_STATE;
            LOG_INFO("MAIN_STATE_MACHINE", "Entering CALIBRATION_STATE");
            break;
    }

    POWER_MANAGER.setup();
    state->setup();

    SPIScreenManager::getInstance().setup();

    while(1) {
        
        state->loop();

        CONNECTION_MANAGER.loop();
        POWER_MANAGER.loop();
        SPIScreenManager::getInstance().loop();

    }

}
