#include "rf_reliable_event.hpp"
#include <cassert>
#include <cstring>
#include <initializer_list>

uint32_t testReliableNow = 0u;

int main()
{
    static_assert(RFReliableEvent::maxPayloadLength >= 25u);
    // Real queue/decoder: legacy and current CH585 status payloads, including
    // the capability bytes which previously overflowed the consumer contract.
    for (uint8_t size : {23u, 24u, 25u}) {
        RFReliableEvent::resetSession();
        uint8_t frame[30] = {0xa5u, 0x82u, size};
        uint8_t* payload = frame + 3;
        payload[0] = 4u;
        payload[1] = 1u;
        payload[3] = 0x40u;
        payload[4] = 0x1fu;
        payload[20] = 7u;
        payload[21] = 10u;
        if (size >= 24u) payload[23] = 0xa0u;
        if (size >= 25u) payload[24] = 0xd1u;
        for (unsigned i = 0; i < size + 3u; ++i) frame[size + 3u] += frame[i];
        assert(RFReliableEvent::completeFrameIfNeeded(frame, size + 4u));
        uint8_t event = 0, length = 0, out[25] = {};
        testReliableNow += 9u;
        RFReliableEvent::poll();
        assert(!RFReliableEvent::popCompleted(&event, out, &length, sizeof(out)));
        ++testReliableNow;
        RFReliableEvent::poll();
        assert(!RFReliableEvent::popCompleted(&event, out, &length, size - 1u));
        assert(RFReliableEvent::popCompleted(&event, out, &length, sizeof(out)));
        assert(event == 0x82u && length == size && !std::memcmp(out, payload, size));
        // A completed duplicate is ignored; a cold session accepts it again.
        assert(RFReliableEvent::completeFrameIfNeeded(frame, size + 4u));
        testReliableNow += 10u;
        RFReliableEvent::poll();
        assert(!RFReliableEvent::popCompleted(&event, out, &length, sizeof(out)));
        RFReliableEvent::resetSession();
        assert(RFReliableEvent::completeFrameIfNeeded(frame, size + 4u));
        testReliableNow += 10u;
        RFReliableEvent::poll();
        assert(RFReliableEvent::popCompleted(&event, out, &length, sizeof(out)));
    }
    RFReliableEvent::resetSession();
    uint8_t oversized[26] = {};
    oversized[20] = 1u;
    assert(!RFReliableEvent::completeIfNeeded(0x82u, oversized, sizeof(oversized)));
}
