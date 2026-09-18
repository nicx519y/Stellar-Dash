#include "hbox_client/hid_control_transport.hpp"

#include <hidsdi.h>
#include <setupapi.h>

#include <array>
#include <cstring>
#include <sstream>
#include <vector>

namespace hbox {
namespace {

std::string winError(const char* operation) {
  std::ostringstream out;
  out << operation << " failed (Win32 " << GetLastError() << ')';
  return out.str();
}

HANDLE findNativeControl(std::string& error) {
  GUID hidGuid{};
  HidD_GetHidGuid(&hidGuid);
  HDEVINFO devices = SetupDiGetClassDevsW(
      &hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (devices == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

  HANDLE result = INVALID_HANDLE_VALUE;
  unsigned acceptedCount = 0;
  for (DWORD index = 0;; ++index) {
    SP_DEVICE_INTERFACE_DATA interfaceData{sizeof(interfaceData)};
    if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &hidGuid,
                                     index, &interfaceData)) break;
    DWORD required = 0;
    SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, nullptr, 0,
                                     &required, nullptr);
    std::vector<std::uint8_t> storage(required);
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(
        storage.data());
    detail->cbSize = sizeof(*detail);
    if (!SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, detail,
                                          required, nullptr, nullptr)) {
      continue;
    }
    HANDLE candidate = CreateFileW(
        detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (candidate == INVALID_HANDLE_VALUE) continue;
    HIDD_ATTRIBUTES attributes{sizeof(attributes)};
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    HIDP_CAPS caps{};
    const bool accepted = HidD_GetAttributes(candidate, &attributes) &&
        attributes.VendorID == HBOX_CLIENT_NATIVE_VID &&
        attributes.ProductID == HBOX_CLIENT_NATIVE_PID &&
        HidD_GetPreparsedData(candidate, &preparsed) &&
        HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS &&
        caps.UsagePage == 0xFF00u && caps.Usage == 0x0001u &&
        caps.FeatureReportByteLength >= HBOX_CLIENT_CONTROL_BYTES + 1u;
    if (preparsed) HidD_FreePreparsedData(preparsed);
    if (accepted) {
      ++acceptedCount;
      if (result == INVALID_HANDLE_VALUE) {
        result = candidate;
      } else {
        CloseHandle(candidate);
      }
      continue;
    }
    CloseHandle(candidate);
  }
  SetupDiDestroyDeviceInfoList(devices);
  if (acceptedCount > 1u) {
    CloseHandle(result);
    error = "multiple native HBox devices found; V1 supports one device";
    return INVALID_HANDLE_VALUE;
  }
  return result;
}

}  // namespace

HidControlTransport::~HidControlTransport() { close(); }

bool HidControlTransport::open(std::string& error) {
  close();
  handle_ = findNativeControl(error);
  if (handle_ == INVALID_HANDLE_VALUE) {
    if (error.empty()) error = "HBox native control HID was not found";
    return false;
  }
  return true;
}

void HidControlTransport::close() noexcept {
  if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
  handle_ = INVALID_HANDLE_VALUE;
}

bool HidControlTransport::exchange(
    const hbox_client_control_v1_t& request,
    hbox_client_control_v1_t& response,
    std::string& error) {
  if (!isOpen()) {
    error = "native control HID is closed";
    return false;
  }
  std::array<std::uint8_t, HBOX_CLIENT_CONTROL_BYTES + 1> feature{};
  std::memcpy(feature.data() + 1, &request, sizeof(request));
  if (!HidD_SetFeature(handle_, feature.data(), feature.size())) {
    error = winError("HidD_SetFeature");
    return false;
  }
  feature.fill(0);
  if (!HidD_GetFeature(handle_, feature.data(), feature.size())) {
    error = winError("HidD_GetFeature");
    return false;
  }
  std::memcpy(&response, feature.data() + 1, sizeof(response));
  if (!validateControl(response) ||
      response.transaction_le != request.transaction_le ||
      response.opcode != request.opcode) {
    error = "invalid or mismatched HBox control response";
    return false;
  }
  return true;
}

}  // namespace hbox
