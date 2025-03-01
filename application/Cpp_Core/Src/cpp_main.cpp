#include "cpp_main.hpp"
#include <stdio.h>
#include "bsp/board_api.h"
#include "board_cfg.h"
#include "main_state_machine.hpp"
// #include "fsdata.h"
#include "qspi-w25q64.h"
#include "message_center.hpp"
#include "adc.h"

extern "C" {
    int cpp_main(void) 
    {   
        
        MAIN_STATE_MACHINE.setup();

        return 0;
    }
}


