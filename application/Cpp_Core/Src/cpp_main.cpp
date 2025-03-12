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


        // ADC_MANAGER.startADCSamping();
        // while(1) {
        //     ADC_MANAGER.ADCValuesTestPrint();
        // }

        return 0;
    }
}


