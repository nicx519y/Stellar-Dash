#include "states/calibration_state.hpp"
#include "adc_btns/adc_calibration.hpp"
#include "pwm-ws2812b.h"
#include "storagemanager.hpp"
#include "micro_timer.hpp"

// 定义静态成员变量
uint32_t CalibrationState::rebootTime = 0;

void CalibrationState::allCalibrationCompletedCallback(uint8_t totalButtons, uint8_t successCount, uint8_t failedCount) {
    ADC_CALIBRATION_MANAGER.stopCalibration();
    STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_INPUT);
    STORAGE_MANAGER.saveConfig();
    rebootTime = MICROS_TIMER.micros();
}

void CalibrationState::setup() {
    APP_DBG("CalibrationState::setup");
    // 启动手动校准模式
    ADC_CALIBRATION_MANAGER.startManualCalibration();
    ADC_CALIBRATION_MANAGER.setAllCalibrationCompletedCallback(allCalibrationCompletedCallback);
    isRunning = true;
    rebootTime = 0;
}

void CalibrationState::loop() {
    if (isRunning) {
        // 如果校准完成，等待1秒后重启
        if(rebootTime > 0 && MICROS_TIMER.checkInterval(1000000, rebootTime)) {
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
    // 可根据需要添加退出校准模式的处理逻辑
} 


