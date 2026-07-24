#include "webhid_rpc_dispatcher.hpp"

#include <cstring>

#include "configs/websocket_command_handler.hpp"
#include "configs/websocket_message.hpp"
#include "device_security_protocol.h"

namespace {

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

std::string serializeResponse(uint32_t transactionId,
                              WebSocketDownstreamMessage &response)
{
    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) {
        return {};
    }
    cJSON_AddNumberToObject(root, "transactionId", transactionId);
    cJSON_AddNumberToObject(root, "errNo", response.getErrNo());
    cJSON_AddStringToObject(root,
                            "command",
                            response.getCommand().c_str());
    if (response.getData() != nullptr) {
        cJSON_AddItemToObject(
            root, "data", cJSON_Duplicate(response.getData(), 1));
        cJSON *message =
            cJSON_GetObjectItem(response.getData(), "errorMessage");
        if (message != nullptr && cJSON_IsString(message)) {
            cJSON_AddStringToObject(
                root, "errorMessage", message->valuestring);
        }
    } else {
        cJSON_AddItemToObject(root, "data", cJSON_CreateObject());
    }

    char *encoded = cJSON_PrintUnformatted(root);
    std::string result = encoded == nullptr ? "" : encoded;
    if (encoded != nullptr) {
        cJSON_free(encoded);
    }
    cJSON_Delete(root);
    return result;
}

WebHidRpcResult localError(uint32_t transactionId,
                           int error,
                           const char *message)
{
    WebHidRpcResult result;
    cJSON *root = cJSON_CreateObject();
    result.transactionId = transactionId;
    result.error = error;
    if (root == nullptr) {
        return result;
    }
    cJSON_AddNumberToObject(root, "transactionId", transactionId);
    cJSON_AddNumberToObject(root, "errNo", error);
    cJSON_AddStringToObject(root, "errorMessage", message);
    cJSON_AddItemToObject(root, "data", cJSON_CreateObject());
    char *encoded = cJSON_PrintUnformatted(root);
    if (encoded != nullptr) {
        result.json = encoded;
        cJSON_free(encoded);
    }
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
        WebSocketCommandManager::getInstance().initializeHandlers();
        initialized = true;
    }
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
        WebHidRpcResult result;
        result.transactionId = transactionId;
        result.error = 0;
        cJSON *root = cJSON_CreateObject();
        cJSON *data = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "transactionId", transactionId);
        cJSON_AddNumberToObject(root, "errNo", 0);
        cJSON_AddStringToObject(root, "command", "ping");
        cJSON_AddStringToObject(data, "message", "pong");
        cJSON_AddItemToObject(root, "data", data);
        char *encoded = cJSON_PrintUnformatted(root);
        if (encoded != nullptr) {
            result.json = encoded;
            cJSON_free(encoded);
        }
        cJSON_Delete(root);
        return result;
    }

    WebSocketUpstreamMessage request;
    request.setCid(transactionId);
    request.setCommand(command);
    request.setConnection(nullptr);
    request.setParams(params == nullptr
                          ? cJSON_CreateObject()
                          : cJSON_Duplicate(params, 1));
    WebSocketDownstreamMessage response =
        WebSocketCommandManager::getInstance().processCommand(request);
    WebHidRpcResult result;
    result.transactionId = transactionId;
    result.error = response.getErrNo();
    result.json = serializeResponse(transactionId, response);
    if (result.json.empty()) {
        return localError(
            transactionId, 500, "Failed to serialize RPC response");
    }
    return result;
}
