#include "hbox_client/winusb_transport.hpp"

#include <avrt.h>
#include <setupapi.h>

#include <algorithm>
#include <cstring>
#include <sstream>
#include <vector>

namespace hbox {
namespace {

constexpr GUID kHighRateInterfaceGuid = {
    0x53F2D8A1, 0x6C17, 0x4EB5,
    {0x92, 0xF1, 0x48, 0x42, 0x4F, 0x58, 0x48, 0x31}};

std::string winError(const char* operation) {
  std::ostringstream out;
  out << operation << " failed (Win32 " << GetLastError() << ')';
  return out.str();
}

}  // namespace

WinUsbTransport::~WinUsbTransport() { stop(); }

bool WinUsbTransport::findAndOpen(std::string& error) {
  HDEVINFO devices = SetupDiGetClassDevsW(
      &kHighRateInterfaceGuid, nullptr, nullptr,
      DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (devices == INVALID_HANDLE_VALUE) {
    error = winError("SetupDiGetClassDevsW");
    return false;
  }
  for (DWORD index = 0;; ++index) {
    SP_DEVICE_INTERFACE_DATA interfaceData{sizeof(interfaceData)};
    if (!SetupDiEnumDeviceInterfaces(devices, nullptr,
                                     &kHighRateInterfaceGuid, index,
                                     &interfaceData)) break;
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
    device_ = CreateFileW(detail->DevicePath,
                          GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                          nullptr);
    if (device_ != INVALID_HANDLE_VALUE) break;
  }
  SetupDiDestroyDeviceInfoList(devices);
  if (device_ == INVALID_HANDLE_VALUE) {
    error = "HBox CAFE:4023 WinUSB interface was not found";
    return false;
  }
  if (!WinUsb_Initialize(device_, &interface_)) {
    error = winError("WinUsb_Initialize");
    return false;
  }
  USB_INTERFACE_DESCRIPTOR descriptor{};
  if (!WinUsb_QueryInterfaceSettings(interface_, 0, &descriptor)) {
    error = winError("WinUsb_QueryInterfaceSettings");
    return false;
  }
  for (UCHAR index = 0; index < descriptor.bNumEndpoints; ++index) {
    WINUSB_PIPE_INFORMATION pipe{};
    if (!WinUsb_QueryPipe(interface_, 0, index, &pipe)) continue;
    if (pipe.PipeType != UsbdPipeTypeInterrupt) continue;
    if ((pipe.PipeId & 0x80u) != 0u && pipe.MaximumPacketSize >= 64) {
      inputPipe_ = pipe.PipeId;
    } else if ((pipe.PipeId & 0x80u) == 0u &&
               pipe.MaximumPacketSize >= 32) {
      outputPipe_ = pipe.PipeId;
    }
  }
  if (inputPipe_ == 0 || outputPipe_ == 0) {
    error = "HBox WinUSB interrupt endpoints do not match protocol V1";
    return false;
  }
  UCHAR enabled = TRUE;
  WinUsb_SetPipePolicy(interface_, inputPipe_, RAW_IO,
                       sizeof(enabled), &enabled);
  return true;
}

bool WinUsbTransport::queryLease(std::string& error) {
  hbox_client_control_v1_t status{};
  WINUSB_SETUP_PACKET setup{};
  setup.RequestType = 0xC0u;
  setup.Request = HBOX_CLIENT_STATUS_VENDOR_CODE;
  setup.Value = 0u;
  setup.Index = 0u;
  setup.Length = sizeof(status);
  ULONG transferred = 0;
  OVERLAPPED overlapped{};
  overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!overlapped.hEvent) {
    error = winError("CreateEventW(GET_STATUS)");
    return false;
  }
  BOOL result = WinUsb_ControlTransfer(interface_, setup,
                                       reinterpret_cast<PUCHAR>(&status),
                                       sizeof(status), &transferred,
                                       &overlapped);
  DWORD controlError = result ? ERROR_SUCCESS : GetLastError();
  if (!result && controlError == ERROR_IO_PENDING) {
    const DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 1000);
    if (waitResult == WAIT_OBJECT_0) {
      result = WinUsb_GetOverlappedResult(interface_, &overlapped,
                                          &transferred, FALSE);
      controlError = result ? ERROR_SUCCESS : GetLastError();
    } else {
      controlError = waitResult == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
      CancelIoEx(device_, &overlapped);
      ULONG cancelledBytes = 0;
      (void)WinUsb_GetOverlappedResult(interface_, &overlapped,
                                       &cancelledBytes, TRUE);
    }
  }
  CloseHandle(overlapped.hEvent);
  if (!result) {
    SetLastError(controlError);
    error = winError("WinUsb_ControlTransfer(GET_STATUS)");
    return false;
  }
  if (transferred != sizeof(status) || !validateControl(status) ||
      !std::equal(token_.begin(), token_.end(), status.lease_token)) {
    error = "WinUSB device lease token does not match the acquired HBox";
    return false;
  }
  return true;
}

bool WinUsbTransport::issueRead(ReadContext& context) {
  std::memset(&context.overlapped, 0, sizeof(context.overlapped));
  const BOOL result = WinUsb_ReadPipe(
      interface_, inputPipe_, context.bytes.data(), context.bytes.size(),
      nullptr, &context.overlapped);
  return result || GetLastError() == ERROR_IO_PENDING;
}

bool WinUsbTransport::openAndStart(const LeaseToken& token,
                                   FrameCallback onFrame,
                                   ErrorCallback onError,
                                   std::string& error) {
  stop();
  token_ = token;
  onFrame_ = std::move(onFrame);
  onError_ = std::move(onError);
  if (!findAndOpen(error) || !queryLease(error)) {
    stop();
    return false;
  }
  /*
   * Associate the device only after the synchronous lease query. Otherwise
   * that stack-owned OVERLAPPED can leave a completion in the IOCP which the
   * streaming worker could mistake for one of the persistent read contexts.
   */
  completionPort_ = CreateIoCompletionPort(device_, nullptr, 1, 1);
  if (!completionPort_) {
    error = winError("CreateIoCompletionPort");
    stop();
    return false;
  }
  for (auto& read : reads_) {
    if (!issueRead(read)) {
      error = winError("WinUsb_ReadPipe");
      stop();
      return false;
    }
  }
  running_.store(true);
  queueControl(HBOX_CLIENT_CONTROL_HEARTBEAT);
  worker_ = std::thread(&WinUsbTransport::worker, this);
  return true;
}

void WinUsbTransport::queueControl(std::uint8_t opcode) {
  std::lock_guard lock(controlMutex_);
  queuedControl_ = makeControl(opcode, ++transaction_, token_);
}

void WinUsbTransport::issuePendingControl() {
  if (write_.pending) return;
  {
    std::lock_guard lock(controlMutex_);
    if (!queuedControl_) return;
    write_.packet = *queuedControl_;
    queuedControl_.reset();
  }
  std::memset(&write_.overlapped, 0, sizeof(write_.overlapped));
  const BOOL result = WinUsb_WritePipe(
      interface_, outputPipe_, reinterpret_cast<PUCHAR>(&write_.packet),
      sizeof(write_.packet), nullptr, &write_.overlapped);
  if (result || GetLastError() == ERROR_IO_PENDING) {
    write_.pending = true;
  } else {
    fail(winError("WinUsb_WritePipe"));
  }
}

void WinUsbTransport::worker() {
  DWORD taskIndex = 0;
  HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Games", &taskIndex);
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
  auto nextHeartbeat = std::chrono::steady_clock::now();
  while (running_.load()) {
    if (std::chrono::steady_clock::now() >= nextHeartbeat) {
      queueControl(HBOX_CLIENT_CONTROL_HEARTBEAT);
      nextHeartbeat = std::chrono::steady_clock::now() +
          std::chrono::milliseconds(HBOX_CLIENT_HEARTBEAT_MS);
    }
    issuePendingControl();

    DWORD bytes = 0;
    ULONG_PTR key = 0;
    OVERLAPPED* completed = nullptr;
    const BOOL ok = GetQueuedCompletionStatus(
        completionPort_, &bytes, &key, &completed, 25);
    if (!running_.load()) break;
    if (!completed) {
      if (!ok && GetLastError() != WAIT_TIMEOUT) {
        fail(winError("GetQueuedCompletionStatus"));
      }
      continue;
    }
    if (completed == &write_.overlapped) {
      write_.pending = false;
      if (!ok) fail(winError("WinUSB control write completion"));
      continue;
    }
    auto* read = reinterpret_cast<ReadContext*>(completed);
    if (ok && bytes == HBOX_CLIENT_INPUT_BYTES) {
      onFrame_(read->bytes, std::chrono::steady_clock::now());
    } else if (!ok && GetLastError() != ERROR_OPERATION_ABORTED) {
      fail(winError("WinUSB input completion"));
    }
    if (running_.load() && !issueRead(*read)) {
      fail(winError("WinUsb_ReadPipe(reissue)"));
    }
  }
  if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
}

void WinUsbTransport::fail(const std::string& message) {
  running_.store(false);
  if (onError_) onError_(message);
}

void WinUsbTransport::sendRelease() {
  if (running_.load()) queueControl(HBOX_CLIENT_CONTROL_RELEASE);
}

void WinUsbTransport::stop() noexcept {
  running_.store(false);
  if (device_ != INVALID_HANDLE_VALUE) CancelIoEx(device_, nullptr);
  if (completionPort_) PostQueuedCompletionStatus(completionPort_, 0, 0, nullptr);
  if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
    worker_.join();
  }
  if (interface_) WinUsb_Free(interface_);
  interface_ = nullptr;
  if (completionPort_) CloseHandle(completionPort_);
  completionPort_ = nullptr;
  if (device_ != INVALID_HANDLE_VALUE) CloseHandle(device_);
  device_ = INVALID_HANDLE_VALUE;
  inputPipe_ = 0;
  outputPipe_ = 0;
  write_.pending = false;
  std::lock_guard lock(controlMutex_);
  queuedControl_.reset();
}

}  // namespace hbox
