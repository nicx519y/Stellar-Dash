#ifndef WEBSOCKET_COMMAND_HANDLER_HPP
#define WEBSOCKET_COMMAND_HANDLER_HPP

#include "configs/websocket_message.hpp"
#include "config.hpp"
#include "cJSON.h"
#include <string>
#include <map>

// WebSocket命令处理器基类
class WebSocketCommandHandler {
public:
    static bool needReboot;
    static uint32_t rebootTick;
    virtual ~WebSocketCommandHandler() = default;
    virtual WebSocketDownstreamMessage handle(const WebSocketUpstreamMessage& request) = 0;
    
protected:
    // 通用辅助函数
    WebSocketDownstreamMessage create_error_response(int cid, const std::string& command, 
                                                   int errNo, const std::string& errorMessage);
    WebSocketDownstreamMessage create_success_response(int cid, const std::string& command, 
                                                     cJSON* data = nullptr);
};

// 全局配置命令处理器
class GlobalConfigCommandHandler : public WebSocketCommandHandler {
public:
    static GlobalConfigCommandHandler& getInstance();
    
    // 全局配置相关命令
    WebSocketDownstreamMessage handleGetGlobalConfig(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleUpdateGlobalConfig(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleGetHotkeysConfig(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleUpdateHotkeysConfig(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleReboot(const WebSocketUpstreamMessage& request);
    
    // LED配置相关命令
    WebSocketDownstreamMessage handlePushLedsConfig(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleClearLedsPreview(const WebSocketUpstreamMessage& request);
    
    // WebSocketCommandHandler接口实现
    WebSocketDownstreamMessage handle(const WebSocketUpstreamMessage& request) override;

private:
    GlobalConfigCommandHandler() = default;
    
    // 辅助函数
    cJSON* buildHotkeysConfigJSON(Config& config);
    
    // 映射表
    static const std::map<InputMode, const char*> INPUT_MODE_STRINGS;
    static const std::map<std::string, InputMode> STRING_TO_INPUT_MODE;
    static const std::map<std::string, GamepadHotkey> STRING_TO_GAMEPAD_HOTKEY;
    static const std::map<GamepadHotkey, const char*> GAMEPAD_HOTKEY_TO_STRING;
};

// 配置文件命令处理器
class ProfileCommandHandler : public WebSocketCommandHandler {
public:
    static ProfileCommandHandler& getInstance();
    
    // 配置文件相关命令
    WebSocketDownstreamMessage handleGetProfileList(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleGetDefaultProfile(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleUpdateProfile(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleCreateProfile(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleDeleteProfile(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleSwitchDefaultProfile(const WebSocketUpstreamMessage& request);
    
    // WebSocketCommandHandler接口实现
    WebSocketDownstreamMessage handle(const WebSocketUpstreamMessage& request) override;

private:
    ProfileCommandHandler() = default;
    
    // 辅助函数
    cJSON* buildKeyMappingJSON(uint32_t virtualMask);
    uint32_t getKeyMappingVirtualMask(cJSON* keyMappingJSON);
    cJSON* buildProfileListJSON();
    cJSON* buildProfileJSON(GamepadProfile* profile);
};

// 轴体映射和标记命令处理器
class MSMarkCommandHandler : public WebSocketCommandHandler {
public:
    static MSMarkCommandHandler& getInstance();
    
    // 轴体映射相关命令
    WebSocketDownstreamMessage handleGetList(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleGetMarkStatus(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleSetDefault(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleGetDefault(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleCreateMapping(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleDeleteMapping(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleRenameMapping(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleMarkMappingStart(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleMarkMappingStop(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleMarkMappingStep(const WebSocketUpstreamMessage& request);
    WebSocketDownstreamMessage handleGetMapping(const WebSocketUpstreamMessage& request);
    
    // WebSocketCommandHandler接口实现
    WebSocketDownstreamMessage handle(const WebSocketUpstreamMessage& request) override;

private:
    MSMarkCommandHandler() = default;
    
    // 辅助函数
    cJSON* buildMappingListJSON();
};

// WebSocket命令处理器管理器
class WebSocketCommandManager {
public:
    static WebSocketCommandManager& getInstance();
    
    // 注册和处理命令
    void registerHandler(const std::string& command, WebSocketCommandHandler* handler);
    WebSocketDownstreamMessage processCommand(const WebSocketUpstreamMessage& request);
    
    // 初始化所有命令处理器
    void initializeHandlers();

private:
    WebSocketCommandManager() = default;
    std::map<std::string, WebSocketCommandHandler*> handlers;
};

#endif // WEBSOCKET_COMMAND_HANDLER_HPP 