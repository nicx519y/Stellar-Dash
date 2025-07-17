#pragma once

#include "configs/websocket_command_handler.hpp"
#include "websocket_message.hpp"
#include "cJSON.h"

/**
 * @brief 固件相关命令处理器
 * 处理设备认证、固件元数据等WebSocket命令
 */
class FirmwareCommandHandler : public WebSocketCommandHandler {
private:
    // 单例模式：私有构造函数
    FirmwareCommandHandler() = default;
    ~FirmwareCommandHandler() = default;
    
    // 禁用拷贝构造和赋值
    FirmwareCommandHandler(const FirmwareCommandHandler&) = delete;
    FirmwareCommandHandler& operator=(const FirmwareCommandHandler&) = delete;

public:
    // 单例模式：获取实例
    static FirmwareCommandHandler& getInstance() {
        static FirmwareCommandHandler instance;
        return instance;
    }

    // 实现基类纯虚函数
    WebSocketDownstreamMessage handle(const WebSocketUpstreamMessage& request) override;

    // WebSocket命令处理方法
    
    /**
     * @brief 获取设备认证信息
     * WebSocket命令: get_device_auth
     * 对应HTTP接口: GET /api/device-auth
     */
    WebSocketDownstreamMessage handleGetDeviceAuth(const WebSocketUpstreamMessage& request);
    
    /**
     * @brief 获取固件元数据信息
     * WebSocket命令: get_firmware_metadata
     * 对应HTTP接口: GET /api/firmware-metadata
     */
    WebSocketDownstreamMessage handleGetFirmwareMetadata(const WebSocketUpstreamMessage& request);

private:
    /**
     * @brief 创建设备认证数据的JSON对象
     * @return cJSON* 包含设备认证信息的JSON对象
     */
    cJSON* createDeviceAuthJSON();
    
    /**
     * @brief 创建固件元数据的JSON对象
     * @return cJSON* 包含固件元数据信息的JSON对象
     */
    cJSON* createFirmwareMetadataJSON();
}; 