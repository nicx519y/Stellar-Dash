#ifndef SAFE_RECOVERY_STATE_HPP
#define SAFE_RECOVERY_STATE_HPP

#include "base_state.hpp"

class SafeRecoveryState final : public BaseState {
public:
    static SafeRecoveryState& getInstance()
    {
        static SafeRecoveryState instance;
        return instance;
    }

    bool enter() override;
    void tick() override;
    void exit() override;

private:
    SafeRecoveryState() = default;
};

#define SAFE_RECOVERY_STATE SafeRecoveryState::getInstance()

#endif
