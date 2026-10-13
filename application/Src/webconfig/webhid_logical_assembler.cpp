#include "webhid_logical_assembler.hpp"
#include "webhid_protocol.h"
#include "device_security_crypto.h"
#include <algorithm>
#include <cstring>
namespace {
bool extractTransactionId(
    const uint8_t *json,
    size_t length,
    uint32_t &transactionId)
{
    static constexpr char kKey[] = "\"transactionId\"";
    if (json == nullptr || length < sizeof(kKey)) {
        return false;
    }
    const uint8_t *const end = json + length;
    const uint8_t *cursor = std::search(
        json,
        end,
        reinterpret_cast<const uint8_t *>(kKey),
        reinterpret_cast<const uint8_t *>(kKey) + sizeof(kKey) - 1u);
    if (cursor == end) {
        return false;
    }
    cursor += sizeof(kKey) - 1u;
    while (cursor != end &&
           (*cursor == ' ' || *cursor == '\t' ||
            *cursor == '\r' || *cursor == '\n')) {
        ++cursor;
    }
    if (cursor == end || *cursor != ':') {
        return false;
    }
    ++cursor;
    while (cursor != end &&
           (*cursor == ' ' || *cursor == '\t' ||
            *cursor == '\r' || *cursor == '\n')) {
        ++cursor;
    }
    uint64_t value = 0u;
    const uint8_t *const firstDigit = cursor;
    while (cursor != end && *cursor >= '0' && *cursor <= '9') {
        value = value * 10u + static_cast<uint64_t>(*cursor - '0');
        if (value > UINT32_MAX) {
            return false;
        }
        ++cursor;
    }
    if (cursor == firstDigit || value == 0u) {
        return false;
    }
    transactionId = static_cast<uint32_t>(value);
    return true;
}
}
void WebHidLogicalAssembler::reset()
{
    if (length != 0u) HBoxCrypto_Zeroize(bytes.data(), length + 1u);
    active = false;
    discardingOversize = false;
    errorTransactionId = 0u;
    length = 0u;
}
WebHidLogicalAssembler::Result WebHidLogicalAssembler::accept(
    uint8_t messageType, bool encrypted, uint8_t flags,
    const uint8_t *payload, uint16_t fragmentLength)
{
    const bool fragmented = (flags & WEBHID_REPORT_FLAG_FRAGMENTED) != 0u;
    const bool last = (flags & WEBHID_REPORT_FLAG_LAST) != 0u;
    if (!last && !fragmented) return {Status::Invalid, encrypted, 0u};
    if (!active) {
        active = true; type = messageType; secure = encrypted;
        discardingOversize = false; errorTransactionId = 0u; length = 0u;
    } else if (!fragmented || type != messageType || secure != encrypted) {
        return {Status::Invalid, encrypted, 0u};
    }
    if (!discardingOversize) {
        const size_t copied = std::min<size_t>(fragmentLength, Capacity - length);
        memcpy(bytes.data() + length, payload, copied);
        length += copied;
        if (copied != fragmentLength) {
            bytes[length] = 0u;
            (void)extractTransactionId(bytes.data(), length, errorTransactionId);
            discardingOversize = true;
        }
    }
    if (discardingOversize) {
        if (!last) return {Status::Pending, secure, 0u};
        const Result result{Status::Oversize, secure, errorTransactionId};
        reset();
        return result;
    }
    if (fragmented && !last) return {Status::Pending, secure, 0u};
    bytes[length] = 0u;
    active = false;
    return {Status::Complete, secure, 0u};
}
