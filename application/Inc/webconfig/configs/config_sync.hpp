#ifndef XORA_CONFIG_SYNC_HPP
#define XORA_CONFIG_SYNC_HPP

#include "configs/device_command_message.hpp"

// Hash only the resource body, never the RPC envelope or C++ padding.
bool configResourceDigest(const cJSON* value, char output[65]);
bool addConfigResponseVersions(const DeviceCommandRequest& request, cJSON* data);
DeviceCommandResponse getConfigManifest(const DeviceCommandRequest& request);

#endif
