#pragma once

#include <cstdint>
#include <functional>
#include <vector>

struct WebConfigADCButtonConfig {};

struct ADCBtnTestEvent {
    uint8_t buttonIndex = 0u;
    uint8_t virtualPin = 0u;
    bool isPressEvent = false;
};

class WebConfigBtnsManager {
public:
    using ButtonStateChangedCallback = std::function<void()>;
    using ButtonPerformanceMonitoringCallback = std::function<void()>;
    using ADCBtnTestCallback = std::function<void(const ADCBtnTestEvent &)>;

    static WebConfigBtnsManager &getInstance();
    void setButtonStateChangedCallback(ButtonStateChangedCallback callback);
    void setButtonPerformanceMonitoringCallback(
        ButtonPerformanceMonitoringCallback callback);
    void setADCBtnTestCallback(ADCBtnTestCallback callback);
    bool startButtonWorkers();
    void stopButtonWorkers();
    bool isActive() const;
    uint8_t getTotalButtonCount() const;
    void enableTestMode(bool enabled);
    bool isTestModeEnabled() const;
    uint32_t getCurrentMask() const;
    std::vector<uint8_t> buildButtonPerformanceMonitoringBinaryData();
    void resetForContractTest() { active_ = false; testMode_ = false; stateCallback_ = {}; }

private:
    bool active_ = false;
    bool testMode_ = false;
    ButtonStateChangedCallback stateCallback_;
};

#define WEBCONFIG_BTNS_MANAGER WebConfigBtnsManager::getInstance()
