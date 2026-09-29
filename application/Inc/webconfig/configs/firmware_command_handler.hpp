#pragma once

#include "configs/device_command_handler.hpp"
#include "device_command_message.hpp"
#include "cJSON.h"

// 二进制命令定义
#define BINARY_CMD_UPLOAD_FIRMWARE_CHUNK 0x01

// 二进制固件分片头部结构（106字节固定大小）
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
    uint8_t checksum[32];               // 完整SHA256校验和
};
#pragma pack(pop)
static_assert(sizeof(BinaryFirmwareChunkHeader) == 106,
              "BinaryFirmwareChunkHeader ABI mismatch");

/**
 * @brief 固件相关命令处理器
 * 处理设备认证、固件元数据、固件上传等DeviceCommand命令
 */
class FirmwareCommandHandler : public DeviceCommandHandler {
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
    DeviceCommandResponse handle(const DeviceCommandRequest& request) override;

    // 二进制消息处理
    /**
     * @brief 处理二进制固件分片上传
     * @param data 二进制数据
     * @param length 数据长度
     * @return bool 处理成功返回true
     */
    bool handleBinaryFirmwareChunk(const uint8_t* data, size_t length);

    // DeviceCommand命令处理方法
    
    /**
     * @brief 获取设备认证信息
     * DeviceCommand命令: get_device_auth
     * 对应HTTP接口: GET /api/device-auth
     */
    DeviceCommandResponse handleGetDeviceAuth(const DeviceCommandRequest& request);
    
    /**
     * @brief 获取固件元数据信息
     * DeviceCommand命令: get_firmware_metadata
     * 对应HTTP接口: GET /api/firmware-metadata
     */
    DeviceCommandResponse handleGetFirmwareMetadata(const DeviceCommandRequest& request);

    /**
     * @brief 创建固件升级会话
     * DeviceCommand命令: create_firmware_upgrade_session
     * 对应HTTP接口: POST /api/firmware-upgrade (action=create)
     */
    DeviceCommandResponse handleCreateFirmwareUpgradeSession(const DeviceCommandRequest& request);
    
    /**
     * @brief 上传固件分片（JSON版本，保留兼容性）
     * DeviceCommand命令: upload_firmware_chunk
     * 对应HTTP接口: POST /api/firmware-upgrade/chunk
     */
    DeviceCommandResponse handleUploadFirmwareChunk(const DeviceCommandRequest& request);
    
    /**
     * @brief 完成固件升级会话
     * DeviceCommand命令: complete_firmware_upgrade_session
     * 对应HTTP接口: POST /api/firmware-upgrade-complete
     */
    DeviceCommandResponse handleCompleteFirmwareUpgradeSession(const DeviceCommandRequest& request);
    
    /**
     * @brief 中止固件升级会话
     * DeviceCommand命令: abort_firmware_upgrade_session
     * 对应HTTP接口: POST /api/firmware-upgrade-abort
     */
    DeviceCommandResponse handleAbortFirmwareUpgradeSession(const DeviceCommandRequest& request);
    
    /**
     * @brief 获取固件升级会话状态
     * DeviceCommand命令: get_firmware_upgrade_status
     * 对应HTTP接口: POST /api/firmware-upgrade-status
     */
    DeviceCommandResponse handleGetFirmwareUpgradeStatus(const DeviceCommandRequest& request);
    
    /**
     * @brief 清理固件升级会话
     * DeviceCommand命令: cleanup_firmware_upgrade_session
     * 对应HTTP接口: POST /api/firmware-upgrade-cleanup
     */
    DeviceCommandResponse handleCleanupFirmwareUpgradeSession(const DeviceCommandRequest& request);
    DeviceCommandResponse handleCh585UpdateStatus(const DeviceCommandRequest& request);
    DeviceCommandResponse handleCh585UpdateBegin(const DeviceCommandRequest& request);
    DeviceCommandResponse handleCh585UpdateChunk(const DeviceCommandRequest& request);
    DeviceCommandResponse handleCh585UpdateComplete(const DeviceCommandRequest& request);

private:
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
    uint8_t* base64_decode_device_command(const char* base64_data, size_t* out_len);
    
    /**
     * @brief 发送二进制响应
     * @param success 处理是否成功
     * @param chunk_index 分片索引
     * @param progress 进度（0-100）
     * @param error_message 错误消息（可选）
     */
    void sendBinaryChunkResponse(bool success,
                                uint32_t chunk_index, uint32_t progress = 0,
                                const char* error_message = nullptr);
};
