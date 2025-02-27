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
        

        // 注册ADC消息
        MC.registerMessage(MessageId::DMA_ADC_CONV_CPLT);
        MC.registerMessage(MessageId::ADC_BTNS_STATE_CHANGED);

        getFSRoot();

        // 测试qspi flash
        // QSPI_W25Qxx_ExitMemoryMappedMode();
        // APP_DBG("cpp_main: QSPI_W25Qxx_ExitMemoryMappedMode success.");
        // uint8_t data[1024];
        // for (int i = 0; i < 1024; i++) {
        //     data[i] = i;
        // }
        // QSPI_W25Qxx_WriteBuffer(data, 0x04000000, 1024);
        // APP_DBG("cpp_main: QSPI_W25Qxx_WriteBuffer success.");

        // uint8_t read_data[1024];
        // QSPI_W25Qxx_ReadBuffer(read_data, 0x04000000, 1024);
        // APP_DBG("cpp_main: QSPI_W25Qxx_ReadBuffer success.");
        // for (int i = 0; i < 1024; i++) {
        //     APP_DBG("cpp_main: read_data[%d] = 0x%02x", i, read_data[i]);
        // }

        APP_DBG("cpp_main: getFSRoot success.");

        MAIN_STATE_MACHINE.setup();

        return 0;
    }
}

// ADC转换完成回调
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
    MC.publish(MessageId::DMA_ADC_CONV_CPLT, hadc);
}

// ADC错误回调
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    uint32_t error = HAL_ADC_GetError(hadc);
    APP_DBG("ADC Error: Instance=0x%p", (void*)hadc->Instance);
    APP_DBG("State=0x%x", HAL_ADC_GetState(hadc));
    APP_DBG("Error flags: 0x%lx", error);
    
    if (error & HAL_ADC_ERROR_INTERNAL) APP_DBG("- Internal error");
    if (error & HAL_ADC_ERROR_OVR) APP_DBG("- Overrun error");
    if (error & HAL_ADC_ERROR_DMA) APP_DBG("- DMA transfer error");
}

