#include "configs/device_command_message.hpp"
#include "board_cfg.h"

// ==================== DeviceCommandRequest 实现 ====================

DeviceCommandRequest::DeviceCommandRequest()
    : cid_(0), params_(nullptr) {
}

DeviceCommandRequest::~DeviceCommandRequest() {
    if (params_) {
        cJSON_Delete(params_);
        params_ = nullptr;
    }
}

DeviceCommandRequest::DeviceCommandRequest(DeviceCommandRequest&& other) noexcept
    : cid_(other.cid_), command_(std::move(other.command_)),
      params_(other.params_) {
    other.params_ = nullptr;
}

DeviceCommandRequest& DeviceCommandRequest::operator=(DeviceCommandRequest&& other) noexcept {
    if (this != &other) {
        if (params_) {
            cJSON_Delete(params_);
        }
        cid_ = other.cid_;
        command_ = std::move(other.command_);
        params_ = other.params_;
        other.params_ = nullptr;
    }
    return *this;
}

void DeviceCommandRequest::setParams(cJSON* params) {
    if (params_) {
        cJSON_Delete(params_);
    }
    params_ = params;
}

// ==================== DeviceCommandResponse 实现 ====================

DeviceCommandResponse::DeviceCommandResponse()
    : cid_(0), errNo_(0), data_(nullptr) {
}

DeviceCommandResponse::~DeviceCommandResponse() {
    if (data_) {
        cJSON_Delete(data_);
        data_ = nullptr;
    }
}

DeviceCommandResponse::DeviceCommandResponse(DeviceCommandResponse&& other) noexcept
    : cid_(other.cid_), command_(std::move(other.command_)),
      errNo_(other.errNo_), data_(other.data_) {
    other.data_ = nullptr;
}

DeviceCommandResponse& DeviceCommandResponse::operator=(DeviceCommandResponse&& other) noexcept {
    if (this != &other) {
        if (data_) {
            cJSON_Delete(data_);
        }
        cid_ = other.cid_;
        command_ = std::move(other.command_);
        errNo_ = other.errNo_;
        data_ = other.data_;
        other.data_ = nullptr;
    }
    return *this;
}

void DeviceCommandResponse::setData(cJSON* data) {
    if (data_) {
        cJSON_Delete(data_);
    }
    data_ = data;
}

cJSON* DeviceCommandResponse::releaseData() {
    cJSON* data = data_;
    data_ = nullptr;
    return data;
}

// ==================== 工具函数 ====================

DeviceCommandResponse create_device_command_response(uint32_t cid, const std::string& command, int errNo, cJSON* data, const std::string& errorMessage) {
    APP_DBG("create_device_command_response: cid=%d, command=%s, errNo=%d", cid, command.c_str(), errNo);
    DeviceCommandResponse response;
    response.setCid(cid);
    response.setCommand(command);
    response.setErrNo(errNo);

    cJSON* responseData;
    if (data) {
        responseData = data;
    } else {
        responseData = cJSON_CreateObject();
    }

    if (!errorMessage.empty() && responseData) {
        cJSON_AddStringToObject(responseData, "errorMessage", errorMessage.c_str());
    }

    response.setData(responseData);


    return response;
}
