#pragma once

#include <cstdint>

enum class BoardMode : uint8_t {
    Usb = 0,
    Rf = 1,
    CenterOff = 2,
    Fault = 3,
};

class BoardModeManager {
public:
    static BoardModeManager &getInstance();
    BoardMode current() const { return mode_; }
    bool isStable() const { return true; }
    void setForContractTest(BoardMode mode) { mode_ = mode; }

private:
    BoardMode mode_ = BoardMode::Usb;
};

#define BOARD_MODE BoardModeManager::getInstance()
