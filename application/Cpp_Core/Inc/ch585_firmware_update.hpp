#ifndef CH585_FIRMWARE_UPDATE_HPP
#define CH585_FIRMWARE_UPDATE_HPP

#include <stddef.h>
#include <stdint.h>

enum class Ch585FirmwareUpdateStatus : uint8_t {
    Idle = 0,
    Receiving,
    Ready,
    Scheduled,
    Programming,
    Completed,
    Failed
};

class Ch585FirmwareUpdate {
public:
    Ch585FirmwareUpdate(const Ch585FirmwareUpdate&) = delete;
    Ch585FirmwareUpdate& operator=(const Ch585FirmwareUpdate&) = delete;

    static Ch585FirmwareUpdate& getInstance()
    {
        static Ch585FirmwareUpdate instance;
        return instance;
    }

    bool begin(uint32_t totalSize, const uint8_t expectedSha256[32]);
    bool write(uint32_t offset, const uint8_t* data, uint32_t length);
    bool finalizeAndSchedule();
    void process();
    bool performPendingUpdate();
    bool requestRetry();

    bool isPending() const;
    bool hasFailed() const;
    Ch585FirmwareUpdateStatus status() const { return currentStatus; }
    uint8_t progress() const;
    uint32_t totalSize() const { return imageSize; }
    uint32_t receivedSize() const { return received; }

private:
    Ch585FirmwareUpdate() = default;
    bool loadAndVerifyStagedImage();
    bool persistServiceState(bool pending, bool failed, bool confirmed);

    uint32_t imageSize = 0u;
    uint32_t received = 0u;
    uint32_t rebootAtMs = 0u;
    uint8_t expectedSha[32] = {};
    Ch585FirmwareUpdateStatus currentStatus = Ch585FirmwareUpdateStatus::Idle;
};

#define CH585_FIRMWARE_UPDATE Ch585FirmwareUpdate::getInstance()

#endif
