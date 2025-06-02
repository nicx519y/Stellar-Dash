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
    
    // 自动校准值：每个按键的初始值(完全按下)和末尾值(完全释放)
    struct {
        uint16_t topValue;      // 完全按下时的值(初始值)
        uint16_t bottomValue;   // 完全释放时的值(末尾值)
    } autoCalibrationValues[NUM_ADC_BUTTONS];
    
    // 手动校准值：每个按键的初始值(完全按下)和末尾值(完全释放)
    struct {
        uint16_t topValue;      // 完全按下时的值(初始值)
        uint16_t bottomValue;   // 完全释放时的值(末尾值)
    } manualCalibrationValues[NUM_ADC_BUTTONS];
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
    uint32_t startTime;     // 开始时间
    uint32_t endTime;       // 结束时间
    uint32_t noiseValue;    // 噪声值
    std::vector<uint32_t> values;
    std::vector<uint32_t> diffValues;
};

struct ADCButtonValueInfo {
    uint8_t virtualPin;
    uint32_t* valuePtr;
};

struct ADCIndexInfo {
    int8_t ADCIndex;
    int8_t indexInDMA;
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

        // 获取校准值
        ADCBtnsError getCalibrationValues(const char* mappingId, uint8_t buttonIndex, bool isAutoCalibration, uint16_t& topValue, uint16_t& bottomValue) const;
        
        // 设置校准值
        ADCBtnsError setCalibrationValues(const char* mappingId, uint8_t buttonIndex, bool isAutoCalibration, uint16_t topValue, uint16_t bottomValue);

        // 开始采样
        ADCBtnsError startADCSamping(bool enableSamplingRate = false, 
                                   uint8_t virtualPin = 0, 
                                   uint32_t samplingCountMax = 0);

        // 停止采样
        void stopADCSamping();

        /**
         * @brief 读取ADC值 按virtualPin排序
         * 值要减去ADC_BASE_V 基准电压
         * @return 按virtualPin排序的ADC值
         */
        inline const std::array<ADCButtonValueInfo, NUM_ADC_BUTTONS>& readADCValues() const
        {
            for(uint8_t i = 0; i < NUM_ADC; i++) {
                SCB_CleanInvalidateDCache_by_Addr(adcBufferInfo[i].buffer, adcBufferInfo[i].size);
            }

            return ADCBufferInfoList;
        }

        inline void ADCValuesTestPrint() {
            const std::array<ADCButtonValueInfo, NUM_ADC_BUTTONS>& adcValues = readADCValues();

            // const std::array<ADCButtonValueInfo, NUM_ADC_BUTTONS>& adcValues = rawADCBufferInfoList;

            for(uint8_t i = 0; i < NUM_ADC_BUTTONS; i++){
                printf("%d", *adcValues[i].valuePtr);
                if(i != NUM_ADC_BUTTONS - 1){
                    printf(", ");
                }
            }
            printf("\n");
        }
        

    private:
        ADCManager();
        ~ADCManager();

        // ADC DMA 缓冲区必须保持静态
        static __attribute__((section("._RAM_D1_Area"))) uint32_t ADC1_Values[NUM_ADC1_BUTTONS];
        static __attribute__((section("._RAM_D1_Area"))) uint32_t ADC2_Values[NUM_ADC2_BUTTONS];
        static __attribute__((section("._RAM_D3_Area"))) uint32_t ADC3_Values[NUM_ADC3_BUTTONS];
        static uint32_t ADC_Values_Result[NUM_ADC_BUTTONS];

        MessageHandler messageHandler;

        // 非静态成员变量
        ADCValuesMappingStore store;
        ADCBufferInfo adcBufferInfo[NUM_ADC];
        ADCChannelStats ADCButtonStats;
        bool samplingRateEnabled;
        uint32_t samplingCountMax;
        ADCIndexInfo samplingADCInfo;

        void handleADCStats(ADC_HandleTypeDef *hadc);
        int8_t saveStore();
        ADCIndexInfo findADCButtonVirtualPin(uint8_t virtualPin);

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