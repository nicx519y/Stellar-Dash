#include "states/calibration_state.hpp"
#include "adc_btns/adc_calibration.hpp"
#include "pwm-ws2812b.h"

void CalibrationState::setup() {
    APP_DBG("CalibrationState::setup");
    // 启动手动校准模式
    ADC_CALIBRATION_MANAGER.startManualCalibration();
    isRunning = true;
}

void CalibrationState::loop() {
    if (isRunning) {
        ADC_CALIBRATION_MANAGER.processCalibration();
        // 可根据需要添加更多校准相关的处理逻辑
    }
}

void CalibrationState::reset() {
    isRunning = false;
    // 可根据需要添加退出校准模式的处理逻辑
} 