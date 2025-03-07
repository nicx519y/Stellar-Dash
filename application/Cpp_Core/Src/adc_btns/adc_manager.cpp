#include <numeric>   // 为 std::accumulate
#include <algorithm> // 为 std::sort
#include "adc_btns/adc_manager.hpp"
#include "board_cfg.h"

// 内存图
/*
 * QSPI Flash 内存布局 (从 ADC_VALUES_MAPPING_ADDR 开始):
 * 
 * ADCValuesMappingStore 结构体:
 * +------------------------+ 0x00
 * | 版本号 (4 bytes)      |
 * +------------------------+ 0x04
 * | 映射数量 (1 byte)     |
 * +------------------------+ 0x05
 * | 默认映射ID (16 bytes) |
 * +------------------------+ 0x15
 * | 映射数据              |
 * | - ADCValuesMapping[0] |
 * | - ADCValuesMapping[1] |
 * | ...                   |
 * +------------------------+
 */



// 定义静态 ADC DMA 缓冲区
__attribute__((section(".DMA_Section"))) uint32_t ADCManager::ADC1_Values[NUM_ADC1_BUTTONS];
__attribute__((section(".DMA_Section"))) uint32_t ADCManager::ADC2_Values[NUM_ADC2_BUTTONS];
// ADC3 BDMA 只能访问 _RAM_D3_Area 区域
__attribute__((section(".BDMA_Section"))) uint32_t ADCManager::ADC3_Values[NUM_ADC3_BUTTONS];

#define ADC_VALUES_MAPPING_ADDR_QSPI (ADC_VALUES_MAPPING_ADDR & 0x0FFFFFFF)

const uint8_t ADC1_BUTTONS_MAPPING[NUM_ADC1_BUTTONS] = ADC1_BUTTONS_MAPPING_DMA_TO_VIRTUALPIN;
const uint8_t ADC2_BUTTONS_MAPPING[NUM_ADC2_BUTTONS] = ADC2_BUTTONS_MAPPING_DMA_TO_VIRTUALPIN;
const uint8_t ADC3_BUTTONS_MAPPING[NUM_ADC3_BUTTONS] = ADC3_BUTTONS_MAPPING_DMA_TO_VIRTUALPIN;

ADCManager::ADCManager() {
    
    // 读取整个存储结构
    QSPI_W25Qxx_ReadBuffer_WithXIPOrNot((uint8_t*)&store, ADC_VALUES_MAPPING_ADDR_QSPI, sizeof(ADCValuesMappingStore));
    
    APP_DBG("ADCValuesMappingUtils version: 0x%x", store.version);
    APP_DBG("ADC_MAPPING_VERSION == version: %d", ADC_MAPPING_VERSION == store.version);
    
    // 如果版本号不匹配，初始化整个存储
    if(store.version != ADC_MAPPING_VERSION) {
        APP_DBG("ADCValuesMappingUtils version is not match, version: 0x%x", store.version);
        // 擦除64K
        // QSPI_W25Qxx_BufferErase(ADC_VALUES_MAPPING_ADDR_QSPI, 64*1024);
        
        // 初始化存储结构
        memset(&store, 0, sizeof(ADCValuesMappingStore));
        store.version = ADC_MAPPING_VERSION;
        store.num = 0;
        strcpy(store.defaultId, "");
        
        // 写入初始化后的存储结构
        if(QSPI_W25Qxx_WriteBuffer_WithXIPOrNot((uint8_t*)&store, ADC_VALUES_MAPPING_ADDR_QSPI, sizeof(ADCValuesMappingStore)) != QSPI_W25Qxx_OK) {
            APP_ERR("ADCValuesMappingUtils init failed");
        } else {
            APP_DBG("ADCValuesMappingUtils init success");
        }
    }
    

    // 注册消息
    MC.registerMessage(MessageId::DMA_ADC_CONV_CPLT);           // DMA ADC 转换完成消息
    MC.registerMessage(MessageId::ADC_SAMPLING_STATS_COMPLETE); // ADC 采样统计完成消息

    this->samplingCountMax = 1000; // 采样次数 默认1000次
    this->samplingRateEnabled = false; // 采样率统计是否开启 默认关闭
    this->ADCButtonStats = {0}; // 采样统计信息
    this->samplingADCInfo = ADCIndexInfo{0, 0}; // 采样ADC信息
    this->adcBufferInfo[0] = {ADC1_Values, sizeof(ADC1_Values), ADC1_BUTTONS_MAPPING, NUM_ADC1_BUTTONS}; // ADC1缓存信息
    this->adcBufferInfo[1] = {ADC2_Values, sizeof(ADC2_Values), ADC2_BUTTONS_MAPPING, NUM_ADC2_BUTTONS}; // ADC2缓存信息    
    this->adcBufferInfo[2] = {ADC3_Values, sizeof(ADC3_Values), ADC3_BUTTONS_MAPPING, NUM_ADC3_BUTTONS}; // ADC3缓存信息

    for(uint8_t i = 0; i < NUM_ADC1_BUTTONS; i++) {
        this->ADCBufferInfoList[i].valuePtr = &this->adcBufferInfo[0].buffer[i];
        this->ADCBufferInfoList[i].virtualPin = ADC1_BUTTONS_MAPPING[i];
    }

    for(uint8_t j = NUM_ADC1_BUTTONS; j < NUM_ADC1_BUTTONS + NUM_ADC2_BUTTONS; j++) {
        this->ADCBufferInfoList[j].valuePtr = &this->adcBufferInfo[1].buffer[j - NUM_ADC1_BUTTONS];
        this->ADCBufferInfoList[j].virtualPin = ADC2_BUTTONS_MAPPING[j - NUM_ADC1_BUTTONS];
    }

    for(uint8_t k = NUM_ADC1_BUTTONS + NUM_ADC2_BUTTONS; k < NUM_ADC1_BUTTONS + NUM_ADC2_BUTTONS + NUM_ADC3_BUTTONS; k++) {
        this->ADCBufferInfoList[k].valuePtr = &this->adcBufferInfo[2].buffer[k - NUM_ADC1_BUTTONS - NUM_ADC2_BUTTONS];
        this->ADCBufferInfoList[k].virtualPin = ADC3_BUTTONS_MAPPING[k - NUM_ADC1_BUTTONS - NUM_ADC2_BUTTONS];
    }


    // 使用 std::sort 按 virtualPin 排序
    std::sort(this->ADCBufferInfoList.begin(), this->ADCBufferInfoList.end(), 
        [](const ADCButtonValueInfo& a, const ADCButtonValueInfo& b) {
            return a.virtualPin < b.virtualPin;
        });

}

ADCManager::~ADCManager() {
    this->stopADCSamping();

    // 取消注册ADC消息
    MC.unregisterMessage(MessageId::DMA_ADC_CONV_CPLT);
    MC.unregisterMessage(MessageId::ADC_SAMPLING_STATS_COMPLETE);
}

// 保存整个存储结构到Flash
int8_t ADCManager::saveStore() {
    return QSPI_W25Qxx_WriteBuffer_WithXIPOrNot((uint8_t*)&store, ADC_VALUES_MAPPING_ADDR_QSPI, sizeof(ADCValuesMappingStore));
}

/**
 * @brief 查找映射ID的索引
 * @param id 映射ID
 * @return 映射ID的索引
 */
int8_t ADCManager::findMappingById(const char* const id) const {
    if (!id) return -1;
    
    // 遍历映射数据，检查名称
    for(uint8_t i = 0; i < store.num; i++) {
        if(strcmp(store.mapping[i].id, id) == 0) {
            return i;
        }
    }
    
    return -1;
}

/**
 * @brief 删除映射
 * @param id 映射ID
 * @return 是否删除成功
 */
ADCBtnsError ADCManager::removeADCMapping(const char* id) {
    if (!id) return ADCBtnsError::INVALID_PARAMS;
    
    // 查找要删除的映射索引
    int8_t targetIdx = findMappingById(id);
    if(targetIdx == -1) return ADCBtnsError::MAPPING_NOT_FOUND;

    // 如果只有一个映射，则不能删除
    if(store.num <= 1) return ADCBtnsError::MAPPING_DELETE_FAILED;

    // 移动数据
    if(targetIdx < store.num - 1) {
        memmove(&store.mapping[targetIdx], 
                &store.mapping[targetIdx + 1], 
                (store.num - targetIdx - 1) * sizeof(ADCValuesMapping));
    }
    
    store.num--;
    
    // 保存更新后的存储结构
    if(saveStore() != QSPI_W25Qxx_OK) {
        return ADCBtnsError::MAPPING_DELETE_FAILED;
    }

    return ADCBtnsError::SUCCESS;
}

/**
 * @brief 创建映射
 * @param id射ID
 * @param length 映射长度
 * @param step 步长
 * @return 是否创建成功
 */
ADCBtnsError ADCManager::createADCMapping(const char* name, size_t length, float_t step) {
    if (!name) return ADCBtnsError::INVALID_PARAMS;
    
    // 检查映射名称是否已存在
    for(uint8_t i = 0; i < store.num; i++) {
        if(strcmp(store.mapping[i].name, name) == 0) {
            return ADCBtnsError::MAPPING_ALREADY_EXISTS;
        }
    }

    // 检查映射数量是否已满
    if(store.num >= NUM_ADC_VALUES_MAPPING) return ADCBtnsError::MAPPING_STORAGE_FULL;

    // 创建新映射
    ADCValuesMapping& newMapping = store.mapping[store.num];
    memset(&newMapping, 0, sizeof(ADCValuesMapping));
    snprintf(newMapping.id, sizeof(newMapping.id), "%s-%d", name, HAL_GetTick());
    snprintf(newMapping.name, sizeof(newMapping.name), "%s", name);
    newMapping.length = length;
    newMapping.step = step;
    newMapping.samplingNoise = 0;
    newMapping.samplingFrequency = 0;
    memset(newMapping.originalValues, 0, sizeof(newMapping.originalValues));
    
    store.num++;
    
    // 如果这是第一个映射，则设置为默认映射
    if(store.num == 1) {
        snprintf(store.defaultId, sizeof(store.defaultId), "%s", newMapping.id);
    }

    // 保存更新后的存储结构
    if(saveStore() != QSPI_W25Qxx_OK) {
        store.num--;
        return ADCBtnsError::MAPPING_CREATE_FAILED;
    }
    
    return ADCBtnsError::SUCCESS;
}

ADCBtnsError ADCManager::renameADCMapping(const char* id, const char* name) {
    if (!id || !name) return ADCBtnsError::INVALID_PARAMS;
    
    int idx = findMappingById(id);
    if(idx == -1) return ADCBtnsError::MAPPING_NOT_FOUND;
    
    snprintf(store.mapping[idx].name, sizeof(store.mapping[idx].name), "%s", name);
    
    // 保存更新后的存储结构

    if(saveStore() != QSPI_W25Qxx_OK) {
        return ADCBtnsError::MAPPING_UPDATE_FAILED;
    }

    return ADCBtnsError::SUCCESS;
}

ADCBtnsError ADCManager::updateADCMapping(const char* id, const ADCValuesMapping& map) {
    if (!id) return ADCBtnsError::INVALID_PARAMS;
    if (map.length == 0 || map.length > MAX_ADC_VALUES_LENGTH) return ADCBtnsError::INVALID_PARAMS;
    

    int idx = findMappingById(id);
    if(idx == -1) return ADCBtnsError::MAPPING_NOT_FOUND;

    // printf("ADCValuesMappingUtils: update - begin update mapping.\n");
    // printf("ADCValuesMappingUtils: update - mapping id: %s, name: %s, length: %d, step: %f, samplingNoise: %d, samplingFrequency: %d\n", 
    //        map.id, map.name, map.length, map.step, map.samplingNoise, map.samplingFrequency);

    // 更新映射数据
    memcpy(&store.mapping[idx], &map, sizeof(ADCValuesMapping));

    // 保存更新后的存储结构
    if(saveStore() != QSPI_W25Qxx_OK) {
        return ADCBtnsError::MAPPING_UPDATE_FAILED;
    }
    
    return ADCBtnsError::SUCCESS;
}

/**
 * @brief 设置默认映射
 * @param id 映射ID
 * @return 错误码
 */
ADCBtnsError ADCManager::setDefaultMapping(const char* id) {
    if (!id) return ADCBtnsError::INVALID_PARAMS;
    
    uint8_t idx = findMappingById(id);
    if(idx == -1) return ADCBtnsError::MAPPING_NOT_FOUND;
    
    snprintf(store.defaultId, sizeof(store.defaultId), "%s", id);
    
    // 保存更新后的存储结构
    if(saveStore() != QSPI_W25Qxx_OK) {
        return ADCBtnsError::MAPPING_UPDATE_FAILED;
    }
    
    return ADCBtnsError::SUCCESS;
}

/**
 * @brief 获取映射列表
 * @return 映射列表
 */
 std::vector<ADCValuesMapping*> ADCManager::getMappingList() {
    std::vector<ADCValuesMapping*> mappingList;
    for(uint8_t i = 0; i < store.num; i++) {
        mappingList.push_back(&store.mapping[i]);
    }
    return mappingList;
}

/**
 * @brief 获取默认映射名称
 * @return 默认映射名称
 */
std::string ADCManager::getDefaultMapping() const {
    // 如果映射数量为0，则返回空字符串
    if(store.num == 0) return std::string("");
    // 如果默认映射名称未设置，则返回第一个映射名称
    if(store.defaultId[0] == '\0') {
        APP_DBG("ADCManager: getDefaultMapping defaultId is empty, return first mapping id.");
        return std::string(store.mapping[0].id);
    };
    APP_DBG("ADCManager: getDefaultMapping defaultId: %s", store.defaultId);
    return std::string(store.defaultId);
}

/**
 * @brief 获取映射JSON
 * @param name 映射名称
 * @return 映射JSON
 */
const ADCValuesMapping* ADCManager::getMapping(const char* const id) const {
    if (!id) return nullptr;

    // 查找映射
    int8_t idx = findMappingById(id);
    if(idx == -1) return nullptr;
    
    return &store.mapping[idx];
}


// 参数验证辅助函数
bool validateMarkParams(const char* id, uint32_t* values, uint8_t length) {
    if (!id || !values || length == 0 || length > MAX_ADC_VALUES_LENGTH) {
        return false;
    }
    return true;
}

ADCBtnsError ADCManager::markMapping(const char* const id, 
                                   const uint32_t* const values,
                                   const uint16_t samplingNoise,
                                   const uint16_t samplingFrequency) {
    if (!id || !values || samplingNoise == 0 || samplingFrequency == 0) return ADCBtnsError::INVALID_PARAMS;
    
    int idx = findMappingById(id);
    if(idx == -1) return ADCBtnsError::MAPPING_NOT_FOUND;

    ADCValuesMapping& mapping = store.mapping[idx];
    
    // 保存原始状态用于回滚
    uint8_t oldLength = mapping.length;
    uint32_t* oldOriginValues = (uint32_t*)calloc(sizeof(mapping.originalValues), 1);
    
    if (!oldOriginValues) {
        free(oldOriginValues);
        return ADCBtnsError::MEMORY_ERROR;
    }
    
    // 保存原始数据
    memcpy(oldOriginValues, mapping.originalValues, sizeof(mapping.originalValues));
    
    // 更新数据
    mapping.samplingNoise = samplingNoise;
    mapping.samplingFrequency = samplingFrequency;
    memset(mapping.originalValues, 0, sizeof(mapping.originalValues));
    memcpy(mapping.originalValues, values, mapping.length * sizeof(uint32_t));

    // printf("ADCValuesMappingUtils: mark - begin update mapping.\n");
    // printf("ADCValuesMappingUtils: mark - mapping id: %s, name: %s, length: %d, step: %f, samplingNoise: %d, samplingFrequency: %d\n", 
    // 如果更新失败，回滚所有更改
    ADCBtnsError err = updateADCMapping(mapping.id, mapping);
    if (err != ADCBtnsError::SUCCESS) {
        memcpy(mapping.originalValues, oldOriginValues, sizeof(mapping.originalValues));
        free(oldOriginValues);
        APP_ERR("ADCValuesMappingUtils: mark - update mapping failed. err: %d", err);
        return err;
    }
    
    free(oldOriginValues);
    return ADCBtnsError::SUCCESS;
}


/**
 * @brief 开始ADC采样
 * @param enableSamplingRate 是否启用采样率统计
 * @param buttonIndex 采样按钮索引 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
 * @param updateInterval 采样间隔
 * @return 错误码
 */
ADCBtnsError ADCManager::startADCSamping(bool enableSamplingRate, 
                                        uint8_t virtualPin, 
                                        uint32_t samplingCountMax) {
    // 停止所有 ADC
    this->stopADCSamping();

    // 初始化DMA缓存
    memset(ADC1_Values, 0, sizeof(ADC1_Values)); // DMA缓存清零  
    memset(ADC2_Values, 0, sizeof(ADC2_Values)); // DMA缓存清零  
    memset(ADC3_Values, 0, sizeof(ADC3_Values)); // DMA缓存清零

    // 校准 ADC1
    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
        APP_ERR("ADC1 calibration failed\n");
        return ADCBtnsError::ADC1_CALIB_FAILED;
    }

    // 校准 ADC2
    if (HAL_ADCEx_Calibration_Start(&hadc2, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
        APP_ERR("ADC2 calibration failed\n");
        return ADCBtnsError::ADC2_CALIB_FAILED;
    }

    // 校准 ADC3
    if (HAL_ADCEx_Calibration_Start(&hadc3, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
        APP_ERR("ADC3 calibration failed\n");
        return ADCBtnsError::ADC3_CALIB_FAILED;
    }

    // 启动 ADC1
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t*)&ADC1_Values[0], NUM_ADC1_BUTTONS) != HAL_OK) {
        APP_ERR("ADC1 DMA start failed\n");
        return ADCBtnsError::DMA1_START_FAILED;
    }

    // 启动 ADC2
    if (HAL_ADC_Start_DMA(&hadc2, (uint32_t*)&ADC2_Values[0], NUM_ADC2_BUTTONS) != HAL_OK) {
        APP_ERR("ADC2 DMA start failed\n");
        HAL_ADC_Stop_DMA(&hadc1);  // 清理已启动的 ADC1
        return ADCBtnsError::DMA2_START_FAILED;
    }

    // 启动 ADC3
    if (HAL_ADC_Start_DMA(&hadc3, (uint32_t*)&ADC3_Values[0], NUM_ADC3_BUTTONS) != HAL_OK) {
        APP_ERR("ADC3 DMA start failed\n");
        HAL_ADC_Stop_DMA(&hadc1);  // 清理已启动的 ADC
        HAL_ADC_Stop_DMA(&hadc2);
        return ADCBtnsError::DMA3_START_FAILED;
    }

    // 如果启用采样率统计，则注册回调
    if(enableSamplingRate) {
        // 设置采样率统计状态
        samplingRateEnabled = enableSamplingRate;
        if(samplingCountMax > 0) {
            this->samplingCountMax = samplingCountMax;
        }
        this->samplingADCInfo = findADCButtonVirtualPin(virtualPin);
    
        if(this->samplingADCInfo.ADCIndex == -1) {
            APP_ERR("Invalid button index\n");
            return ADCBtnsError::INVALID_PARAMS;
        }

        ADCButtonStats.adcIndex = this->samplingADCInfo.ADCIndex;
        ADCButtonStats.startTime = HAL_GetTick();
        ADCButtonStats.endTime = 0;
        ADCButtonStats.count = 0;
        ADCButtonStats.averageValue = 0;
        ADCButtonStats.samplingFreq = 0;
        ADCButtonStats.values.clear();
        ADCButtonStats.values.resize(this->samplingCountMax);
        ADCButtonStats.diffValues.clear();
        ADCButtonStats.diffValues.resize(this->samplingCountMax);

        // 注册ADC转换完成回调
        messageHandler = [this](const void* data) {
            if (data) {
                this->handleADCStats((ADC_HandleTypeDef*)data);
            }
        };
        MC.subscribe(MessageId::DMA_ADC_CONV_CPLT, messageHandler);

        APP_DBG("All ADCs started sampling successfully\n");
    }

    APP_DBG("All ADCs started successfully\n");
    return ADCBtnsError::SUCCESS;
}

void ADCManager::stopADCSamping() {
    if(HAL_ADC_Stop_DMA(&hadc1) != HAL_OK) {
        return;
    }

    if(HAL_ADC_Stop_DMA(&hadc2) != HAL_OK) {
        return;
    }

    if(HAL_ADC_Stop_DMA(&hadc3) != HAL_OK) {
        return;
    }

    if(messageHandler) {
        MC.unsubscribe(MessageId::DMA_ADC_CONV_CPLT, messageHandler);
        messageHandler = nullptr;
    }
}   


/**
 * @brief 处理ADC转换完成中断
 * 在每次采样完成后，会调用此函数，用于更新采样统计信息
 * 更新ADCButtonValues的值
 * 如果采样率统计开启，则更新采样统计信息，包括每个通道的平均值、最小值、最大值
 * @param hadc ADC句柄
 */
void ADCManager::handleADCStats(ADC_HandleTypeDef *hadc) {
    uint32_t adcIndex = (hadc->Instance == ADC1) ? 0 : 
                        (hadc->Instance == ADC2) ? 1 : 
                        (hadc->Instance == ADC3) ? 2 : 3;
    
    // 如果采样ADC索引不匹配，则返回，此处只处理采样ADC索引对应的ADC
    if(!samplingRateEnabled || adcIndex != this->samplingADCInfo.ADCIndex) return;

    // 处理数据...
    const auto& info = adcBufferInfo[adcIndex];
    SCB_CleanInvalidateDCache_by_Addr(info.buffer, info.size);
    
    uint32_t value = info.buffer[this->samplingADCInfo.indexInDMA];


    if(value == 0) return;
    ADCButtonStats.values[ADCButtonStats.count] = value; // 保存当前值
    ADCButtonStats.count++;                          // 计数器加1

    if(ADCButtonStats.count >= samplingCountMax) {

        uint32_t t = HAL_GetTick();
        ADCButtonStats.samplingFreq = (uint32_t)(ADCButtonStats.count * 1000 / (t - ADCButtonStats.startTime));
        ADCButtonStats.endTime = t;

        ADCButtonStats.averageValue = std::accumulate(ADCButtonStats.values.begin(), ADCButtonStats.values.end(), 0) / ADCButtonStats.count;

        for(uint32_t i = 0; i < ADCButtonStats.count; i++) {
            ADCButtonStats.diffValues[i] = abs((int32_t)ADCButtonStats.averageValue - (int32_t)ADCButtonStats.values[i]);
        }

        ADCButtonStats.noiseValue = std::accumulate(ADCButtonStats.diffValues.begin(), ADCButtonStats.diffValues.end(), 0) / ADCButtonStats.count * 2;

        uint32_t crossCount = 0;
        for(uint32_t i = 0; i < ADCButtonStats.count; i++) {
            if(ADCButtonStats.diffValues[i] > ADCButtonStats.noiseValue * 2) {
                APP_DBG("diff: %d, index: %d", ADCButtonStats.diffValues[i], i);
                crossCount++;
            }
        }

        APP_DBG("avg: %d, noise: %d, freq: %d, cross: %d", ADCButtonStats.averageValue, ADCButtonStats.noiseValue, ADCButtonStats.samplingFreq, crossCount);

        MC.publish(MessageId::ADC_SAMPLING_STATS_COMPLETE, &ADCButtonStats);
    }
}

// 根据按钮索引查找对应的ADC索引
ADCIndexInfo ADCManager::findADCButtonVirtualPin(uint8_t virtualPin) {
    // 检查 ADC1
    for(uint8_t i = 0; i < NUM_ADC1_BUTTONS; i++) { 
        if(ADC1_BUTTONS_MAPPING[i] == virtualPin) {
            return ADCIndexInfo{0, int8_t(i)};  // 返回 ADC1 的索引
        }
    }

    // 检查 ADC2
    for(uint8_t i = 0; i < NUM_ADC2_BUTTONS; i++) {
        if(ADC2_BUTTONS_MAPPING[i] == virtualPin) {
            return ADCIndexInfo{1, int8_t(i)};  // 返回 ADC2 的索引
        }
    }
    
    // 检查 ADC3
    for(uint8_t i = 0; i < NUM_ADC3_BUTTONS; i++) {
        if(ADC3_BUTTONS_MAPPING[i] == virtualPin) {
            return ADCIndexInfo{2, int8_t(i)};  // 返回 ADC3 的索引
        }
    }
    
    return ADCIndexInfo{-1, -1};  // 如果没找到，默认返回 ADC1
}

// ADC转换完成回调
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
    MC.publish(MessageId::DMA_ADC_CONV_CPLT, hadc);
}

// ADC错误回调
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    uint32_t error = HAL_ADC_GetError(hadc);
    APP_ERR("ADC Error: Instance=0x%p", (void*)hadc->Instance);
    APP_ERR("State=0x%x", HAL_ADC_GetState(hadc));
    APP_ERR("Error flags: 0x%lx", error);
    
    if (error & HAL_ADC_ERROR_INTERNAL) APP_DBG("- Internal error");
    if (error & HAL_ADC_ERROR_OVR) APP_DBG("- Overrun error");
    if (error & HAL_ADC_ERROR_DMA) APP_DBG("- DMA transfer error");
}






