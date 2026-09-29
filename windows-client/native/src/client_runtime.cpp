#include "hbox_client/client_runtime.hpp"

#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#if defined(HBOX_INTERNAL_VIGEM_MVP)
#include "hbox_client/vigem_gamepad.hpp"
#else
#include "hbox_client/umdf_gamepad.hpp"
#endif

namespace hbox {
namespace {

std::filesystem::path settingsPath() {
  PWSTR raw = nullptr;
  std::filesystem::path result;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
    result = std::filesystem::path(raw) / L"HBox Client" / L"settings.json";
  }
  CoTaskMemFree(raw);
  return result;
}

LeaseToken randomToken() {
  GUID guid{};
  LeaseToken token{};
  if (SUCCEEDED(CoCreateGuid(&guid))) {
    std::memcpy(token.data(), &guid, token.size());
  } else {
    const auto now = std::chrono::high_resolution_clock::now()
                         .time_since_epoch().count();
    std::memcpy(token.data(), &now, std::min(sizeof(now), token.size()));
    token.back() = 1u;
  }
  return token;
}

}  // namespace

ClientRuntime::ClientRuntime()
#if defined(HBOX_INTERNAL_VIGEM_MVP)
    : virtualGamepad_(std::make_unique<VigemGamepad>()) {
#else
    : virtualGamepad_(std::make_unique<UmdfGamepad>()) {
#endif
  state_.virtualBackend = virtualGamepad_->backendName();
}

ClientRuntime::~ClientRuntime() { stop(); }

void ClientRuntime::start() {
  if (running_.exchange(true)) return;
  loadSettings();
  deviceChangePending_.store(true);
  injectionThread_ = std::thread(&ClientRuntime::injectionLoop, this);
  snapshotThread_ = std::thread(&ClientRuntime::snapshotLoop, this);
  controlThread_ = std::thread(&ClientRuntime::controlLoop, this);
}

void ClientRuntime::stop() {
  if (!running_.load()) return;
  if (winusb_.running()) {
    winusb_.sendRelease();
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
  }
  winusb_.stop();
  {
    std::lock_guard lock(gamepadCommandMutex_);
    gamepadCommand_ = GamepadCommand::Stop;
    gamepadCommandFinished_ = false;
  }
  running_.store(false);
  controlWake_.notify_all();
  injectionWake_.notify_all();
  if (controlThread_.joinable()) controlThread_.join();
  if (injectionThread_.joinable()) injectionThread_.join();
  if (snapshotThread_.joinable()) snapshotThread_.join();
}

void ClientRuntime::notifyDevicesChanged() {
  deviceChangePending_.store(true);
  controlWake_.notify_one();
}

void ClientRuntime::prepareForSuspend() {
  suspended_.store(true);
  notifyDevicesChanged();
}

void ClientRuntime::resumeFromSuspend() {
  suspended_.store(false);
  notifyDevicesChanged();
}

void ClientRuntime::setHighPerformanceEnabled(bool enabled) {
  highPerformanceEnabled_.store(enabled);
  saveSettings();
  notifyDevicesChanged();
}

bool ClientRuntime::highPerformanceEnabled() const noexcept {
  return highPerformanceEnabled_.load();
}

RuntimeSnapshot ClientRuntime::snapshot() const { return published_.read(); }

std::string ClientRuntime::snapshotJson() const {
  return snapshotToJson(snapshot());
}

void ClientRuntime::setMode(RuntimeMode mode, std::string code,
                            std::string message) {
  std::lock_guard lock(stateMutex_);
  state_.mode = mode;
  state_.errorCode = std::move(code);
  state_.errorMessage = std::move(message);
}

bool ClientRuntime::startVirtualGamepad(std::string& error) {
  std::unique_lock lock(gamepadCommandMutex_);
  gamepadCommandFinished_ = false;
  gamepadCommand_ = GamepadCommand::Start;
  injectionWake_.notify_one();
  if (!gamepadCommandDone_.wait_for(lock, std::chrono::seconds(3),
                                    [&] { return gamepadCommandFinished_; })) {
    error = "virtual controller startup timed out";
    return false;
  }
  error = gamepadCommandError_;
  if (gamepadCommandResult_) {
    std::lock_guard stateLock(stateMutex_);
    state_.virtualConnected = true;
    state_.virtualSlot = virtualGamepad_->slot();
  }
  return gamepadCommandResult_;
}

void ClientRuntime::stopVirtualGamepad() {
  std::unique_lock lock(gamepadCommandMutex_);
  gamepadCommandFinished_ = false;
  gamepadCommand_ = GamepadCommand::Stop;
  injectionWake_.notify_one();
  (void)gamepadCommandDone_.wait_for(lock, std::chrono::seconds(2),
                                     [&] { return gamepadCommandFinished_; });
  std::lock_guard stateLock(stateMutex_);
  state_.virtualConnected = false;
  state_.virtualSlot = -1;
}

void ClientRuntime::stopTurbo(bool requestRelease) {
  if (requestRelease && winusb_.running()) {
    winusb_.sendRelease();
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
  }
  winusb_.stop();
  stopVirtualGamepad();
  inputQueue_.clear();
  setMode(RuntimeMode::Recovering);
}

void ClientRuntime::controlLoop() {
  bool transitional = true;
  while (running_.load()) {
    {
      std::unique_lock lock(controlMutex_);
      if (transitional) {
        controlWake_.wait_for(lock, std::chrono::milliseconds(250));
      } else {
        controlWake_.wait(lock, [&] {
          return !running_.load() || deviceChangePending_.load();
        });
      }
    }
    if (!running_.load()) break;
    deviceChangePending_.store(false);

    RuntimeMode currentMode;
    {
      std::lock_guard lock(stateMutex_);
      currentMode = state_.mode;
    }
    transitional = currentMode == RuntimeMode::Acquiring ||
                   currentMode == RuntimeMode::Recovering;

    if (!winusb_.running()) winusb_.stop();

    if (!highPerformanceEnabled_.load() || suspended_.load()) {
      if (winusb_.running()) stopTurbo(true);
      if (currentMode == RuntimeMode::Acquiring) {
        HidControlTransport hid;
        std::string ignored;
        if (hid.open(ignored)) {
          auto release = makeControl(HBOX_CLIENT_CONTROL_RELEASE, 3u, token_);
          hbox_client_control_v1_t response{};
          (void)hid.exchange(release, response, ignored);
        }
        stopVirtualGamepad();
      }
    }

    if (suspended_.load()) {
      transitional = false;
      continue;
    }

    if (winusb_.running()) {
      HidControlTransport secondDevice;
      std::string secondDeviceError;
      if (secondDevice.open(secondDeviceError) ||
          secondDeviceError.find("multiple native HBox") != std::string::npos) {
        setMode(RuntimeMode::Turbo, "MULTIPLE_DEVICES",
                "V1 only supports one HBox; the additional device remains in native mode");
      } else {
        setMode(RuntimeMode::Turbo);
      }
      transitional = false;
      continue;
    }

    if (currentMode == RuntimeMode::Acquiring &&
        highPerformanceEnabled_.load()) {
      std::string error;
      if (winusb_.openAndStart(
              token_,
              [this](auto bytes, auto completedAt) {
                handleUsbFrame(bytes, completedAt);
              },
              [this](const std::string& message) {
                setMode(RuntimeMode::Recovering, "WINUSB_STREAM", message);
                notifyDevicesChanged();
              }, error)) {
        {
          std::lock_guard lock(stateMutex_);
          state_.connected = true;
          state_.usbSpeed = "HIGH";
        }
        setMode(RuntimeMode::Turbo);
        transitional = false;
        continue;
      }
      if (std::chrono::steady_clock::now() - acquireStarted_ <
          std::chrono::milliseconds(2500)) {
        transitional = true;
        continue;
      }
      stopVirtualGamepad();
      setMode(RuntimeMode::Recovering, "ACQUIRE_TIMEOUT", error);
      transitional = true;
      continue;
    }

    HidControlTransport hid;
    std::string error;
    if (!hid.open(error)) {
      if (error.find("multiple native HBox") != std::string::npos) {
        {
          std::lock_guard lock(stateMutex_);
          state_.connected = true;
          state_.usbSpeed = "HIGH";
        }
        setMode(RuntimeMode::Fault, "MULTIPLE_DEVICES",
                "V1 supports one HBox at a time; disconnect the additional device");
        transitional = false;
        continue;
      }
      std::lock_guard lock(stateMutex_);
      state_.connected = false;
      state_.usbSpeed = "NONE";
      if (state_.mode != RuntimeMode::Recovering) state_.mode = RuntimeMode::Native;
      transitional = state_.mode == RuntimeMode::Recovering;
      continue;
    }

    const LeaseToken emptyToken{};
    auto query = makeControl(HBOX_CLIENT_CONTROL_QUERY, 1u, emptyToken);
    hbox_client_control_v1_t response{};
    if (!hid.exchange(query, response, error) ||
        response.status != HBOX_CLIENT_STATUS_OK) {
      setMode(RuntimeMode::Fault, "HID_QUERY", error);
      transitional = false;
      continue;
    }
    configuredHz_.store(response.effective_rate_hz_le);
    {
      std::lock_guard lock(stateMutex_);
      state_.connected = true;
      state_.usbSpeed = (response.flags & HBOX_CLIENT_FLAG_USB_HS) != 0u
                            ? "HIGH" : "FULL";
      state_.mode = RuntimeMode::Native;
      state_.errorCode.clear();
      state_.errorMessage.clear();
    }

    const bool eligible = highPerformanceEnabled_.load() &&
        response.effective_rate_hz_le > 1000u &&
        (response.flags & HBOX_CLIENT_FLAG_USB_HS) != 0u;
    if (!eligible) {
      transitional = false;
      continue;
    }

    if (!startVirtualGamepad(error)) {
      setMode(RuntimeMode::Fault, "VIRTUAL_PAD", error);
      transitional = false;
      continue;
    }
    token_ = randomToken();
    auto acquire = makeControl(HBOX_CLIENT_CONTROL_ACQUIRE, 2u, token_);
    if (!hid.exchange(acquire, response, error) ||
        response.status != HBOX_CLIENT_STATUS_OK) {
      stopVirtualGamepad();
      setMode(RuntimeMode::Fault, "ACQUIRE_REJECTED", error);
      transitional = false;
      continue;
    }
    acquireStarted_ = std::chrono::steady_clock::now();
    setMode(RuntimeMode::Acquiring);
    transitional = true;
  }
}

void ClientRuntime::handleUsbFrame(
    std::span<const std::uint8_t> bytes,
    std::chrono::steady_clock::time_point completedAt) {
  const auto parsed = parseInputFrame(bytes, token_);
  if (!parsed.frame) {
    invalid_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const auto& frame = *parsed.frame;
  const bool hadSequence = hasSequence_.exchange(true, std::memory_order_relaxed);
  const auto previous = lastSequence_.exchange(frame.sequence,
                                                std::memory_order_relaxed);
  if (hadSequence) {
    const std::uint32_t distance = frame.sequence - previous;
    if (distance > 1u && distance < 0x80000000u) {
      dropped_.fetch_add(distance - 1u, std::memory_order_relaxed);
    }
  }
  const auto nowNs = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          completedAt.time_since_epoch()).count());
  const auto previousNs = lastCompletionNs_.exchange(nowNs,
                                                      std::memory_order_relaxed);
  if (previousNs != 0u) {
    const auto interval = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(nowNs - previousNs, UINT32_MAX));
    const auto cursor = intervalCursor_.fetch_add(1, std::memory_order_relaxed);
    intervalsNs_[cursor % intervalsNs_.size()].store(interval,
                                                     std::memory_order_relaxed);
  }
  configuredHz_.store(frame.effectiveRateHz, std::memory_order_relaxed);
  received_.fetch_add(1, std::memory_order_relaxed);
  if (!inputQueue_.push(frame)) {
    overflowLatest_.write(frame);
    if (overflowPending_.exchange(true, std::memory_order_release)) {
      coalesced_.fetch_add(1, std::memory_order_relaxed);
    }
  }
  injectionWake_.notify_one();
}

void ClientRuntime::injectionLoop() {
  for (;;) {
    {
      std::unique_lock lock(gamepadCommandMutex_);
      injectionWake_.wait_for(lock, std::chrono::milliseconds(2), [&] {
        return gamepadCommand_ != GamepadCommand::None || !running_.load();
      });
      if (gamepadCommand_ != GamepadCommand::None) {
        gamepadCommandError_.clear();
        if (gamepadCommand_ == GamepadCommand::Start) {
          gamepadCommandResult_ = virtualGamepad_->start(gamepadCommandError_);
        } else {
          virtualGamepad_->neutralize();
          virtualGamepad_->stop();
          gamepadCommandResult_ = true;
        }
        gamepadCommand_ = GamepadCommand::None;
        gamepadCommandFinished_ = true;
        gamepadCommandDone_.notify_all();
      }
    }
    if (!running_.load()) break;

    std::uint64_t skipped = 0;
    auto frame = inputQueue_.popLatest(skipped);
    if (overflowPending_.exchange(false, std::memory_order_acq_rel)) {
      const auto overflow = overflowLatest_.read();
      if (frame) ++skipped;
      frame = overflow;
    }
    if (!frame) continue;
    if (skipped != 0u) coalesced_.fetch_add(skipped, std::memory_order_relaxed);
    std::string error;
    if (!virtualGamepad_->submit(frame->state, frame->sequence, error)) {
      setMode(RuntimeMode::Fault, "VIRTUAL_SUBMIT", error);
      continue;
    }
    currentInput_[0].store(static_cast<std::int32_t>(frame->actionMask));
    currentInput_[1].store(frame->state.buttons);
    currentInput_[2].store(frame->state.leftTrigger);
    currentInput_[3].store(frame->state.rightTrigger);
    currentInput_[4].store(frame->state.leftX);
    currentInput_[5].store(frame->state.leftY);
    currentInput_[6].store(frame->state.rightX);
    currentInput_[7].store(frame->state.rightY);
  }
}

void ClientRuntime::snapshotLoop() {
  auto previousTime = std::chrono::steady_clock::now();
  std::uint64_t previousReceived = 0u;
  while (running_.load()) {
    RuntimeSnapshot snapshot;
    {
      std::lock_guard lock(stateMutex_);
      snapshot = state_;
    }
    snapshot.highPerformanceEnabled = highPerformanceEnabled_.load();
    snapshot.configuredHz = configuredHz_.load();
    snapshot.received = received_.load();
    snapshot.dropped = dropped_.load();
    snapshot.invalid = invalid_.load();
    snapshot.coalesced = coalesced_.load();
    snapshot.hasSequence = hasSequence_.load();
    snapshot.lastSequence = lastSequence_.load();
    snapshot.actionMask = static_cast<std::uint32_t>(currentInput_[0].load());
    snapshot.input.buttons = static_cast<std::uint16_t>(currentInput_[1].load());
    snapshot.input.leftTrigger = static_cast<std::uint8_t>(currentInput_[2].load());
    snapshot.input.rightTrigger = static_cast<std::uint8_t>(currentInput_[3].load());
    snapshot.input.leftX = static_cast<std::int16_t>(currentInput_[4].load());
    snapshot.input.leftY = static_cast<std::int16_t>(currentInput_[5].load());
    snapshot.input.rightX = static_cast<std::int16_t>(currentInput_[6].load());
    snapshot.input.rightY = static_cast<std::int16_t>(currentInput_[7].load());

    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - previousTime).count();
    if (elapsed > 0.0) {
      snapshot.measuredHz = static_cast<double>(snapshot.received - previousReceived) /
                            elapsed;
    }
    previousTime = now;
    previousReceived = snapshot.received;

    const auto cursor = intervalCursor_.load(std::memory_order_relaxed);
    const auto count = static_cast<std::size_t>(
        std::min<std::uint64_t>(cursor, intervalsNs_.size()));
    std::vector<std::uint32_t> intervals;
    intervals.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      intervals.push_back(intervalsNs_[index].load(std::memory_order_relaxed));
    }
    if (!intervals.empty()) {
      std::sort(intervals.begin(), intervals.end());
      const auto value = [&](double percentile) {
        return intervals[static_cast<std::size_t>(
            percentile * static_cast<double>(intervals.size() - 1u))] / 1000.0;
      };
      snapshot.intervalUs = {value(0.50), value(0.95), value(0.99),
                             intervals.back() / 1000.0};
    }
    published_.write(snapshot);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

void ClientRuntime::loadSettings() {
  const auto path = settingsPath();
  if (path.empty() || !std::filesystem::exists(path)) return;
  std::ifstream input(path);
  const std::string content((std::istreambuf_iterator<char>(input)), {});
  highPerformanceEnabled_.store(
      content.find("\"highPerformanceEnabled\":false") == std::string::npos);
}

void ClientRuntime::saveSettings() {
  const auto path = settingsPath();
  if (path.empty()) return;
  std::error_code ignored;
  std::filesystem::create_directories(path.parent_path(), ignored);
  std::ofstream output(path, std::ios::trunc);
  output << "{\"schemaVersion\":1,\"highPerformanceEnabled\":"
         << (highPerformanceEnabled_.load() ? "true" : "false") << "}\n";
}

}  // namespace hbox
