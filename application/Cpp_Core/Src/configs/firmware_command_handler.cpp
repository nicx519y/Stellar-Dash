#include "configs/firmware_command_handler.hpp"
#include "websocket_message.hpp"
#include "firmware/firmware_manager.hpp"
#include "utils.h"
#include "main.h"
#include "system_logger.h"
#include <string>

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
    // LOG_INFO("WebSocket", "Handling get_device_auth command, cid: %d", request.getCid());
    
    // 创建设备认证数据
    cJSON* dataJSON = createDeviceAuthJSON();
    if (!dataJSON) {
        LOG_ERROR("WebSocket", "get_device_auth: Failed to create device auth data");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to get device identifiers");
    }
    
    // LOG_INFO("WebSocket", "get_device_auth command completed successfully");
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
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
    LOG_INFO("WebSocket", "Handling create_firmware_upgrade_session command, cid: %d", request.getCid());
    
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
    
    // 解析manifest到FirmwareMetadata结构
    FirmwareMetadata manifest = {0};
    
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
    
    // 解析组件
    cJSON* componentsItem = cJSON_GetObjectItem(manifestItem, "components");
    if (componentsItem && cJSON_IsArray(componentsItem)) {
        int arraySize = cJSON_GetArraySize(componentsItem);
        manifest.component_count = arraySize > FIRMWARE_COMPONENT_COUNT ? FIRMWARE_COMPONENT_COUNT : arraySize;
        
        for (int i = 0; i < manifest.component_count; i++) {
            cJSON* compItem = cJSON_GetArrayItem(componentsItem, i);
            if (compItem) {
                FirmwareComponent* comp = &manifest.components[i];
                
                cJSON* nameItem = cJSON_GetObjectItem(compItem, "name");
                if (nameItem && cJSON_IsString(nameItem)) {
                    strncpy(comp->name, cJSON_GetStringValue(nameItem), sizeof(comp->name) - 1);
                }
                
                cJSON* fileItem = cJSON_GetObjectItem(compItem, "file");
                if (fileItem && cJSON_IsString(fileItem)) {
                    strncpy(comp->file, cJSON_GetStringValue(fileItem), sizeof(comp->file) - 1);
                }
                
                cJSON* addressItem = cJSON_GetObjectItem(compItem, "address");
                if (addressItem && cJSON_IsNumber(addressItem)) {
                    comp->address = (uint32_t)cJSON_GetNumberValue(addressItem);
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

    LOG_INFO("WebSocket", "create_firmware_upgrade_session command completed");
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

/**
 * @brief 上传固件分片（JSON版本，保留兼容性）
 */
WebSocketDownstreamMessage FirmwareCommandHandler::handleUploadFirmwareChunk(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling upload_firmware_chunk command, cid: %d", request.getCid());
    
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

    LOG_INFO("WebSocket", "upload_firmware_chunk: Validating parameters...");
    
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

    LOG_INFO("WebSocket", "upload_firmware_chunk: Parameters validated successfully");

    // 构建ChunkData结构
    ChunkData chunk = {0};
    chunk.chunk_index = (uint32_t)cJSON_GetNumberValue(chunkIndexItem);
    chunk.total_chunks = (uint32_t)cJSON_GetNumberValue(totalChunksItem);
    chunk.chunk_size = (uint32_t)cJSON_GetNumberValue(chunkSizeItem);
    chunk.chunk_offset = (uint32_t)cJSON_GetNumberValue(chunkOffsetItem);
    strncpy(chunk.checksum, cJSON_GetStringValue(checksumItem), sizeof(chunk.checksum) - 1);

    LOG_INFO("WebSocket", "upload_firmware_chunk: chunk_index=%u, total_chunks=%u, chunk_size=%u",
             chunk.chunk_index, chunk.total_chunks, chunk.chunk_size);

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

    LOG_INFO("WebSocket", "upload_firmware_chunk: target_address=0x%08X", chunk.target_address);

    // 解码Base64数据
    const char* base64Data = cJSON_GetStringValue(dataItem);
    LOG_INFO("WebSocket", "upload_firmware_chunk: Starting Base64 decode, input length=%zu", strlen(base64Data));
    
    size_t binaryDataLen = 0;
    uint8_t* binaryData = base64_decode_websocket(base64Data, &binaryDataLen);
    
    if (!binaryData || binaryDataLen == 0) {
        if (binaryData) free(binaryData);
        LOG_ERROR("WebSocket", "upload_firmware_chunk: Failed to decode Base64 data");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to decode Base64 data");
    }

    LOG_INFO("WebSocket", "upload_firmware_chunk: Base64 decode successful, binary data length=%zu", binaryDataLen);
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
    
    LOG_INFO("WebSocket", "upload_firmware_chunk: Calling ProcessFirmwareChunk with session=%s, component=%s, index=%u",
             sessionId, componentName, chunk.chunk_index);
    APP_DBG("Begin ProcessFirmwareChunk: %s, %s, %d", sessionId, componentName, chunk.chunk_index);
    
    // 处理固件分片
    bool success = manager->ProcessFirmwareChunk(sessionId, componentName, &chunk);

    LOG_INFO("WebSocket", "upload_firmware_chunk: ProcessFirmwareChunk returned: %s", success ? "SUCCESS" : "FAILED");

    // 清理资源
    free(binaryData);

    // 构建响应
    LOG_INFO("WebSocket", "upload_firmware_chunk: Building response...");
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
        LOG_INFO("WebSocket", "upload_firmware_chunk: Success, progress=%u", progress);
    } else {
        // 检查是否是会话不存在的错误
        cJSON_AddStringToObject(dataJSON, "error", "Chunk processing failed. Session may not exist or chunk data is invalid.");
        LOG_ERROR("WebSocket", "upload_firmware_chunk: Failed - session may not exist or chunk data invalid");
    }

    LOG_INFO("WebSocket", "upload_firmware_chunk: Creating success response with cid=%d", request.getCid());
    WebSocketDownstreamMessage response = create_success_response(request.getCid(), request.getCommand(), dataJSON);
    LOG_INFO("WebSocket", "upload_firmware_chunk: Response created, returning...");
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
    LOG_INFO("WebSocket", "Handling binary firmware chunk, data length: %zu", length);
    
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
    
    // 提取字符串参数
    std::string sessionId(header->session_id, header->session_id_len);
    std::string componentName(header->component_name, header->component_name_len);
    
    // 计算分片数据的起始位置和大小
    size_t payload_offset = sizeof(BinaryFirmwareChunkHeader);
    size_t payload_size = length - payload_offset;
    
    if (payload_size != header->chunk_size) {
        LOG_WARN("WebSocket", "Payload size mismatch: expected %u, actual %zu", header->chunk_size, payload_size);
        // 使用实际大小继续处理
    }
    
    LOG_INFO("WebSocket", "Binary chunk: session=%s, component=%s, index=%u/%u, size=%u, offset=%u, addr=0x%08X",
             sessionId.c_str(), componentName.c_str(), header->chunk_index, header->total_chunks,
             header->chunk_size, header->chunk_offset, header->target_address);
    
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
    
    // 复制校验和（只使用前8字节）
    char checksum_str[17] = {0};
    for (int i = 0; i < 8; i++) {
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
    
    LOG_INFO("WebSocket", "Binary firmware chunk processing completed: %s", success ? "success" : "failed");
    return success;
}

/**
 * @brief 发送二进制响应
 */
void FirmwareCommandHandler::sendBinaryChunkResponse(WebSocketConnection* connection, bool success, 
                                                    uint32_t chunk_index, uint32_t progress, 
                                                    const char* error_message) {
    if (!connection) return;
    
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
    connection->send_binary((const uint8_t*)&response, sizeof(response));
    
    APP_DBG("Binary chunk response sent: success=%d, chunk_index=%u, progress=%u", 
            response.success, response.chunk_index, response.progress);
}

/**
 * @brief 完成固件升级会话
 */
WebSocketDownstreamMessage FirmwareCommandHandler::handleCompleteFirmwareUpgradeSession(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling complete_firmware_upgrade_session command, cid: %d", request.getCid());
    
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
        
        // 设置需要重启 2秒后重启
        WebSocketCommandHandler::rebootTick = HAL_GetTick() + 2000;
        WebSocketCommandHandler::needReboot = true;
    } else {
        cJSON_AddStringToObject(dataJSON, "error", "Failed to complete upgrade session");
        LOG_ERROR("WebSocket", "complete_firmware_upgrade_session: Failed to complete upgrade session");
    }

    LOG_INFO("WebSocket", "complete_firmware_upgrade_session command completed");
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

/**
 * @brief 中止固件升级会话
 */
WebSocketDownstreamMessage FirmwareCommandHandler::handleAbortFirmwareUpgradeSession(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling abort_firmware_upgrade_session command, cid: %d", request.getCid());
    
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

    LOG_INFO("WebSocket", "abort_firmware_upgrade_session command completed");
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

/**
 * @brief 获取固件升级会话状态
 */
WebSocketDownstreamMessage FirmwareCommandHandler::handleGetFirmwareUpgradeStatus(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling get_firmware_upgrade_status command, cid: %d", request.getCid());
    
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

    LOG_INFO("WebSocket", "get_firmware_upgrade_status command completed");
    return create_success_response(request.getCid(), request.getCommand(), dataJSON);
}

/**
 * @brief 清理固件升级会话
 */
WebSocketDownstreamMessage FirmwareCommandHandler::handleCleanupFirmwareUpgradeSession(const WebSocketUpstreamMessage& request) {
    LOG_INFO("WebSocket", "Handling cleanup_firmware_upgrade_session command, cid: %d", request.getCid());
    
    FirmwareManager* manager = FirmwareManager::GetInstance();
    if (!manager) {
        LOG_ERROR("WebSocket", "cleanup_firmware_upgrade_session: Failed to get firmware manager instance");
        return create_error_response(request.getCid(), request.getCommand(), 1, "Failed to get firmware manager instance");
    }

    // 强制清理当前会话
    manager->ForceCleanupSession();
    
    // 返回成功响应
    cJSON* dataJSON = cJSON_CreateObject();
    cJSON_AddBoolToObject(dataJSON, "success", true);
    cJSON_AddStringToObject(dataJSON, "message", "Session cleanup completed successfully");

    LOG_INFO("WebSocket", "cleanup_firmware_upgrade_session command completed");
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
    if (!metadata) {
        LOG_ERROR("WebSocket", "createFirmwareMetadataJSON: Failed to get firmware metadata");
        return nullptr;
    }

    cJSON* data = cJSON_CreateObject();
    if (!data) {
        LOG_ERROR("WebSocket", "createFirmwareMetadataJSON: Failed to create JSON object");
        return nullptr;
    }
    
    // 当前槽位信息
    cJSON_AddStringToObject(data, "currentSlot", 
        metadata->target_slot == FIRMWARE_SLOT_A ? "A" : "B");
    cJSON_AddStringToObject(data, "targetSlot", 
        manager->GetTargetUpgradeSlot() == FIRMWARE_SLOT_A ? "A" : "B");
    cJSON_AddStringToObject(data, "version", metadata->firmware_version);
    cJSON_AddStringToObject(data, "buildDate", metadata->build_date);
    
    // 组件信息
    cJSON* componentsArray = cJSON_CreateArray();
    if (componentsArray) {
        for (uint32_t i = 0; i < metadata->component_count; i++) {
            cJSON* componentObj = cJSON_CreateObject();
            if (componentObj) {
                cJSON_AddStringToObject(componentObj, "name", metadata->components[i].name);
                cJSON_AddStringToObject(componentObj, "file", metadata->components[i].file);
                cJSON_AddNumberToObject(componentObj, "address", metadata->components[i].address);
                cJSON_AddNumberToObject(componentObj, "size", metadata->components[i].size);
                cJSON_AddStringToObject(componentObj, "sha256", metadata->components[i].sha256);
                cJSON_AddBoolToObject(componentObj, "active", metadata->components[i].active);
                cJSON_AddItemToArray(componentsArray, componentObj);
            }
        }
        cJSON_AddItemToObject(data, "components", componentsArray);
    }
    
    // LOG_INFO("WebSocket", "createFirmwareMetadataJSON: Created firmware metadata successfully");
    return data;
} 