#pragma once
#include "base_state.hpp"
#include "adc_btns/adc_calibration.hpp"
#include "pwm-ws2812b.h"

class CalibrationState : public BaseState {
public:
    CalibrationState() = default;
    virtual ~CalibrationState() = default;

    static CalibrationState& getInstance() {
        static CalibrationState instance;
        return instance;
    }

    void setup() override;
    void loop() override;
    void reset() override;

private:
    bool isRunning = false;
    static uint32_t rebootTime;
    static void allCalibrationCompletedCallback(uint8_t totalButtons, uint8_t successCount, uint8_t failedCount);
}; 

#define CALIBRATION_STATE CalibrationState::getInstance()