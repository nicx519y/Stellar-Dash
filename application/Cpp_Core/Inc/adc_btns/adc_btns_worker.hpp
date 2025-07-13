#ifndef __ADC_BTNS_WORKER_HPP__
#define __ADC_BTNS_WORKER_HPP__

#include <stdio.h>
#include "stm32h7xx.h"
#include "storagemanager.hpp"
#include "adc.h"
#include "message_center.hpp"
#include "adc_btns_error.hpp"
#include "ring_buffer_sliding_window.hpp"
#include "utils.h"
#include "adc_manager.hpp"
#include "micro_timer.hpp"
#include "board_cfg.h"
#include "adc_btns/adc_debounce_filter.hpp"

#define NUM_MAPPING_INDEX_WINDOW_SIZE 32

// 外部ADC按键配置结构（用于WebConfig等模式）
struct ExternalADCButtonConfig {
    float pressAccuracy;      // 按下精度（mm）
    float releaseAccuracy;    // 释放精度（mm）
    float topDeadzone;        // 顶部死区（mm）
    float bottomDeadzone;     // 底部死区（mm）
};

// 错误码定义
enum class ADCBtnsWorkerError {
    SUCCESS = 0,
    INVALID_PARAMS = -1,        // 参数无效
    MEMORY_ERROR = -2,          // 内存错误
    ADC1_CALIB_FAILED = -3,     // ADC1校准失败
    ADC2_CALIB_FAILED = -4,     // ADC2校准失败
    DMA1_START_FAILED = -5,     // DMA1启动失败
    DMA2_START_FAILED = -6,     // DMA2启动失败
    MAPPING_ERROR = -7,         // 映射操作失败
    ALREADY_STARTED = -8,       // DMA已经启动
    NOT_STARTED = -9,           // DMA未启动
    BUTTON_CONFIG_ERROR = -10   // 按钮配置错误
};


class ADCBtnsWorker {
    public:
        ADCBtnsWorker(ADCBtnsWorker const&) = delete;
        void operator=(ADCBtnsWorker const&) = delete;

        static ADCBtnsWorker& getInstance() {
            static ADCBtnsWorker instance;
            return instance;
        }
        ADCBtnsError setup();
        
        /**
         * @brief 使用外部配置初始化ADC按键工作器（用于WebConfig等模式）
         * @param externalConfigs 外部配置数组，长度必须为NUM_ADC_BUTTONS
         * @return ADCBtnsError 初始化结果
         */
        ADCBtnsError setup(const ExternalADCButtonConfig* externalConfigs);
        
        uint32_t read();

        ADCBtnsError deinit();
        ADCBtnsWorker();
        ~ADCBtnsWorker();

        // 动态校准
        void dynamicCalibration();

        /**
         * @brief 设置防抖过滤器配置
         * @param config 防抖配置
         */
        void setDebounceConfig(const ADCDebounceFilter::Config& config);

        /**
         * @brief 获取防抖过滤器配置
         * @return 当前防抖配置
         */
        const ADCDebounceFilter::Config& getDebounceConfig() const;

        /**
         * @brief 重置防抖状态
         */
        void resetDebounceState();

        /**
         * @brief 获取指定按钮的防抖状态 (调试用)
         * @param buttonIndex 按钮索引
         * @return 防抖状态值
         */
        uint8_t getButtonDebounceState(uint8_t buttonIndex) const;

    private:

        // 校准保存延迟常量 (毫秒)
        static constexpr uint32_t CALIBRATION_SAVE_DELAY_MS = 5000;  // 5秒后保存
        static constexpr uint32_t MIN_CALIBRATION_INTERVAL_MS = 1000; // 最小校准间隔1秒

        // 按钮状态枚举
        enum class ButtonState {
            RELEASED,       // 完全释放状态
            RELEASING,      // 正在释放过程中
            PRESSED,        // 完全按下状态
            PRESSING,       // 正在按下过程中
        };

        // 按钮事件枚举
        enum class ButtonEvent {
            NONE,
            PRESS_START,    // 开始按下
            PRESS_COMPLETE, // 按下完成
            RELEASE_START,  // 开始释放
            RELEASE_COMPLETE// 释放完成
        };

        // 按钮结构体
        struct ADCBtn {
            uint8_t virtualPin;  // 虚拟引脚
            uint16_t valueMapping[MAX_ADC_VALUES_LENGTH];  // 当前使用的校准后映射值
            uint16_t calibratedMapping[MAX_ADC_VALUES_LENGTH];      // 根据校准值生成的完整映射
            ButtonState state;  // 按钮状态
            bool initCompleted;  // 初始化完成标志

            // 新的基于距离和ADC值的字段
            float lastTravelDistance = 0.0f;    // 上次行程距离（mm）
            uint16_t lastAdcValue = 0;           // 上次ADC值
            float pressAccuracyMm = 0.0f;        // 按下精度（mm）
            float releaseAccuracyMm = 0.0f;      // 释放精度（mm）
            float highPrecisionReleaseAccuracyMm = 0.0f; // 高精度释放精度（mm）
            float topDeadzoneMm = 0.0f;          // 顶部死区（mm）
            float bottomDeadzoneMm = 0.0f;       // 底部死区（mm）
            float halfwayDistanceMm = 0.0f;      // 中点距离（mm），用于高精度判断
            uint16_t triggerValue = 0;            // 触发值
            bool needCalibration = false;
            bool needSaveCalibration = false;          // 需要保存校准值到存储
            uint32_t lastCalibrationTime = 0;         // 上次校准时间
            uint32_t lastSaveTime = 0;                // 上次保存时间
            RingBufferSlidingWindow<uint16_t> topValueWindow{NUM_MAPPING_INDEX_WINDOW_SIZE};  // 最小值滑动窗口
            RingBufferSlidingWindow<uint16_t> bottomValueWindow{NUM_MAPPING_INDEX_WINDOW_SIZE};   // 最大值滑动窗口
            uint16_t limitValue = 0;  // 限制值
        };

        // 获取按钮事件
        ButtonEvent getButtonEvent(ADCBtn* btn, const uint16_t currentValue, const uint8_t buttonIndex);
        // 处理状态转换
        void handleButtonState(ADCBtn* btn, const ButtonEvent event);

        void initButtonMapping(ADCBtn* btn, const uint16_t releaseValue);

        // 新的基于距离和ADC值换算的核心方法
        float getDistanceByValue(ADCBtn* btn, const uint16_t adcValue);
        uint16_t getValueByDistance(ADCBtn* btn, const uint16_t baseAdcValue, const float distanceMm);
        float getCurrentPressAccuracy(ADCBtn* btn, const float currentDistance);
        float getCurrentReleaseAccuracy(ADCBtn* btn, const float currentDistance);
        void updateLimitValue(ADCBtn* btn, const uint16_t currentValue);
        void resetLimitValue(ADCBtn* btn, const uint16_t currentValue);
        
        // 校准保存相关方法
        void saveCalibrationValues();
        bool shouldSaveCalibration(ADCBtn* btn, uint32_t currentTime);

        /**
         * 根据校准值生成完整的校准后映射
         * @param btn 按钮指针
         * @param topValue 完全释放时的校准值（较小的ADC值）
         * @param bottomValue 完全按下时的校准值（较大的ADC值）
         */
        void generateCalibratedMapping(ADCBtn* btn, uint16_t topValue, uint16_t bottomValue);

        const ADCValuesMapping* mapping = nullptr;  // 映射表指针
        ADCBtn* buttonPtrs[NUM_ADC_BUTTONS];        // 按钮指针数组
        uint32_t virtualPinMask = 0x0;              // 虚拟引脚掩码
        bool buttonTriggerStatusChanged = false;    // 按钮触发状态是否改变
        uint16_t minValueDiff;                      // 最小值差值
        uint32_t enabledKeysMask = 0x0;             // 启用按键掩码
        
        // 防抖过滤器
        ADCDebounceFilter debounceFilter_;

        // 动态校准相关函数
};

#define ADC_BTNS_WORKER ADCBtnsWorker::getInstance()

#endif