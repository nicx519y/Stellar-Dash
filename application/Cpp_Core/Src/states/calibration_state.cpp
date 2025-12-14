#include "states/calibration_state.hpp"
#include "adc_btns/adc_calibration.hpp"
#include "pwm-ws2812b.h"
#include "storagemanager.hpp"

#include "system_logger.h"

// 定义静态成员变量
uint32_t CalibrationState::rebootTime = 0;

void CalibrationState::allCalibrationCompletedCallback(uint8_t totalButtons, uint8_t successCount, uint8_t failedCount) {
    LOG_INFO("CALIBRATION", "Calibration completed - total: %d, success: %d, failed: %d", 
             totalButtons, successCount, failedCount);
    
    ADC_CALIBRATION_MANAGER.stopCalibration();
    STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
    STORAGE_MANAGER.saveConfig();
    
    LOG_INFO("CALIBRATION", "Boot mode changed to INPUT, system will reboot in 1 second");
    rebootTime = HAL_GetTick();
}

void CalibrationState::setup() {
    LOG_INFO("CALIBRATION", "Starting calibration state setup");
    APP_DBG("CalibrationState::setup");
    
    // 启动手动校准模式
    ADC_CALIBRATION_MANAGER.startManualCalibration();
    ADC_CALIBRATION_MANAGER.setAllCalibrationCompletedCallback(allCalibrationCompletedCallback);
    
    isRunning = true;
    rebootTime = 0;
    
    LOG_INFO("CALIBRATION", "Calibration state setup completed - waiting for user input");
    Logger_Flush();
}

void CalibrationState::loop() {
    if (isRunning) {
        // 如果校准完成，等待1秒后重启
        if(rebootTime > 0 && HAL_GetTick() - rebootTime >= 1000) {
            LOG_INFO("CALIBRATION", "Initiating system reboot after calibration completion");
            Logger_Flush(); // 确保日志被写入Flash
            NVIC_SystemReset();
        } else {    
            ADC_CALIBRATION_MANAGER.processCalibration();
        }
        // 可根据需要添加更多校准相关的处理逻辑
    }
}

void CalibrationState::reset() {
    isRunning = false;
    rebootTime = 0;
    
    // 重置状态标志
    static bool calibrationStatusLogged = false;
    calibrationStatusLogged = false;
    
    LOG_DEBUG("CALIBRATION", "Calibration state reset completed");
    // 可根据需要添加退出校准模式的处理逻辑
} 


