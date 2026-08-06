#ifndef WEB_CONFIG_STATE_HPP
#define WEB_CONFIG_STATE_HPP

#include <stdint.h>

#include "base_state.hpp"

enum class WebConfigRuntimeStatus : uint8_t
{
    Starting = 0,
    Ready,
    Authenticated,
    ErrorUsbMode,
    ErrorMaintenance,
    ErrorSecurity,
    ErrorStorageInit,
    ErrorStorage,
};

class WebConfigState : public BaseState
{
public:
    WebConfigState(WebConfigState const &) = delete;
    void operator=(WebConfigState const &) = delete;

    static WebConfigState &getInstance()
    {
        static WebConfigState instance;
        return instance;
    }

    void setup() override;
    void loop() override;
    void reset() override;

    WebConfigRuntimeStatus status() const { return runtimeStatus; }
    bool canRetry() const;
    void requestRetry();
    void reportStorageFailure();

private:
    WebConfigState() = default;
    void enterFailure(WebConfigRuntimeStatus failureStatus);

    bool isRunning = false;
    bool retryRequested = false;
    bool recoveryUiPending = false;
    WebConfigRuntimeStatus runtimeStatus = WebConfigRuntimeStatus::Starting;
};

#define WEB_CONFIG_STATE WebConfigState::getInstance()

#endif
