#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace hbox {

template <typename T, std::size_t Capacity>
class SpscRing {
  static_assert(Capacity >= 2);
  static_assert(std::is_trivially_copyable_v<T>);

 public:
  bool push(const T& value) noexcept {
    const auto head = head_.load(std::memory_order_relaxed);
    const auto next = increment(head);
    if (next == tail_.load(std::memory_order_acquire)) return false;
    entries_[head] = value;
    head_.store(next, std::memory_order_release);
    return true;
  }

  std::optional<T> pop() noexcept {
    const auto tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return std::nullopt;
    T value = entries_[tail];
    tail_.store(increment(tail), std::memory_order_release);
    return value;
  }

  std::optional<T> popLatest(std::uint64_t& coalesced) noexcept {
    auto latest = pop();
    if (!latest) return std::nullopt;
    while (auto next = pop()) {
      latest = next;
      ++coalesced;
    }
    return latest;
  }

  void clear() noexcept {
    tail_.store(head_.load(std::memory_order_acquire),
                std::memory_order_release);
  }

 private:
  static constexpr std::size_t increment(std::size_t value) noexcept {
    return (value + 1) % Capacity;
  }

  alignas(64) std::array<T, Capacity> entries_{};
  alignas(64) std::atomic<std::size_t> head_{0};
  alignas(64) std::atomic<std::size_t> tail_{0};
};

}  // namespace hbox
