#pragma once
#include <stdint.h>

// First power-on attempt only. Main-context writers; no IRQ logging, UART or
// cache maintenance on SPI edges. The bootstrap publishes once after selection.
namespace Ch585StartupTrace {
enum class Event : uint32_t {
    PowerOn = 1, ReadyBegin, Line, ReadyEnd, SendBegin, SendEnd,
    ReadBegin, ReadEnd, ReleaseFault, SelectionEnd, HandoffBegin, HandoffEnd
};
struct Entry { uint32_t tick, event, arg0, arg1; };
struct Snapshot {
    uint32_t magic, version, bytes, state; // state: 0 unused, 1 recording, 2 sealed
    uint32_t role, count, dropped, rawLength;
    uint32_t rawResult, sends, reads, lastLine; // lastLine: 0 unknown, 1 low, 2 high
    Entry entries[24];
    uint8_t raw[80];
};
static_assert(sizeof(Snapshot) == 512, "startup trace ABI");
}

alignas(32) inline Ch585StartupTrace::Snapshot g_ch585_startup = {};

namespace Ch585StartupTrace {
inline bool active() { return g_ch585_startup.state == 1u; }
inline void record(Event event, uint32_t tick, uint32_t arg0 = 0, uint32_t arg1 = 0) {
    if (!active()) return;
    auto& s = g_ch585_startup;
    // Reserve the last entry for the final result even on a noisy line.
    const uint32_t limit = event == Event::SelectionEnd ? 24u : 23u;
    if (s.count >= limit) { ++s.dropped; return; }
    s.entries[s.count++] = {tick, static_cast<uint32_t>(event), arg0, arg1};
}
inline void powerOn(uint32_t tick, uint32_t role) {
    auto& s = g_ch585_startup;
    if (s.state != 0u) return;
    s.magic = 0x43535431u; s.version = 1u; s.bytes = sizeof(s);
    s.rawResult = 0xffffffffu; // No read yet; do not imply a valid frame.
    s.role = role; s.state = 1u;
    record(Event::PowerOn, tick);
}
inline void line(uint32_t tick, bool high) {
    if (!active()) return;
    const uint32_t value = high ? 2u : 1u;
    if (g_ch585_startup.lastLine == value) return;
    g_ch585_startup.lastLine = value;
    record(Event::Line, tick, high);
}
inline void sendBegin(uint32_t tick, bool high) {
    if (!active()) return;
    if (++g_ch585_startup.sends == 1u) record(Event::SendBegin, tick, high);
}
inline void sendEnd(uint32_t tick, uint32_t halResult, bool high) {
    if (active() && g_ch585_startup.sends == 1u)
        record(Event::SendEnd, tick, halResult, high);
}
inline bool readBegin(uint32_t tick) {
    if (!active() || !g_ch585_startup.sends) return false;
    if (++g_ch585_startup.reads != 1u) return false;
    record(Event::ReadBegin, tick);
    return true;
}
// result: 0 framed/checksum valid, 1 HAL transfer failed, 2 invalid frame.
// arg1 packs byte count and the line level immediately after NSS release.
inline void readEnd(uint32_t tick, const uint8_t* raw, uint32_t length,
                    uint32_t result, uint32_t halResult, bool high) {
    if (!active() || g_ch585_startup.reads != 1u) return;
    auto& s = g_ch585_startup;
    s.rawLength = length < sizeof(s.raw) ? length : sizeof(s.raw);
    s.rawResult = result;
    for (uint32_t i = 0; i < s.rawLength; ++i) s.raw[i] = raw[i];
    record(Event::ReadEnd, tick, halResult, s.rawLength | (uint32_t(high) << 16));
}
inline bool finish(uint32_t tick, bool selected) {
    if (!active()) return false;
    record(Event::SelectionEnd, tick, selected);
    g_ch585_startup.state = 2u;
    return true;
}
}
