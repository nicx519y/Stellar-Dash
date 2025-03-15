#ifndef __INPUT_STATE_HPP__
#define __INPUT_STATE_HPP__

#include "base_state.hpp"
#include "drivermanager.hpp"

class InputState : public BaseState {
    public:
        // 禁用拷贝构造和赋值操作符
        InputState(InputState const&) = delete;
        void operator=(InputState const&) = delete;

        // 获取单例实例
        static InputState& getInstance() {
            static InputState instance;
            return instance;
        }

        // 实现基类的虚函数
        void setup() override;
        void loop() override;
        void reset() override;

    private:
        // 私有构造函数
        InputState() = default;
        bool isRunning = false;
        GPDriver * inputDriver;

        uint32_t workTime = 0;
        uint32_t calibrationTime = 0;
        uint32_t ledAnimationTime = 0;
        uint32_t virtualPinMask = 0x0;
};

// 定义一个宏方便使用
#define INPUT_STATE InputState::getInstance()

#endif
