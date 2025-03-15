#include "cpp_main.hpp"
#include <stdio.h>
#include "bsp/board_api.h"
#include "main_state_machine.hpp"
// #include "fsdata.h"
#include "qspi-w25q64.h"
#include "pwm-ws2812b.h"
#include "message_center.hpp"
#include "adc.h"

extern "C" {
    int cpp_main(void) 
    {   
        
        // WS2812B_Test();
        MAIN_STATE_MACHINE.setup();


        // DRIVER_MANAGER.setup(InputMode::INPUT_MODE_XINPUT);
        // GPDriver* inputDriver = DRIVER_MANAGER.getDriver();
        // tud_init(TUD_OPT_RHPORT); // 初始化TinyUSB
        // while(1) {
        //     tud_task();
        // }
        

        return 0;
    }
}


