#pragma once

#ifdef _WIN32

#include <windows.h>

#include "hbox_client/virtual_gamepad.hpp"

namespace hbox {

class VigemGamepad final : public IVirtualGamepad {
 public:
  ~VigemGamepad() override;
  bool start(std::string& error) override;
  bool submit(const XusbState& state,
              std::uint32_t sourceSequence,
              std::string& error) override;
  void neutralize() noexcept override;
  void stop() noexcept override;
  const char* backendName() const noexcept override { return "vigem"; }
  int slot() const noexcept override { return slot_; }

 private:
  using Client = void*;
  using Target = void*;
  struct Report {
    USHORT buttons;
    BYTE leftTrigger;
    BYTE rightTrigger;
    SHORT leftX;
    SHORT leftY;
    SHORT rightX;
    SHORT rightY;
  };

  HMODULE library_{};
  Client client_{};
  Target target_{};
  int slot_{-1};
  Client(__cdecl* alloc_)(){};
  void(__cdecl* free_)(Client){};
  std::uint32_t(__cdecl* connect_)(Client){};
  void(__cdecl* disconnect_)(Client){};
  Target(__cdecl* targetAlloc_)(){};
  void(__cdecl* targetFree_)(Target){};
  std::uint32_t(__cdecl* targetAdd_)(Client, Target){};
  void(__cdecl* targetRemove_)(Client, Target){};
  std::uint32_t(__cdecl* targetUpdate_)(Client, Target, Report){};
  std::uint32_t(__cdecl* targetUserIndex_)(Client, Target, unsigned long*){};
};

}  // namespace hbox

#endif
