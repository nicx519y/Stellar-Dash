#pragma once

#include "config.hpp"

class WebConfigLedsManager {
public:
    static WebConfigLedsManager &getInstance();
    void applyPreviewConfig(const LEDProfile &config);
    void clearPreviewConfig();
    bool isInPreviewMode() const;
    void resetForContractTest() { preview_ = false; }

private:
    bool preview_ = false;
};

#define WEBCONFIG_LEDS_MANAGER WebConfigLedsManager::getInstance()
