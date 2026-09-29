#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "hbox_client/protocol.hpp"

namespace hbox {

class IVirtualGamepad {
 public:
  virtual ~IVirtualGamepad() = default;
  virtual bool start(std::string& error) = 0;
  virtual bool submit(const XusbState& state,
                      std::uint32_t sourceSequence,
                      std::string& error) = 0;
  virtual void neutralize() noexcept = 0;
  virtual void stop() noexcept = 0;
  virtual const char* backendName() const noexcept = 0;
  virtual int slot() const noexcept = 0;
};

}  // namespace hbox
