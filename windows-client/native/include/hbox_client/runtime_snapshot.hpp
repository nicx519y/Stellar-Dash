#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include "hbox_client/protocol.hpp"

namespace hbox {

enum class RuntimeMode { Native, Acquiring, Turbo, Recovering, Fault };

struct IntervalSummary {
  double p50{};
  double p95{};
  double p99{};
  double maximum{};
};

struct RuntimeSnapshot {
  std::uint32_t schemaVersion{1};
  RuntimeMode mode{RuntimeMode::Native};
  bool connected{};
  std::string usbSpeed{"NONE"};
  std::string firmwareVersion{};
  std::uint16_t configuredHz{1000};
  double measuredHz{};
  std::uint64_t received{};
  std::uint64_t dropped{};
  std::uint64_t invalid{};
  std::uint64_t coalesced{};
  bool hasSequence{};
  std::uint32_t lastSequence{};
  IntervalSummary intervalUs{};
  std::string virtualBackend{"vigem"};
  bool virtualConnected{};
  int virtualSlot{-1};
  std::uint32_t actionMask{};
  XusbState input{};
  std::string errorCode{};
  std::string errorMessage{};
  bool highPerformanceEnabled{true};
};

class SnapshotStore {
 public:
  void write(const RuntimeSnapshot& snapshot) noexcept;
  RuntimeSnapshot read() const noexcept;

 private:
  mutable std::mutex mutex_;
  RuntimeSnapshot value_{};
};

class LatestInputStore {
 public:
  void write(const InputFrame& input) noexcept;
  InputFrame read() const noexcept;

 private:
  static constexpr std::size_t kWordCount =
      (sizeof(InputFrame) + sizeof(std::uint64_t) - 1u) /
      sizeof(std::uint64_t);
  mutable std::atomic<std::uint64_t> sequence_{0};
  std::array<std::atomic<std::uint64_t>, kWordCount> words_{};
};

std::string snapshotToJson(const RuntimeSnapshot& snapshot);
const char* runtimeModeName(RuntimeMode mode) noexcept;

}  // namespace hbox
