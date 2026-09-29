#pragma once
#include "configs/device_command_handler.hpp"
class RfBindingCommandHandler : public DeviceCommandHandler {
public:
    static RfBindingCommandHandler& getInstance(){static RfBindingCommandHandler h;return h;}
    DeviceCommandResponse handle(const DeviceCommandRequest& request) override;
};
