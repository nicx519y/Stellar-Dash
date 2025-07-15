#pragma once
#include <cstdint>
#include <string>
#include "types.hpp"
#include "config.hpp"

/**
 * @brief Web配置模式下的LED预览管理器
 * 用于在webconfig模式下实时预览LED效果，不保存到存储
 */
class WebConfigLedsManager {
public:
    static WebConfigLedsManager& getInstance();

    /**
     * @brief 应用LED预览配置
     * @param config LED配置（来自前端apiPushLedsConfig请求）
     */
    void applyPreviewConfig(const LEDProfile& config);
    
    /**
     * @brief 清除预览配置，恢复默认配置
     */
    void clearPreviewConfig();
    
    /**
     * @brief 检查是否正在预览
     * @return true 如果正在预览模式
     */
    bool isInPreviewMode() const;
    
    /**
     * @brief 更新LED效果（在主循环中调用）
     * @param buttonMask 当前按键状态掩码，用于交互式效果
     */
    void update(uint32_t buttonMask = 0);
    
    /**
     * @brief 获取当前LED配置的JSON表示
     * @return JSON字符串
     */
    std::string toJSON() const;

private:
    WebConfigLedsManager();
    ~WebConfigLedsManager();
    
    /**
     * @brief 初始化启用按键掩码
     */
    void initializeEnabledKeysMask();
    
    bool previewMode;           // 是否在预览模式
    LEDProfile previewConfig;   // 预览配置
    uint32_t lastButtonMask;    // 上次按键状态，用于动画更新
    uint32_t enabledKeysMask;   // 启用按键掩码，控制只有启用的按键才点亮
}; 

#define WEBCONFIG_LEDS_MANAGER WebConfigLedsManager::getInstance()