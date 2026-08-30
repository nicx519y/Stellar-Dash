#ifndef HBOX_DEVICE_COMMAND_HANDLER_HPP
#define HBOX_DEVICE_COMMAND_HANDLER_HPP

#include "configs/device_command_message.hpp"
#include "config.hpp"
#include "cJSON.h"
#include <string>
#include <map>

// 前向声明
class FirmwareCommandHandler;
class CommonCommandHandler;

// DeviceCommand命令处理器基类
class DeviceCommandHandler {
public:
    static bool needReboot;
    static uint32_t rebootTick;
    virtual ~DeviceCommandHandler() = default;
    virtual DeviceCommandResponse handle(const DeviceCommandRequest& request) = 0;

protected:
    // 通用辅助函数
    DeviceCommandResponse create_error_response(int cid, const std::string& command,
                                                   int errNo, const std::string& errorMessage);
    DeviceCommandResponse create_success_response(int cid, const std::string& command,
                                                     cJSON* data = nullptr);
};

// 全局配置命令处理器
class GlobalConfigCommandHandler : public DeviceCommandHandler {
public:
    static GlobalConfigCommandHandler& getInstance();

    // 全局配置相关命令
    DeviceCommandResponse handleGetGlobalConfig(const DeviceCommandRequest& request);
    DeviceCommandResponse handleUpdateGlobalConfig(const DeviceCommandRequest& request);
    DeviceCommandResponse handleGetHotkeysConfig(const DeviceCommandRequest& request);
    DeviceCommandResponse handleUpdateHotkeysConfig(const DeviceCommandRequest& request);
    DeviceCommandResponse handleGetScreenControlConfig(const DeviceCommandRequest& request);
    DeviceCommandResponse handleUpdateScreenControlConfig(const DeviceCommandRequest& request);
    DeviceCommandResponse handleExportAllConfig(const DeviceCommandRequest& request);
    DeviceCommandResponse handleImportAllConfig(const DeviceCommandRequest& request);
    DeviceCommandResponse handleImportConfigBegin(const DeviceCommandRequest& request);
    DeviceCommandResponse handleImportConfigPart(const DeviceCommandRequest& request);
    DeviceCommandResponse handleImportConfigFinish(const DeviceCommandRequest& request);
    DeviceCommandResponse handleImportConfigAbort(const DeviceCommandRequest& request);
    DeviceCommandResponse handleReboot(const DeviceCommandRequest& request);

    // LED配置相关命令
    DeviceCommandResponse handlePushLedsConfig(const DeviceCommandRequest& request);
    DeviceCommandResponse handleClearLedsPreview(const DeviceCommandRequest& request);

    // DeviceCommandHandler接口实现
    DeviceCommandResponse handle(const DeviceCommandRequest& request) override;

private:
    GlobalConfigCommandHandler() = default;

    // 辅助函数
    // cJSON* buildGlobalConfigJSON(Config& config);
    // cJSON* buildHotkeysConfigJSON(Config& config);

    // 映射表
    // static const std::map<InputMode, const char*> INPUT_MODE_STRINGS;
    // static const std::map<std::string, InputMode> STRING_TO_INPUT_MODE;
    // static const std::map<std::string, GamepadHotkey> STRING_TO_GAMEPAD_HOTKEY;
    // static const std::map<GamepadHotkey, const char*> GAMEPAD_HOTKEY_TO_STRING;
};

// 配置文件命令处理器
class ProfileCommandHandler : public DeviceCommandHandler {
public:
    static ProfileCommandHandler& getInstance();

    // 静态辅助函数，供其他处理器调用
    static cJSON* buildKeyMappingJSON(uint32_t virtualMask);
    static cJSON* buildKeyCombinationJSON(KeyCombination* keyCombination);
    static uint32_t getKeyMappingVirtualMask(cJSON* keyMappingJSON);
    static KeyCombination getKeyCombination(cJSON* keyCombinationJSON);
    static cJSON* buildProfileJSON(GamepadProfile* profile);
    static cJSON* buildProfileExportJSON(GamepadProfile* profile);
    static void parseProfileJSON(cJSON* profileJSON, GamepadProfile* targetProfile);

    // 配置文件相关命令
    DeviceCommandResponse handleGetProfileList(const DeviceCommandRequest& request);
    DeviceCommandResponse handleGetDefaultProfile(const DeviceCommandRequest& request);
    DeviceCommandResponse handleGetProfileDetails(const DeviceCommandRequest& request);
    DeviceCommandResponse handleUpdateProfile(const DeviceCommandRequest& request);
    DeviceCommandResponse handleGetMacro(const DeviceCommandRequest& request);
    DeviceCommandResponse handleUpdateMacro(const DeviceCommandRequest& request);
    DeviceCommandResponse handleGetProfileMacros(const DeviceCommandRequest& request);
    DeviceCommandResponse handleUpdateProfileMacros(const DeviceCommandRequest& request);
    DeviceCommandResponse handleCreateProfile(const DeviceCommandRequest& request);
    DeviceCommandResponse handleDeleteProfile(const DeviceCommandRequest& request);
    DeviceCommandResponse handleSwitchDefaultProfile(const DeviceCommandRequest& request);

    // DeviceCommandHandler接口实现
    DeviceCommandResponse handle(const DeviceCommandRequest& request) override;

private:
    ProfileCommandHandler() = default;

    // 辅助函数
    cJSON* buildProfileListJSON();
};

// 轴体映射和标记命令处理器
class MSMarkCommandHandler : public DeviceCommandHandler {
public:
    static MSMarkCommandHandler& getInstance();

    // 轴体映射相关命令
    DeviceCommandResponse handleGetList(const DeviceCommandRequest& request);
    DeviceCommandResponse handleGetMarkStatus(const DeviceCommandRequest& request);
    DeviceCommandResponse handleSetDefault(const DeviceCommandRequest& request);
    DeviceCommandResponse handleGetDefault(const DeviceCommandRequest& request);
    DeviceCommandResponse handleCreateMapping(const DeviceCommandRequest& request);
    DeviceCommandResponse handleDeleteMapping(const DeviceCommandRequest& request);
    DeviceCommandResponse handleRenameMapping(const DeviceCommandRequest& request);
    DeviceCommandResponse handleMarkMappingStart(const DeviceCommandRequest& request);
    DeviceCommandResponse handleMarkMappingStop(const DeviceCommandRequest& request);
    DeviceCommandResponse handleMarkMappingStep(const DeviceCommandRequest& request);
    DeviceCommandResponse handleMarkMappingSync(const DeviceCommandRequest& request);
    DeviceCommandResponse handleGetMapping(const DeviceCommandRequest& request);
    DeviceCommandResponse handleGetConfigBackup(const DeviceCommandRequest& request);
    DeviceCommandResponse handleInstallMapping(const DeviceCommandRequest& request);
    DeviceCommandResponse handleClearInstalledMapping(const DeviceCommandRequest& request);
    DeviceCommandResponse handleDraftBegin(const DeviceCommandRequest& request);
    DeviceCommandResponse handleDraftGet(const DeviceCommandRequest& request);

    // DeviceCommandHandler接口实现
    DeviceCommandResponse handle(const DeviceCommandRequest& request) override;

    // 发送标记状态变化通知
    void sendMarkingStatusNotification();

private:
    MSMarkCommandHandler() = default;

    // 辅助函数
    cJSON* buildMappingListJSON();
};

// 校准和按键监控命令处理器前向声明
class CalibrationCommandHandler;

// DeviceCommand命令处理器管理器
class DeviceCommandDispatcher {
public:
    static DeviceCommandDispatcher& getInstance();

    // 注册和处理命令
    void registerHandler(const std::string& command, DeviceCommandHandler* handler);
    DeviceCommandResponse processCommand(const DeviceCommandRequest& request);

    // 初始化所有命令处理器
    void initializeHandlers();

private:
    DeviceCommandDispatcher() = default;
    std::map<std::string, DeviceCommandHandler*> handlers;
};

#endif // HBOX_DEVICE_COMMAND_HANDLER_HPP
