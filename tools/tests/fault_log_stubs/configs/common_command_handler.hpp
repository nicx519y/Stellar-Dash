#pragma once
#include "device_command_message.hpp"
class CommonCommandHandler {
public:
    DeviceCommandResponse handleClearDeviceLogs(const DeviceCommandRequest&);
    DeviceCommandResponse handleGetDeviceLogsPage(const DeviceCommandRequest&);
    DeviceCommandResponse handleGetDeviceLogsList(const DeviceCommandRequest&);
protected:
    DeviceCommandResponse create_error_response(int cid,const std::string &cmd,int error,const std::string &message) {
        return create_device_command_response(cid,cmd,error,nullptr,message);
    }
    DeviceCommandResponse create_success_response(int cid,const std::string &cmd,cJSON *data) {
        return create_device_command_response(cid,cmd,0,data);
    }
};
