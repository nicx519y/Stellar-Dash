#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Owns bounded plaintext until the caller finishes synchronous dispatch.
// No heap allocations, transport, authentication, or RPC dependencies.
class WebHidLogicalAssembler {
public:
    static constexpr size_t Capacity = 16u * 1024u;
    enum class Status { Invalid, Pending, Complete, Oversize };
    struct Result { Status status; bool secure; uint32_t transactionId; };
    Result accept(uint8_t type, bool secure, uint8_t flags,
                  const uint8_t *payload, uint16_t length);
    const uint8_t *data() const { return bytes.data(); }
    size_t size() const { return length; }
    void reset();
private:
    bool active = false;
    uint8_t type = 0u;
    bool secure = false;
    bool discardingOversize = false;
    uint32_t errorTransactionId = 0u;
    size_t length = 0u;
    std::array<uint8_t, Capacity + 1u> bytes = {};
};
