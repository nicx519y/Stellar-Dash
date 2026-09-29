#pragma once

#include <string>

// These commands only persist ordinary configuration. WebHID services live
// input/LED feedback during their QSPI waits, without dispatching another RPC.
inline bool webhidIsLiveConfigWrite(const std::string &command)
{
    return command == "update_global_config" ||
           command == "update_screen_control_config" ||
           command == "update_hotkeys_config" ||
           command == "update_profile" ||
           command == "update_macro" ||
           command == "update_profile_macros" ||
           command == "switch_default_profile";
}

// Calibration, performance measurement, imports and upgrades remain exclusive.
inline bool webhidShouldBlockConfigWrite(
    const std::string &command,
    bool hasConfigWriteScope,
    bool buttonMonitorActive,
    bool performanceMonitoring = false)
{
    if (!buttonMonitorActive) {
        return false;
    }

    if (hasConfigWriteScope && !performanceMonitoring && webhidIsLiveConfigWrite(command)) {
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
