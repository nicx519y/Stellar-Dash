#ifndef RF_RELIABLE_EVENT_HPP
#define RF_RELIABLE_EVENT_HPP

#include <stdint.h>

namespace RFReliableEvent {
// Current CH585 status: 23 legacy bytes + capture marker + DMA capability.
// Keep both the pending queue and its consumer large enough for the extension.
constexpr uint8_t maxPayloadLength = 25u;
void resetSession();

bool completeIfNeeded(uint8_t evt, const uint8_t* payload, uint8_t payloadLen);
bool completeFrameIfNeeded(const uint8_t* frame, uint16_t frameLen);
void poll();
bool popCompleted(uint8_t* evt, uint8_t* payload, uint8_t* payloadLen, uint8_t payloadCapacity);

}

#endif
