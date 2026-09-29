#include "hbox_client/stream_statistics.hpp"

#include <algorithm>
#include <vector>

namespace hbox {

void StreamStatistics::reset() noexcept { *this = {}; }

void StreamStatistics::accepted(
    std::uint32_t sequence,
    std::chrono::steady_clock::time_point completedAt) noexcept {
  if (hasSequence_) {
    const std::uint32_t distance = sequence - lastSequence_;
    if (distance > 1u && distance < 0x80000000u) dropped_ += distance - 1u;
  }
  lastSequence_ = sequence;
  hasSequence_ = true;
  if (hasCompletion_) {
    const auto interval = std::chrono::duration<double, std::micro>(
        completedAt - lastCompletion_).count();
    intervalsUs_[intervalCursor_] = interval;
    intervalCursor_ = (intervalCursor_ + 1u) % intervalsUs_.size();
    intervalCount_ = std::min(intervalCount_ + 1u, intervalsUs_.size());
  } else {
    hasCompletion_ = true;
    rateWindowStart_ = completedAt;
  }
  lastCompletion_ = completedAt;
  ++received_;
}

void StreamStatistics::copyTo(
    RuntimeSnapshot& snapshot,
    std::chrono::steady_clock::time_point now) const {
  snapshot.received = received_;
  snapshot.dropped = dropped_;
  snapshot.invalid = invalid_;
  snapshot.coalesced = coalesced_;
  snapshot.hasSequence = hasSequence_;
  snapshot.lastSequence = lastSequence_;
  if (hasCompletion_) {
    const double seconds = std::chrono::duration<double>(
        now - rateWindowStart_).count();
    if (seconds > 0.0) snapshot.measuredHz =
        static_cast<double>(received_ - rateWindowStartCount_) / seconds;
  }
  if (intervalCount_ == 0u) return;
  std::vector<double> values;
  values.reserve(intervalCount_);
  for (std::size_t index = 0; index < intervalCount_; ++index) {
    values.push_back(intervalsUs_[index]);
  }
  std::sort(values.begin(), values.end());
  const auto percentile = [&](double value) {
    const auto index = static_cast<std::size_t>(
        value * static_cast<double>(values.size() - 1u));
    return values[index];
  };
  snapshot.intervalUs = {
      percentile(0.50), percentile(0.95), percentile(0.99), values.back()};
}

}  // namespace hbox
