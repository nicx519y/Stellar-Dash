#include "board_mode.hpp"

void BoardModeManager::setup(BoardMode mode)
{
    stableMode = mode;
    stable = mode == BoardMode::Usb || mode == BoardMode::Rf;
}
