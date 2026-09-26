#include "configs/config_sync.hpp"
#include "configs/device_command_handler.hpp"
#include "storagemanager.hpp"
#include "board_cfg.h"
#include "firmware_metadata.h"
#include "sha256_simple.h"
#include <cstring>

bool configResourceDigest(const cJSON* value, char output[65]) {
    if (!value) return false;
    char* json = cJSON_PrintUnformatted(value);
    if (!json) return false;
    const bool ok = sha256_calculate(reinterpret_cast<const uint8_t*>(json), strlen(json), output) == 1;
    cJSON_free(json);
    return ok;
}

namespace {
bool addVersion(cJSON* versions, const char* key, const cJSON* value) {
    if (!value) return true;
    char digest[65] = {};
    return configResourceDigest(value, digest) && cJSON_AddStringToObject(versions, key, digest);
}
bool addProfileVersion(cJSON* versions, const cJSON* profile) {
    if (!profile) return true;
    const cJSON* id = cJSON_GetObjectItemCaseSensitive(profile, "id");
    if (!cJSON_IsString(id) || !id->valuestring) return false;
    return addVersion(versions, (std::string("profile:") + id->valuestring).c_str(), profile);
}
}

bool addConfigResponseVersions(const DeviceCommandRequest& request, cJSON* data) {
    if (!cJSON_IsObject(data)) return true;
    // Only these replies use the complete GET resource representation. Export,
    // telemetry and preview replies must not become configuration cache entries.
    const std::string& command = request.getCommand();
    if (command != "get_global_config" && command != "update_global_config" &&
        command != "get_screen_control_config" && command != "update_screen_control_config" &&
        command != "get_hotkeys_config" && command != "update_hotkeys_config" &&
        command != "get_profile_list" && command != "get_default_profile" &&
        command != "get_profile_details" && command != "update_profile" &&
        command != "get_profile_macros" && command != "update_profile_macros" &&
        command != "switch_default_profile") return true;
    cJSON* versions = cJSON_CreateObject();
    if (!versions) return false;
    bool ok = addVersion(versions, "global", cJSON_GetObjectItemCaseSensitive(data, "globalConfig")) &&
        addVersion(versions, "screen-control", cJSON_GetObjectItemCaseSensitive(data, "screenControl")) &&
        addVersion(versions, "hotkeys", cJSON_GetObjectItemCaseSensitive(data, "hotkeysConfig")) &&
        addVersion(versions, "profile-list", cJSON_GetObjectItemCaseSensitive(data, "profileList")) &&
        addProfileVersion(versions, cJSON_GetObjectItemCaseSensitive(data, "profileDetails"));
    const cJSON* fallback = cJSON_GetObjectItemCaseSensitive(data, "defaultProfileDetails");
    // Some legacy replies contain both names for the same profile.
    if (!cJSON_GetObjectItemCaseSensitive(data, "profileDetails")) ok = ok && addProfileVersion(versions, fallback);
    const cJSON* macros = cJSON_GetObjectItemCaseSensitive(data, "m");
    if (macros) {
        const cJSON* pid = cJSON_GetObjectItemCaseSensitive(request.getParams(), "pid");
        ok = ok && cJSON_IsString(pid) && pid->valuestring &&
            addVersion(versions, (std::string("macros:") + pid->valuestring).c_str(), macros);
    }
    if (ok && versions->child) {
        ok = cJSON_AddItemToObject(data, "configVersions", versions);
        if (ok) return true;
    }
    cJSON_Delete(versions);
    return ok;
}

DeviceCommandResponse getConfigManifest(const DeviceCommandRequest& request) {
    cJSON* data = cJSON_CreateObject();
    cJSON* modules = cJSON_CreateObject();
    if (!data || !modules || !cJSON_AddItemToObject(data, "modules", modules)) {
        cJSON_Delete(data); cJSON_Delete(modules);
        return create_device_command_response(request.getCid(), request.getCommand(), 1, nullptr, "Configuration manifest allocation failed");
    }
    // Ordinary silicon UID, unrelated to Option Bytes or provisioned identity.
    uint8_t identity[sizeof("XORA/config-cache/v1") - 1 + 12] = {};
    constexpr size_t prefixSize = sizeof("XORA/config-cache/v1") - 1;
    memcpy(identity, "XORA/config-cache/v1", prefixSize);
    for (size_t word = 0; word < 3; ++word) {
        const uint32_t value = word == 0 ? HAL_GetUIDw0() : word == 1 ? HAL_GetUIDw1() : HAL_GetUIDw2();
        for (size_t byte = 0; byte < 4; ++byte) identity[prefixSize + word * 4 + byte] = static_cast<uint8_t>(value >> (byte * 8));
    }
    char deviceKey[65] = {};
    bool ok = sha256_calculate(identity, sizeof(identity), deviceKey) == 1 &&
        cJSON_AddStringToObject(data, "deviceCacheKey", deviceKey) &&
        cJSON_AddStringToObject(data, "hardwareVersion", HARDWARE_VERSION_STRING) &&
        cJSON_AddNumberToObject(data, "schemaVersion", 1);
    auto read = [&](DeviceCommandHandler& handler, const char* command, const char* key, const char* field, const char* id) {
        if (!ok) return;
        DeviceCommandRequest subrequest;
        subrequest.setCommand(command);
        cJSON* params = cJSON_CreateObject();
        if (!params) { ok = false; return; }
        if ((id && !cJSON_AddStringToObject(params, strcmp(field, "m") == 0 ? "pid" : "profileId", id)) ||
            !cJSON_AddBoolToObject(params, "listOnly", true)) {
            cJSON_Delete(params); ok = false; return;
        }
        subrequest.setParams(params);
        // Call the existing GET directly: no nested RPC dispatch or transport.
        auto response = handler.handle(subrequest);
        const cJSON* body = cJSON_GetObjectItemCaseSensitive(response.getData(), field);
        ok = response.getErrNo() == 0 && body && addVersion(modules, key, body);
    };
    auto& global = GlobalConfigCommandHandler::getInstance();
    auto& profiles = ProfileCommandHandler::getInstance();
    read(global, "get_global_config", "global", "globalConfig", nullptr);
    read(global, "get_screen_control_config", "screen-control", "screenControl", nullptr);
    read(global, "get_hotkeys_config", "hotkeys", "hotkeysConfig", nullptr);
    read(profiles, "get_profile_list", "profile-list", "profileList", nullptr);
    for (const auto& profile : Storage::getInstance().config.profiles) {
        read(profiles, "get_profile_details", (std::string("profile:") + profile.id).c_str(), "profileDetails", profile.id);
        read(profiles, "get_profile_macros", (std::string("macros:") + profile.id).c_str(), "m", profile.id);
    }
    if (!ok) {
        cJSON_Delete(data);
        return create_device_command_response(request.getCid(), request.getCommand(), 1, nullptr, "Configuration manifest failed");
    }
    return create_device_command_response(request.getCid(), request.getCommand(), 0, data);
}
