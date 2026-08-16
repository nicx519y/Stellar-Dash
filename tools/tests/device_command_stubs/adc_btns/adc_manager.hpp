#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "adc_btns/adc_btns_error.hpp"

#ifndef MAX_ADC_VALUES_LENGTH
#define MAX_ADC_VALUES_LENGTH 40
#endif

struct ADCValuesMapping {
    char id[16] = {};
    char name[16] = {};
    size_t length = 0u;
    float_t step = 0.0f;
    uint16_t samplingNoise = 0u;
    uint16_t samplingFrequency = 0u;
    uint32_t originalValues[MAX_ADC_VALUES_LENGTH] = {};
};

class ADCManager {
public:
    static ADCManager &getInstance();
    std::vector<ADCValuesMapping *> getMappingList();
    const ADCValuesMapping *getMapping(const char *id) const;
    ADCBtnsError createADCMapping(const char *name, size_t length, float_t step);
    ADCBtnsError removeADCMapping(const char *id);
    ADCBtnsError renameADCMapping(const char *id, const char *name);
    ADCBtnsError setDefaultMapping(const char *id);
    std::string getDefaultMapping() const;
    void resetForContractTest();

private:
    ADCValuesMapping mapping_ = {};
    std::string default_ = "mapping-default";
};

#define ADC_MANAGER ADCManager::getInstance()
