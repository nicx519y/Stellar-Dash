#ifndef __GPIO_BTNS_WORKER_HPP__
#define __GPIO_BTNS_WORKER_HPP__

#include <stdio.h>
#include "stm32h7xx.h"
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

        typedef struct {
            uint32_t virtualPin;         // 虚拟引脚
            ButtonState state;           // 当前状态
            uint32_t lastStateTime;      // 进入当前状态的时间
            uint32_t debounceTime;       // 防抖时间
            bool lastRawState;           // 上一次的原始状态
        } GPIOBtn;

        // 按钮初始化回调函数
        static GPIOBtnsWorker* instance_;
        static void initCallback(uint8_t virtualPin, bool isPressed, uint8_t idx);
        static void readCallback(uint8_t virtualPin, bool isPressed, uint8_t idx);
        
        std::array<GPIOBtn, NUM_GPIO_BUTTONS> buttonStates;
        uint32_t virtualPinMask = 0x0;  // 虚拟引脚掩码
        bool buttonStateChanged = false;
        uint8_t currentInitIndex = 0;    // 当前初始化的按钮索引
};

#define GPIO_BTNS_WORKER GPIOBtnsWorker::getInstance()

#endif 