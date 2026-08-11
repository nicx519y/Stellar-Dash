#ifndef CH585_IAP_CLIENT_HPP
#define CH585_IAP_CLIENT_HPP

#include <stdint.h>

enum class Ch585IapClientStatus : uint8_t {
    Idle = 0,
    Ready,
    LinkError,
    ProtocolError,
    DeviceError,
    InvalidImage,
    Completed
};

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
    bool programCombinedImage(uint32_t mappedAddress, uint32_t totalSize);
    Ch585IapClientStatus status() const { return currentStatus; }
    uint8_t progress() const { return currentProgress; }
    uint8_t deviceStatus() const { return lastDeviceStatus; }

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
    Ch585IapClientStatus currentStatus = Ch585IapClientStatus::Idle;
};

#define CH585_IAP_CLIENT Ch585IapClient::getInstance()

#endif
