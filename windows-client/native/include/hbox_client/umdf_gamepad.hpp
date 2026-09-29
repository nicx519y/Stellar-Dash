#pragma once

#ifdef _WIN32

#include <windows.h>

#include "hbox_client/virtual_gamepad.hpp"

namespace hbox {

class UmdfGamepad final : public IVirtualGamepad {
 public:
  ~UmdfGamepad() override;
  bool start(std::string& error) override;
  bool submit(const XusbState& state,
              std::uint32_t sourceSequence,
              std::string& error) override;
  void neutralize() noexcept override;
  void stop() noexcept override;
  const char* backendName() const noexcept override { return "hbox-umdf2"; }
  int slot() const noexcept override { return slot_; }

 private:
  HANDLE device_{INVALID_HANDLE_VALUE};
  int slot_{-1};
};

}  // namespace hbox

#endif
