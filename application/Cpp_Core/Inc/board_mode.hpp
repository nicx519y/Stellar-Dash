#ifndef BOARD_MODE_HPP
#define BOARD_MODE_HPP

#include <stdint.h>

enum class BoardMode : uint8_t {
    Usb = 0,
    Rf = 1,
    CenterOff = 2,
    Fault = 3,
};

class BoardModeManager {
public:
    BoardModeManager(const BoardModeManager&) = delete;
    BoardModeManager& operator=(const BoardModeManager&) = delete;

    static BoardModeManager& getInstance()
    {
        static BoardModeManager instance;
        return instance;
    }

    void setup();
    void update(uint32_t nowMs);
    BoardMode current() const { return stableMode; }
    bool isStable() const { return stable; }
    bool consumeChanged();

private:
    BoardModeManager() = default;
    BoardMode readRaw() const;

    BoardMode stableMode = BoardMode::CenterOff;
    BoardMode candidateMode = BoardMode::CenterOff;
    uint32_t candidateSinceMs = 0u;
    bool stable = false;
    bool changed = false;
};

#define BOARD_MODE BoardModeManager::getInstance()

#endif
