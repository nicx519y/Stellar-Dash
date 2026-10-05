#pragma once
#include "configs/device_command_handler.hpp"
class ResourceCommandHandler : public DeviceCommandHandler {
public:
  static ResourceCommandHandler &instance() {
    static ResourceCommandHandler h;
    return h;
  }
  DeviceCommandResponse handle(const DeviceCommandRequest &) override;
};
