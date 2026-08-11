#include "ch585_iap_client.hpp"

#include <string.h>

#include "board_cfg.h"
#include "board_power.hpp"
#include "ch585_iap_protocol.h"
#include "ch585_role_bootstrap.hpp"
#include "stm32h7xx_hal.h"
#include "system_logger.h"
#include "usb_board_link.hpp"
#include "usb_board_link_port.hpp"

namespace {

static constexpr uint32_t kCombinedIapBytes = CH585_IAP_APP_START;
static constexpr uint32_t kProbeResponseTimeoutMs = 20u;
static constexpr uint32_t kEraseResponseTimeoutMs = 10000u;
static constexpr uint32_t kWriteResponseTimeoutMs = 100u;

static uint32_t crc32Update(uint32_t crc, const uint8_t* data, uint32_t length)
{
    while (length-- != 0u) {
        crc ^= *data++;
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

static uint8_t crc8Sum(const uint8_t* data, uint32_t length)
{
    uint8_t sum = 0u;
    while (length-- != 0u) sum = static_cast<uint8_t>(sum + *data++);
    return sum;
}

} // namespace

bool Ch585IapClient::enterLoader()
{
    USB_BOARD_LINK.shutdown();
    CH585_ROLE_BOOTSTRAP.shutdown();
    USBBoardLinkPort_Shutdown();
    HAL_Delay(CH585_POWER_OFF_MIN_MS);
    BOARD_POWER.setCh585Enabled(true);
    HAL_Delay(2u);
    return USBBoardLinkPort_InitIap();
}

bool Ch585IapClient::transact(uint8_t command,
                              uint32_t offset,
                              uint32_t value,
                              const uint8_t* payload,
                              uint16_t payloadLength,
                              uint32_t timeoutMs)
{
    ch585_iap_packet_t packet = {};
    ch585_iap_response_t response = {};
    packet.magic = CH585_IAP_PROTOCOL_MAGIC;
    packet.version = CH585_IAP_PROTOCOL_VERSION;
    packet.command = command;
    packet.sequence = ++sequence;
    packet.offset = offset;
    packet.value = value;
    packet.payload_length = payloadLength;
    if (payloadLength > CH585_IAP_DATA_SIZE ||
        (payloadLength != 0u && payload == nullptr)) {
        currentStatus = Ch585IapClientStatus::ProtocolError;
        return false;
    }
    if (payloadLength != 0u) memcpy(packet.data, payload, payloadLength);
    uint32_t crc = crc32Update(0xFFFFFFFFu,
                               reinterpret_cast<const uint8_t*>(&packet),
                               sizeof(packet) - sizeof(packet.packet_crc32));
    packet.packet_crc32 = crc ^ 0xFFFFFFFFu;

    if (!USBBoardLinkPort_RawTransact(
            reinterpret_cast<const uint8_t*>(&packet),
            sizeof(packet),
            reinterpret_cast<uint8_t*>(&response),
            sizeof(response),
            timeoutMs)) {
        currentStatus = Ch585IapClientStatus::LinkError;
        return false;
    }
    if (response.magic != CH585_IAP_RESPONSE_MAGIC ||
        response.version != CH585_IAP_PROTOCOL_VERSION ||
        response.command != command || response.sequence != packet.sequence ||
        crc8Sum(reinterpret_cast<const uint8_t*>(&response),
                sizeof(response) - 1u) != response.crc8) {
        currentStatus = Ch585IapClientStatus::ProtocolError;
        return false;
    }
    lastDeviceStatus = response.status;
    if (response.status != CH585_IAP_STATUS_OK) {
        currentStatus = Ch585IapClientStatus::DeviceError;
        return false;
    }
    return true;
}

bool Ch585IapClient::probe()
{
    currentStatus = Ch585IapClientStatus::Idle;
    currentProgress = 0u;
    lastDeviceStatus = 0u;
    if (!enterLoader()) {
        currentStatus = Ch585IapClientStatus::LinkError;
        return false;
    }

    const uint32_t started = HAL_GetTick();
    do {
        if (transact(CH585_IAP_CMD_PROBE,
                     0u,
                     0u,
                     nullptr,
                     0u,
                     kProbeResponseTimeoutMs)) {
            currentStatus = Ch585IapClientStatus::Ready;
            return true;
        }
        USBBoardLinkPort_Shutdown();
        HAL_Delay(2u);
        (void)USBBoardLinkPort_InitIap();
    } while ((HAL_GetTick() - started) < 300u);

    currentStatus = Ch585IapClientStatus::LinkError;
    return false;
}

bool Ch585IapClient::programCombinedImage(uint32_t mappedAddress,
                                          uint32_t totalSize)
{
    if (totalSize <= kCombinedIapBytes) {
        currentStatus = Ch585IapClientStatus::InvalidImage;
        return false;
    }
    const uint32_t appSize = totalSize - kCombinedIapBytes;
    if (appSize > CH585_IAP_APP_CAPACITY || (appSize & 3u) != 0u) {
        currentStatus = Ch585IapClientStatus::InvalidImage;
        return false;
    }
    const uint8_t* app = reinterpret_cast<const uint8_t*>(
        mappedAddress + kCombinedIapBytes);
    const uint32_t imageCrc =
        crc32Update(0xFFFFFFFFu, app, appSize) ^ 0xFFFFFFFFu;

    if (!probe() ||
        !transact(CH585_IAP_CMD_BEGIN,
                  appSize,
                  imageCrc,
                  nullptr,
                  0u,
                  kEraseResponseTimeoutMs)) {
        return false;
    }

    uint32_t offset = 0u;
    while (offset < appSize) {
        const uint16_t chunk = static_cast<uint16_t>(
            ((appSize - offset) > CH585_IAP_DATA_SIZE)
                ? CH585_IAP_DATA_SIZE
                : (appSize - offset));
        if (!transact(CH585_IAP_CMD_WRITE,
                      offset,
                      0u,
                      app + offset,
                      chunk,
                      kWriteResponseTimeoutMs)) {
            return false;
        }
        offset += chunk;
        currentProgress = static_cast<uint8_t>((offset * 100u) / appSize);
    }

    if (!transact(CH585_IAP_CMD_END,
                  appSize,
                  imageCrc,
                  nullptr,
                  0u,
                  kEraseResponseTimeoutMs)) {
        return false;
    }
    currentProgress = 100u;
    currentStatus = Ch585IapClientStatus::Completed;
    APP_STAGE("M10", "CH585 SPI IAP completed: app_size=%lu crc=%08lx",
              static_cast<unsigned long>(appSize),
              static_cast<unsigned long>(imageCrc));
    return true;
}
