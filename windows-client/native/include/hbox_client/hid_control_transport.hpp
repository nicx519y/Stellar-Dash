#pragma once

#ifdef _WIN32

#include <windows.h>

#include <optional>
#include <string>

#include "hbox_client/protocol.hpp"

namespace hbox {

class HidControlTransport {
 public:
  HidControlTransport() = default;
  ~HidControlTransport();
  HidControlTransport(const HidControlTransport&) = delete;
  HidControlTransport& operator=(const HidControlTransport&) = delete;

  bool open(std::string& error);
  void close() noexcept;
  bool exchange(const hbox_client_control_v1_t& request,
                hbox_client_control_v1_t& response,
                std::string& error);
  bool isOpen() const noexcept { return handle_ != INVALID_HANDLE_VALUE; }

 private:
  HANDLE handle_{INVALID_HANDLE_VALUE};
};

}  // namespace hbox

#endif
