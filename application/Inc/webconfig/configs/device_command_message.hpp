#pragma once

#include <string>
#include <cstdint>
#include "cJSON.h"

/**
 * @brief Transport-neutral device command request.
 * 消息格式：{"cid": 2, "command": "ping", "params": {...}}
 */
class DeviceCommandRequest {
private:
    uint32_t cid_;                    // 命令ID
    std::string command_;             // 命令名称
    cJSON* params_;                   // 参数对象

public:
    DeviceCommandRequest();
    ~DeviceCommandRequest();

    // 禁用拷贝构造和赋值，只允许移动
    DeviceCommandRequest(const DeviceCommandRequest&) = delete;
    DeviceCommandRequest& operator=(const DeviceCommandRequest&) = delete;

    // 移动构造和赋值
    DeviceCommandRequest(DeviceCommandRequest&& other) noexcept;
    DeviceCommandRequest& operator=(DeviceCommandRequest&& other) noexcept;

    // Getter方法
    uint32_t getCid() const { return cid_; }
    const std::string& getCommand() const { return command_; }
    cJSON* getParams() const { return params_; }

    // Setter方法
    void setCid(uint32_t cid) { cid_ = cid; }
    void setCommand(const std::string& command) { command_ = command; }
    void setParams(cJSON* params);
};

/**
 * @brief Transport-neutral device command response.
 * 消息格式：{"cid": 2, "command": "ping", "errNo": 0, "data": {...}}
 */
class DeviceCommandResponse {
private:
    uint32_t cid_;        // 命令ID，与上行消息保持一致
    std::string command_; // 命令名称
    int errNo_;           // 错误码 (0表示成功)
    cJSON* data_;         // 响应数据对象

public:
    DeviceCommandResponse();
    ~DeviceCommandResponse();

    // 禁用拷贝构造和赋值，只允许移动
    DeviceCommandResponse(const DeviceCommandResponse&) = delete;
    DeviceCommandResponse& operator=(const DeviceCommandResponse&) = delete;
    
    // 移动构造和赋值
    DeviceCommandResponse(DeviceCommandResponse&& other) noexcept;
    DeviceCommandResponse& operator=(DeviceCommandResponse&& other) noexcept;
    
    // Getter方法
    uint32_t getCid() const { return cid_; }
    const std::string& getCommand() const { return command_; }
    int getErrNo() const { return errNo_; }
    cJSON* getData() const { return data_; }
    cJSON* releaseData();
    
    // Setter方法
    void setCid(uint32_t cid) { cid_ = cid; }
    void setCommand(const std::string& command) { command_ = command; }
    void setErrNo(int errNo) { errNo_ = errNo; }
    void setData(cJSON* data);
};

// ==================== 工具函数声明 ====================

/**
 * @brief 创建设备命令响应消息
 * @param cid 命令ID
 * @param command 命令名称
 * @param errNo 错误码
 * @param data 响应数据（可选）
 * @param errorMessage 错误消息（可选）
 * @return 设备命令响应对象
 */
DeviceCommandResponse create_device_command_response(uint32_t cid, const std::string& command,
                                                    int errNo, cJSON* data = nullptr,
                                                    const std::string& errorMessage = "");
