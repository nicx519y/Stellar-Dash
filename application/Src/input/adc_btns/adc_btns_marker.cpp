#include "adc_btns/adc_btns_marker.hpp"
#include "adc_btns/adc_btns_worker.hpp"
#include "board_cfg.h"
#include <numeric>

// 包含完整的头文件以访问MSMarkCommandHandler
#include "configs/device_command_handler.hpp"

// 添加外部访问MSMarkCommandHandler的声明
extern MSMarkCommandHandler& getMarkingCommandHandler();

/**
 * @brief 构造函数
 * 初始化DMA缓存，临时值，标记值，映射名称，标记状态
 * 用于初始化ADC值标记器
 */

ADCBtnsMarker::ADCBtnsMarker() {
    // 使用值初始化替代memset
    step_info = {};
}

/**
 * @brief 重置ADC值标记器
 */
void ADCBtnsMarker::reset() {

    if (step_info.is_sampling) {
        // Stop only the temporary sampling-rate statistics. The WebConfig
        // state's circular ADC DMA remains mounted.
        ADC_MANAGER.stopADCSamping();
    }

    // 使用值初始化替代memset
    step_info = {};
    draftMode = false;
    
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
        return markingFinish();
    }

    const ADCBtnsError samplingResult = ADC_MANAGER.startADCSamping(true, 2);
    if (samplingResult != ADCBtnsError::SUCCESS) {
        return samplingResult;
    }
    step_info.is_sampling = true;

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

    APP_DBG("ADCBtnsMarker: stepFinish - index: %d, value: %lu, Frequency: %lu, Noise: %lu", step_info.index, step_info.values.at(step_info.index), step_info.frequency_values.at(step_info.index), step_info.noise_values.at(step_info.index));

    MSMarkCommandHandler& handler = getMarkingCommandHandler();
    handler.sendMarkingStatusNotification();
}

ADCBtnsError ADCBtnsMarker::persistProgress() {
    if (draftMode || step_info.index < 0 || step_info.is_sampling) {
        return ADCBtnsError::NOT_MARKING;
    }
    const ADCValuesMapping* current = ADC_MANAGER.getMapping(step_info.id);
    if (current == nullptr) {
        return ADCBtnsError::MAPPING_NOT_FOUND;
    }
    ADCValuesMapping progress = *current;
    const size_t completed = static_cast<size_t>(step_info.index) + 1u;
    memset(progress.originalValues, 0, sizeof(progress.originalValues));
    memcpy(progress.originalValues, step_info.values.data(),
           completed * sizeof(uint32_t));
    progress.samplingNoise = static_cast<uint16_t>(
        std::accumulate(step_info.noise_values.begin(),
                        step_info.noise_values.begin() + completed,
                        static_cast<uint32_t>(0)) / completed);
    progress.samplingFrequency = static_cast<uint16_t>(
        std::accumulate(step_info.frequency_values.begin(),
                        step_info.frequency_values.begin() + completed,
                        static_cast<uint32_t>(0)) / completed);
    return ADC_MANAGER.updateADCMapping(step_info.id, progress);
}

/**
 * @brief 标记完成
 * 将标记值保存到映射中，并重置标记器
 */

ADCBtnsError ADCBtnsMarker::markingFinish() {
    ADCBtnsError err = ADCBtnsError::SUCCESS;
    if (!draftMode) {
        err = ADC_MANAGER.markMapping(
            step_info.id,
            step_info.values.data(),
            std::accumulate(step_info.noise_values.begin(),
                            step_info.noise_values.end(), (uint32_t)0) /
                step_info.length,
            std::accumulate(step_info.frequency_values.begin(),
                            step_info.frequency_values.end(), (uint32_t)0) /
                step_info.length);
    }


    if(err != ADCBtnsError::SUCCESS) {
        APP_ERR("ADCBtnsMarker: markingFinish - mark save failed. err: %d", static_cast<int>(err));
        return err;
    }

    if (!draftMode) {
        const ADCBtnsError reloadResult = ADC_BTNS_WORKER.setup();
        if (reloadResult != ADCBtnsError::SUCCESS) {
            APP_ERR("ADCBtnsMarker: recorded mapping saved but worker reload failed: %d",
                    static_cast<int>(reloadResult));
        }
    }

    step_info.is_completed = true;
    step_info.is_sampling = false;
    step_info.is_marking = false;

    // 发送标记状态变化通知
    MSMarkCommandHandler& handler = getMarkingCommandHandler();
    handler.sendMarkingStatusNotification();

    return ADCBtnsError::SUCCESS;
}

ADCBtnsError ADCBtnsMarker::getDraftMapping(ADCValuesMapping& mapping) const {
    if (!draftMode || !step_info.is_completed || step_info.length < 2u) {
        return ADCBtnsError::NOT_MARKING;
    }
    memset(&mapping, 0, sizeof(mapping));
    strncpy(mapping.name, step_info.mapping_name, sizeof(mapping.name) - 1u);
    mapping.length = step_info.length;
    mapping.step = step_info.step;
    mapping.samplingNoise = static_cast<uint16_t>(
        std::accumulate(step_info.noise_values.begin(),
                        step_info.noise_values.end(), (uint32_t)0) /
        step_info.length);
    mapping.samplingFrequency = static_cast<uint16_t>(
        std::accumulate(step_info.frequency_values.begin(),
                        step_info.frequency_values.end(), (uint32_t)0) /
        step_info.length);
    memcpy(mapping.originalValues, step_info.values.data(),
           step_info.length * sizeof(uint32_t));
    return ADCBtnsError::SUCCESS;
}

ADCBtnsError ADCBtnsMarker::setupDraft(const char* name,
                                       size_t length,
                                       float_t step) {
    if (!name || name[0] == '\0' || strlen(name) >= 16u ||
        length < 2u || length > MAX_ADC_VALUES_LENGTH ||
        !std::isfinite(step) || step < 0.1f || step > 10.0f) {
        return ADCBtnsError::INVALID_PARAMS;
    }
    reset();
    draftMode = true;
    snprintf(step_info.id, sizeof(step_info.id), "%s", "draft");
    snprintf(step_info.mapping_name, sizeof(step_info.mapping_name), "%s", name);
    step_info.index = -1;
    step_info.length = static_cast<uint8_t>(length);
    step_info.step = step;
    step_info.values.assign(length, 0u);
    step_info.noise_values.assign(length, 0u);
    step_info.frequency_values.assign(length, 0u);
    step_info.is_marking = true;
    step_info.is_completed = false;
    step_info.is_sampling = false;
    messageHandler = [this](const void* data) {
        if (data) this->stepFinish((ADCChannelStats*)data);
    };
    MC.subscribe(MessageId::ADC_SAMPLING_STATS_COMPLETE, messageHandler);
    return ADCBtnsError::SUCCESS;
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
    uint32_t completedSteps = step_info.index >= 0 ? static_cast<uint32_t>(step_info.index) + 1u : 0u;
    uint32_t samplingNoise = 0;
    uint32_t samplingFrequency = 0;
    if (completedSteps > 0) {
        samplingNoise = std::accumulate(step_info.noise_values.begin(), step_info.noise_values.end(), (uint32_t)0) / completedSteps;
        samplingFrequency = std::accumulate(step_info.frequency_values.begin(), step_info.frequency_values.end(), (uint32_t)0) / completedSteps;
    }
    cJSON_AddNumberToObject(json, "sampling_noise", samplingNoise);
    cJSON_AddNumberToObject(json, "sampling_frequency", samplingFrequency);


    cJSON* valuesJSON = cJSON_CreateArray();
    for(uint8_t i = 0; i < step_info.length; i++) {
        cJSON_AddItemToArray(valuesJSON, cJSON_CreateNumber(step_info.values.at(i)));
    }
    cJSON_AddItemToObject(json, "values", valuesJSON);

    return json;
}


