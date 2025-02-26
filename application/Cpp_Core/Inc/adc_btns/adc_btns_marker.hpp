#ifndef __ADC_BTNS_MARKER_HPP__
#define __ADC_BTNS_MARKER_HPP__

#include <stdio.h>
#include "stm32h7xx.h"
#include "constant.hpp"
#include "adc_manager.hpp"
#include "storagemanager.hpp"
#include "adc.h"
#include "message_center.hpp"
#include "adc_btns_error.hpp"
#include <functional>

#define MAX_NUM_TMP_MARKING 200

// 步进信息结构体
struct StepInfo {
    char id[16];
    char mapping_name[16];
    float_t step;
    uint8_t length;
    uint8_t index;
    uint32_t values[MAX_ADC_VALUES_LENGTH];
    uint16_t sampling_noise;
    uint16_t sampling_frequency;
    bool is_marking;
    bool is_completed;
    bool is_sampling;
};

class ADCBtnsMarker {
    public:
        ADCBtnsMarker(ADCBtnsMarker const&) = delete;
        void operator=(ADCBtnsMarker const&) = delete;
        static ADCBtnsMarker& getInstance() {
            static ADCBtnsMarker instance;
            return instance;
        }
        ADCBtnsError setup(const char* const id);
        ADCBtnsError step();
        
        void reset();
        const uint32_t* getCurrentMarkingValues() const;
        
        const StepInfo& getStepInfo() const { return step_info; }
        cJSON* getStepInfoJSON() const;

    private:
        ADCBtnsMarker();

        StepInfo step_info;

        void stepFinish(const ADCChannelStats* const stats);
        void markingFinish();
        std::function<void(const void*)> messageHandler;
        uint16_t tmpSamplingNoise;
        uint16_t tmpSamplingFrequency;
};

#define ADC_BTNS_MARKER ADCBtnsMarker::getInstance()

#endif // __ADC_BTNS_MARKER_HPP__