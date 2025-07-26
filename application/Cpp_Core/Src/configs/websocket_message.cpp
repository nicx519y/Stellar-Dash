#include "configs/websocket_message.hpp"
#include "system_logger.h"
#include <cstdlib>
#include "board_cfg.h"
#include "cpp_utils.hpp"

// ==================== WebSocketUpstreamMessage 实现 ====================

WebSocketUpstreamMessage::WebSocketUpstreamMessage() 
    : cid_(0), params_(nullptr), connection_(nullptr) {
}

WebSocketUpstreamMessage::~WebSocketUpstreamMessage() {
    if (params_) {
        cJSON_Delete(params_);
        params_ = nullptr;
    }
}

WebSocketUpstreamMessage::WebSocketUpstreamMessage(WebSocketUpstreamMessage&& other) noexcept 
    : cid_(other.cid_), command_(std::move(other.command_)), 
      params_(other.params_), connection_(other.connection_) {
    other.params_ = nullptr;
    other.connection_ = nullptr;
}

WebSocketUpstreamMessage& WebSocketUpstreamMessage::operator=(WebSocketUpstreamMessage&& other) noexcept {
    if (this != &other) {
        if (params_) {
            cJSON_Delete(params_);
        }
        cid_ = other.cid_;
        command_ = std::move(other.command_);
        params_ = other.params_;
        connection_ = other.connection_;
        other.params_ = nullptr;
        other.connection_ = nullptr;
    }
    return *this;
}

void WebSocketUpstreamMessage::setParams(cJSON* params) {
    if (params_) {
        cJSON_Delete(params_);
    }
    params_ = params;
}

// ==================== WebSocketDownstreamMessage 实现 ====================

WebSocketDownstreamMessage::WebSocketDownstreamMessage() 
    : cid_(0), errNo_(0), data_(nullptr) {
}

WebSocketDownstreamMessage::~WebSocketDownstreamMessage() {
    if (data_) {
        cJSON_Delete(data_);
        data_ = nullptr;
    }
}

WebSocketDownstreamMessage::WebSocketDownstreamMessage(WebSocketDownstreamMessage&& other) noexcept
    : cid_(other.cid_), command_(std::move(other.command_)), 
      errNo_(other.errNo_), data_(other.data_) {
    other.data_ = nullptr;
}

WebSocketDownstreamMessage& WebSocketDownstreamMessage::operator=(WebSocketDownstreamMessage&& other) noexcept {
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

void WebSocketDownstreamMessage::setData(cJSON* data) {
    if (data_) {
        cJSON_Delete(data_);
    }
    data_ = data;
}

// ==================== 工具函数 ====================

WebSocketDownstreamMessage create_websocket_response(uint32_t cid, const std::string& command, int errNo, cJSON* data, const std::string& errorMessage) {
    APP_DBG("create_websocket_response: cid=%d, command=%s, errNo=%d", cid, command.c_str(), errNo);
    WebSocketDownstreamMessage response;
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



void send_websocket_response(WebSocketConnection* conn, WebSocketDownstreamMessage&& response) {
    if (!conn) {
        LOG_ERROR("WebSocket", "Invalid connection for sending response");
        return;
    }
    
    cJSON* json = cJSON_CreateObject();
    if (!json) {
        LOG_ERROR("WebSocket", "Failed to create JSON response object");
        return;
    }
    
    cJSON_AddNumberToObject(json, "cid", response.getCid());
    cJSON_AddStringToObject(json, "command", response.getCommand().c_str());
    cJSON_AddNumberToObject(json, "errNo", response.getErrNo());
    
    if (response.getData()) {
        cJSON_AddItemToObject(json, "data", cJSON_Duplicate(response.getData(), 1));
    } else {
        cJSON_AddItemToObject(json, "data", cJSON_CreateObject());
    }
    
    char* json_string = cJSON_PrintUnformatted(json);

    APP_DBG("send_websocket_response: %s", json_string);

    if (json_string) {
        // 检查并修复UTF-8编码
        std::string json_str(json_string);
        if (!is_valid_utf8(json_str.c_str())) {
            LOG_WARN("WebSocket", "Invalid UTF-8 detected in JSON, attempting to fix");
            json_str = fix_utf8_string(json_str);
            if (!is_valid_utf8(json_str.c_str())) {
                LOG_ERROR("WebSocket", "Failed to fix UTF-8 encoding, sending as-is");
            }
        }
        
        conn->send_text(json_str);
        free(json_string);
    } else {
        LOG_ERROR("WebSocket", "Failed to serialize JSON response");
    }
    
    cJSON_Delete(json);
} 