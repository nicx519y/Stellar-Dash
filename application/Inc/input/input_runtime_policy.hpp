#ifndef INPUT_RUNTIME_POLICY_HPP
#define INPUT_RUNTIME_POLICY_HPP

#include "enums.hpp"

constexpr InputMode effectiveInputModeForConnection(ConnectionMode connectionMode,
                                                     InputMode configuredMode)
{
    return connectionMode == CONNECTION_MODE_RF24G
        ? INPUT_MODE_XINPUT
        : configuredMode;
}

constexpr bool requiresRfXInputPersistence(ConnectionMode connectionMode,
                                           InputMode configuredMode)
{
    return connectionMode == CONNECTION_MODE_RF24G &&
           configuredMode != INPUT_MODE_XINPUT;
}

constexpr bool requiresNeutralSocdForBypass(InputMode inputMode)
{
    return inputMode == INPUT_MODE_SWITCH ||
           inputMode == INPUT_MODE_PS4 ||
           inputMode == INPUT_MODE_PS5;
}

#endif
