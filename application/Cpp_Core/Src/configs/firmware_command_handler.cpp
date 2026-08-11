#include "configs/firmware_command_handler.hpp"
#include "websocket_message.hpp"
#include "firmware/firmware_manager.hpp"
#include "utils.h"
#include "main.h"
#include "system_logger.h"
#include <string>
#include "storagemanager.hpp"
#include "qspi-w25q64.h"
#include "config_transport_sink.hpp"
#include "ch585_firmware_update.hpp"

namespace {

constexpr const char* kStm32OtaComponentNames[FIRMWARE_COMPONENT_COUNT] = {
    "application",
    "webresources",
    "adc_mapping",
};

bool isValidStm32OtaComponentSet(const cJSON* components) {
    if (!components || !cJSON_IsArray(components) ||
        cJSON_GetArraySize(components) != FIRMWARE_COMPONENT_COUNT) {
        return false;
    }

    bool found[FIRMWARE_COMPONENT_COUNT] = {false};
    for (int i = 0; i < FIRMWARE_COMPONENT_COUNT; ++i) {
        const cJSON* component = cJSON_GetArrayItem(components, i);
        const cJSON* name = component ? cJSON_GetObjectItem(component, "name") : nullptr;
        if (!component || !cJSON_IsObject(component) || !name || !cJSON_IsString(name)) {
            return false;
        }

        const char* componentName = cJSON_GetStringValue(name);
        bool matched = false;
        for (int allowedIndex = 0; allowedIndex < FIRMWARE_COMPONENT_COUNT; ++allowedIndex) {
            if (strcmp(componentName, kStm32OtaComponentNames[allowedIndex]) == 0) {
                if (found[allowedIndex]) {
                    return false;
                }
                found[allowedIndex] = true;
                matched = true;
                break;
            }
        }

        if (!matched) {
            return false;
        }
    }

    for (bool componentFound : found) {
        if (!componentFound) {
            return false;
        }
    }
    return true;
}

int decodeHexDigit(char digit) {
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    return -1;
}

bool decodeHexField(const cJSON* item, uint8_t* output, size_t outputLength) {
    if (!item || !cJSON_IsString(item) || !output) return false;
    const char* encoded = cJSON_GetStringValue(item);
    if (!encoded || strlen(encoded) != outputLength * 2) return false;
    for (size_t i = 0; i < outputLength; ++i) {
        const int high = decodeHexDigit(encoded[i * 2]);
        const int low = decodeHexDigit(encoded[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        output[i] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

bool parseUint32(const cJSON* item, uint32_t* output) {
    if (!item || !output) return false;
    if (cJSON_IsNumber(item)) {
        const double value = cJSON_GetNumberValue(item);
        if (value < 0 || value > 4294967295.0) return false;
        *output = static_cast<uint32_t>(value);
        return true;
    }
    if (cJSON_IsString(item)) {
        const char* text = cJSON_GetStringValue(item);
        if (!text || *text == '\0') return false;
        char* end = nullptr;
        const unsigned long value = strtoul(text, &end, 0);
        if (end == text || *end != '\0' || value > 0xFFFFFFFFul) return false;
        *output = static_cast<uint32_t>(value);
        return true;
    }
    return false;
}

} // namespace

/**
 * @brief 处理WebSocket命令的统一入口
 * @param request WebSocket上行消息
 * @return WebSocketDownstreamMessage WebSocket下行消息
 */
WebSocketDownstreamMessage FirmwareCommandHandler::handle(const WebSocketUpstreamMessage& request) {
    const std::string& command = request.getCommand();
    
    if (command == "get_device_auth") {
        return handleGetDeviceAuth(request);
    } else if (command == "get_firmware_metadata") {
        return handleGetFirmwareMetadata(request);
    } else if (command == "create_firmware_upgrade_session") {
        return handleCreateFirmwareUpgradeSession(request);
    } else if (command == "upload_firmware_chunk") {
        return handleUploadFirmwareChunk(request);
    } else if (command == "complete_firmware_upgrade_session") {
        return handleCompleteFirmwareUpgradeSession(request);
    } else if (command == "abort_firmware_upgrade_session") {
        return handleAbortFirmwareUpgradeSession(request);
    } else if (command == "get_firmware_upgrade_status") {
        return handleGetFirmwareUpgradeStatus(request);
    } else if (command == "cleanup_firmware_upgrade_session") {
        return handleCleanupFirmwareUpgradeSession(request);
    } else if (command == "ch585_update_status") {
        return handleCh585UpdateStatus(request);
    } else if (command == "ch585_update_begin") {
        return handleCh585UpdateBegin(request);
    } else if (command == "ch585_update_chunk") {
        return handleCh585UpdateChunk(request);
    } else if (command == "ch585_update_complete") {
        return handleCh585UpdateComplete(request);
    }
    
    // 未知命令
    LOG_WARN("WebSocket", "FirmwareCommandHandler: Unknown command: %s", command.c_str());
    return create_error_response(request.getCid(), command, -1, "Unknown firmware command");
}

/**
 * @brief 获取设备认证信息
 * 
 * WebSocket命令格式:
 * {
 *   "cid": 1,
 *   "command": "get_device_auth",
 *   "params": {}
 * }
 * 
 * 响应格式:
 * {
 *   "cid": 1,
 *   "command": "get_device_auth",
 *   "errNo": 0,
 *   "data": {
 *     "deviceId": "DEVICE_xxxx",
 *     "originalUniqueId": "0123456789ABCDEF",
 *     "challenge": "DEV_12345678_87654321",
 *     "timestamp": 1234567890,
 *     "signature": "SIG_12345678",
 *     "expiresIn": 1800
 *   }
 * }
 */
WebSocketDownstreamMessage FirmwareCommandHandler::handleGetDeviceAuth(const WebSocketUpstreamMessage& request) {
#if defined(HBOX_SECURE_BOOT_REQUIRED) && HBOX_SECURE_BOOT_REQUIRED
    /*
     * V2 identity is proven only by the manufacturer certificate, boot
     * attestation and one-shot server permit.  Never expose the STM32 UID or
     * the historical public 32-bit hash from a secure build.
     */
    return create_error_response(
        request.getCid(),
        request.getCommand(),
        410,
        "Legacy weak device authentication is disabled");
#else
    // LOG_INFO("WebSocket", "Handling get_device_auth command, cid: %d", request.getCid());
    
    // 创建设备认证数据
    cJSON* dataJSON = createDeviceAuthJSON();
    if (!dataJSON) {
        LOG_ERROR("WebSocket", "get_device_auth: Failed to create device auth data");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to get device identifiers");
    }
    
    // LOG_INFO("WebSocket", "get_device_auth command completed successfully");
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
#endif
}

WebSocketDownstreamMessage FirmwareCommandHandler::handleCh585UpdateStatus(
    const WebSocketUpstreamMessage& request)
{
    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "status",
        static_cast<int>(CH585_FIRMWARE_UPDATE.status()));
    cJSON_AddNumberToObject(data, "progress", CH585_FIRMWARE_UPDATE.progress());
    cJSON_AddNumberToObject(data, "total_size", CH585_FIRMWARE_UPDATE.totalSize());
    cJSON_AddNumberToObject(data, "received_size", CH585_FIRMWARE_UPDATE.receivedSize());
    cJSON_AddBoolToObject(data, "pending", CH585_FIRMWARE_UPDATE.isPending());
    cJSON_AddBoolToObject(data, "failed", CH585_FIRMWARE_UPDATE.hasFailed());
    return create_success_response(request.getCid(), request.getCommand(), data);
}

WebSocketDownstreamMessage FirmwareCommandHandler::handleCh585UpdateBegin(
    const WebSocketUpstreamMessage& request)
{
    cJSON* params = request.getParams();
    uint32_t totalSize = 0u;
    uint8_t expectedSha[32];
    if (!params ||
        !parseUint32(cJSON_GetObjectItem(params, "total_size"), &totalSize) ||
        !decodeHexField(cJSON_GetObjectItem(params, "sha256"),
                        expectedSha,
                        sizeof(expectedSha))) {
        return create_error_response(request.getCid(), request.getCommand(),
                                     1, "Invalid CH585 image size or SHA-256");
    }
    if (!CH585_FIRMWARE_UPDATE.begin(totalSize, expectedSha)) {
        return create_error_response(request.getCid(), request.getCommand(),
                                     2, "Failed to initialize CH585 staging area");
    }
    cJSON* data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", true);
    cJSON_AddNumberToObject(data, "max_chunk_size", 1024);
    return create_success_response(request.getCid(), request.getCommand(), data);
}

WebSocketDownstreamMessage FirmwareCommandHandler::handleCh585UpdateChunk(
    const WebSocketUpstreamMessage& request)
{
    cJSON* params = request.getParams();
    uint32_t offset = 0u;
    cJSON* encodedItem = params ? cJSON_GetObjectItem(params, "data") : nullptr;
    if (!params ||
        !parseUint32(cJSON_GetObjectItem(params, "offset"), &offset) ||
        !encodedItem || !cJSON_IsString(encodedItem)) {
        return create_error_response(request.getCid(), request.getCommand(),
                                     1, "Invalid CH585 chunk parameters");
    }
    size_t decodedLength = 0u;
    uint8_t* decoded = base64_decode_websocket(
        cJSON_GetStringValue(encodedItem), &decodedLength);
    if (!decoded || decodedLength == 0u || decodedLength > 1024u) {
        free(decoded);
        return create_error_response(request.getCid(), request.getCommand(),
                                     1, "Invalid CH585 chunk data");
    }
    const bool success = CH585_FIRMWARE_UPDATE.write(
        offset, decoded, static_cast<uint32_t>(decodedLength));
    free(decoded);
    if (!success) {
        return create_error_response(request.getCid(), request.getCommand(),
                                     2, "CH585 chunk write failed or is out of sequence");
    }
    cJSON* data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", true);
    cJSON_AddNumberToObject(data, "received_size",
                            CH585_FIRMWARE_UPDATE.receivedSize());
    cJSON_AddNumberToObject(data, "progress", CH585_FIRMWARE_UPDATE.progress());
    return create_success_response(request.getCid(), request.getCommand(), data);
}

WebSocketDownstreamMessage FirmwareCommandHandler::handleCh585UpdateComplete(
    const WebSocketUpstreamMessage& request)
{
    if (!CH585_FIRMWARE_UPDATE.finalizeAndSchedule()) {
        return create_error_response(request.getCid(), request.getCommand(),
                                     2, "CH585 staged firmware verification failed");
    }
    cJSON* data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", true);
    cJSON_AddBoolToObject(data, "reboot_scheduled", true);
    cJSON_AddStringToObject(data, "message",
                            "Device will reboot and update CH585 locally");
    return create_success_response(request.getCid(), request.getCommand(), data);
}

/**
 * @brief 获取固件元数据信息
 * 
 * WebSocket命令格式:
 * {
 *   "cid": 1,
 *   "command": "get_firmware_metadata",
 *   "params": {}
 * }
 * 
 * 响应格式:
 * {
 *   "cid": 1,
 *   "command": "get_firmware_metadata",
 *   "errNo": 0,
 *   "data": {
 *     "currentSlot": "A",
 *     "targetSlot": "B",
 *     "version": "1.0.0",
 *     "buildDate": "2024-01-01",
 *     "components": [
 *       {
 *         "name": "bootloader",
 *         "file": "bootloader.bin",
 *         "address": 134217728,
 *         "size": 32768,
 *         "sha256": "abc123...",
 *         "active": true
 *       }
 *     ]
 *   }
 * }
 */
WebSocketDownstreamMessage FirmwareCommandHandler::handleGetFirmwareMetadata(const WebSocketUpstreamMessage& request) {
    // LOG_INFO("WebSocket", "Handling get_firmware_metadata command, cid: %d", request.getCid());
    
    // 创建固件元数据
    cJSON* dataJSON = createFirmwareMetadataJSON();
    if (!dataJSON) {
        LOG_ERROR("WebSocket", "get_firmware_metadata: Failed to create firmware metadata");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to get firmware metadata");
    }
    
    // LOG_INFO("WebSocket", "get_firmware_metadata command completed successfully");
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

/**
 * @brief 创建固件升级会话
 */
WebSocketDownstreamMessage FirmwareCommandHandler::handleCreateFirmwareUpgradeSession(const WebSocketUpstreamMessage& request) {
    // LOG_INFO("WebSocket", "Handling create_firmware_upgrade_session command, cid: %d", request.getCid());
    
    FirmwareManager* manager = FirmwareManager::GetInstance();
    if (!manager) {
        LOG_ERROR("WebSocket", "create_firmware_upgrade_session: Failed to get firmware manager instance");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to get firmware manager instance");
    }

    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("WebSocket", "create_firmware_upgrade_session: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    cJSON* sessionIdItem = cJSON_GetObjectItem(params, "session_id");
    cJSON* manifestItem = cJSON_GetObjectItem(params, "manifest");
    
    if (!sessionIdItem || !cJSON_IsString(sessionIdItem) ||
        !manifestItem) {
        LOG_ERROR("WebSocket", "create_firmware_upgrade_session: Missing required parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing required parameters");
    }

    const char* sessionId = cJSON_GetStringValue(sessionIdItem);

    const cJSON* hardwareVersionItem =
        cJSON_GetObjectItem(manifestItem, "hardware_version");
    const cJSON* hardwareVersionCodeItem =
        cJSON_GetObjectItem(manifestItem, "hardware_version_code");
    if (!hardwareVersionItem || !cJSON_IsString(hardwareVersionItem) ||
        strcmp(cJSON_GetStringValue(hardwareVersionItem),
               HARDWARE_VERSION_STRING) != 0 ||
        !hardwareVersionCodeItem || !cJSON_IsNumber(hardwareVersionCodeItem) ||
        cJSON_GetNumberValue(hardwareVersionCodeItem) !=
            static_cast<double>(HARDWARE_VERSION)) {
        LOG_ERROR(
            "WebSocket",
            "create_firmware_upgrade_session: Hardware version mismatch");
        return create_error_response(
            request.getCid(),
            request.getCommand(),
            1,
            "Firmware package hardware version does not match this V2 board");
    }

    cJSON* componentsItem = cJSON_GetObjectItem(manifestItem, "components");
    if (!isValidStm32OtaComponentSet(componentsItem)) {
        LOG_ERROR("WebSocket", "create_firmware_upgrade_session: Invalid STM32 OTA component set");
        return create_error_response(
            request.getCid(),
            request.getCommand(),
            1,
            "STM32 OTA requires exactly application, webresources and adc_mapping");
    }

    // 解析manifest到FirmwareMetadata结构
    FirmwareMetadata manifest = {0};
    manifest.magic = FIRMWARE_MAGIC;
    manifest.metadata_version_major = METADATA_VERSION_MAJOR;
    manifest.metadata_version_minor = METADATA_VERSION_MINOR;
    manifest.metadata_size = METADATA_STRUCT_SIZE;
    strncpy(manifest.device_model,
            DEVICE_MODEL_STRING,
            sizeof(manifest.device_model) - 1);
    manifest.hardware_version = HARDWARE_VERSION;
    manifest.bootloader_min_version = BOOTLOADER_VERSION;
    
    // 解析版本
    cJSON* versionItem = cJSON_GetObjectItem(manifestItem, "version");
    if (versionItem && cJSON_IsString(versionItem)) {
        strncpy(manifest.firmware_version, cJSON_GetStringValue(versionItem), sizeof(manifest.firmware_version) - 1);
    }
    
    // 解析槽位
    cJSON* slotItem = cJSON_GetObjectItem(manifestItem, "slot");
    if (slotItem && cJSON_IsString(slotItem)) {
        const char* slotStr = cJSON_GetStringValue(slotItem);
        if (strcmp(slotStr, "A") == 0) {
            manifest.target_slot = FIRMWARE_SLOT_A;
        } else if (strcmp(slotStr, "B") == 0) {
            manifest.target_slot = FIRMWARE_SLOT_B;
        } else {
            manifest.target_slot = FIRMWARE_SLOT_A; // 默认槽位
        }
    }
    
    // 解析构建日期
    cJSON* buildDateItem = cJSON_GetObjectItem(manifestItem, "build_date");
    if (buildDateItem && cJSON_IsString(buildDateItem)) {
        strncpy(manifest.build_date, cJSON_GetStringValue(buildDateItem), sizeof(manifest.build_date) - 1);
    }

    uint32_t signatureAlgorithm = 0;
    uint32_t securityVersion = 0;
    const cJSON* buildTimestampItem =
        cJSON_GetObjectItem(manifestItem, "build_timestamp");
    const cJSON* signatureAlgorithmItem =
        cJSON_GetObjectItem(manifestItem, "signature_algorithm");
    const cJSON* securityVersionItem =
        cJSON_GetObjectItem(manifestItem, "security_version");
    const cJSON* webresourcesOptionalItem =
        cJSON_GetObjectItem(manifestItem, "webresources_optional");
    if (!parseUint32(buildTimestampItem, &manifest.build_timestamp) ||
        !parseUint32(signatureAlgorithmItem, &signatureAlgorithm) ||
        !parseUint32(securityVersionItem, &securityVersion) ||
        signatureAlgorithm != FIRMWARE_SIGNATURE_ECDSA_P256_SHA256 ||
        securityVersion < FIRMWARE_SECURITY_VERSION ||
        !webresourcesOptionalItem ||
        !cJSON_IsBool(webresourcesOptionalItem) ||
        !decodeHexField(cJSON_GetObjectItem(manifestItem, "firmware_hash"),
                        manifest.firmware_hash,
                        sizeof(manifest.firmware_hash)) ||
        !decodeHexField(cJSON_GetObjectItem(manifestItem, "signature"),
                        manifest.signature,
                        sizeof(manifest.signature))) {
        return create_error_response(
            request.getCid(),
            request.getCommand(),
            1,
            "Signed firmware manifest fields are missing or invalid");
    }
    manifest.signature_algorithm = signatureAlgorithm;
    manifest.security_version = securityVersion;
    manifest.webresources_optional =
        cJSON_IsTrue(webresourcesOptionalItem) ? 1 : 0;
    
    // 解析组件
    if (componentsItem && cJSON_IsArray(componentsItem)) {
        manifest.component_count = FIRMWARE_COMPONENT_COUNT;
        
        for (int i = 0; i < manifest.component_count; i++) {
            cJSON* compItem = cJSON_GetArrayItem(componentsItem, i);
            if (compItem) {
                FirmwareComponent* comp = &manifest.components[i];
                comp->active = true;
                
                cJSON* nameItem = cJSON_GetObjectItem(compItem, "name");
                if (nameItem && cJSON_IsString(nameItem)) {
                    strncpy(comp->name, cJSON_GetStringValue(nameItem), sizeof(comp->name) - 1);
                }
                
                cJSON* fileItem = cJSON_GetObjectItem(compItem, "file");
                if (fileItem && cJSON_IsString(fileItem)) {
                    strncpy(comp->file, cJSON_GetStringValue(fileItem), sizeof(comp->file) - 1);
                }
                
                cJSON* addressItem = cJSON_GetObjectItem(compItem, "address");
                if (!parseUint32(addressItem, &comp->address)) {
                    return create_error_response(
                        request.getCid(), request.getCommand(), 1,
                        "Invalid component address");
                }
                
                cJSON* sizeItem = cJSON_GetObjectItem(compItem, "size");
                if (sizeItem && cJSON_IsNumber(sizeItem)) {
                    comp->size = (uint32_t)cJSON_GetNumberValue(sizeItem);
                }
                
                cJSON* sha256Item = cJSON_GetObjectItem(compItem, "sha256");
                if (sha256Item && cJSON_IsString(sha256Item)) {
                    strncpy(comp->sha256, cJSON_GetStringValue(sha256Item), sizeof(comp->sha256) - 1);
                }
                
                cJSON* activeItem = cJSON_GetObjectItem(compItem, "active");
                if (activeItem && cJSON_IsBool(activeItem)) {
                    comp->active = cJSON_IsTrue(activeItem);
                }
            }
        }
    }
    
    APP_DBG("Begin CreateUpgradeSession: %s", sessionId);

    bool success = manager->CreateUpgradeSession(sessionId, &manifest);
    
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddBoolToObject(dataJSON, "success", success);
    cJSON_AddStringToObject(dataJSON, "session_id", sessionId);
    
    if (!success) {
        cJSON_AddStringToObject(dataJSON, "error", "Failed to create upgrade session. This may be due to an existing active session. Please try again or abort any existing sessions.");
        LOG_ERROR("WebSocket", "create_firmware_upgrade_session: CreateUpgradeSession failed for session %s", sessionId);
    }

    // LOG_INFO("WebSocket", "create_firmware_upgrade_session command completed");
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

/**
 * @brief 上传固件分片（JSON版本，保留兼容性）
 */
WebSocketDownstreamMessage FirmwareCommandHandler::handleUploadFirmwareChunk(const WebSocketUpstreamMessage& request) {
    // LOG_INFO("WebSocket", "Handling upload_firmware_chunk command, cid: %d", request.getCid());
    
    FirmwareManager* manager = FirmwareManager::GetInstance();
    if (!manager) {
        LOG_ERROR("WebSocket", "upload_firmware_chunk: Failed to get firmware manager instance");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to get firmware manager instance");
    }

    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("WebSocket", "upload_firmware_chunk: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    // 解析必要字段
    cJSON* sessionIdItem = cJSON_GetObjectItem(params, "session_id");
    cJSON* componentNameItem = cJSON_GetObjectItem(params, "component_name");
    cJSON* chunkIndexItem = cJSON_GetObjectItem(params, "chunk_index");
    cJSON* totalChunksItem = cJSON_GetObjectItem(params, "total_chunks");
    cJSON* chunkSizeItem = cJSON_GetObjectItem(params, "chunk_size");
    cJSON* chunkOffsetItem = cJSON_GetObjectItem(params, "chunk_offset");
    cJSON* targetAddressItem = cJSON_GetObjectItem(params, "target_address");
    cJSON* checksumItem = cJSON_GetObjectItem(params, "checksum");
    cJSON* dataItem = cJSON_GetObjectItem(params, "data");

    // LOG_INFO("WebSocket", "upload_firmware_chunk: Validating parameters...");
    
    if (!sessionIdItem || !cJSON_IsString(sessionIdItem) ||
        !componentNameItem || !cJSON_IsString(componentNameItem) ||
        !chunkIndexItem || !cJSON_IsNumber(chunkIndexItem) ||
        !totalChunksItem || !cJSON_IsNumber(totalChunksItem) ||
        !chunkSizeItem || !cJSON_IsNumber(chunkSizeItem) ||
        !chunkOffsetItem || !cJSON_IsNumber(chunkOffsetItem) ||
        !checksumItem || !cJSON_IsString(checksumItem) ||
        !dataItem || !cJSON_IsString(dataItem)) {
        
        LOG_ERROR("WebSocket", "upload_firmware_chunk: Missing or invalid parameters");
        LOG_ERROR("WebSocket", "sessionId: %s, componentName: %s, chunkIndex: %s, totalChunks: %s",
                  sessionIdItem ? "OK" : "NULL", componentNameItem ? "OK" : "NULL",
                  chunkIndexItem ? "OK" : "NULL", totalChunksItem ? "OK" : "NULL");
        LOG_ERROR("WebSocket", "chunkSize: %s, chunkOffset: %s, checksum: %s, data: %s",
                  chunkSizeItem ? "OK" : "NULL", chunkOffsetItem ? "OK" : "NULL",
                  checksumItem ? "OK" : "NULL", dataItem ? "OK" : "NULL");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing or invalid parameters");
    }

    // LOG_INFO("WebSocket", "upload_firmware_chunk: Parameters validated successfully");

    // 构建ChunkData结构
    ChunkData chunk = {0};
    chunk.chunk_index = (uint32_t)cJSON_GetNumberValue(chunkIndexItem);
    chunk.total_chunks = (uint32_t)cJSON_GetNumberValue(totalChunksItem);
    chunk.chunk_size = (uint32_t)cJSON_GetNumberValue(chunkSizeItem);
    chunk.chunk_offset = (uint32_t)cJSON_GetNumberValue(chunkOffsetItem);
    strncpy(chunk.checksum, cJSON_GetStringValue(checksumItem), sizeof(chunk.checksum) - 1);

    // LOG_INFO("WebSocket", "upload_firmware_chunk: chunk_index=%u, total_chunks=%u, chunk_size=%u",
    //          chunk.chunk_index, chunk.total_chunks, chunk.chunk_size);

    // 添加调试输出
    APP_DBG("WebSocket::upload_firmware_chunk: Received checksum: '%s', length: %d", chunk.checksum, strlen(chunk.checksum));

    // 解析目标地址（支持字符串格式的十六进制地址）
    if (targetAddressItem) {
        if (cJSON_IsString(targetAddressItem)) {
            const char* addrStr = cJSON_GetStringValue(targetAddressItem);

            if (strncmp(addrStr, "0x", 2) == 0 || strncmp(addrStr, "0X", 2) == 0) {
                chunk.target_address = strtoul(addrStr, nullptr, 16);
            } else {
                chunk.target_address = strtoul(addrStr, nullptr, 10);
            }
        } else if (cJSON_IsNumber(targetAddressItem)) {
            chunk.target_address = (uint32_t)cJSON_GetNumberValue(targetAddressItem);
        }
    }

    // LOG_INFO("WebSocket", "upload_firmware_chunk: target_address=0x%08X", chunk.target_address);

    // 解码Base64数据
    const char* base64Data = cJSON_GetStringValue(dataItem);
    // LOG_INFO("WebSocket", "upload_firmware_chunk: Starting Base64 decode, input length=%zu", strlen(base64Data));
    
    size_t binaryDataLen = 0;
    uint8_t* binaryData = base64_decode_websocket(base64Data, &binaryDataLen);
    
    if (!binaryData || binaryDataLen == 0) {
        if (binaryData) free(binaryData);
        LOG_ERROR("WebSocket", "upload_firmware_chunk: Failed to decode Base64 data");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to decode Base64 data");
    }

    // LOG_INFO("WebSocket", "upload_firmware_chunk: Base64 decode successful, binary data length=%zu", binaryDataLen);
    APP_DBG("Received binary data from WebSocket, size: %d", binaryDataLen);
    
    // 检查是否有二进制数据
    if (binaryDataLen >= 32) {
        char hex_debug[65];
        for (int i = 0; i < 32; i++) {
            sprintf(hex_debug + i*2, "%02x", binaryData[i]);
        }
        hex_debug[64] = '\0';
        APP_DBG("First 32 bytes of received data: %s", hex_debug);
    }
    
    // 输出后32字节用于调试
    if (binaryDataLen > 32) {
        char hex_debug_end[65];
        int start_pos = binaryDataLen - 32;
        for (int i = 0; i < 32; i++) {
            sprintf(hex_debug_end + i*2, "%02x", binaryData[start_pos + i]);
        }
        hex_debug_end[64] = '\0';
        APP_DBG("Last 32 bytes of binary data: %s", hex_debug_end);
    }

    // 验证数据大小
    if (binaryDataLen != chunk.chunk_size) {
        LOG_WARN("WebSocket", "upload_firmware_chunk: Data size mismatch: expected %u, actual %zu", 
                 chunk.chunk_size, binaryDataLen);
        chunk.chunk_size = binaryDataLen; // 使用实际数据大小
    }

    chunk.data = binaryData;

    APP_DBG("chunk.data: %p, chunk.chunk_size: %d", chunk.data, chunk.chunk_size);

    const char* sessionId = cJSON_GetStringValue(sessionIdItem);
    const char* componentName = cJSON_GetStringValue(componentNameItem);
    
    // LOG_INFO("WebSocket", "upload_firmware_chunk: Calling ProcessFirmwareChunk with session=%s, component=%s, index=%u",
    //          sessionId, componentName, chunk.chunk_index);
    APP_DBG("Begin ProcessFirmwareChunk: %s, %s, %d", sessionId, componentName, chunk.chunk_index);
    
    // 处理固件分片
    bool success = manager->ProcessFirmwareChunk(sessionId, componentName, &chunk);

    // LOG_INFO("WebSocket", "upload_firmware_chunk: ProcessFirmwareChunk returned: %s", success ? "SUCCESS" : "FAILED");

    // 清理资源
    free(binaryData);

    // 构建响应
    // LOG_INFO("WebSocket", "upload_firmware_chunk: Building response...");
    cJSON* dataJSON = cJSON_CreateObject();
    if (!dataJSON) {
        LOG_ERROR("WebSocket", "upload_firmware_chunk: Failed to create response JSON");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to create response");
    }
    
    cJSON_AddBoolToObject(dataJSON, "success", success);
    cJSON_AddNumberToObject(dataJSON, "chunk_index", chunk.chunk_index);
    
    if (success) {
        uint32_t progress = manager->GetUpgradeProgress(sessionId);
        cJSON_AddNumberToObject(dataJSON, "progress", progress);
        // LOG_INFO("WebSocket", "upload_firmware_chunk: Success, progress=%u", progress);
    } else {
        // 检查是否是会话不存在的错误
        cJSON_AddStringToObject(dataJSON, "error", "Chunk processing failed. Session may not exist or chunk data is invalid.");
        LOG_ERROR("WebSocket", "upload_firmware_chunk: Failed - session may not exist or chunk data invalid");
    }

    // LOG_INFO("WebSocket", "upload_firmware_chunk: Creating success response with cid=%d", request.getCid());
    WebSocketDownstreamMessage response = create_success_response(request.getCid(), request.getCommand(), dataJSON);
    // LOG_INFO("WebSocket", "upload_firmware_chunk: Response created, returning...");
    return response;
}

/**
 * @brief 处理二进制固件分片上传
 * @param data 二进制数据
 * @param length 数据长度  
 * @param connection WebSocket连接
 * @return bool 处理成功返回true
 */
bool FirmwareCommandHandler::handleBinaryFirmwareChunk(const uint8_t* data, size_t length, WebSocketConnection* connection) {
    // LOG_INFO("WebSocket", "Handling binary firmware chunk, data length: %zu", length);
    
    // 检查数据长度是否足够包含头部
    if (length < sizeof(BinaryFirmwareChunkHeader)) {
        LOG_ERROR("WebSocket", "Binary firmware chunk too small: %zu < %zu", length, sizeof(BinaryFirmwareChunkHeader));
        sendBinaryChunkResponse(connection, false, 0, 0, "Invalid data length");
        return false;
    }
    
    // 解析头部
    const BinaryFirmwareChunkHeader* header = reinterpret_cast<const BinaryFirmwareChunkHeader*>(data);
    
    // 验证命令类型
    if (header->command != BINARY_CMD_UPLOAD_FIRMWARE_CHUNK) {
        LOG_ERROR("WebSocket", "Invalid binary command: %d", header->command);
        sendBinaryChunkResponse(connection, false, header->chunk_index, 0, "Invalid command");
        return false;
    }
    if (header->session_id_len == 0 ||
        header->session_id_len > sizeof(header->session_id) ||
        header->component_name_len == 0 ||
        header->component_name_len > sizeof(header->component_name)) {
        sendBinaryChunkResponse(connection, false, header->chunk_index, 0,
                                "Invalid header string length");
        return false;
    }
    
    // 提取字符串参数
    std::string sessionId(header->session_id, header->session_id_len);
    std::string componentName(header->component_name, header->component_name_len);
    
    // 计算分片数据的起始位置和大小
    size_t payload_offset = sizeof(BinaryFirmwareChunkHeader);
    size_t payload_size = length - payload_offset;
    
    if (payload_size != header->chunk_size) {
        LOG_ERROR("WebSocket", "Payload size mismatch: expected %u, actual %zu", header->chunk_size, payload_size);
        sendBinaryChunkResponse(connection, false, header->chunk_index, 0,
                                "Payload size mismatch");
        return false;
    }
    
    // LOG_INFO("WebSocket", "Binary chunk: session=%s, component=%s, index=%u/%u, size=%u, offset=%u, addr=0x%08X",
    //          sessionId.c_str(), componentName.c_str(), header->chunk_index, header->total_chunks,
    //          header->chunk_size, header->chunk_offset, header->target_address);
    
    FirmwareManager* manager = FirmwareManager::GetInstance();
    if (!manager) {
        LOG_ERROR("WebSocket", "Binary firmware chunk: Failed to get firmware manager instance");
        sendBinaryChunkResponse(connection, false, header->chunk_index, 0, "Firmware manager not available");
        return false;
    }
    
    // 构建ChunkData结构
    ChunkData chunk = {0};
    chunk.chunk_index = header->chunk_index;
    chunk.total_chunks = header->total_chunks;
    chunk.chunk_size = (uint32_t)payload_size;
    chunk.chunk_offset = header->chunk_offset;
    chunk.target_address = header->target_address;
    
    // 复制完整32字节SHA-256。
    char checksum_str[65] = {0};
    for (int i = 0; i < 32; i++) {
        sprintf(checksum_str + i*2, "%02x", header->checksum[i]);
    }
    strncpy(chunk.checksum, checksum_str, sizeof(chunk.checksum) - 1);
    
    // 分配内存并复制分片数据
    uint8_t* chunk_data = (uint8_t*)malloc(payload_size);
    if (!chunk_data) {
        LOG_ERROR("WebSocket", "Binary firmware chunk: Failed to allocate memory for chunk data");
        sendBinaryChunkResponse(connection, false, header->chunk_index, 0, "Memory allocation failed");
        return false;
    }
    
    memcpy(chunk_data, data + payload_offset, payload_size);
    chunk.data = chunk_data;
    
    APP_DBG("Binary chunk data: %p, size: %d", chunk.data, chunk.chunk_size);
    APP_DBG("Begin ProcessFirmwareChunk: %s, %s, %d", sessionId.c_str(), componentName.c_str(), chunk.chunk_index);
    
    // 处理固件分片
    bool success = manager->ProcessFirmwareChunk(sessionId.c_str(), componentName.c_str(), &chunk);
    
    // 清理资源
    free(chunk_data);
    
    // 获取进度
    uint32_t progress = 0;
    if (success) {
        progress = manager->GetUpgradeProgress(sessionId.c_str());
    }
    
    // 发送响应
    sendBinaryChunkResponse(connection, success, header->chunk_index, progress, 
                           success ? nullptr : "Chunk processing failed");
    
    // LOG_INFO("WebSocket", "Binary firmware chunk processing completed: %s", success ? "success" : "failed");
    return success;
}

/**
 * @brief 发送二进制响应
 */
void FirmwareCommandHandler::sendBinaryChunkResponse(WebSocketConnection* connection, bool success, 
                                                    uint32_t chunk_index, uint32_t progress, 
                                                    const char* error_message) {
    // 构建二进制响应（简化格式）
    struct BinaryChunkResponse {
        uint8_t command;        // 0x81 表示响应
        uint8_t success;        // 0=失败, 1=成功
        uint32_t chunk_index;   // 分片索引
        uint32_t progress;      // 进度（0-100）
        uint8_t error_len;      // 错误消息长度
        char error_msg[64];     // 错误消息（最多64字节）
    } __attribute__((packed));
    
    BinaryChunkResponse response = {0};
    response.command = 0x81; // 响应命令
    response.success = success ? 1 : 0;
    response.chunk_index = chunk_index;
    response.progress = progress;
    
    if (error_message && !success) {
        size_t error_len = strlen(error_message);
        if (error_len > 63) error_len = 63; // 保留空间给null terminator
        response.error_len = (uint8_t)error_len;
        strncpy(response.error_msg, error_message, error_len);
        response.error_msg[error_len] = '\0';
    } else {
        response.error_len = 0;
    }
    
    // 发送二进制响应
    ConfigTransport_ReplyBinary(
        connection,
        reinterpret_cast<const uint8_t *>(&response),
        sizeof(response));
    
    APP_DBG("Binary chunk response sent: success=%d, chunk_index=%u, progress=%u", 
            response.success, response.chunk_index, response.progress);
}

/**
 * @brief 完成固件升级会话
 */
WebSocketDownstreamMessage FirmwareCommandHandler::handleCompleteFirmwareUpgradeSession(const WebSocketUpstreamMessage& request) {
    // LOG_INFO("WebSocket", "Handling complete_firmware_upgrade_session command, cid: %d", request.getCid());
    
    FirmwareManager* manager = FirmwareManager::GetInstance();
    if (!manager) {
        LOG_ERROR("WebSocket", "complete_firmware_upgrade_session: Failed to get firmware manager instance");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to get firmware manager instance");
    }

    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("WebSocket", "complete_firmware_upgrade_session: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    cJSON* sessionIdItem = cJSON_GetObjectItem(params, "session_id");
    if (!sessionIdItem || !cJSON_IsString(sessionIdItem)) {
        LOG_ERROR("WebSocket", "complete_firmware_upgrade_session: Missing session ID");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing session ID");
    }
    
    const char* sessionId = cJSON_GetStringValue(sessionIdItem);
    
    // 完成升级会话
    bool success = manager->CompleteUpgradeSession(sessionId);
    
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddBoolToObject(dataJSON, "success", success);
    
    if (success) {
        cJSON_AddStringToObject(dataJSON, "message", "Firmware upgrade completed successfully. System will restart in 2 seconds.");
        
        STORAGE_MANAGER.setBootMode(BootMode::BOOT_MODE_WEB_CONFIG);
        STORAGE_MANAGER.saveConfig(); // 保存配置

        // 设置需要重启 2秒后重启
        WebSocketCommandHandler::rebootTick = HAL_GetTick() + 2000;
        WebSocketCommandHandler::needReboot = true;

    } else {
        cJSON_AddStringToObject(dataJSON, "error", "Failed to complete upgrade session");
        LOG_ERROR("WebSocket", "complete_firmware_upgrade_session: Failed to complete upgrade session");
    }

    // LOG_INFO("WebSocket", "complete_firmware_upgrade_session command completed");
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

/**
 * @brief 中止固件升级会话
 */
WebSocketDownstreamMessage FirmwareCommandHandler::handleAbortFirmwareUpgradeSession(const WebSocketUpstreamMessage& request) {
    // LOG_INFO("WebSocket", "Handling abort_firmware_upgrade_session command, cid: %d", request.getCid());
    
    FirmwareManager* manager = FirmwareManager::GetInstance();
    if (!manager) {
        LOG_ERROR("WebSocket", "abort_firmware_upgrade_session: Failed to get firmware manager instance");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to get firmware manager instance");
    }

    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("WebSocket", "abort_firmware_upgrade_session: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    cJSON* sessionIdItem = cJSON_GetObjectItem(params, "session_id");
    if (!sessionIdItem || !cJSON_IsString(sessionIdItem)) {
        LOG_ERROR("WebSocket", "abort_firmware_upgrade_session: Missing session ID");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing session ID");
    }
    
    const char* sessionId = cJSON_GetStringValue(sessionIdItem);
    
    // 中止升级会话
    bool success = manager->AbortUpgradeSession(sessionId);
    
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddBoolToObject(dataJSON, "success", success);
    
    if (success) {
        cJSON_AddStringToObject(dataJSON, "message", "Firmware upgrade session aborted successfully");
    } else {
        cJSON_AddStringToObject(dataJSON, "error", "Failed to abort upgrade session");
        LOG_ERROR("WebSocket", "abort_firmware_upgrade_session: Failed to abort upgrade session");
    }

    // LOG_INFO("WebSocket", "abort_firmware_upgrade_session command completed");
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

/**
 * @brief 获取固件升级会话状态
 */
WebSocketDownstreamMessage FirmwareCommandHandler::handleGetFirmwareUpgradeStatus(const WebSocketUpstreamMessage& request) {
    // LOG_INFO("WebSocket", "Handling get_firmware_upgrade_status command, cid: %d", request.getCid());
    
    FirmwareManager* manager = FirmwareManager::GetInstance();
    if (!manager) {
        LOG_ERROR("WebSocket", "get_firmware_upgrade_status: Failed to get firmware manager instance");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to get firmware manager instance");
    }

    // 获取请求参数
    cJSON* params = request.getParams();
    if (!params) {
        LOG_ERROR("WebSocket", "get_firmware_upgrade_status: Invalid parameters");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Invalid parameters");
    }

    cJSON* sessionIdItem = cJSON_GetObjectItem(params, "session_id");
    if (!sessionIdItem || !cJSON_IsString(sessionIdItem)) {
        LOG_ERROR("WebSocket", "get_firmware_upgrade_status: Missing session ID");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Missing session ID");
    }

    const char* sessionId = cJSON_GetStringValue(sessionIdItem);

    // 获取固件升级会话状态
    uint32_t progress = manager->GetUpgradeProgress(sessionId);
    
    cJSON* dataJSON = cJSON_CreateObject();
    
    if (progress == 0) {
        cJSON_AddBoolToObject(dataJSON, "success", false);
        cJSON_AddStringToObject(dataJSON, "error", "Session not found");
        LOG_ERROR("WebSocket", "get_firmware_upgrade_status: Session not found");
    } else {
        cJSON_AddBoolToObject(dataJSON, "success", true);
        cJSON_AddStringToObject(dataJSON, "status", "active");
        cJSON_AddNumberToObject(dataJSON, "progress", progress);
    }

    // LOG_INFO("WebSocket", "get_firmware_upgrade_status command completed");
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

/**
 * @brief 清理固件升级会话
 */
WebSocketDownstreamMessage FirmwareCommandHandler::handleCleanupFirmwareUpgradeSession(const WebSocketUpstreamMessage& request) {
    // LOG_INFO("WebSocket", "Handling cleanup_firmware_upgrade_session command, cid: %d", request.getCid());
    
    FirmwareManager* manager = FirmwareManager::GetInstance();
    if (!manager) {
        LOG_ERROR("WebSocket", "cleanup_firmware_upgrade_session: Failed to get firmware manager instance");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to get firmware manager instance");
    }

    cJSON* params = request.getParams();
    cJSON* sessionIdItem = params
        ? cJSON_GetObjectItem(params, "session_id")
        : nullptr;
    if (!sessionIdItem || !cJSON_IsString(sessionIdItem) ||
        cJSON_GetStringValue(sessionIdItem) == nullptr ||
        cJSON_GetStringValue(sessionIdItem)[0] == '\0') {
        return create_error_response(
            request.getCid(),
            request.getCommand(),
            1,
            "Missing session ID");
    }

    /*
     * An external cleanup request must be session-bound.  ForceCleanupSession
     * intentionally has no identifier and is reserved for trusted internal
     * expiry handling; exposing it here would let any permit holder destroy a
     * physically confirmed upgrade owned by another session.
     */
    const char* sessionId = cJSON_GetStringValue(sessionIdItem);
    const bool success = manager->AbortUpgradeSession(sessionId);
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddBoolToObject(dataJSON, "success", success);
    if (success) {
        cJSON_AddStringToObject(
            dataJSON,
            "message",
            "Session cleanup completed successfully");
    } else {
        cJSON_AddStringToObject(
            dataJSON,
            "error",
            "Session not found or session ID mismatch");
    }

    // LOG_INFO("WebSocket", "cleanup_firmware_upgrade_session command completed");
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

/**
 * @brief Base64解码函数（用于WebSocket）
 */
uint8_t* FirmwareCommandHandler::base64_decode_websocket(const char* base64_data, size_t* out_len) {
    static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    if (!base64_data || !out_len) return nullptr;
    
    size_t input_len = strlen(base64_data);
    if (input_len == 0) return nullptr;
    
    // 计算输出大小
    size_t padding = 0;
    if (input_len >= 2) {
        if (base64_data[input_len - 1] == '=') padding++;
        if (base64_data[input_len - 2] == '=') padding++;
    }
    
    *out_len = (input_len * 3) / 4 - padding;
    uint8_t* ret = (uint8_t*)malloc(*out_len);
    if (!ret) {
        *out_len = 0;
        return nullptr;
    }
    
    size_t pos = 0;
    uint32_t char_array_4[4], char_array_3[3];
    int i = 0;
    
    for (size_t idx = 0; idx < input_len; idx++) {
        if (base64_data[idx] == '=') break;
        
        const char* found = strchr(base64_chars, base64_data[idx]);
        if (!found) continue;
        
        char_array_4[i++] = found - base64_chars;
        
        if (i == 4) {
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            
            for (i = 0; i < 3; i++) {
                if (pos < *out_len) ret[pos++] = char_array_3[i];
            }
            i = 0;
        }
    }
    
    if (i) {
        for (int j = i; j < 4; j++) {
            char_array_4[j] = 0;
        }
        
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
        
        for (int j = 0; (j < i - 1); j++) {
            if (pos < *out_len) ret[pos++] = char_array_3[j];
        }
    }
    
    return ret;
}

/**
 * @brief 创建设备认证数据的JSON对象（从webconfig.cpp复制）
 */
cJSON* FirmwareCommandHandler::createDeviceAuthJSON() {
#if defined(HBOX_SECURE_BOOT_REQUIRED) && HBOX_SECURE_BOOT_REQUIRED
    /*
     * Keep this symbol for source compatibility, but make accidental calls
     * fail closed in V2.  Explicit legacy builds can set
     * HBOX_SECURE_BOOT_REQUIRED=0 and retain the former wire contract.
     */
    return nullptr;
#else
    cJSON* data = cJSON_CreateObject();
    
    char* uniqueId = str_stm32_unique_id();
    char* deviceId = get_device_id_hash();
    
    if (!uniqueId || !deviceId) {
        if (uniqueId) free(uniqueId);
        if (deviceId) free(deviceId);
        if (data) cJSON_Delete(data);
        return nullptr;
    }
    
    uint32_t deviceTimestamp = HAL_GetTick();
    uint32_t deviceRandom = deviceTimestamp ^ 0xA5A5A5A5;
    
    char challenge[64];
    snprintf(challenge, sizeof(challenge), "DEV_%08X_%08X", deviceTimestamp, deviceRandom);
    
    // 简单签名：deviceId + challenge + timestamp的哈希
    uint32_t hash = 0x9E3779B9;
    std::string signData = std::string(deviceId) + challenge + std::to_string(deviceTimestamp);
    for (char c : signData) {
        hash = ((hash << 5) + hash) + c;
    }
    
    char signature[32];
    snprintf(signature, sizeof(signature), "SIG_%08X", hash);
    
    cJSON_AddStringToObject(data, "deviceId", deviceId);
    cJSON_AddStringToObject(data, "originalUniqueId", uniqueId);
    cJSON_AddStringToObject(data, "challenge", challenge);
    cJSON_AddNumberToObject(data, "timestamp", deviceTimestamp);
    cJSON_AddStringToObject(data, "signature", signature);
    cJSON_AddNumberToObject(data, "expiresIn", 30 * 60);
    
    free(uniqueId);
    free(deviceId);
    
    return data;
#endif
}

/**
 * @brief 创建固件元数据的JSON对象（从webconfig.cpp复制）
 */
cJSON* FirmwareCommandHandler::createFirmwareMetadataJSON() {
    FirmwareManager* manager = FirmwareManager::GetInstance();
    if (!manager) {
        LOG_ERROR("WebSocket", "createFirmwareMetadataJSON: Firmware manager not initialized");
        return nullptr;
    }

    const FirmwareMetadata* metadata = manager->GetCurrentMetadata();
    FirmwareMetadata raw_metadata;
    bool metadata_validated = (metadata != nullptr);

    if (!metadata) {
        memset(&raw_metadata, 0, sizeof(raw_metadata));
        uint32_t flash_address = METADATA_ADDR - EXTERNAL_FLASH_BASE;
        int8_t result = QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(
            reinterpret_cast<uint8_t*>(&raw_metadata),
            flash_address,
            sizeof(FirmwareMetadata)
        );

        if (result == QSPI_W25Qxx_OK) {
            metadata = &raw_metadata;
        }
    }

    cJSON* data = cJSON_CreateObject();
    if (!data) {
        LOG_ERROR("WebSocket", "createFirmwareMetadataJSON: Failed to create JSON object");
        return nullptr;
    }

    cJSON_AddBoolToObject(data, "metadataValidated", metadata_validated);

    // 当前槽位信息
    const char* currentSlot =
        (metadata && metadata->target_slot == static_cast<uint8_t>(FIRMWARE_SLOT_B)) ? "B" : "A";
    const char* targetSlot = (currentSlot[0] == 'A') ? "B" : "A";

    cJSON_AddStringToObject(data, "currentSlot", currentSlot);
    cJSON_AddStringToObject(data, "targetSlot", targetSlot);

    if (metadata) {
        char version_buf[sizeof(metadata->firmware_version) + 1];
        memcpy(version_buf, metadata->firmware_version, sizeof(metadata->firmware_version));
        version_buf[sizeof(metadata->firmware_version)] = '\0';

        char build_date_buf[sizeof(metadata->build_date) + 1];
        memcpy(build_date_buf, metadata->build_date, sizeof(metadata->build_date));
        build_date_buf[sizeof(metadata->build_date)] = '\0';

        cJSON_AddStringToObject(data, "version", version_buf);
        cJSON_AddStringToObject(data, "buildDate", build_date_buf);
    } else {
        cJSON_AddStringToObject(data, "version", "");
        cJSON_AddStringToObject(data, "buildDate", "");
    }
    
    // 组件信息
    cJSON* componentsArray = cJSON_CreateArray();
    if (componentsArray) {
        if (metadata) {
            uint32_t count = metadata->component_count;
            if (count > FIRMWARE_COMPONENT_COUNT) {
                count = FIRMWARE_COMPONENT_COUNT;
            }

            for (uint32_t i = 0; i < count; i++) {
                cJSON* componentObj = cJSON_CreateObject();
                if (!componentObj) {
                    continue;
                }

                char name_buf[sizeof(metadata->components[i].name) + 1];
                memcpy(name_buf, metadata->components[i].name, sizeof(metadata->components[i].name));
                name_buf[sizeof(metadata->components[i].name)] = '\0';

                char file_buf[sizeof(metadata->components[i].file) + 1];
                memcpy(file_buf, metadata->components[i].file, sizeof(metadata->components[i].file));
                file_buf[sizeof(metadata->components[i].file)] = '\0';

                char sha256_buf[sizeof(metadata->components[i].sha256) + 1];
                memcpy(sha256_buf, metadata->components[i].sha256, sizeof(metadata->components[i].sha256));
                sha256_buf[sizeof(metadata->components[i].sha256)] = '\0';

                cJSON_AddStringToObject(componentObj, "name", name_buf);
                cJSON_AddStringToObject(componentObj, "file", file_buf);
                cJSON_AddNumberToObject(componentObj, "address", metadata->components[i].address);
                cJSON_AddNumberToObject(componentObj, "size", metadata->components[i].size);
                cJSON_AddStringToObject(componentObj, "sha256", sha256_buf);
                cJSON_AddBoolToObject(componentObj, "active", metadata->components[i].active);
                cJSON_AddItemToArray(componentsArray, componentObj);
            }
        }
        cJSON_AddItemToObject(data, "components", componentsArray);
    }

    // LOG_INFO("WebSocket", "createFirmwareMetadataJSON: Created firmware metadata successfully");
    return data;
} 
