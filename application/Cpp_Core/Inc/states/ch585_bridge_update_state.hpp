#ifndef CH585_BRIDGE_UPDATE_STATE_HPP
#define CH585_BRIDGE_UPDATE_STATE_HPP

#include "base_state.hpp"

class Ch585BridgeUpdateState final : public BaseState {
public:
    static Ch585BridgeUpdateState& getInstance()
    {
        static Ch585BridgeUpdateState instance;
        return instance;
    }

    bool enter() override;
    void tick() override;
    void exit() override;

private:
    Ch585BridgeUpdateState() = default;
    bool resetRequested = false;
};

#define CH585_BRIDGE_UPDATE_STATE Ch585BridgeUpdateState::getInstance()

#endif
