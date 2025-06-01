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

#define NUM_MAPPING_INDEX_WINDOW_SIZE 32
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
        uint32_t read();

        ADCBtnsError deinit();
        ADCBtnsWorker();
        ~ADCBtnsWorker();

        #if ENABLED_DYNAMIC_CALIBRATION == 1
        // 动态校准
        void dynamicCalibration();
        #endif

    private:

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

        typedef struct {
            // 按钮配置
            uint32_t virtualPin = 0;    // 虚拟引脚
            uint8_t pressAccuracyIndex = 0;   // 按下精度 转换成mapping索引数量
            uint8_t releaseAccuracyIndex = 0; // 释放精度 转换成mapping索引数量 (0.1mm精度)
            uint8_t topDeadzoneIndex = 0;     // 顶部死区 转换成mapping索引值
            uint8_t bottomDeadzoneIndex = 0;  // 底部死区 转换成mapping索引值 (0.1mm精度)
            
            // 高精度配置 (0.01mm精度)
            uint8_t highPrecisionReleaseAccuracyIndex = 0; // 高精度释放精度索引
            uint8_t highPrecisionBottomDeadzoneIndex = 0;  // 高精度底部死区索引

            // 校准参数
            bool initCompleted = false;     // 初始化完成
            uint8_t lastTriggerIndex = 0;  // 上一次触发索引

            uint16_t valueMapping[MAX_ADC_VALUES_LENGTH] = {0};           // 值映射 (0.1mm精度)
            
            // 高精度映射表 - 用于抬起行程前半段的精确检测
            uint16_t highPrecisionMapping[MAX_ADC_VALUES_LENGTH * 10] = {0}; // 高精度值映射 (0.01mm精度)
            uint16_t highPrecisionLength = 0;                                // 高精度映射表长度 (前半段行程)
            uint8_t halfwayIndex = 0;                                        // 中点索引 (完全按下到弹起一半的位置)

            ButtonState state = ButtonState::RELEASED;  // 当前状态
            uint8_t lastStateIndex = 0;                // 进入当前状态时的索引值

            #if ENABLED_DYNAMIC_CALIBRATION == 1
            bool needCalibration = false;
            RingBufferSlidingWindow<uint16_t> topValueWindow{NUM_MAPPING_INDEX_WINDOW_SIZE};  // 最小值滑动窗口
            RingBufferSlidingWindow<uint16_t> bottomValueWindow{NUM_MAPPING_INDEX_WINDOW_SIZE};   // 最大值滑动窗口
            uint16_t limitValue = 0;  // 限制值
            #endif

        } ADCBtn;

        

        // 获取按钮事件
        ButtonEvent getButtonEvent(ADCBtn* btn, const uint8_t currentIndex, const uint16_t currentValue);
        // 处理状态转换
        void handleButtonState(ADCBtn* btn, const ButtonEvent event);

        void updateButtonMapping(uint16_t* mapping, uint16_t firstValue, uint16_t lastValue);
        void initButtonMapping(ADCBtn* btn, const uint16_t releaseValue);

        uint8_t searchIndexInMapping(const uint8_t buttonIndex, const uint16_t value);
        
        // 高精度映射表相关函数
        void initHighPrecisionMapping(ADCBtn* btn);
        uint8_t searchIndexInHighPrecisionMapping(ADCBtn* btn, const uint16_t value);
        bool isInHighPrecisionRange(ADCBtn* btn, const uint8_t currentIndex);

        ADCBtn* buttonPtrs[NUM_ADC_BUTTONS];
        uint32_t virtualPinMask = 0x0;  // 虚拟引脚掩码
        uint16_t minValueDiff;
        const ADCValuesMapping* mapping;
        bool buttonTriggerStatusChanged = false;
};

#define ADC_BTNS_WORKER ADCBtnsWorker::getInstance()

#endif