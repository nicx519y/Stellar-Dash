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

// ADC按键技术测试模式下的触发事件信息
struct ADCBtnTestEvent {
    uint8_t buttonIndex;           // 按键索引
    uint8_t virtualPin;            // 虚拟引脚
    uint16_t adcValue;             // 触发时的ADC值
    float triggerDistance;         // 触发时的物理行程（mm）
    float limitValueDistance;      // limitValue对应的物理行程（mm）
    uint16_t limitValue;           // limitValue值
    bool isPressEvent;             // 是否为按下事件（true=按下，false=释放）
    uint32_t timestamp;            // 时间戳
};

class WebConfigBtnsManager {
public:
    static WebConfigBtnsManager& getInstance();

    // 按键状态变化回调类型
    using ButtonStateChangedCallback = std::function<void()>;
    
    // ADC按键技术测试模式回调类型
    using ADCBtnTestCallback = std::function<void(const ADCBtnTestEvent&)>;
    
    // 设置按键状态变化回调
    void setButtonStateChangedCallback(ButtonStateChangedCallback callback);
    
    // 设置ADC按键技术测试模式回调
    void setADCBtnTestCallback(ADCBtnTestCallback callback);
    
    void startButtonWorkers(); // 启动按键工作器
    void stopButtonWorkers(); // 停止按键工作器
    bool isActive() const; // 检查按键工作器是否活跃
    
    uint8_t getTotalButtonCount() const; // 获取总按键数量

    // ========== 技术测试模式控制 ==========
    
    /**
     * @brief 启用技术测试模式
     * @param enabled 是否启用
     */
    void enableTestMode(bool enabled);
    
    /**
     * @brief 检查技术测试模式是否启用
     * @return true 如果启用
     */
    bool isTestModeEnabled() const;

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

    void update(); // 更新按键状态 返回 按键触发掩码

    uint32_t getCurrentMask() const; // 获取当前按键状态掩码

private:
    WebConfigBtnsManager();
    ~WebConfigBtnsManager();
    
    void setupButtonWorkers(); // 初始化按键工作器
    void cleanupButtonWorkers(); // 清理按键工作器
    
    // 技术测试模式相关方法
    void processADCBtnTestEvent(uint8_t buttonIndex, bool isPressEvent, uint16_t adcValue);
    
    uint32_t currentMask; // 当前按键状态掩码
    uint32_t previousMask; // 上一次按键状态掩码
    bool isWorkerActive; // 按键工作器活跃状态
    bool isTestModeEnabled_; // 技术测试模式是否启用
    
    // ========== WebConfig模式ADC按键配置 ==========
    std::vector<WebConfigADCButtonConfig> adcButtonConfigs; // ADC按键配置数组
    
    // ========== 按键状态变化回调 ==========
    ButtonStateChangedCallback buttonStateChangedCallback; // 按键状态变化回调函数
    ADCBtnTestCallback adcBtnTestCallback; // ADC按键技术测试模式回调函数
    
    // ========== 技术测试模式事件收集 ==========
    std::vector<ADCBtnTestEvent> testEventCollector; // 测试事件收集器
}; 

#define WEBCONFIG_BTNS_MANAGER WebConfigBtnsManager::getInstance()