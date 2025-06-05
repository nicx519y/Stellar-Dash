#include "webconfig_leds_manager.hpp"
#include <cJSON.h>
// TODO: 替换为实际LED驱动API头文件
//#include "your_led_driver_api.h"

WebConfigLedsManager& WebConfigLedsManager::getInstance() {
    static WebConfigLedsManager instance;
    return instance;
}

WebConfigLedsManager::WebConfigLedsManager() {
    // 初始化默认LED配置
    currentConfig = {100, 0xFFFFFF, 0};
}

void WebConfigLedsManager::applyLedsConfig(const LedsConfig& config) {
    currentConfig = config;
    // TODO: 实际调用LED驱动
    // set_led_brightness(config.brightness);
    // set_led_color(config.color);
    // set_led_effect(config.effect);
}

void WebConfigLedsManager::update() {
    // 可选：周期性刷新LED效果
}

std::string WebConfigLedsManager::toJSON() const {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "brightness", currentConfig.brightness);
    cJSON_AddNumberToObject(root, "color", currentConfig.color);
    cJSON_AddNumberToObject(root, "effect", currentConfig.effect);
    char* str = cJSON_PrintUnformatted(root);
    std::string result(str);
    cJSON_Delete(root);
    free(str);
    return result;
} 