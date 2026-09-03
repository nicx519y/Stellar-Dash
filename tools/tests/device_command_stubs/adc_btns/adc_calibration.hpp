#pragma once

#include <cstdint>
#include <functional>

#include "adc_btns/adc_btns_error.hpp"

using CalibrationStatusChangedCallback = std::function<void()>;

enum class CalibrationLEDColor {
    OFF = 0,
    RED,
    CYAN,
    DARK_BLUE,
    GREEN,
    YELLOW,
};

enum class CalibrationPhase {
    IDLE = 0,
    TOP_SAMPLING,
    BOTTOM_SAMPLING,
    COMPLETED,
    ERROR,
};

class ADCCalibrationManager {
public:
    static ADCCalibrationManager &getInstance();

    ADCBtnsError startManualCalibration();
    ADCBtnsError stopCalibration();
    ADCBtnsError resetAllCalibration();
    void setCalibrationStatusChangedCallback(CalibrationStatusChangedCallback callback);

    bool isCalibrationActive() const;
    bool isAllButtonsCalibrated(bool useCache = true);
    uint8_t getUncalibratedButtonCount() const;
    uint8_t getActiveCalibrationButtonCount() const;
    CalibrationPhase getButtonPhase(uint8_t index) const;
    CalibrationLEDColor getButtonLEDColor(uint8_t index) const;
    bool isButtonCalibrated(uint8_t index) const;
    ADCBtnsError getCalibrationValues(uint8_t index,
                                      uint16_t &topValue,
                                      uint16_t &bottomValue) const;
    void resetForContractTest() { active_ = false; callback_ = {}; }

private:
    bool active_ = false;
    CalibrationStatusChangedCallback callback_;
};

#define ADC_CALIBRATION_MANAGER ADCCalibrationManager::getInstance()
