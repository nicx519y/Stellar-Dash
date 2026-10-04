#pragma once
#include "base_state.hpp"

// ROM ISP is owned by the external download tool, not the SPI updater.
class TxIspState final : public BaseState {
public:
    static TxIspState& getInstance() { static TxIspState instance; return instance; }
    bool enter() override;
    void tick() override;
    void exit() override;
private:
    TxIspState() = default;
};
#define TX_ISP_STATE TxIspState::getInstance()
