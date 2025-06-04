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
}; 

#define CALIBRATION_STATE CalibrationState::getInstance()