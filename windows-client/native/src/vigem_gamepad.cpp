#include "hbox_client/vigem_gamepad.hpp"

#include <filesystem>
#include <sstream>

namespace hbox {
namespace {
constexpr std::uint32_t kVigemErrorNone = 0x20000000u;

template <typename T>
bool loadSymbol(HMODULE module, const char* name, T& target,
                std::string& error) {
  target = reinterpret_cast<T>(GetProcAddress(module, name));
  if (target) return true;
  error = std::string("ViGEmClient.dll does not export ") + name;
  return false;
}
}  // namespace

VigemGamepad::~VigemGamepad() { stop(); }

bool VigemGamepad::start(std::string& error) {
#if !defined(HBOX_INTERNAL_VIGEM_MVP)
  error = "ViGEm is disabled; install the signed HBox virtual-pad backend";
  return false;
#else
  if (target_) return true;
  library_ = LoadLibraryExW(L"ViGEmClient.dll", nullptr,
                            LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                            LOAD_LIBRARY_SEARCH_SYSTEM32 |
                            LOAD_LIBRARY_SEARCH_USER_DIRS);
  if (!library_) {
    error = "ViGEmClient.dll is missing; install the internal ViGEm MVP runtime";
    return false;
  }
  if (!loadSymbol(library_, "vigem_alloc", alloc_, error) ||
      !loadSymbol(library_, "vigem_free", free_, error) ||
      !loadSymbol(library_, "vigem_connect", connect_, error) ||
      !loadSymbol(library_, "vigem_disconnect", disconnect_, error) ||
      !loadSymbol(library_, "vigem_target_x360_alloc", targetAlloc_, error) ||
      !loadSymbol(library_, "vigem_target_free", targetFree_, error) ||
      !loadSymbol(library_, "vigem_target_add", targetAdd_, error) ||
      !loadSymbol(library_, "vigem_target_remove", targetRemove_, error) ||
      !loadSymbol(library_, "vigem_target_x360_update", targetUpdate_, error)) {
    stop();
    return false;
  }
  targetUserIndex_ = reinterpret_cast<decltype(targetUserIndex_)>(
      GetProcAddress(library_, "vigem_target_x360_get_user_index"));
  client_ = alloc_();
  if (!client_ || connect_(client_) != kVigemErrorNone) {
    error = "ViGEmBus is unavailable or the client connection failed";
    stop();
    return false;
  }
  target_ = targetAlloc_();
  if (!target_ || targetAdd_(client_, target_) != kVigemErrorNone) {
    error = "ViGEm could not create the virtual Xbox 360 controller";
    stop();
    return false;
  }
  if (targetUserIndex_) {
    unsigned long index = 0;
    if (targetUserIndex_(client_, target_, &index) == kVigemErrorNone) {
      slot_ = static_cast<int>(index);
    }
  }
  return true;
#endif
}

bool VigemGamepad::submit(const XusbState& state,
                          std::uint32_t sourceSequence,
                          std::string& error) {
  (void)sourceSequence;
  if (!client_ || !target_) {
    error = "virtual controller is not connected";
    return false;
  }
  const Report report{state.buttons, state.leftTrigger, state.rightTrigger,
                      state.leftX, state.leftY, state.rightX, state.rightY};
  if (targetUpdate_(client_, target_, report) != kVigemErrorNone) {
    error = "ViGEm rejected an Xbox 360 state update";
    return false;
  }
  return true;
}

void VigemGamepad::neutralize() noexcept {
  if (client_ && target_ && targetUpdate_) {
    const Report neutral{};
    (void)targetUpdate_(client_, target_, neutral);
  }
}

void VigemGamepad::stop() noexcept {
  neutralize();
  if (client_ && target_ && targetRemove_) targetRemove_(client_, target_);
  if (target_ && targetFree_) targetFree_(target_);
  target_ = nullptr;
  if (client_ && disconnect_) disconnect_(client_);
  if (client_ && free_) free_(client_);
  client_ = nullptr;
  slot_ = -1;
  if (library_) FreeLibrary(library_);
  library_ = nullptr;
  alloc_ = nullptr;
  free_ = nullptr;
  connect_ = nullptr;
  disconnect_ = nullptr;
  targetAlloc_ = nullptr;
  targetFree_ = nullptr;
  targetAdd_ = nullptr;
  targetRemove_ = nullptr;
  targetUpdate_ = nullptr;
  targetUserIndex_ = nullptr;
}

}  // namespace hbox
