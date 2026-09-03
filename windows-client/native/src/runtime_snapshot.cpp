#include "hbox_client/runtime_snapshot.hpp"

#include <cstring>
#include <iomanip>
#include <sstream>
#include <type_traits>

namespace hbox {
namespace {
std::string quoted(const std::string& value) {
  std::ostringstream out;
  out << '"';
  for (const unsigned char ch : value) {
    switch (ch) {
      case '\\': out << "\\\\"; break;
      case '"': out << "\\\""; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (ch < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<int>(ch) << std::dec;
        } else {
          out << static_cast<char>(ch);
        }
    }
  }
  out << '"';
  return out.str();
}
}  // namespace

void SnapshotStore::write(const RuntimeSnapshot& snapshot) noexcept {
  std::lock_guard lock(mutex_);
  value_ = snapshot;
}

RuntimeSnapshot SnapshotStore::read() const noexcept {
  std::lock_guard lock(mutex_);
  return value_;
}

void LatestInputStore::write(const InputFrame& input) noexcept {
  static_assert(std::is_trivially_copyable_v<InputFrame>);
  std::array<std::uint64_t, kWordCount> copy{};
  std::memcpy(copy.data(), &input, sizeof(input));
  sequence_.fetch_add(1, std::memory_order_seq_cst);
  for (std::size_t index = 0; index < kWordCount; ++index) {
    words_[index].store(copy[index], std::memory_order_relaxed);
  }
  sequence_.fetch_add(1, std::memory_order_seq_cst);
}

InputFrame LatestInputStore::read() const noexcept {
  std::array<std::uint64_t, kWordCount> words{};
  InputFrame copy{};
  for (;;) {
    const auto before = sequence_.load(std::memory_order_acquire);
    if ((before & 1u) != 0u) continue;
    for (std::size_t index = 0; index < kWordCount; ++index) {
      words[index] = words_[index].load(std::memory_order_relaxed);
    }
    const auto after = sequence_.load(std::memory_order_acquire);
    if (before == after) {
      std::memcpy(&copy, words.data(), sizeof(copy));
      return copy;
    }
  }
}

const char* runtimeModeName(RuntimeMode mode) noexcept {
  switch (mode) {
    case RuntimeMode::Native: return "native";
    case RuntimeMode::Acquiring: return "acquiring";
    case RuntimeMode::Turbo: return "turbo";
    case RuntimeMode::Recovering: return "recovering";
    case RuntimeMode::Fault: return "fault";
  }
  return "fault";
}

std::string snapshotToJson(const RuntimeSnapshot& s) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(2)
      << "{\"schemaVersion\":" << s.schemaVersion
      << ",\"mode\":" << quoted(runtimeModeName(s.mode))
      << ",\"highPerformanceEnabled\":"
      << (s.highPerformanceEnabled ? "true" : "false")
      << ",\"device\":{\"connected\":" << (s.connected ? "true" : "false")
      << ",\"usbSpeed\":" << quoted(s.usbSpeed)
      << ",\"firmwareVersion\":"
      << (s.firmwareVersion.empty() ? "null" : quoted(s.firmwareVersion)) << "}"
      << ",\"stream\":{\"configuredHz\":" << s.configuredHz
      << ",\"measuredHz\":" << s.measuredHz
      << ",\"received\":" << s.received
      << ",\"dropped\":" << s.dropped
      << ",\"invalid\":" << s.invalid
      << ",\"coalesced\":" << s.coalesced
      << ",\"lastSequence\":"
      << (s.hasSequence ? std::to_string(s.lastSequence) : "null")
      << ",\"intervalUs\":{\"p50\":" << s.intervalUs.p50
      << ",\"p95\":" << s.intervalUs.p95
      << ",\"p99\":" << s.intervalUs.p99
      << ",\"max\":" << s.intervalUs.maximum << "}}"
      << ",\"virtualPad\":{\"backend\":" << quoted(s.virtualBackend)
      << ",\"connected\":" << (s.virtualConnected ? "true" : "false")
      << ",\"slot\":" << (s.virtualSlot < 0 ? "null" : std::to_string(s.virtualSlot)) << "}"
      << ",\"input\":{\"actionMask\":" << s.actionMask
      << ",\"buttons\":" << s.input.buttons
      << ",\"lt\":" << static_cast<unsigned>(s.input.leftTrigger)
      << ",\"rt\":" << static_cast<unsigned>(s.input.rightTrigger)
      << ",\"lx\":" << s.input.leftX << ",\"ly\":" << s.input.leftY
      << ",\"rx\":" << s.input.rightX << ",\"ry\":" << s.input.rightY << "}"
      << ",\"lastError\":";
  if (s.errorCode.empty()) {
    out << "null";
  } else {
    out << "{\"code\":" << quoted(s.errorCode)
        << ",\"message\":" << quoted(s.errorMessage) << "}";
  }
  out << "}";
  return out.str();
}

}  // namespace hbox
