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
    SPIScreenManager::getInstance().setup();

#if APPLICATION_DEBUG_PRINT
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    const uint32_t cycles_per_us = (SYSTEM_CLOCK_FREQ / 1000000UL);
    uint64_t screen_loop_acc_us = 0;
    uint32_t screen_loop_count = 0;
    uint32_t screen_loop_last_log_ms = HAL_GetTick();
#endif

    while(1) {
        
        state->loop();
        POWER_MANAGER.loop();

#if APPLICATION_DEBUG_PRINT
        uint32_t t0_cycles = DWT->CYCCNT;
#endif

        SPIScreenManager::getInstance().loop();

    }

}
