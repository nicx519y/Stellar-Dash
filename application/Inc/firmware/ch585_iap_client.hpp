#ifndef CH585_IAP_CLIENT_HPP
#define CH585_IAP_CLIENT_HPP

#include <stdint.h>

#include "ch585_staging.h"
#include "release_install_protocol.h"

enum class Ch585IapClientStatus : uint8_t {
    Idle = 0,
    Ready,
    LinkError,
    ProtocolError,
    DeviceError,
    InvalidImage,
    Completed,
    CheckpointError
};

enum class Ch585IapTransferMode : uint32_t { Unknown = 0, SmallPacket = 1, Dma = 2 };

class Ch585IapClient {
public:
    Ch585IapClient(const Ch585IapClient&) = delete;
    Ch585IapClient& operator=(const Ch585IapClient&) = delete;

    static Ch585IapClient& getInstance()
    {
        static Ch585IapClient instance;
        return instance;
    }

    bool probe();
    using TransferCheckpoint = bool (*)(Ch585IapTransferMode);
    bool programCombinedImage(uint32_t mappedAddress, uint32_t totalSize, TransferCheckpoint checkpoint = nullptr);
    bool programApplicationImage(uint32_t mappedAddress, uint32_t size, TransferCheckpoint checkpoint = nullptr);
    Ch585IapTransferMode transferMode() const { return currentTransferMode; }
    bool validateApplication(xora_release_identity_t* identity = nullptr);
    Ch585IapClientStatus status() const { return currentStatus; }
    uint8_t progress() const { return currentProgress; }
    uint8_t deviceStatus() const { return lastDeviceStatus; }
    uint32_t offset() const { return currentOffset; }
    uint8_t stage() const { return currentStage; }

private:
    Ch585IapClient() = default;
    bool enterLoader();
    bool transact(uint8_t command,
                  uint32_t offset,
                  uint32_t value,
                  const uint8_t* payload,
                  uint16_t payloadLength,
                  uint32_t timeoutMs);

    uint16_t sequence = 0u;
    uint8_t currentProgress = 0u;
    uint8_t lastDeviceStatus = 0u;
    uint8_t currentStage = CH585_STAGING_STAGE_NONE;
    uint32_t currentOffset = 0u;
    bool lastTransactionTimedOut = false;
    bool endResponseConfirmed = false;
    bool dmaIap = false;
    Ch585IapTransferMode currentTransferMode = Ch585IapTransferMode::Unknown;
    Ch585IapClientStatus currentStatus = Ch585IapClientStatus::Idle;
};

#define CH585_IAP_CLIENT Ch585IapClient::getInstance()

#endif
