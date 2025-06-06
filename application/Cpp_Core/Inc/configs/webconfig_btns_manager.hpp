#pragma once
#include <vector>
#include <string>
#include <cstdint>

class WebConfigBtnsManager {
public:
    static WebConfigBtnsManager& getInstance();

    void update(); // 刷新按键状态（主循环调用）
    std::vector<bool> getButtonStates() const; // 获取所有按键当前状态
    std::string toJSON() const; // 转为JSON字符串
    
    void startButtonWorkers(); // 启动按键工作器
    void stopButtonWorkers(); // 停止按键工作器
    bool isActive() const; // 检查按键工作器是否活跃
    
    // 新增：按键触发检测
    uint32_t getAndClearTriggerMask(); // 获取并清空触发掩码
    bool hasTriggers() const; // 检查是否有按键触发
    uint8_t getTotalButtonCount() const; // 获取总按键数量

private:
    WebConfigBtnsManager();
    ~WebConfigBtnsManager();
    
    void setupButtonWorkers(); // 初始化按键工作器
    void cleanupButtonWorkers(); // 清理按键工作器
    
    void checkButtonTriggers(); // 检查按键触发（0->1跳变）
    
    std::vector<bool> btnStates; // 按键当前状态
    std::vector<bool> prevBtnStates; // 按键上一次状态
    uint32_t triggerMask; // 按键触发掩码（本周期内从0->1的按键）
    bool isWorkerActive; // 按键工作器活跃状态
}; 

#define WEBCONFIG_BTNS_MANAGER WebConfigBtnsManager::getInstance()