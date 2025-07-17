#include "configs/firmware_command_handler.hpp"
#include "websocket_message.hpp"
#include "firmware/firmware_manager.hpp"
#include "utils.h"
#include "main.h"
#include "system_logger.h"

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
 * @brief 创建设备认证数据的JSON对象
 * @return cJSON* 包含设备认证信息的JSON对象
 */
cJSON* FirmwareCommandHandler::createDeviceAuthJSON() {
    cJSON* data = cJSON_CreateObject();
    if (!data) {
        return nullptr;
    }
    
    char* uniqueId = str_stm32_unique_id();
    char* deviceId = get_device_id_hash();
    
    if (!uniqueId || !deviceId) {
        cJSON_Delete(data);
        if (uniqueId) free(uniqueId);
        if (deviceId) free(deviceId);
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
    cJSON_AddNumberToObject(data, "expiresIn", 30 * 60); // 30分钟过期
    
    free(uniqueId);
    free(deviceId);
    
    return data;
}

/**
 * @brief 创建固件元数据的JSON对象
 * @return cJSON* 包含固件元数据信息的JSON对象
 */
cJSON* FirmwareCommandHandler::createFirmwareMetadataJSON() {
    FirmwareManager* manager = FirmwareManager::GetInstance();
    if (!manager) {
        LOG_ERROR("WebSocket", "createFirmwareMetadataJSON: Firmware manager not initialized");
        
        // 创建错误信息的JSON
        cJSON* data = cJSON_CreateObject();
        if (data) {
            cJSON_AddStringToObject(data, "error", "Firmware manager not initialized");
        }
        return data;
    }

    const FirmwareMetadata* metadata = manager->GetCurrentMetadata();
    if (!metadata) {
        LOG_ERROR("WebSocket", "createFirmwareMetadataJSON: Failed to get firmware metadata");
        
        // 创建错误信息的JSON
        cJSON* data = cJSON_CreateObject();
        if (data) {
            cJSON_AddStringToObject(data, "error", "Failed to get firmware metadata");
        }
        return data;
    }

    cJSON* data = cJSON_CreateObject();
    if (!data) {
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