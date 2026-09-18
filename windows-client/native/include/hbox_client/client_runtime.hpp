#pragma once

#ifdef _WIN32

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "hbox_client/hid_control_transport.hpp"
#include "hbox_client/runtime_snapshot.hpp"
#include "hbox_client/spsc_ring.hpp"
#include "hbox_client/virtual_gamepad.hpp"
#include "hbox_client/winusb_transport.hpp"

namespace hbox {

class ClientRuntime {
 public:
  ClientRuntime();
  ~ClientRuntime();
  ClientRuntime(const ClientRuntime&) = delete;
  ClientRuntime& operator=(const ClientRuntime&) = delete;

  void start();
  void stop();
  void notifyDevicesChanged();
  void prepareForSuspend();
  void resumeFromSuspend();
  void setHighPerformanceEnabled(bool enabled);
  bool highPerformanceEnabled() const noexcept;
  RuntimeSnapshot snapshot() const;
  std::string snapshotJson() const;

 private:
  void controlLoop();
  void injectionLoop();
  void snapshotLoop();
  void handleUsbFrame(std::span<const std::uint8_t> bytes,
                      std::chrono::steady_clock::time_point completedAt);
  void setMode(RuntimeMode mode, std::string code = {},
               std::string message = {});
  bool startVirtualGamepad(std::string& error);
  void stopVirtualGamepad();
  void stopTurbo(bool requestRelease);
  void loadSettings();
  void saveSettings();

  std::unique_ptr<IVirtualGamepad> virtualGamepad_;
  WinUsbTransport winusb_;
  SpscRing<InputFrame, 32768> inputQueue_;
  LatestInputStore overflowLatest_;
  std::atomic<bool> overflowPending_{false};
  LeaseToken token_{};
  std::atomic<bool> running_{false};
  std::atomic<bool> highPerformanceEnabled_{true};
  std::atomic<bool> deviceChangePending_{true};
  std::atomic<bool> suspended_{false};
  std::thread controlThread_;
  std::thread injectionThread_;
  std::thread snapshotThread_;
  mutable std::mutex controlMutex_;
  std::condition_variable controlWake_;
  std::condition_variable injectionWake_;
  enum class GamepadCommand { None, Start, Stop };
  std::mutex gamepadCommandMutex_;
  std::condition_variable gamepadCommandDone_;
  GamepadCommand gamepadCommand_{GamepadCommand::None};
  bool gamepadCommandFinished_{};
  bool gamepadCommandResult_{};
  std::string gamepadCommandError_;

  mutable std::mutex stateMutex_;
  RuntimeSnapshot state_{};
  SnapshotStore published_{};

  std::atomic<std::uint64_t> received_{0};
  std::atomic<std::uint64_t> dropped_{0};
  std::atomic<std::uint64_t> invalid_{0};
  std::atomic<std::uint64_t> coalesced_{0};
  std::atomic<std::uint32_t> lastSequence_{0};
  std::atomic<bool> hasSequence_{false};
  std::atomic<std::uint64_t> lastCompletionNs_{0};
  std::array<std::atomic<std::uint32_t>, 8192> intervalsNs_{};
  std::atomic<std::uint64_t> intervalCursor_{0};
  std::atomic<std::uint16_t> configuredHz_{1000};
  std::array<std::atomic<std::int32_t>, 8> currentInput_{};
  std::chrono::steady_clock::time_point acquireStarted_{};
};

}  // namespace hbox

#endif
