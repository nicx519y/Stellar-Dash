#ifndef __ADC_BTNS_MARKER_HPP__
#define __ADC_BTNS_MARKER_HPP__

#include <stdio.h>
#include "stm32h7xx.h"
#include "adc_manager.hpp"
#include "storagemanager.hpp"
#include "adc.h"
#include "message_center.hpp"
#include "adc_btns_error.hpp"
#include "board_cfg.h"
#include <functional>

#define MAX_NUM_TMP_MARKING 200

// 步进信息结构体
struct StepInfo {
    char id[16];
    char mapping_name[16];
    float_t step;
    uint8_t length;
    int16_t index;
    std::vector<uint32_t> values;
    std::vector<uint16_t> noise_values;
    std::vector<uint16_t> frequency_values;
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
        
        const StepInfo& getStepInfo() const { return step_info; }
        cJSON* getStepInfoJSON() const;

    private:
        ADCBtnsMarker();

        StepInfo step_info;

        void stepFinish(const ADCChannelStats* const stats);
        void markingFinish();
        std::function<void(const void*)> messageHandler;
};

#define ADC_BTNS_MARKER ADCBtnsMarker::getInstance()

#endif // __ADC_BTNS_MARKER_HPP__