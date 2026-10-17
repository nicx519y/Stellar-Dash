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

    // Latched at runtime entry; saving a preference does not hot-switch.
    void setup(BoardMode mode = BoardMode::Usb);
    BoardMode current() const { return stableMode; }
    bool isStable() const { return stable; }
    bool isWebConfigAllowed() const { return stable && (stableMode == BoardMode::Usb || stableMode == BoardMode::Rf); }
    /* Prepared USB power belongs to the selected runtime, not GPIO levels. */
    bool isUsbStartupSafe() const {
        return stable && stableMode == BoardMode::Usb;
    }

private:
    BoardModeManager() = default;

    BoardMode stableMode = BoardMode::CenterOff;
    bool stable = false;
};

#define BOARD_MODE BoardModeManager::getInstance()

#endif
