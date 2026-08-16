#pragma once

#include <cstdint>

enum class Ch585FirmwareUpdateStatus : uint8_t {
    Idle = 0,
    Receiving,
    Ready,
    Scheduled,
    Programming,
    Completed,
    Failed,
};

class Ch585FirmwareUpdate {
public:
    static Ch585FirmwareUpdate &getInstance();
    bool begin(uint32_t totalSize, const uint8_t expectedSha256[32]);
    bool write(uint32_t offset, const uint8_t *data, uint32_t length);
    bool finalizeAndSchedule();
    bool isPending() const;
    bool hasFailed() const;
    Ch585FirmwareUpdateStatus status() const;
    uint8_t progress() const;
    uint32_t totalSize() const;
    uint32_t receivedSize() const;
    void resetForContractTest() { total_ = 0u; received_ = 0u; status_ = Ch585FirmwareUpdateStatus::Idle; }

private:
    uint32_t total_ = 0u;
    uint32_t received_ = 0u;
    Ch585FirmwareUpdateStatus status_ = Ch585FirmwareUpdateStatus::Idle;
};

#define CH585_FIRMWARE_UPDATE Ch585FirmwareUpdate::getInstance()
