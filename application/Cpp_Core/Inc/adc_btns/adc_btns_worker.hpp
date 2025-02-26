#ifndef __ADC_BTNS_WORKER_HPP__
#define __ADC_BTNS_WORKER_HPP__

#include <stdio.h>
#include "stm32h7xx.h"
#include "constant.hpp"
#include "storagemanager.hpp"
#include "adc.h"
#include "message_center.hpp"
#include "adc_btns_error.hpp"
#include "ring_buffer_sliding_window.hpp"
#include "utils.h"
#include "adc_manager.hpp"
#include "micro_timer.hpp"
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

        void dynamicCalibration();

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
            uint8_t releaseAccuracyIndex = 0; // 释放精度 转换成mapping索引数量
            uint8_t topDeadzoneIndex = 0;     // 顶部死区 转换成mapping索引值
            uint8_t bottomDeadzoneIndex = 0;  // 底部死区 转换成mapping索引值

            // 校准参数
            bool initCompleted = false;     // 初始化完成
            uint8_t lastTriggerIndex = 0;  // 上一次触发索引

            uint16_t valueMapping[MAX_ADC_VALUES_LENGTH] = {0};           // 值映射

            ButtonState state = ButtonState::RELEASED;  // 当前状态
            uint8_t lastStateIndex = 0;                // 进入当前状态时的索引值

            #if ENABLED_DYNAMIC_CALIBRATION == 1
            bool needCalibration = false;
            #endif

        } ADCBtn;

        // 获取按钮事件
        ButtonEvent getButtonEvent(ADCBtn* btn, const uint8_t currentIndex, const uint16_t currentValue);
        // 处理状态转换
        void handleButtonState(ADCBtn* btn, const uint8_t currentIndex, const uint16_t currentValue, const ButtonEvent event);

        void updateButtonMapping(uint16_t* mapping, uint16_t firstValue, uint16_t lastValue);
        uint8_t searchIndexInMapping(const uint8_t buttonIndex, const uint16_t value);

        ADCBtn* buttonPtrs[NUM_ADC_BUTTONS];
        uint32_t virtualPinMask = 0x0;  // 虚拟引脚掩码
        const ADCValuesMapping* mapping;
        bool buttonTriggerStatusChanged = false;
        
        uint32_t lastWorkTime = 0;

        #if ENABLED_DYNAMIC_CALIBRATION == 1    
        uint32_t lastCalibrationTime = 0;
        // 使用滑动窗口平滑更新
        RingBufferSlidingWindow<uint16_t> firstValueWindow;  // 最小值滑动窗口
        RingBufferSlidingWindow<uint16_t> lastValueWindow;   // 最大值滑动窗口
        RingBufferSlidingWindow<uint16_t> travelValueWindow; // 过渡值滑动窗口
        #endif

};

#define ADC_BTNS_WORKER ADCBtnsWorker::getInstance()

#endif