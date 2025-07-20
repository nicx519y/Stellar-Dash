#pragma once

#include "configs/websocket_command_handler.hpp"
#include "websocket_message.hpp"
#include "cJSON.h"

// 二进制命令定义
#define BINARY_CMD_UPLOAD_FIRMWARE_CHUNK 0x01

// 二进制固件分片头部结构（82字节固定大小）
#pragma pack(push, 1)
struct BinaryFirmwareChunkHeader {
    uint8_t command;                    // 命令类型 (BINARY_CMD_UPLOAD_FIRMWARE_CHUNK)
    uint8_t reserved1;                  // 保留字节，用于对齐
    uint16_t session_id_len;            // session_id字符串长度
    char session_id[32];                // session_id字符串（固定32字节，不足补0）
    uint16_t component_name_len;        // component_name字符串长度  
    char component_name[16];            // component_name字符串（固定16字节，不足补0）
    uint32_t chunk_index;               // 分片索引
    uint32_t total_chunks;              // 总分片数
    uint32_t chunk_size;                // 分片大小
    uint32_t chunk_offset;              // 分片偏移
    uint32_t target_address;            // 目标地址
    uint8_t checksum[8];                // SHA256校验和的前8字节
};
#pragma pack(pop)

/**
 * @brief 固件相关命令处理器
 * 处理设备认证、固件元数据、固件上传等WebSocket命令
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

    // 二进制消息处理
    /**
     * @brief 处理二进制固件分片上传
     * @param data 二进制数据
     * @param length 数据长度
     * @param connection WebSocket连接
     * @return bool 处理成功返回true
     */
    bool handleBinaryFirmwareChunk(const uint8_t* data, size_t length, WebSocketConnection* connection);

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

    /**
     * @brief 创建固件升级会话
     * WebSocket命令: create_firmware_upgrade_session
     * 对应HTTP接口: POST /api/firmware-upgrade (action=create)
     */
    WebSocketDownstreamMessage handleCreateFirmwareUpgradeSession(const WebSocketUpstreamMessage& request);
    
    /**
     * @brief 上传固件分片（JSON版本，保留兼容性）
     * WebSocket命令: upload_firmware_chunk
     * 对应HTTP接口: POST /api/firmware-upgrade/chunk
     */
    WebSocketDownstreamMessage handleUploadFirmwareChunk(const WebSocketUpstreamMessage& request);
    
    /**
     * @brief 完成固件升级会话
     * WebSocket命令: complete_firmware_upgrade_session
     * 对应HTTP接口: POST /api/firmware-upgrade-complete
     */
    WebSocketDownstreamMessage handleCompleteFirmwareUpgradeSession(const WebSocketUpstreamMessage& request);
    
    /**
     * @brief 中止固件升级会话
     * WebSocket命令: abort_firmware_upgrade_session
     * 对应HTTP接口: POST /api/firmware-upgrade-abort
     */
    WebSocketDownstreamMessage handleAbortFirmwareUpgradeSession(const WebSocketUpstreamMessage& request);
    
    /**
     * @brief 获取固件升级会话状态
     * WebSocket命令: get_firmware_upgrade_status
     * 对应HTTP接口: POST /api/firmware-upgrade-status
     */
    WebSocketDownstreamMessage handleGetFirmwareUpgradeStatus(const WebSocketUpstreamMessage& request);
    
    /**
     * @brief 清理固件升级会话
     * WebSocket命令: cleanup_firmware_upgrade_session
     * 对应HTTP接口: POST /api/firmware-upgrade-cleanup
     */
    WebSocketDownstreamMessage handleCleanupFirmwareUpgradeSession(const WebSocketUpstreamMessage& request);

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
    
    /**
     * @brief Base64解码函数
     * @param base64_data Base64编码的字符串
     * @param out_len 输出解码后数据的长度
     * @return uint8_t* 解码后的二进制数据，需要调用者释放
     */
    uint8_t* base64_decode_websocket(const char* base64_data, size_t* out_len);
    
    /**
     * @brief 发送二进制响应
     * @param connection WebSocket连接
     * @param success 处理是否成功
     * @param chunk_index 分片索引
     * @param progress 进度（0-100）
     * @param error_message 错误消息（可选）
     */
    void sendBinaryChunkResponse(WebSocketConnection* connection, bool success, 
                                uint32_t chunk_index, uint32_t progress = 0, 
                                const char* error_message = nullptr);
}; 