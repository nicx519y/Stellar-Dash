#pragma once

#include <cstdint>
#include <vector>

#include "adc_btns/adc_btns_error.hpp"
#include "cJSON.h"

struct StepInfo {
    char id[16] = {};
    char mapping_name[16] = {};
    float step = 0.0f;
    uint8_t length = 0u;
    int16_t index = 0;
    std::vector<uint32_t> values;
    std::vector<uint16_t> noise_values;
    std::vector<uint16_t> frequency_values;
    bool is_marking = false;
    bool is_completed = false;
    bool is_sampling = false;
};

class ADCBtnsMarker {
public:
    static ADCBtnsMarker &getInstance();
    ADCBtnsError setup(const char *id);
    ADCBtnsError setupDraft(const char *name, size_t length, float_t step);
    ADCBtnsError step();
    ADCBtnsError persistProgress();
    void reset();
    cJSON *getStepInfoJSON() const;
    ADCBtnsError getDraftMapping(ADCValuesMapping &mapping) const;
    void resetForContractTest() { step_ = {}; }

private:
    StepInfo step_ = {};
    bool draft_ = false;
};

#define ADC_BTNS_MARKER ADCBtnsMarker::getInstance()
