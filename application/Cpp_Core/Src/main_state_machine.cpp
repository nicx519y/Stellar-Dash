#include "main_state_machine.hpp"
#include "states/calibration_state.hpp"
#include "system_logger.h"
#include "adc_btns/adc_manager.hpp"
#include "tusb.h"
extern "C" {
#include "spi-st7789.h"
}

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
            STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT); // 设置为输入模式，保证下次启动时进入输入模式
            STORAGE_MANAGER.saveConfig();
            LOG_INFO("MAIN_STATE_MACHINE", "Entering CALIBRATION_STATE");
            break;
    }

    state->setup();

    APP_DBG("MainStateMachine::setup SPIST7789_Init()");
    SPIST7789_Init();
    SPIST7789_SetBacklight100();
    (void)SPIST7789_FillBlueAsync();
    APP_DBG("MainStateMachine::setup SPIST7789_Init() done");

    uint32_t last_alive_ms = HAL_GetTick();
    uint32_t alive_count = 0;
    uint8_t loop_stage = 0;
    uint32_t last_fill_ms = HAL_GetTick();

    while(1) {
        // 执行状态机循环
        loop_stage = 1;
        state->loop();
        loop_stage = 2;

        loop_stage = 3;
        uint32_t now_ms = HAL_GetTick();
        SPIST7789_Service();
        if ((uint32_t)(now_ms - last_fill_ms) >= 2000u) {
            if (!SPIST7789_IsBusy()) {
                (void)SPIST7789_FillBlueAsync();
            }
            last_fill_ms = now_ms;
        }
        loop_stage = 4;

        alive_count++;
        if (SPIST7789_ConsumeDmaDoneFlag()) {
            APP_DBG("[SPIST7789] dma done (cs high)");
        }
        if (SPIST7789_ConsumeDmaErrFlag()) {
            APP_DBG("[SPIST7789] dma err");
        }
        if ((uint32_t)(now_ms - last_alive_ms) >= 1000u) {
            uint32_t ndtr = 0, dcr = 0, disr = 0, ssr = 0, scfg1 = 0, scr1 = 0, scr2 = 0;
            SPIST7789_GetDebug(&ndtr, &dcr, &disr, &ssr, &scfg1, &scr1, &scr2);
            APP_DBG("[MAIN] alive n=%lu stage=%u lcd_busy=%u ndtr=%lu dcr=0x%08lX disr=0x%08lX ssr=0x%08lX scfg1=0x%08lX scr1=0x%08lX scr2=0x%08lX",
                    (unsigned long)alive_count,
                    (unsigned)loop_stage,
                    SPIST7789_IsBusy() ? 1u : 0u,
                    (unsigned long)ndtr,
                    (unsigned long)dcr,
                    (unsigned long)disr,
                    (unsigned long)ssr,
                    (unsigned long)scfg1,
                    (unsigned long)scr1,
                    (unsigned long)scr2);
            alive_count = 0;
            last_alive_ms = now_ms;
        }
    }

}
