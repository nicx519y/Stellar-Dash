#ifndef __ADC_VALUES_MAPPING_HPP__
#define __ADC_VALUES_MAPPING_HPP__

#include "qspi-w25q64.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <string>
#include <array>
#include "stm32h7xx.h"
#include "stm32h750xx.h"
#include "adc_btns_error.hpp"
#include "cJSON.h"
#include "adc.h"
#include "message_center.hpp"
#include <algorithm>  // 为 std::sort
#include "board_cfg.h"

struct ADCValuesMapping {
    char id[16];                                            // 映射ID
    char name[16];                                          // 映射名称
    size_t length;                                          // 映射长度
    float_t step;                                           // 步长
    uint16_t samplingNoise;                                 // 噪声阈值
    uint16_t samplingFrequency;                             // 采样频率
    uint32_t originalValues[MAX_ADC_VALUES_LENGTH];         // 采集原始值
};

struct ADCValuesMappingStore {
    uint32_t version;
    uint8_t num;
    char defaultId[16];
    ADCValuesMapping mapping[NUM_ADC_VALUES_MAPPING];
};

// 采样统计相关成员，每个ADC一个
struct ADCBufferInfo {
    uint32_t* buffer;
    uint32_t size;
    const uint8_t* indexMap;
    uint8_t count;
};

// 每个通道的统计信息
struct ADCChannelStats {
    uint8_t adcIndex;
    uint32_t samplingFreq;  // 采样频率
    uint32_t averageValue;  // 采样均值
    uint32_t count;         // 采样次数
    uint32_t minValue;      // 最小值
    uint32_t maxValue;      // 最大值
    uint32_t startTime;     // 开始时间
    uint32_t endTime;       // 结束时间
};

struct ADCButtonValueInfo {
    uint8_t virtualPin;
    uint32_t* valuePtr;
};

class ADCManager {
    public:
        ADCManager(ADCManager const&) = delete;
        void operator=(ADCManager const&) = delete;
        static ADCManager& getInstance() {
            static ADCManager instance;
            return instance;
        }

        // 获取映射索引
        int8_t findMappingById(const char* const id) const;

        // 获取映射
        const ADCValuesMapping* getMapping(const char* const id) const;

        // 获取映射列表
        std::vector<ADCValuesMapping*> getMappingList();

        // 创建映射
        ADCBtnsError createADCMapping(const char* name, size_t length, float_t step);

        // 删除映射
        ADCBtnsError removeADCMapping(const char* id);

        // 重命名映射
        ADCBtnsError renameADCMapping(const char* id, const char* name);

        // 更新映射
        ADCBtnsError updateADCMapping(const char* id, const ADCValuesMapping& mapping);

        // 标记映射
        ADCBtnsError markMapping(const char* const id, 
                               const uint32_t* const values,
                               const uint16_t samplingNoise,
                               const uint16_t samplingFrequency);

        // 设置默认映射
        ADCBtnsError setDefaultMapping(const char* id);

        // 获取默认映射
        std::string getDefaultMapping() const;

        // 开始采样
        ADCBtnsError startADCSamping(bool enableSamplingRate = false, 
                                   uint8_t virtualPin = 0, 
                                   uint32_t samplingCountMax = 0);

        // 停止采样
        void stopADCSamping();

        // 读取ADC值 按virtualPin排序
        inline const std::array<ADCButtonValueInfo, NUM_ADC_BUTTONS>& readADCValues() const
        {
            for(uint8_t i = 0; i < NUM_ADC; i++) {
                SCB_CleanInvalidateDCache_by_Addr(adcBufferInfo[i].buffer, adcBufferInfo[i].size);
            }
            return ADCBufferInfoList;
        }

        /**
         * @brief 读取指定按钮的ADC值
         * @param virtualPin 虚拟按钮索引
         * @return 指定按钮的ADC值
         */
        inline const uint32_t readADCValue(uint8_t virtualPin) {
            auto indexInfo = findADCButtonVirtualPin(virtualPin);  // 现在可以直接调用静态函数
            if(indexInfo.first == -1) {
                return 0;
            }
            const auto& info = adcBufferInfo[indexInfo.first];
            SCB_CleanInvalidateDCache_by_Addr(info.buffer, info.size);
            return info.buffer[indexInfo.second];
        }

        inline void ADCValuesTestPrint() {
            SCB_CleanInvalidateDCache_by_Addr(adcBufferInfo[0].buffer, adcBufferInfo[0].size);
            SCB_CleanInvalidateDCache_by_Addr(adcBufferInfo[1].buffer, adcBufferInfo[1].size);
            SCB_CleanInvalidateDCache_by_Addr(adcBufferInfo[2].buffer, adcBufferInfo[2].size);
            printf("%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n", 
                adcBufferInfo[0].buffer[0],
                adcBufferInfo[0].buffer[1],
                adcBufferInfo[0].buffer[2],
                adcBufferInfo[0].buffer[3],
                adcBufferInfo[0].buffer[4],
                adcBufferInfo[0].buffer[5],
                adcBufferInfo[1].buffer[0],
                adcBufferInfo[1].buffer[1],
                adcBufferInfo[1].buffer[2],
                adcBufferInfo[1].buffer[3],
                adcBufferInfo[1].buffer[4],
                adcBufferInfo[1].buffer[5],
                adcBufferInfo[2].buffer[0],
                adcBufferInfo[2].buffer[1],
                adcBufferInfo[2].buffer[2],
                adcBufferInfo[2].buffer[3],
                adcBufferInfo[2].buffer[4]);
        }
        

    private:
        ADCManager();
        ~ADCManager();

        // ADC DMA 缓冲区必须保持静态
        static __attribute__((section("._RAM_D1_Area"))) uint32_t ADC1_Values[NUM_ADC1_BUTTONS];
        static __attribute__((section("._RAM_D1_Area"))) uint32_t ADC2_Values[NUM_ADC2_BUTTONS];
        static __attribute__((section("._RAM_D3_Area"))) uint32_t ADC3_Values[NUM_ADC3_BUTTONS];

        MessageHandler messageHandler;

        // 非静态成员变量
        ADCValuesMappingStore store;
        ADCBufferInfo adcBufferInfo[NUM_ADC];
        ADCChannelStats ADCButtonStats;
        bool samplingRateEnabled;
        uint32_t samplingCountMax;
        std::pair<uint8_t, uint8_t> samplingADCInfo;

        void handleADCStats(ADC_HandleTypeDef *hadc);
        int8_t saveStore();
        std::pair<uint8_t, uint8_t> findADCButtonVirtualPin(uint8_t virtualPin);

        // 添加 const 修饰符
        ADCBtnsError loadMapping(const char* const id) const;
        void handleADCConvCplt(const ADC_HandleTypeDef* const hadc);
        void handleADCStats(const ADCChannelStats* const stats) const;

        // 成员变量
        std::array<ADCButtonValueInfo, NUM_ADC_BUTTONS> ADCBufferInfoList;
        std::string defaultMappingId;
        ADCValuesMapping* currentMapping;
        bool isStarted;
        bool enableStats;
        uint32_t statsInterval;
        uint32_t lastStatsTime;
};

#define ADC_MANAGER ADCManager::getInstance()

#endif // __ADC_VALUES_MAPPING_HPP__