#pragma once

#ifdef _WIN32

#include <windows.h>
#include <winusb.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "hbox_client/protocol.hpp"

namespace hbox {

class WinUsbTransport {
 public:
  using FrameCallback = std::function<void(
      std::span<const std::uint8_t>, std::chrono::steady_clock::time_point)>;
  using ErrorCallback = std::function<void(const std::string&)>;

  WinUsbTransport() = default;
  ~WinUsbTransport();
  WinUsbTransport(const WinUsbTransport&) = delete;
  WinUsbTransport& operator=(const WinUsbTransport&) = delete;

  bool openAndStart(const LeaseToken& token,
                    FrameCallback onFrame,
                    ErrorCallback onError,
                    std::string& error);
  void sendRelease();
  void stop() noexcept;
  bool running() const noexcept { return running_.load(); }

 private:
  struct ReadContext {
    OVERLAPPED overlapped{};
    std::array<std::uint8_t, HBOX_CLIENT_INPUT_BYTES> bytes{};
  };
  struct WriteContext {
    OVERLAPPED overlapped{};
    hbox_client_control_v1_t packet{};
    bool pending{};
  };

  bool findAndOpen(std::string& error);
  bool queryLease(std::string& error);
  bool issueRead(ReadContext& context);
  void queueControl(std::uint8_t opcode);
  void issuePendingControl();
  void worker();
  void fail(const std::string& message);

  HANDLE device_{INVALID_HANDLE_VALUE};
  HANDLE completionPort_{nullptr};
  WINUSB_INTERFACE_HANDLE interface_{nullptr};
  UCHAR inputPipe_{};
  UCHAR outputPipe_{};
  std::array<ReadContext, 16> reads_{};
  WriteContext write_{};
  LeaseToken token_{};
  std::uint32_t transaction_{};
  std::atomic<bool> running_{false};
  std::thread worker_;
  FrameCallback onFrame_;
  ErrorCallback onError_;
  std::mutex controlMutex_;
  std::optional<hbox_client_control_v1_t> queuedControl_;
};

}  // namespace hbox

#endif
