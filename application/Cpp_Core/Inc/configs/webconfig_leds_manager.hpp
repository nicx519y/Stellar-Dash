#pragma once
#include <cstdint>
#include <string>

struct LedsConfig {
    uint8_t brightness;
    uint32_t color;
    uint8_t effect;
    // 可扩展更多LED参数
};

class WebConfigLedsManager {
public:
    static WebConfigLedsManager& getInstance();

    void applyLedsConfig(const LedsConfig& config);
    void update(); // 刷新LED效果
    std::string toJSON() const; // 可选，返回当前LED状态

private:
    WebConfigLedsManager();
    LedsConfig currentConfig;
}; 