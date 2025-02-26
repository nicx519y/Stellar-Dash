#ifndef __WEBCONFIG_STATE_HPP__
#define __WEBCONFIG_STATE_HPP__

#include "base_state.hpp"
#include "fsdata.h"
#include "drivermanager.hpp"
#include "configmanager.hpp"
#include "message_center.hpp"

class WebConfigState : public BaseState {
    public:
        // 禁用拷贝构造和赋值操作符
        WebConfigState(WebConfigState const&) = delete;
        void operator=(WebConfigState const&) = delete;

        // 获取单例实例
        static WebConfigState& getInstance() {
            static WebConfigState instance;
            return instance;
        }

        // 实现基类的虚函数
        void setup() override;
        void loop() override;
        void reset() override;

    private:
        // 私有构造函数
        WebConfigState() = default;
        bool isRunning = false;
        GPDriver * inputDriver;
};

// 定义一个宏方便使用
#define WEB_CONFIG_STATE WebConfigState::getInstance()

#endif
