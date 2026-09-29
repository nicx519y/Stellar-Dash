#include "hbox_client/protocol.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

namespace hbox {

static_assert(std::endian::native == std::endian::little,
              "HBox Windows client requires little-endian x64");

ParseResult parseInputFrame(std::span<const std::uint8_t> bytes,
                            const LeaseToken& expectedToken) {
  if (bytes.size() != sizeof(hbox_client_input_v1_t)) {
    return {.error = ParseError::Size};
  }
  hbox_client_input_v1_t packet{};
  std::memcpy(&packet, bytes.data(), sizeof(packet));
  if (packet.magic_le != HBOX_CLIENT_INPUT_MAGIC) {
    return {.error = ParseError::Magic};
  }
  if (packet.version != HBOX_CLIENT_PROTOCOL_VERSION) {
    return {.error = ParseError::Version};
  }
  if (packet.length_le != HBOX_CLIENT_INPUT_BYTES) {
    return {.error = ParseError::Length};
  }
  if (!std::equal(expectedToken.begin(), expectedToken.end(),
                  packet.lease_token)) {
    return {.error = ParseError::Token};
  }
  if (packet.crc32_le != hbox_client_crc32(
          bytes.data(), offsetof(hbox_client_input_v1_t, crc32_le))) {
    return {.error = ParseError::Crc};
  }

  InputFrame frame{};
  frame.sequence = packet.stream_sequence_le;
  frame.producerTimeUs = packet.producer_time_us_le;
  std::copy_n(packet.lease_token, frame.token.size(), frame.token.begin());
  frame.actionMask = packet.action_mask_le;
  frame.sampleAgeUs = packet.sample_age_us_le;
  frame.effectiveRateHz = packet.effective_rate_hz_le;
  frame.state = {
      packet.xinput_buttons_le, packet.left_trigger, packet.right_trigger,
      packet.left_stick_x_le, packet.left_stick_y_le,
      packet.right_stick_x_le, packet.right_stick_y_le};
  frame.flags = packet.flags_le;
  frame.batteryCode = packet.battery_code;
  frame.overwriteCount = packet.device_overwrite_count_le;
  frame.boardLinkFaultCount = packet.board_link_fault_count_le;
  return {.frame = frame};
}

hbox_client_control_v1_t makeControl(std::uint8_t opcode,
                                     std::uint32_t transaction,
                                     const LeaseToken& token) {
  hbox_client_control_v1_t packet{};
  packet.magic_le = HBOX_CLIENT_CONTROL_MAGIC;
  packet.version = HBOX_CLIENT_PROTOCOL_VERSION;
  packet.opcode = opcode;
  packet.transaction_le = transaction;
  std::copy(token.begin(), token.end(), packet.lease_token);
  packet.crc16_le = hbox_client_crc16_ccitt(
      reinterpret_cast<const std::uint8_t*>(&packet),
      offsetof(hbox_client_control_v1_t, crc16_le));
  return packet;
}

bool validateControl(const hbox_client_control_v1_t& control) {
  return control.magic_le == HBOX_CLIENT_CONTROL_MAGIC &&
         control.version == HBOX_CLIENT_PROTOCOL_VERSION &&
         control.crc16_le == hbox_client_crc16_ccitt(
             reinterpret_cast<const std::uint8_t*>(&control),
             offsetof(hbox_client_control_v1_t, crc16_le));
}

}  // namespace hbox
