#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <functional>
#include "board_cfg.h"
#include "config.hpp"
#include "adc_btns/adc_btns_worker.hpp"  // 包含ExternalADCButtonConfig定义

// WebConfig模式下ADC按键的配置结构
struct WebConfigADCButtonConfig {
    float pressAccuracy = WEBCONFIG_ADC_DEFAULT_PRESS_ACCURACY;      // 按下精度（mm）
    float releaseAccuracy = WEBCONFIG_ADC_DEFAULT_RELEASE_ACCURACY;  // 释放精度（mm）
    float topDeadzone = WEBCONFIG_ADC_DEFAULT_TOP_DEADZONE;          // 顶部死区（mm）
    float bottomDeadzone = WEBCONFIG_ADC_DEFAULT_BOTTOM_DEADZONE;    // 底部死区（mm）
    bool enableHighSensitivity = WEBCONFIG_ADC_DEFAULT_HIGH_SENSITIVITY; // 是否启用高敏感度模式
};

class WebConfigBtnsManager {
public:
    static WebConfigBtnsManager& getInstance();

    void update();                                      // 更新按键状态（主循环调用）
    
    // 按键状态变化回调类型
    using ButtonStateChangedCallback = std::function<void()>;
    
    // 设置按键状态变化回调
    void setButtonStateChangedCallback(ButtonStateChangedCallback callback);
    
    // 获取触发掩码
    std::vector<bool> getButtonStates() const; // 获取所有按键当前状态
    
    void startButtonWorkers(); // 启动按键工作器
    void stopButtonWorkers(); // 停止按键工作器
    bool isActive() const; // 检查按键工作器是否活跃
    
    // 新增：按键触发检测
    uint32_t getAndClearTriggerMask(); // 获取并清空触发掩码
    bool hasTriggers() const; // 检查是否有按键触发
    uint8_t getTotalButtonCount() const; // 获取总按键数量

    // ========== ADC按键WebConfig模式专用配置接口 ==========
    
    /**
     * @brief 设置WebConfig模式下ADC按键的配置
     * @param buttonIndex ADC按键索引 (0 到 NUM_ADC_BUTTONS-1)
     * @param config ADC按键配置
     * @return true 设置成功
     */
    bool setADCButtonConfig(uint8_t buttonIndex, const WebConfigADCButtonConfig& config);
    
    /**
     * @brief 获取WebConfig模式下ADC按键的配置
     * @param buttonIndex ADC按键索引
     * @return ADC按键配置，如果索引无效返回默认配置
     */
    WebConfigADCButtonConfig getADCButtonConfig(uint8_t buttonIndex) const;
    
    /**
     * @brief 设置所有ADC按键使用相同的配置
     * @param config 统一配置
     */
    void setAllADCButtonsConfig(const WebConfigADCButtonConfig& config);
    
    /**
     * @brief 重置所有ADC按键配置为默认值
     */
    void resetADCButtonsConfig();
    
    /**
     * @brief 获取ADC按键配置的JSON表示
     * @return JSON字符串
     */
    std::string getADCConfigJSON() const;
    
    /**
     * @brief 从JSON字符串设置ADC按键配置
     * @param jsonStr JSON字符串
     * @return true 设置成功
     */
    bool setADCConfigFromJSON(const std::string& jsonStr);

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
    
    // ========== WebConfig模式ADC按键配置 ==========
    std::vector<WebConfigADCButtonConfig> adcButtonConfigs; // ADC按键配置数组
    
    // ========== 按键状态变化回调 ==========
    ButtonStateChangedCallback buttonStateChangedCallback; // 按键状态变化回调函数
}; 

#define WEBCONFIG_BTNS_MANAGER WebConfigBtnsManager::getInstance()