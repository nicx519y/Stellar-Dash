#include "main_state_machine.hpp"
#include "states/calibration_state.hpp"
#include "system_logger.h"
#include "adc_btns/adc_manager.hpp"
#include "screen_control/spi_screen_manager.hpp"
#include "tusb.h"
#include "power_manager.hpp"

#if APPLICATION_DEBUG_PRINT
    #include "board_cfg.h"
#endif

#ifndef RF24G_SPI_BRINGUP_FASTPATH
#define RF24G_SPI_BRINGUP_FASTPATH 1
#endif

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

    POWER_MANAGER.setup();
    state->setup();

    const bool rf24gFastPath =
#if RF24G_SPI_BRINGUP_FASTPATH
        (bootMode == BootMode::BOOT_MODE_INPUT) &&
        (STORAGE_MANAGER.getConnectionMode() == ConnectionMode::CONNECTION_MODE_RF24G);
#else
        false;
#endif

    if (!rf24gFastPath) {
        SPIScreenManager::getInstance().setup();
    } else {
        APP_DBG("[RF_BRIDGE] RF24G fast path: screen loop disabled for 8K SPI bring-up");
    }

    uint32_t lastPowerLoopMs = HAL_GetTick();

    while(1) {
        
        state->loop();

        if (rf24gFastPath) {
            const uint32_t nowMs = HAL_GetTick();
            if ((uint32_t)(nowMs - lastPowerLoopMs) >= 1000u) {
                lastPowerLoopMs = nowMs;
                POWER_MANAGER.loop();
            }
        } else {
            POWER_MANAGER.loop();
        }

        if (!rf24gFastPath) {
            SPIScreenManager::getInstance().loop();
        }

    }

}
