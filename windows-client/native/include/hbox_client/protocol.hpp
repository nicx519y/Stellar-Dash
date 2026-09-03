#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "hbox_high_rate_protocol.h"

namespace hbox {

using LeaseToken = std::array<std::uint8_t, 16>;

struct XusbState {
  std::uint16_t buttons{};
  std::uint8_t leftTrigger{};
  std::uint8_t rightTrigger{};
  std::int16_t leftX{};
  std::int16_t leftY{};
  std::int16_t rightX{};
  std::int16_t rightY{};
};

struct InputFrame {
  std::uint32_t sequence{};
  std::uint32_t producerTimeUs{};
  LeaseToken token{};
  std::uint32_t actionMask{};
  std::uint16_t sampleAgeUs{};
  std::uint16_t effectiveRateHz{1000};
  XusbState state{};
  std::uint16_t flags{};
  std::uint8_t batteryCode{};
  std::uint16_t overwriteCount{};
  std::uint16_t boardLinkFaultCount{};
};

enum class ParseError {
  Size,
  Magic,
  Version,
  Length,
  Token,
  Crc,
};

struct ParseResult {
  std::optional<InputFrame> frame;
  std::optional<ParseError> error;
};

ParseResult parseInputFrame(std::span<const std::uint8_t> bytes,
                            const LeaseToken& expectedToken);
hbox_client_control_v1_t makeControl(std::uint8_t opcode,
                                     std::uint32_t transaction,
                                     const LeaseToken& token);
bool validateControl(const hbox_client_control_v1_t& control);

}  // namespace hbox
