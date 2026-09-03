#pragma once

#include <string>

/**
 * Persistent configuration writes erase/program QSPI synchronously. They must
 * never run while the WebConfig button workers are responsible for real-time
 * state or performance telemetry.
 *
 * This policy is stateless so it adds no mutable/static RAM usage.
 */
inline bool webhidShouldBlockConfigWrite(
    const std::string &command,
    bool hasConfigWriteScope,
    bool buttonMonitorActive)
{
    if (!buttonMonitorActive) {
        return false;
    }

    const bool persistentDeviceControl =
        command == "reboot" ||
        command == "complete_firmware_upgrade_session" ||
        command == "ch585_update_begin" ||
        command == "ch585_update_complete";
    if (!hasConfigWriteScope && !persistentDeviceControl) {
        return false;
    }

    /*
     * Preview writes and multipart staging do not call Storage::saveConfig().
     * exit_webconfig is also allowed through because its handler first stops
     * every runtime owner before it performs its final persistent write.
     */
    return command != "push_leds_config" &&
           command != "clear_leds_preview" &&
           command != "preview_screen_brightness" &&
           command != "import_config_begin" &&
           command != "import_config_part" &&
           command != "import_config_abort" &&
           command != "exit_webconfig";
}
