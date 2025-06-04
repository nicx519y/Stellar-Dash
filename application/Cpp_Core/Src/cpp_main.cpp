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
#include "leds/leds_manager.hpp"
#include "adc_btns/adc_calibration.hpp"

extern "C" {
    int cpp_main(void) 
    {   
        // 注释掉原有的状态机启动，用于手动校准测试
        MAIN_STATE_MACHINE.setup();

        // 手动校准测试
        // manual_calibration_test();

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

/**
 * 手动校准测试函数
 */
void manual_calibration_test(void) {
    APP_DBG("Starting manual calibration test...");
    
    // 初始化ADC管理器
    ADCManager::getInstance().startADCSamping(false);
    // 等待3秒
    HAL_Delay(1000);
    
    // 开始手动校准
    APP_DBG("Starting manual calibration...");
    ADC_CALIBRATION_MANAGER.resetAllCalibration();
    ADCBtnsError result = ADC_CALIBRATION_MANAGER.startManualCalibration();
    
    if (result == ADCBtnsError::SUCCESS) {
        uint8_t activeCount = ADC_CALIBRATION_MANAGER.getActiveCalibrationButtonCount();
        uint8_t uncalibratedCount = ADC_CALIBRATION_MANAGER.getUncalibratedButtonCount();
        APP_DBG("Manual calibration started successfully");
        APP_DBG("Active calibration buttons: %d", activeCount);
        APP_DBG("Uncalibrated buttons: %d", uncalibratedCount);
    } else {
        APP_ERR("Failed to start manual calibration, error: %d", static_cast<int>(result));
        return;
    }
    
    // 主循环 - 处理校准逻辑
    uint32_t last_status_update = HAL_GetTick();
    uint32_t last_adc_print = HAL_GetTick();
    
    while (true) {
        // 处理校准逻辑
        ADC_CALIBRATION_MANAGER.processCalibration();
        
        // 每秒更新一次状态
        uint32_t now = HAL_GetTick();
        if (now - last_status_update > 1000) {
            // print_calibration_status();
            last_status_update = now;
            
            // 检查是否全部完成
            if (!ADC_CALIBRATION_MANAGER.isCalibrationActive()) {
                APP_DBG("All buttons calibration completed!");
                break;
            }
        }
        
        // 每5秒打印一次ADC值，便于调试
        if (now - last_adc_print > 5000) {
            ADCManager::getInstance().ADCValuesTestPrint();
            last_adc_print = now;
        }
        
        // 短暂延时避免CPU占用过高
        HAL_Delay(10);
    }
    
    // 校准完成后的处理
    APP_DBG("Manual calibration test completed!");
    
    // 显示最终结果
    print_final_calibration_results();
    
    // 保持LED显示最终状态
    while (true) {
        HAL_Delay(1000);
        APP_DBG("Calibration test finished. System will stay in this state.");
    }
}

/**
 * 打印校准状态
 */
void print_calibration_status(void) {
    if (!ADC_CALIBRATION_MANAGER.isCalibrationActive()) {
        return;
    }
    
    APP_DBG("=== Calibration Status ===");
    
    for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
        const char* phase_str = "";
        const char* led_color_str = "";
        
        CalibrationPhase phase = ADC_CALIBRATION_MANAGER.getButtonPhase(i);
        CalibrationLEDColor color = ADC_CALIBRATION_MANAGER.getButtonLEDColor(i);
        
        switch (phase) {
            case CalibrationPhase::IDLE:
                phase_str = "IDLE";
                break;
            case CalibrationPhase::TOP_SAMPLING:
                phase_str = "TOP_SAMPLING (release)";
                break;
            case CalibrationPhase::BOTTOM_SAMPLING:
                phase_str = "BOTTOM_SAMPLING (pressed)";
                break;
            case CalibrationPhase::COMPLETED:
                phase_str = "COMPLETED";
                break;
            case CalibrationPhase::ERROR:
                phase_str = "ERROR";
                break;
        }
        
        switch (color) {
            case CalibrationLEDColor::RED:
                led_color_str = "RED";
                break;
            case CalibrationLEDColor::CYAN:
                led_color_str = "CYAN";
                break;
            case CalibrationLEDColor::DARK_BLUE:
                led_color_str = "DARK_BLUE";
                break;
            case CalibrationLEDColor::GREEN:
                led_color_str = "GREEN";
                break;
            case CalibrationLEDColor::YELLOW:
                led_color_str = "YELLOW";
                break;
            default:
                led_color_str = "OFF";
                break;
        }
        
        APP_DBG("Button%d: %s [%s]", i, phase_str, led_color_str);
        
        if (ADC_CALIBRATION_MANAGER.isButtonCalibrated(i)) {
            uint16_t topValue, bottomValue;
            if (ADC_CALIBRATION_MANAGER.getCalibrationValues(i, topValue, bottomValue) == ADCBtnsError::SUCCESS) {
                APP_DBG("  Calibration values: top=%d, bottom=%d", topValue, bottomValue);
            }
        }
    }
    
    uint8_t activeCount = ADC_CALIBRATION_MANAGER.getActiveCalibrationButtonCount();
    uint8_t uncalibratedCount = ADC_CALIBRATION_MANAGER.getUncalibratedButtonCount();
    
    APP_DBG("Progress: %d active, %d uncalibrated", activeCount, uncalibratedCount);
    APP_DBG("==========================");
}

/**
 * 打印最终校准结果
 */
void print_final_calibration_results(void) {
    APP_DBG("=== Final Calibration Results ===");
    
    // 使用校准管理器自己的详细打印功能
    ADC_CALIBRATION_MANAGER.printAllCalibrationResults();
    
    // 简化的状态确认
    if (ADC_CALIBRATION_MANAGER.isAllButtonsCalibrated()) {
        APP_DBG("✓ All buttons successfully calibrated!");
    } else {
        uint8_t calibratedCount = 0;
        for (uint8_t i = 0; i < NUM_ADC_BUTTONS; i++) {
            if (ADC_CALIBRATION_MANAGER.isButtonCalibrated(i)) {
                calibratedCount++;
            }
        }
        APP_DBG("⚠ Partial calibration: %d/%d buttons completed", calibratedCount, NUM_ADC_BUTTONS);
    }
    
    APP_DBG("==================================");
}
