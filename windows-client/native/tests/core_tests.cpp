#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <string>

#include "hbox_client/protocol.hpp"
#include "hbox_client/runtime_snapshot.hpp"
#include "hbox_client/spsc_ring.hpp"
#include "hbox_client/stream_statistics.hpp"

int main() {
  using namespace hbox;
  static constexpr std::uint8_t golden[] = "123456789";
  static_assert(sizeof(hbox_client_control_v1_t) == 32u);
  static_assert(sizeof(hbox_client_input_v1_t) == 64u);
  static_assert(offsetof(hbox_client_input_v1_t, lease_token) == 16u);
  static_assert(offsetof(hbox_client_input_v1_t, crc32_le) == 60u);
  assert(hbox_client_crc16_ccitt(golden, 9u) == 0x29B1u);
  assert(hbox_client_crc32(golden, 9u) == 0xCBF43926u);

  LeaseToken token{};
  token[0] = 0x42;
  const auto control = makeControl(HBOX_CLIENT_CONTROL_ACQUIRE, 7u, token);
  assert(validateControl(control));

  hbox_client_input_v1_t wire{};
  wire.magic_le = HBOX_CLIENT_INPUT_MAGIC;
  wire.version = HBOX_CLIENT_PROTOCOL_VERSION;
  wire.length_le = HBOX_CLIENT_INPUT_BYTES;
  wire.stream_sequence_le = 99u;
  wire.effective_rate_hz_le = 8000u;
  wire.xinput_buttons_le = 0x1001u;
  std::memcpy(wire.lease_token, token.data(), token.size());
  wire.crc32_le = hbox_client_crc32(
      reinterpret_cast<const std::uint8_t*>(&wire),
      offsetof(hbox_client_input_v1_t, crc32_le));
  const auto parsed = parseInputFrame(
      {reinterpret_cast<const std::uint8_t*>(&wire), sizeof(wire)}, token);
  assert(parsed.frame && parsed.frame->sequence == 99u);
  auto wrongToken = token;
  wrongToken[1] = 0x99u;
  assert(parseInputFrame(
      {reinterpret_cast<const std::uint8_t*>(&wire), sizeof(wire)}, wrongToken)
      .error == ParseError::Token);
  const auto savedVersion = wire.version;
  wire.version = 2u;
  assert(parseInputFrame(
      {reinterpret_cast<const std::uint8_t*>(&wire), sizeof(wire)}, token)
      .error == ParseError::Version);
  wire.version = savedVersion;
  wire.crc32_le ^= 1u;
  assert(parseInputFrame(
      {reinterpret_cast<const std::uint8_t*>(&wire), sizeof(wire)}, token)
      .error == ParseError::Crc);

  SpscRing<InputFrame, 4> ring;
  InputFrame frame{};
  frame.sequence = 1u;
  assert(ring.push(frame));
  frame.sequence = 2u;
  assert(ring.push(frame));
  std::uint64_t coalesced = 0;
  const auto newest = ring.popLatest(coalesced);
  assert(newest && newest->sequence == 2u && coalesced == 1u);
  frame.sequence = 3u;
  assert(ring.push(frame));
  frame.sequence = 4u;
  assert(ring.push(frame));
  frame.sequence = 5u;
  assert(ring.push(frame));
  frame.sequence = 6u;
  assert(!ring.push(frame));

  LatestInputStore latest;
  frame.sequence = 0xA5A55A5Au;
  frame.actionMask = 0x13579BDFu;
  latest.write(frame);
  const auto latestCopy = latest.read();
  assert(latestCopy.sequence == frame.sequence);
  assert(latestCopy.actionMask == frame.actionMask);

  StreamStatistics statistics;
  const auto start = std::chrono::steady_clock::now();
  statistics.accepted(0xFFFFFFFEu, start);
  statistics.accepted(0xFFFFFFFFu, start + std::chrono::microseconds(125));
  statistics.accepted(0u, start + std::chrono::microseconds(250));
  RuntimeSnapshot snapshot;
  statistics.copyTo(snapshot, start + std::chrono::milliseconds(1));
  assert(snapshot.received == 3u && snapshot.dropped == 0u);
  assert(snapshot.intervalUs.p50 == 125.0);

  for (const std::uint16_t rate : {1000u, 2000u, 4000u, 8000u}) {
    wire.version = HBOX_CLIENT_PROTOCOL_VERSION;
    wire.crc32_le = 0u;
    wire.effective_rate_hz_le = rate;
    wire.crc32_le = hbox_client_crc32(
        reinterpret_cast<const std::uint8_t*>(&wire),
        offsetof(hbox_client_input_v1_t, crc32_le));
    const auto replay = parseInputFrame(
        {reinterpret_cast<const std::uint8_t*>(&wire), sizeof(wire)}, token);
    assert(replay.frame && replay.frame->effectiveRateHz == rate);
  }
  const auto json = snapshotToJson(snapshot);
  assert(json.find("\"schemaVersion\":1") != std::string::npos);
  return 0;
}
