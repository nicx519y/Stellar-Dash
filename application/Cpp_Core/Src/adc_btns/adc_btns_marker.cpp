#include "adc_btns/adc_btns_marker.hpp"
#include "board_cfg.h"
#include <numeric>

/**
 * @brief 构造函数
 * 初始化DMA缓存，临时值，标记值，映射名称，标记状态
 * 用于初始化ADC值标记器
 */

ADCBtnsMarker::ADCBtnsMarker() {
    memset(&step_info, 0, sizeof(step_info));
}

/**
 * @brief 重置ADC值标记器
 */
void ADCBtnsMarker::reset() {

    memset(&step_info, 0, sizeof(step_info));
    
    ADC_MANAGER.stopADCSamping();

    // 取消订阅ADC转换完成回调
    if (messageHandler) {
        MC.unsubscribe(MessageId::ADC_SAMPLING_STATS_COMPLETE, messageHandler);
        messageHandler = nullptr;
    }

}

/**
 * @brief 初始化ADC值标记器
 * @param mapping_name 映射名称
 */
ADCBtnsError ADCBtnsMarker::setup(const char* const id) {

    if (!id) return ADCBtnsError::INVALID_PARAMS;

    reset();

    const ADCValuesMapping* mapping = ADC_MANAGER.getMapping(id);


    if (!mapping) return ADCBtnsError::MAPPING_NOT_FOUND;

    // 初始化步进信息
    snprintf(step_info.id, sizeof(step_info.id), "%s", id);
    snprintf(step_info.mapping_name, sizeof(step_info.mapping_name), "%s", mapping->name);
    step_info.index = -1;
    step_info.length = mapping->length;
    step_info.step = mapping->step;
    
    step_info.values.clear();
    step_info.values.resize(step_info.length);
    step_info.noise_values.clear();
    step_info.noise_values.resize(step_info.length);
    step_info.frequency_values.clear();
    step_info.frequency_values.resize(step_info.length);
    

    step_info.is_marking = true;
    step_info.is_completed = false;
    step_info.is_sampling = false;


    // 订阅ADC转换完成回调
    messageHandler = [this](const void* data) {
        if (data) {
            this->stepFinish((ADCChannelStats*)data);
        }
    };
    MC.subscribe(MessageId::ADC_SAMPLING_STATS_COMPLETE, messageHandler);

    return ADCBtnsError::SUCCESS;
}


/**
 * @brief 步进
 * 将标记值保存到映射中，并重置标记器
 * 每次步进后，切换到下一个标记值，并重置临时值
 * 如果标记值已满，则将标记值保存到映射中，并重置标记器
 */
ADCBtnsError ADCBtnsMarker::step() {
    if(!step_info.is_marking) {
        return ADCBtnsError::NOT_MARKING;
    }

    if(step_info.is_sampling) {
        return ADCBtnsError::ALREADY_SAMPLING;
    }

    APP_DBG("ADCBtnsMarker: step - index: %d, length: %d", step_info.index, step_info.length);

    if(step_info.index >= step_info.length - 1) {
        markingFinish();
        return ADCBtnsError::SUCCESS;
    }

    step_info.is_sampling = true;
    ADC_MANAGER.startADCSamping(true, 0);

    return ADCBtnsError::SUCCESS;   
}

/**
 * @brief 步进完成
 * 将临时值保存到标记值中，并重置标记器
 */
void ADCBtnsMarker::stepFinish(const ADCChannelStats* const stats) {

    ADC_MANAGER.stopADCSamping();

    step_info.is_sampling = false;
    step_info.index ++;
    // 计算平均值，double_t精度更高，round四舍五入
    step_info.values.at(step_info.index) = stats->averageValue;
    step_info.noise_values.at(step_info.index) = stats->noiseValue;
    step_info.frequency_values.at(step_info.index) = stats->samplingFreq;

    APP_DBG("ADCBtnsMarker: stepFinish - index: %d, value: %d, Frequency: %d, Noise: %d", step_info.index, step_info.values.at(step_info.index), step_info.frequency_values.at(step_info.index), step_info.noise_values.at(step_info.index));

    
}

/**
 * @brief 标记完成
 * 将标记值保存到映射中，并重置标记器
 */

void ADCBtnsMarker::markingFinish() {

    step_info.is_completed = true;
    step_info.is_sampling = false;
    step_info.is_marking = false;

    ADCBtnsError err = ADC_MANAGER.markMapping(step_info.id, 
        step_info.values.data(), 
        std::accumulate(step_info.noise_values.begin(), step_info.noise_values.end(), (uint32_t)0) / step_info.length, 
        std::accumulate(step_info.frequency_values.begin(), step_info.frequency_values.end(), (uint32_t)0) / step_info.length);


    if(err != ADCBtnsError::SUCCESS) {
        APP_ERR("ADCBtnsMarker: markingFinish - mark save failed. err: %d", err);
        return;
    }

}

/**
 * @brief 获取步进信息JSON
 * @return cJSON* 
 */
cJSON* ADCBtnsMarker::getStepInfoJSON() const {
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "id", step_info.id);
    cJSON_AddStringToObject(json, "mapping_name", step_info.mapping_name);
    cJSON_AddNumberToObject(json, "step", step_info.step);
    cJSON_AddNumberToObject(json, "length", step_info.length);
    cJSON_AddNumberToObject(json, "index", step_info.index);
    cJSON_AddBoolToObject(json, "is_marking", step_info.is_marking);
    cJSON_AddBoolToObject(json, "is_completed", step_info.is_completed);
    cJSON_AddBoolToObject(json, "is_sampling", step_info.is_sampling);
    cJSON_AddNumberToObject(json, "sampling_noise", std::accumulate(step_info.noise_values.begin(), step_info.noise_values.end(), (uint32_t)0) / (step_info.index + 1));
    cJSON_AddNumberToObject(json, "sampling_frequency", std::accumulate(step_info.frequency_values.begin(), step_info.frequency_values.end(), (uint32_t)0) / (step_info.index + 1));


    cJSON* valuesJSON = cJSON_CreateArray();
    for(uint8_t i = 0; i < step_info.length; i++) {
        cJSON_AddItemToArray(valuesJSON, cJSON_CreateNumber(step_info.values.at(i)));
    }
    cJSON_AddItemToObject(json, "values", valuesJSON);

    return json;
}


