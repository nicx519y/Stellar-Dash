#pragma once
#include <cstring>

// Only new transactions are refused at low charge. Accepted upgrade chunks,
// commit and abort must retain their original supply and ordering guarantees.
inline bool batteryStartsUpgrade(const char* command) {
    return !strcmp(command, "begin_release_install") ||
        !strcmp(command, "create_firmware_upgrade_session") ||
        !strcmp(command, "ch585_update_begin") ||
        !strcmp(command, "retry_release_install");
}
inline bool batteryContinuesUpgrade(const char* command) {
    return !strcmp(command, "backup_release_tx") || !strcmp(command, "prepare_release_install") ||
        !strcmp(command, "activate_release_install") || !strcmp(command, "abort_release_install") ||
        !strcmp(command, "upload_firmware_chunk") || !strcmp(command, "complete_firmware_upgrade_session") ||
        !strcmp(command, "abort_firmware_upgrade_session") || !strcmp(command, "cleanup_firmware_upgrade_session") ||
        !strcmp(command, "get_firmware_upgrade_status") || !strcmp(command, "ch585_update_status") ||
        !strcmp(command, "ch585_update_chunk") || !strcmp(command, "ch585_update_complete");
}
inline bool batteryRejectsCommand(const char* command, bool low, bool critical, bool readOnly) {
    return (low && batteryStartsUpgrade(command)) ||
        (critical && !readOnly && !batteryContinuesUpgrade(command));
}
