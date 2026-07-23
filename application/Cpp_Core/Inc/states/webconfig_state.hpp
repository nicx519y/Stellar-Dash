#ifndef WEB_CONFIG_STATE_HPP
#define WEB_CONFIG_STATE_HPP

#include "base_state.hpp"

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

private:
    WebConfigState() = default;
    bool isRunning = false;
};

#define WEB_CONFIG_STATE WebConfigState::getInstance()

#endif
