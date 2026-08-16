#include "webhid_rpc_dispatcher.hpp"

#include <array>
#include <cstring>

#include "configs/device_command_handler.hpp"
#include "configs/device_command_message.hpp"
#include "device_security_protocol.h"

namespace {

constexpr size_t kMaximumSerializedResponseBytes = 16u * 1024u;
/*
 * cJSON_PrintPreallocated() writes directly into this single static response
 * workspace. The dispatcher is called synchronously from the WebHID service,
 * which copies the result into its outbound storage before another request is
 * dispatched. Keeping this buffer out of newlib's 32 KiB heap avoids a second
 * near-16 KiB allocation while the command handler's cJSON tree is alive.
 */
std::array<char, kMaximumSerializedResponseBytes + 5u> serializedResponse = {};

bool commandIn(const std::string &command,
               const char *const *values,
               size_t count)
{
    for (size_t index = 0u; index < count; ++index) {
        if (command == values[index]) {
            return true;
        }
    }
    return false;
}

WebHidRpcResult encodeRoot(cJSON *root,
                           uint32_t transactionId,
                           int error,
                           bool explicitSuccess)
{
    WebHidRpcResult result;
    result.transactionId = transactionId;
    result.error = error;
    result.explicitSuccess = explicitSuccess;
    if (root == nullptr) {
        result.failureMessage = "Failed to allocate RPC response";
        return result;
    }
    serializedResponse.fill('\0');
    if (!cJSON_PrintPreallocated(root,
                                 serializedResponse.data(),
                                 serializedResponse.size(),
                                 false)) {
        result.error = 413;
        result.failureMessage = "RPC response exceeds the 16 KiB limit";
        return result;
    }
    const size_t length = strlen(serializedResponse.data());
    if (length == 0u || length > kMaximumSerializedResponseBytes) {
        result.error = 413;
        result.failureMessage = "RPC response exceeds the 16 KiB limit";
        return result;
    }
    result.json = serializedResponse.data();
    result.jsonLength = length;
    return result;
}

WebHidRpcResult serializeResponse(uint32_t transactionId,
                                  DeviceCommandResponse &response)
{
    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) {
        return encodeRoot(nullptr,
                          transactionId,
                          500,
                          false);
    }
    cJSON_AddNumberToObject(root, "transactionId", transactionId);
    cJSON_AddNumberToObject(root, "errNo", response.getErrNo());
    cJSON_AddStringToObject(root,
                            "command",
                            response.getCommand().c_str());
    cJSON *data = response.releaseData();
    if (data != nullptr) {
        cJSON *message = cJSON_GetObjectItem(data, "errorMessage");
        if (message != nullptr && cJSON_IsString(message)) {
            cJSON_AddStringToObject(
                root, "errorMessage", message->valuestring);
        }
    } else {
        data = cJSON_CreateObject();
    }
    if (data == nullptr) {
        cJSON_Delete(root);
        return encodeRoot(nullptr,
                          transactionId,
                          500,
                          false);
    }
    const bool explicitSuccess =
        response.getErrNo() == 0 &&
        cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(data, "success"));
    cJSON_AddItemToObject(root, "data", data);
    WebHidRpcResult result = encodeRoot(root,
                                        transactionId,
                                        response.getErrNo(),
                                        explicitSuccess);
    cJSON_Delete(root);
    return result;
}

WebHidRpcResult localError(uint32_t transactionId,
                           int error,
                           const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) {
        return encodeRoot(nullptr, transactionId, error, false);
    }
    cJSON_AddNumberToObject(root, "transactionId", transactionId);
    cJSON_AddNumberToObject(root, "errNo", error);
    cJSON_AddStringToObject(root, "errorMessage", message);
    cJSON_AddItemToObject(root, "data", cJSON_CreateObject());
    WebHidRpcResult result = encodeRoot(root,
                                        transactionId,
                                        error,
                                        false);
    cJSON_Delete(root);
    return result;
}

} // namespace

WebHidRpcDispatcher &WebHidRpcDispatcher::getInstance()
{
    static WebHidRpcDispatcher dispatcher;
    return dispatcher;
}

void WebHidRpcDispatcher::initialize()
{
    if (!initialized) {
        DeviceCommandDispatcher::getInstance().initializeHandlers();
        initialized = true;
    }
}

void WebHidRpcDispatcher::clearSerializedResponse()
{
    serializedResponse.fill('\0');
}

uint32_t WebHidRpcDispatcher::requiredScope(const std::string &command)
{
    static const char *const configRead[] = {
        "get_global_config",
        "get_hotkeys_config",
        "get_screen_control_config",
        "export_all_config",
        "get_profile_list",
        "get_default_profile",
        "get_profile_details",
        "get_macro",
        "get_profile_macros",
        "ms_get_list",
        "ms_get_mark_status",
        "ms_get_default",
        "ms_get_mapping",
        "get_hitbox_layout",
        "get_firmware_metadata",
    };
    static const char *const configWrite[] = {
        "update_global_config",
        "update_hotkeys_config",
        "update_screen_control_config",
        "import_all_config",
        "import_config_part",
        "import_config_finish",
        "push_leds_config",
        "clear_leds_preview",
        "update_profile",
        "update_macro",
        "update_profile_macros",
        "create_profile",
        "delete_profile",
        "switch_default_profile",
        "ms_set_default",
        "ms_create_mapping",
        "ms_delete_mapping",
        "ms_rename_mapping",
        "clear_manual_calibration_data",
    };
    static const char *const monitorRead[] = {
        "start_button_monitoring",
        "stop_button_monitoring",
        "start_button_performance_monitoring",
        "stop_button_performance_monitoring",
        "get_button_states",
        "get_device_logs_list",
        "performance.get-checkpoint",
        "performance.clock-sync",
    };
    static const char *const deviceControl[] = {
        "reboot",
        "start_manual_calibration",
        "stop_manual_calibration",
        "get_calibration_status",
        "check_is_manual_calibration_completed",
        "ms_mark_mapping_start",
        "ms_mark_mapping_stop",
        "ms_mark_mapping_step",
    };
    static const char *const firmwareUpdate[] = {
        "create_firmware_upgrade_session",
        "upload_firmware_chunk",
        "complete_firmware_upgrade_session",
        "abort_firmware_upgrade_session",
        "get_firmware_upgrade_status",
        "cleanup_firmware_upgrade_session",
        "ch585_update_status",
        "ch585_update_begin",
        "ch585_update_chunk",
        "ch585_update_complete",
    };

    if (command == "ping" || command == "session.end") {
        return HBOX_SCOPE_CONFIG_READ;
    }
    if (command == "get_device_auth") {
        /*
         * Keep the name recognizable so secure V2 clients receive an
         * explicit retirement error instead of accidentally probing the
         * legacy UID/hash implementation.
         */
        return HBOX_SCOPE_CONFIG_READ;
    }
    if (commandIn(command,
                  configRead,
                  sizeof(configRead) / sizeof(configRead[0]))) {
        return HBOX_SCOPE_CONFIG_READ;
    }
    if (commandIn(command,
                  configWrite,
                  sizeof(configWrite) / sizeof(configWrite[0]))) {
        return HBOX_SCOPE_CONFIG_WRITE;
    }
    if (commandIn(command,
                  monitorRead,
                  sizeof(monitorRead) / sizeof(monitorRead[0]))) {
        return HBOX_SCOPE_MONITOR_READ;
    }
    if (commandIn(command,
                  deviceControl,
                  sizeof(deviceControl) / sizeof(deviceControl[0]))) {
        return HBOX_SCOPE_DEVICE_CONTROL;
    }
    if (commandIn(command,
                  firmwareUpdate,
                  sizeof(firmwareUpdate) / sizeof(firmwareUpdate[0]))) {
        return HBOX_SCOPE_FIRMWARE_UPDATE;
    }
    if (command == "binary.exchange") {
        /*
         * The concrete binary opcode is checked by WebHidService before this
         * pseudo command is executed.
         */
        return 0u;
    }
    if (command == "stream.begin" || command == "stream.credit" ||
        command == "stream.complete" || command == "stream.abort") {
        return 0u;
    }
    return UINT32_MAX;
}

WebHidRpcResult WebHidRpcDispatcher::dispatch(
    cJSON *requestRoot,
    uint32_t grantedScopes)
{
    initialize();
    if (requestRoot == nullptr || !cJSON_IsObject(requestRoot)) {
        return localError(0u, 400, "RPC request must be an object");
    }
    cJSON *transaction =
        cJSON_GetObjectItemCaseSensitive(requestRoot, "transactionId");
    cJSON *commandItem =
        cJSON_GetObjectItemCaseSensitive(requestRoot, "command");
    cJSON *params =
        cJSON_GetObjectItemCaseSensitive(requestRoot, "params");
    if (!cJSON_IsNumber(transaction) ||
        transaction->valuedouble < 1.0 ||
        transaction->valuedouble > 4294967295.0 ||
        !cJSON_IsString(commandItem) ||
        commandItem->valuestring == nullptr ||
        strlen(commandItem->valuestring) > 96u ||
        (params != nullptr && !cJSON_IsObject(params))) {
        return localError(0u, 400, "RPC envelope is invalid");
    }
    const uint32_t transactionId =
        static_cast<uint32_t>(transaction->valuedouble);
    const std::string command(commandItem->valuestring);
    const uint32_t scope = requiredScope(command);
    if (scope == UINT32_MAX) {
        return localError(transactionId, 404, "Unknown command");
    }
    if (scope == 0u ||
        (grantedScopes & scope) != scope) {
        return localError(transactionId, 403, "Command scope denied");
    }
    if (command == "get_device_auth") {
        return localError(
            transactionId, 410, "Legacy weak device authentication is disabled");
    }
    if (command == "ping") {
        cJSON *root = cJSON_CreateObject();
        cJSON *data = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "transactionId", transactionId);
        cJSON_AddNumberToObject(root, "errNo", 0);
        cJSON_AddStringToObject(root, "command", "ping");
        cJSON_AddStringToObject(data, "message", "pong");
        cJSON_AddItemToObject(root, "data", data);
        WebHidRpcResult result = encodeRoot(root,
                                            transactionId,
                                            0,
                                            false);
        cJSON_Delete(root);
        return result;
    }

    DeviceCommandRequest request;
    request.setCid(transactionId);
    request.setCommand(command);
    request.setParams(params == nullptr
                          ? cJSON_CreateObject()
                          : cJSON_DetachItemFromObjectCaseSensitive(
                                requestRoot, "params"));
    DeviceCommandResponse response =
        DeviceCommandDispatcher::getInstance().processCommand(request);
    WebHidRpcResult result;
    result = serializeResponse(transactionId, response);
    if (result.json == nullptr) {
        if (result.error == 413) {
            return result;
        }
        return localError(
            transactionId, 500, "Failed to serialize RPC response");
    }
    return result;
}
