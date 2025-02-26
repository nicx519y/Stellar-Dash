#ifndef __GPIO_BTNS_WORKER_HPP__
#define __GPIO_BTNS_WORKER_HPP__

#include <stdio.h>
#include "stm32h7xx.h"
#include "constant.hpp"
#include "storagemanager.hpp"
#include "message_center.hpp"
#include "gpio-btn.h"
#include "micro_timer.hpp"
#include <array>
#include <algorithm>


class GPIOBtnsWorker {
    public:
        GPIOBtnsWorker(GPIOBtnsWorker const&) = delete;
        void operator=(GPIOBtnsWorker const&) = delete;
        ~GPIOBtnsWorker();
        
        static GPIOBtnsWorker& getInstance() {
            static GPIOBtnsWorker instance;
            return instance;
        }
        
        void setup();
        uint32_t read();
        GPIOBtnsWorker();

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
            uint32_t virtualPin;         // 虚拟引脚
            ButtonState state;           // 当前状态
            uint32_t lastStateTime;      // 进入当前状态的时间
            uint32_t debounceTime;       // 防抖时间
            bool lastRawState;           // 上一次的原始状态
        } GPIOBtn;

        std::array<GPIOBtn, NUM_GPIO_BUTTONS> buttonStates;
        uint32_t virtualPinMask = 0x0;  // 虚拟引脚掩码
        bool buttonStateChanged = false;
        
};

#define GPIO_BTNS_WORKER GPIOBtnsWorker::getInstance()

#endif 