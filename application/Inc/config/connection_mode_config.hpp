#pragma once
#include "screen_control_config.hpp"

namespace ConnectionConfig {
// A1 and older used a physical switch: their stored preference was not
// authoritative. Default to USB once, without changing the binary layout.
inline ConnectionMode selected(const Config& config) {
    return config.screenControl.reservedStyle[0] == ScreenConfig::kSchema &&
           config.connectionMode == CONNECTION_MODE_RF24G
        ? CONNECTION_MODE_RF24G : CONNECTION_MODE_USB;
}
inline bool normalize(Config& config) {
    const auto mode = selected(config);
    const bool changed = config.connectionMode != mode;
    config.connectionMode = mode;
    return changed;
}
}
