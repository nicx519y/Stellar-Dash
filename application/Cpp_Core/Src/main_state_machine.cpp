#include "main_state_machine.hpp"
#include "states/calibration_state.hpp"
#include "system_logger.h"
#include "adc_btns/adc_manager.hpp"
#include "screen_control/spi_screen_manager.hpp"
#include "power_manager.hpp"
#include "board_power.hpp"
#include "board_cfg.h"
#include "connection_manager.hpp"
#include "system_sleep_manager.hpp"
#include "board_mode.hpp"

void MainStateMachine::setup()
{
    APP_DBG("MainStateMachine::setup");
    BOARD_MODE.setup();
    APP_STAGE("A11", "physical mode sampled: mode=%u stable=%u",
              static_cast<unsigned>(BOARD_MODE.current()),
              BOARD_MODE.isStable() ? 1u : 0u);
    STORAGE_MANAGER.initConfig();
    APP_DBG("Storage initConfig success.");

    BootMode bootMode = STORAGE_MANAGER.getBootMode();
    APP_STAGE("A12", "configuration loaded: boot mode=%u",
              static_cast<unsigned>(bootMode));
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
            APP_STAGE("A13", "selected WEB_CONFIG state");
            LOG_INFO("MAIN_STATE_MACHINE", "Entering WEB_CONFIG_STATE");
            break;
        case BootMode::BOOT_MODE_INPUT:
            // INPUT uses continuous circular DMA; report ticks read the latest DMA data.
            ADCManager::getInstance().setADCMode(ADC_MODE_INPUT_CONTINUOUS);
            
            state = &INPUT_STATE;
            APP_STAGE("A13", "selected INPUT state");
            LOG_INFO("MAIN_STATE_MACHINE", "Entering INPUT_STATE");
            break;
        case BootMode::BOOT_MODE_CALIBRATION:

            state = &CALIBRATION_STATE;
            APP_STAGE("A13", "selected CALIBRATION state");
            LOG_INFO("MAIN_STATE_MACHINE", "Entering CALIBRATION_STATE");
            break;
    }

    /*
     * The local display is the product's recovery UI.  Bring it up before
     * probing optional power-management peripherals or negotiating a CH585
     * role so a missing/stuck auxiliary device cannot leave the unit black.
     */
    BOARD_POWER.enterRecoveryUiState();
    APP_STAGE("A14", "local recovery UI power state entered");
    SPIScreenManager::getInstance().setup();
    SPIScreenManager::getInstance().loop();
    APP_STAGE("A15", "screen setup requested and first frame serviced");

    POWER_MANAGER.setup();
    APP_STAGE("A16", "power manager initialized");
    state->setup();
    APP_STAGE("A17", "selected state setup returned");
    SystemSleep_HandleWakeRecovery();
    APP_STAGE("A18", "wake recovery processed");

    APP_STAGE("A19", "main service loop active");
    while(1) {
        BOARD_MODE.update(HAL_GetTick());
        
        state->loop();

        CONNECTION_MANAGER.loop();
        POWER_MANAGER.loop();
        SPIScreenManager::getInstance().loop();

    }

}
