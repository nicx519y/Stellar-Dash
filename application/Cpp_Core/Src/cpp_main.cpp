#include "cpp_main.hpp"
#include <stdio.h>
#include "bsp/board_api.h"
#include "main_state_machine.hpp"
// #include "fsdata.h"
#include "qspi-w25q64.h"
#include "pwm-ws2812b.h"
#include "message_center.hpp"
#include "adc.h"
#include "tusb.h"
#include "adc_btns/adc_manager.hpp"
#include "micro_timer.hpp"

extern "C" {
    int cpp_main(void) 
    {   
        MAIN_STATE_MACHINE.setup();


        // DRIVER_MANAGER.setup(InputMode::INPUT_MODE_XINPUT);
        // GPDriver* inputDriver = DRIVER_MANAGER.getDriver();
        // tud_init(TUD_OPT_RHPORT); // 初始化TinyUSB
        // while(1) {
        //     tud_task();
        // }
        
        // adc_test();

        

        return 0;
    }
}

/**
 * 重写tusb_time_millis_api和tusb_time_delay_ms_api for STM32
 */
uint32_t tusb_time_millis_api(void) {
    return HAL_GetTick();
}

void tusb_time_delay_ms_api(uint32_t ms) {
    HAL_Delay(ms);
}

void adc_test(void) {
    ADCManager::getInstance().startADCSamping(false);
    WS2812B_Test();

    int8_t brightness = 0; 
    int8_t direction = 1;
    uint8_t step = 4;
    uint8_t max_brightness = 80;
    uint8_t delay_time = 20;


    uint32_t last_time = MICROS_TIMER.micros();

    while(1) {
        if(MICROS_TIMER.checkInterval(delay_time * 1000, last_time)) {
            // 柔和的控制灯光亮度 模拟呼吸灯
            // APP_DBG("brightness: %d, direction: %d", brightness, direction);
            WS2812B_SetAllLEDBrightness(uint8_t(brightness));
            brightness += direction * step;
            if(brightness >= max_brightness) {
                brightness = max_brightness;
                direction = -1;
            } else if(brightness <= 0) {
                brightness = 0;
                    direction = 1;
            }

            ADCManager::getInstance().ADCValuesTestPrint();
        }
    }
}
