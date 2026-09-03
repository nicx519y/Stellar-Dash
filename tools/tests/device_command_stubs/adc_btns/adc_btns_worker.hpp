#pragma once

#include "adc_btns/adc_btns_error.hpp"

class ADCBtnsWorker {
public:
    static ADCBtnsWorker &getInstance();
    ADCBtnsError setup();
};

#define ADC_BTNS_WORKER ADCBtnsWorker::getInstance()
