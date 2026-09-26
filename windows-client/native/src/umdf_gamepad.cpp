#include "hbox_client/umdf_gamepad.hpp"

#include <setupapi.h>

#include <sstream>
#include <vector>

#include "hbox_virtual_gamepad_ioctl.h"

namespace hbox {
namespace {

constexpr GUID kVirtualGamepadInterfaceGuid = {
    0xE54BDA55, 0x57B6, 0x4E32,
    {0xA5, 0x8B, 0x48, 0x42, 0x4F, 0x58, 0x56, 0x31}};

std::string winError(const char* operation) {
  std::ostringstream out;
  out << operation << " failed (Win32 " << GetLastError() << ')';
  return out.str();
}

HANDLE openBackend() {
  HDEVINFO devices = SetupDiGetClassDevsW(
      &kVirtualGamepadInterfaceGuid, nullptr, nullptr,
      DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (devices == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
  HANDLE result = INVALID_HANDLE_VALUE;
  SP_DEVICE_INTERFACE_DATA interfaceData{sizeof(interfaceData)};
  if (SetupDiEnumDeviceInterfaces(devices, nullptr,
                                  &kVirtualGamepadInterfaceGuid, 0,
                                  &interfaceData)) {
    DWORD required = 0;
    SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, nullptr, 0,
                                     &required, nullptr);
    std::vector<std::uint8_t> storage(required);
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
        storage.data());
    detail->cbSize = sizeof(*detail);
    if (SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, detail,
                                         required, nullptr, nullptr)) {
      result = CreateFileW(detail->DevicePath,
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    }
  }
  SetupDiDestroyDeviceInfoList(devices);
  return result;
}

}  // namespace

UmdfGamepad::~UmdfGamepad() { stop(); }

bool UmdfGamepad::start(std::string& error) {
  if (device_ != INVALID_HANDLE_VALUE) return true;
  device_ = openBackend();
  if (device_ == INVALID_HANDLE_VALUE) {
    error = "signed HBox UMDF2 virtual-gamepad driver was not found";
    return false;
  }
  const hbox_gamepad_create_v1_t request{
      HBOX_VIRTUAL_GAMEPAD_MAGIC, HBOX_VIRTUAL_GAMEPAD_VERSION,
      sizeof(hbox_gamepad_create_v1_t)};
  hbox_gamepad_create_result_v1_t response{};
  DWORD received = 0;
  if (!DeviceIoControl(device_, IOCTL_HBOX_GAMEPAD_CREATE,
                       const_cast<hbox_gamepad_create_v1_t*>(&request),
                       sizeof(request), &response, sizeof(response),
                       &received, nullptr) ||
      received != sizeof(response) ||
      response.magic != HBOX_VIRTUAL_GAMEPAD_MAGIC ||
      response.version != HBOX_VIRTUAL_GAMEPAD_VERSION ||
      response.size != sizeof(response)) {
    error = winError("IOCTL_HBOX_GAMEPAD_CREATE");
    stop();
    return false;
  }
  slot_ = static_cast<int>(response.slot);
  return true;
}

bool UmdfGamepad::submit(const XusbState& state,
                         std::uint32_t sourceSequence,
                         std::string& error) {
  if (device_ == INVALID_HANDLE_VALUE) {
    error = "HBox UMDF2 virtual controller is not connected";
    return false;
  }
  const hbox_gamepad_update_v1_t update{
      HBOX_VIRTUAL_GAMEPAD_MAGIC, HBOX_VIRTUAL_GAMEPAD_VERSION,
      sizeof(hbox_gamepad_update_v1_t), sourceSequence, state.buttons,
      state.leftTrigger, state.rightTrigger, state.leftX, state.leftY,
      state.rightX, state.rightY};
  DWORD ignored = 0;
  if (!DeviceIoControl(device_, IOCTL_HBOX_GAMEPAD_UPDATE_STATE,
                       const_cast<hbox_gamepad_update_v1_t*>(&update),
                       sizeof(update), nullptr, 0, &ignored, nullptr)) {
    error = winError("IOCTL_HBOX_GAMEPAD_UPDATE_STATE");
    return false;
  }
  return true;
}

void UmdfGamepad::neutralize() noexcept {
  if (device_ == INVALID_HANDLE_VALUE) return;
  const hbox_gamepad_update_v1_t neutral{
      HBOX_VIRTUAL_GAMEPAD_MAGIC, HBOX_VIRTUAL_GAMEPAD_VERSION,
      sizeof(hbox_gamepad_update_v1_t)};
  DWORD ignored = 0;
  (void)DeviceIoControl(device_, IOCTL_HBOX_GAMEPAD_UPDATE_STATE,
                        const_cast<hbox_gamepad_update_v1_t*>(&neutral),
                        sizeof(neutral), nullptr, 0, &ignored, nullptr);
}

void UmdfGamepad::stop() noexcept {
  neutralize();
  if (device_ != INVALID_HANDLE_VALUE) {
    DWORD ignored = 0;
    (void)DeviceIoControl(device_, IOCTL_HBOX_GAMEPAD_REMOVE,
                          nullptr, 0, nullptr, 0, &ignored, nullptr);
    CloseHandle(device_);
  }
  device_ = INVALID_HANDLE_VALUE;
  slot_ = -1;
}

}  // namespace hbox
