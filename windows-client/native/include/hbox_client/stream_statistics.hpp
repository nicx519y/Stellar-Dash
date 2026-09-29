#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include "hbox_client/runtime_snapshot.hpp"

namespace hbox {

class StreamStatistics {
 public:
  void reset() noexcept;
  void accepted(std::uint32_t sequence,
                std::chrono::steady_clock::time_point completedAt) noexcept;
  void invalid() noexcept { ++invalid_; }
  void coalesced(std::uint64_t count) noexcept { coalesced_ += count; }
  void copyTo(RuntimeSnapshot& snapshot,
              std::chrono::steady_clock::time_point now) const;

 private:
  static constexpr std::size_t kIntervals = 8192;
  std::array<double, kIntervals> intervalsUs_{};
  std::size_t intervalCount_{};
  std::size_t intervalCursor_{};
  std::uint64_t received_{};
  std::uint64_t dropped_{};
  std::uint64_t invalid_{};
  std::uint64_t coalesced_{};
  bool hasSequence_{};
  std::uint32_t lastSequence_{};
  bool hasCompletion_{};
  std::chrono::steady_clock::time_point lastCompletion_{};
  std::chrono::steady_clock::time_point rateWindowStart_{};
  std::uint64_t rateWindowStartCount_{};
};

}  // namespace hbox
