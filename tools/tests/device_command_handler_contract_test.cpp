#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "cJSON.h"
#include "contract_recording.hpp"
#include "device_security_protocol.h"
#include "storagemanager.hpp"
#include "webhid_rpc_dispatcher.hpp"
#include "configs/device_command_handler.hpp"
#include "configs/firmware_command_handler.hpp"
#include "adc_btns/adc_calibration.hpp"
#include "adc_btns/adc_manager.hpp"
#include "adc_btns/adc_btns_marker.hpp"
#include "configs/webconfig_btns_manager.hpp"
#include "configs/webconfig_leds_manager.hpp"
#include "firmware/firmware_manager.hpp"
#include "ch585_firmware_update.hpp"

namespace {

constexpr uint32_t kAllScopes = HBOX_SCOPE_CONFIG_READ |
                                HBOX_SCOPE_CONFIG_WRITE |
                                HBOX_SCOPE_MONITOR_READ |
                                HBOX_SCOPE_DEVICE_CONTROL |
                                HBOX_SCOPE_FIRMWARE_UPDATE;

struct DispatchResult {
    int error = -999;
    cJSON *root = nullptr;
};

void resetContractState()
{
    STORAGE_MANAGER.initConfig();
    ADC_CALIBRATION_MANAGER.resetForContractTest();
    ADC_MANAGER.resetForContractTest();
    ADC_BTNS_MARKER.resetForContractTest();
    WEBCONFIG_BTNS_MANAGER.resetForContractTest();
    WEBCONFIG_LEDS_MANAGER.resetForContractTest();
    FirmwareManager::GetInstance()->resetForContractTest();
    CH585_FIRMWARE_UPDATE.resetForContractTest();
    g_deviceCommandContractRecording = {};
    DeviceCommandHandler::needReboot = false;
    DeviceCommandHandler::rebootTick = 0u;
}

DispatchResult dispatch(const char *command, const cJSON *params, uint32_t scopes)
{
    static uint32_t transactionId = 100u;
    cJSON *request = cJSON_CreateObject();
    cJSON_AddNumberToObject(request, "transactionId", ++transactionId);
    cJSON_AddStringToObject(request, "command", command);
    cJSON_AddItemToObject(request, "params",
                          params == nullptr ? cJSON_CreateObject()
                                            : cJSON_Duplicate(params, true));
    const WebHidRpcResult raw = WEBHID_RPC_DISPATCHER.dispatch(request, scopes);
    cJSON_Delete(request);
    DispatchResult result;
    result.error = raw.error;
    if (raw.json != nullptr) result.root = cJSON_ParseWithLength(raw.json, raw.jsonLength);
    return result;
}

bool hasEnvelope(const DispatchResult &result)
{
    if (!cJSON_IsObject(result.root)) return false;
    const cJSON *tx = cJSON_GetObjectItemCaseSensitive(result.root, "transactionId");
    const cJSON *err = cJSON_GetObjectItemCaseSensitive(result.root, "errNo");
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(result.root, "data");
    return cJSON_IsNumber(tx) && tx->valuedouble >= 1.0 &&
           cJSON_IsNumber(err) && err->valueint == result.error && data != nullptr;
}

uint32_t recordingValue(const char *field)
{
    if (!field) return 0u;
#define RECORDING_FIELD(name) if (strcmp(field, #name) == 0) return g_deviceCommandContractRecording.name
    RECORDING_FIELD(storageSaves);
    RECORDING_FIELD(calibrationStarts);
    RECORDING_FIELD(calibrationStops);
    RECORDING_FIELD(calibrationResets);
    RECORDING_FIELD(monitorStarts);
    RECORDING_FIELD(monitorStops);
    RECORDING_FIELD(ledPreviews);
    RECORDING_FIELD(ledClears);
    RECORDING_FIELD(firmwareCreates);
    RECORDING_FIELD(firmwareChunks);
    RECORDING_FIELD(firmwareCompletes);
    RECORDING_FIELD(firmwareAborts);
    RECORDING_FIELD(ch585Begins);
    RECORDING_FIELD(ch585Writes);
    RECORDING_FIELD(ch585Completes);
#undef RECORDING_FIELD
    return 0u;
}

const cJSON *findCase(const cJSON *cases, const char *name)
{
    cJSON *entry = nullptr;
    cJSON_ArrayForEach(entry, cases) {
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(entry, "name");
        if (cJSON_IsString(item) && strcmp(item->valuestring, name) == 0) return entry;
    }
    return nullptr;
}

bool runSetup(const cJSON *entry, const cJSON *cases, std::string &failure)
{
    const cJSON *setup = cJSON_GetObjectItemCaseSensitive(entry, "setup");
    if (setup == nullptr) return true;
    if (!cJSON_IsArray(setup)) { failure = "setup is not an array"; return false; }
    cJSON *step = nullptr;
    cJSON_ArrayForEach(step, setup) {
        const cJSON *commandItem = cJSON_GetObjectItemCaseSensitive(step, "command");
        const cJSON *params = cJSON_GetObjectItemCaseSensitive(step, "params");
        if (!cJSON_IsString(commandItem)) {
            const cJSON *caseItem = cJSON_GetObjectItemCaseSensitive(step, "case");
            if (!cJSON_IsString(caseItem)) { failure = "setup has no command/case"; return false; }
            const cJSON *referenced = findCase(cases, caseItem->valuestring);
            if (!referenced) { failure = "setup references unknown case"; return false; }
            commandItem = cJSON_GetObjectItemCaseSensitive(referenced, "name");
            params = cJSON_GetObjectItemCaseSensitive(referenced, "validParams");
        }
        DispatchResult result = dispatch(commandItem->valuestring, params, kAllScopes);
        const bool ok = result.error == 0 && hasEnvelope(result);
        cJSON_Delete(result.root);
        if (!ok) { failure = std::string("setup failed: ") + commandItem->valuestring; return false; }
    }
    return true;
}

bool requireFields(const cJSON *data, const cJSON *fields, std::string &missing)
{
    cJSON *field = nullptr;
    cJSON_ArrayForEach(field, fields) {
        if (!cJSON_IsString(field) ||
            cJSON_GetObjectItemCaseSensitive(data, field->valuestring) == nullptr) {
            missing = cJSON_IsString(field) ? field->valuestring : "<invalid field entry>";
            return false;
        }
    }
    return true;
}

bool runValidCase(const cJSON *entry, const cJSON *cases, std::string &failure)
{
    resetContractState();
    if (!runSetup(entry, cases, failure)) return false;
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(entry, "validParams");
    const cJSON *expectedError = cJSON_GetObjectItemCaseSensitive(entry, "validErrNo");
    const cJSON *dataType = cJSON_GetObjectItemCaseSensitive(entry, "dataType");
    const cJSON *fields = cJSON_GetObjectItemCaseSensitive(entry, "requiredDataFields");
    if (!cJSON_IsString(name) || !cJSON_IsObject(params) ||
        !cJSON_IsNumber(expectedError) || !cJSON_IsString(dataType) ||
        !cJSON_IsArray(fields)) { failure = "case schema invalid"; return false; }

    DispatchResult result = dispatch(name->valuestring, params, kAllScopes);
    if (!hasEnvelope(result)) { failure = "response envelope invalid"; cJSON_Delete(result.root); return false; }
    if (result.error != expectedError->valueint) {
        failure = "valid errNo expected " + std::to_string(expectedError->valueint) +
                  " got " + std::to_string(result.error);
        cJSON_Delete(result.root); return false;
    }
    const cJSON *command = cJSON_GetObjectItemCaseSensitive(result.root, "command");
    if (result.error != 410 && (!cJSON_IsString(command) || strcmp(command->valuestring, name->valuestring) != 0)) {
        failure = "serialized command missing/mismatched"; cJSON_Delete(result.root); return false;
    }
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(result.root, "data");
    const bool expectedArray = strcmp(dataType->valuestring, "array") == 0;
    if ((expectedArray && !cJSON_IsArray(data)) || (!expectedArray && !cJSON_IsObject(data))) {
        failure = "data type mismatch"; cJSON_Delete(result.root); return false;
    }
    std::string missing;
    if (!requireFields(data, fields, missing)) {
        failure = "missing data field: " + missing; cJSON_Delete(result.root); return false;
    }
    const cJSON *recording = cJSON_GetObjectItemCaseSensitive(entry, "recordingField");
    if (recording != nullptr &&
        (!cJSON_IsString(recording) || recordingValue(recording->valuestring) != 1u)) {
        failure = "recording side-effect boundary count was not exactly one"; cJSON_Delete(result.root); return false;
    }
    cJSON_Delete(result.root);
    return true;
}

bool runInvalidCase(const cJSON *entry, std::string &failure)
{
    resetContractState();
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
    const cJSON *invalid = cJSON_GetObjectItemCaseSensitive(entry, "invalid");
    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(invalid, "kind");
    const cJSON *expected = cJSON_GetObjectItemCaseSensitive(invalid, "errNo");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(invalid, "params");
    if (!cJSON_IsString(name) || !cJSON_IsObject(invalid) ||
        !cJSON_IsString(kind) || !cJSON_IsNumber(expected)) {
        failure = "invalid-case schema invalid"; return false;
    }
    const uint32_t scopes = strcmp(kind->valuestring, "scope") == 0 ? 0u : kAllScopes;
    DispatchResult result = dispatch(name->valuestring, params, scopes);
    if (!hasEnvelope(result)) { failure = "invalid response envelope invalid"; cJSON_Delete(result.root); return false; }
    if (result.error != expected->valueint) {
        failure = "invalid errNo expected " + std::to_string(expected->valueint) +
                  " got " + std::to_string(result.error);
        cJSON_Delete(result.root); return false;
    }
    const cJSON *message = cJSON_GetObjectItemCaseSensitive(result.root, "errorMessage");
    if (result.error != 0 && (!cJSON_IsString(message) || message->valuestring[0] == '\0')) {
        failure = "errorMessage missing"; cJSON_Delete(result.root); return false;
    }
    cJSON_Delete(result.root);
    return true;
}

bool verifyRetiredCommandAlsoReachesItsRegisteredHandler(std::string &failure)
{
    DeviceCommandRequest request;
    request.setCid(9001u);
    request.setCommand("get_device_auth");
    request.setParams(cJSON_CreateObject());
    DeviceCommandResponse response =
        DeviceCommandDispatcher::getInstance().processCommand(request);
    const cJSON *message = cJSON_GetObjectItemCaseSensitive(
        response.getData(), "errorMessage");
    if (response.getErrNo() != 410 ||
        response.getCommand() != "get_device_auth" ||
        !cJSON_IsString(message) || message->valuestring[0] == '\0') {
        failure = "registered get_device_auth tombstone handler failed";
        return false;
    }
    return true;
}

bool verifyBinaryFirmwareChunkBorrowsCallerStorage(std::string &failure)
{
    resetContractState();
    FirmwareManager *manager = FirmwareManager::GetInstance();
    if (!manager->CreateUpgradeSession("binary-zero-copy", nullptr)) {
        failure = "could not create binary firmware contract session";
        return false;
    }

    constexpr size_t kPayloadSize = 8u * 1024u -
                                    sizeof(BinaryFirmwareChunkHeader);
    std::vector<uint8_t> message(
        sizeof(BinaryFirmwareChunkHeader) + kPayloadSize, 0u);
    auto *header = reinterpret_cast<BinaryFirmwareChunkHeader *>(message.data());
    header->command = BINARY_CMD_UPLOAD_FIRMWARE_CHUNK;
    const char session[] = "binary-zero-copy";
    const char component[] = "application";
    header->session_id_len = sizeof(session) - 1u;
    memcpy(header->session_id, session, sizeof(session) - 1u);
    header->component_name_len = sizeof(component) - 1u;
    memcpy(header->component_name, component, sizeof(component) - 1u);
    header->chunk_index = 0u;
    header->total_chunks = 1u;
    header->chunk_size = static_cast<uint32_t>(kPayloadSize);
    header->chunk_offset = 0u;
    header->target_address = SLOT_B_APPLICATION_ADDR;
    memset(message.data() + sizeof(BinaryFirmwareChunkHeader),
           0xA5,
           kPayloadSize);

    const uint8_t *const expectedPayload =
        message.data() + sizeof(BinaryFirmwareChunkHeader);
    if (!FirmwareCommandHandler::getInstance().handleBinaryFirmwareChunk(
            message.data(), message.size()) ||
        g_deviceCommandContractRecording.firmwareChunks != 1u ||
        g_deviceCommandContractRecording.firmwareChunkData != expectedPayload ||
        g_deviceCommandContractRecording.firmwareChunkSize != kPayloadSize) {
        failure = "binary firmware handler copied or changed caller storage";
        return false;
    }
    return true;
}

cJSON *makeHotkeyItem(int key,
                      bool includeKey,
                      int legacyVirtualPin,
                      bool includeLegacyVirtualPin)
{
    cJSON *item = cJSON_CreateObject();
    if (includeKey) cJSON_AddNumberToObject(item, "key", key);
    if (includeLegacyVirtualPin) {
        cJSON_AddNumberToObject(item, "virtualPin", legacyVirtualPin);
    }
    cJSON_AddStringToObject(item, "action", "SYSTEM_REBOOT");
    cJSON_AddBoolToObject(item, "isHold", true);
    cJSON_AddBoolToObject(item, "isLocked", false);
    return item;
}

bool responseHasCanonicalHotkey(const DispatchResult &result,
                                int expectedKey,
                                std::string &failure)
{
    if (result.error != 0 || !hasEnvelope(result)) {
        failure = "hotkey response envelope failed";
        return false;
    }
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(result.root, "data");
    const cJSON *hotkeys =
        cJSON_GetObjectItemCaseSensitive(data, "hotkeysConfig");
    const cJSON *first = cJSON_GetArrayItem(hotkeys, 0);
    const cJSON *key = cJSON_GetObjectItemCaseSensitive(first, "key");
    const cJSON *legacy =
        cJSON_GetObjectItemCaseSensitive(first, "virtualPin");
    if (!cJSON_IsNumber(key) || key->valueint != expectedKey) {
        failure = "hotkey response did not read back the canonical key";
        return false;
    }
    if (legacy != nullptr) {
        failure = "hotkey response leaked the legacy virtualPin field";
        return false;
    }
    return true;
}

bool verifyHotkeyKeyCompatibilityAndReadback(std::string &failure)
{
    resetContractState();

    cJSON *params = cJSON_CreateObject();
    cJSON *hotkeys = cJSON_CreateArray();
    cJSON_AddItemToArray(hotkeys, makeHotkeyItem(2, true, 7, true));
    cJSON_AddItemToObject(params, "hotkeysConfig", hotkeys);
    DispatchResult update = dispatch("update_hotkeys_config", params, kAllScopes);
    cJSON_Delete(params);
    if (STORAGE_MANAGER.config.hotkeys[0].virtualPin != 2 ||
        !responseHasCanonicalHotkey(update, 2, failure)) {
        if (failure.empty()) failure = "canonical key did not win over virtualPin";
        cJSON_Delete(update.root);
        return false;
    }
    cJSON_Delete(update.root);

    DispatchResult readback = dispatch("get_hotkeys_config", nullptr, kAllScopes);
    if (!responseHasCanonicalHotkey(readback, 2, failure)) {
        cJSON_Delete(readback.root);
        return false;
    }
    cJSON_Delete(readback.root);

    resetContractState();
    params = cJSON_CreateObject();
    hotkeys = cJSON_CreateArray();
    cJSON_AddItemToArray(hotkeys, makeHotkeyItem(0, false, 3, true));
    cJSON_AddItemToObject(params, "hotkeysConfig", hotkeys);
    DispatchResult legacyUpdate =
        dispatch("update_hotkeys_config", params, kAllScopes);
    cJSON_Delete(params);
    if (STORAGE_MANAGER.config.hotkeys[0].virtualPin != 3 ||
        !responseHasCanonicalHotkey(legacyUpdate, 3, failure)) {
        if (failure.empty()) failure = "legacy virtualPin input was not accepted";
        cJSON_Delete(legacyUpdate.root);
        return false;
    }
    cJSON_Delete(legacyUpdate.root);

    resetContractState();
    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "section", "hotkeys");
    hotkeys = cJSON_CreateArray();
    cJSON_AddItemToArray(hotkeys, makeHotkeyItem(4, true, 8, true));
    cJSON_AddItemToObject(params, "data", hotkeys);
    DispatchResult importPart =
        dispatch("import_config_part", params, kAllScopes);
    cJSON_Delete(params);
    const bool imported = importPart.error == 0 && hasEnvelope(importPart) &&
                          STORAGE_MANAGER.config.hotkeys[0].virtualPin == 4;
    cJSON_Delete(importPart.root);
    if (!imported) {
        failure = "hotkeys import did not prefer canonical key";
        return false;
    }

    readback = dispatch("get_hotkeys_config", nullptr, kAllScopes);
    const bool importedReadback =
        responseHasCanonicalHotkey(readback, 4, failure);
    cJSON_Delete(readback.root);
    return importedReadback;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "usage: device_command_handler_contract_test <cases.json>\n";
        return EXIT_FAILURE;
    }
    std::ifstream input(argv[1], std::ios::binary);
    std::ostringstream content;
    content << input.rdbuf();
    const std::string text = content.str();
    cJSON *document = cJSON_ParseWithLength(text.c_str(), text.size());
    const cJSON *cases = cJSON_GetObjectItemCaseSensitive(document, "commands");
    if (!cJSON_IsArray(cases)) {
        std::cerr << "case manifest is invalid\n";
        cJSON_Delete(document);
        return EXIT_FAILURE;
    }

    size_t passed = 0u;
    cJSON *entry = nullptr;
    cJSON_ArrayForEach(entry, cases) {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
        std::string failure;
        if (!runValidCase(entry, cases, failure) || !runInvalidCase(entry, failure)) {
            std::cerr << (cJSON_IsString(name) ? name->valuestring : "<unnamed>")
                      << ": " << failure << "\n";
            cJSON_Delete(document);
            return EXIT_FAILURE;
        }
        ++passed;
    }

    std::string retiredFailure;
    if (!verifyRetiredCommandAlsoReachesItsRegisteredHandler(retiredFailure)) {
        std::cerr << retiredFailure << "\n";
        cJSON_Delete(document);
        return EXIT_FAILURE;
    }

    std::string binaryFailure;
    if (!verifyBinaryFirmwareChunkBorrowsCallerStorage(binaryFailure)) {
        std::cerr << binaryFailure << "\n";
        cJSON_Delete(document);
        return EXIT_FAILURE;
    }

    std::string hotkeyFailure;
    if (!verifyHotkeyKeyCompatibilityAndReadback(hotkeyFailure)) {
        std::cerr << "hotkey key compatibility: " << hotkeyFailure << "\n";
        cJSON_Delete(document);
        return EXIT_FAILURE;
    }

    resetContractState();
    const cJSON *ping = cJSON_GetObjectItemCaseSensitive(document, "ping");
    DispatchResult pingResult = dispatch("ping", cJSON_GetObjectItemCaseSensitive(ping, "validParams"), kAllScopes);
    const cJSON *pingData = pingResult.root ? cJSON_GetObjectItemCaseSensitive(pingResult.root, "data") : nullptr;
    const cJSON *message = pingData ? cJSON_GetObjectItemCaseSensitive(pingData, "message") : nullptr;
    if (pingResult.error != 0 || !hasEnvelope(pingResult) || !cJSON_IsString(message) || strcmp(message->valuestring, "pong") != 0) {
        std::cerr << "ping: contract failed\n";
        cJSON_Delete(pingResult.root);
        cJSON_Delete(document);
        return EXIT_FAILURE;
    }
    cJSON_Delete(pingResult.root);
    cJSON_Delete(document);
    std::cout << "real handler contracts passed: " << passed
              << "/59; binary zero-copy, retired tombstone handler and ping passed separately\n";
    return passed == 59u ? EXIT_SUCCESS : EXIT_FAILURE;
}
