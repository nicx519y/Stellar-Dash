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
static constexpr uint32_t kPowerOnSettleMs = 20u;
static constexpr uint32_t kProbeWindowMs = 300u;
static constexpr uint32_t kApplicationCapsWindowMs = 1000u;
static constexpr uint32_t kApplicationCapsRetryMs = 10u;
static constexpr uint8_t kBeginAttempts = 2u;
static constexpr uint8_t kWriteAttempts = 2u;

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
    APP_STAGE("M00L", "CH585 IAP loader entry begin");
    USB_BOARD_LINK.shutdown();
    APP_STAGE("M00L", "CH585 USB board link stopped");
    CH585_ROLE_BOOTSTRAP.shutdown();
    APP_STAGE("M00L", "CH585 role bootstrap stopped");
    USBBoardLinkPort_Shutdown();
    /* Keep the power-cycle explicit here: the IAP window starts at CH585
     * power-on and is only 500 ms long. */
    BOARD_POWER.setCh585Enabled(false);
    HAL_Delay(CH585_POWER_OFF_MIN_MS);
    BOARD_POWER.setCh585Enabled(true);
    /* Do not clock the first byte on the CH585 power-up edge.  The 4 KiB IAP
     * treats a non-IAP first byte as an explicit request to boot the app, so
     * a partially powered SPI sample would close the whole 500 ms window. */
    HAL_Delay(kPowerOnSettleMs);
    const bool initialized = USBBoardLinkPort_InitIap();
    APP_STAGE(initialized ? "M01" : "M01E",
              "CH585 IAP power cycle complete: settle=%lu ms spi=%u",
              static_cast<unsigned long>(kPowerOnSettleMs),
              initialized ? 1u : 0u);
    return initialized;
}

bool Ch585IapClient::transact(uint8_t command,
                              uint32_t offset,
                              uint32_t value,
                              const uint8_t* payload,
                              uint16_t payloadLength,
                              uint32_t timeoutMs)
{
    lastTransactionTimedOut = false;
    lastDeviceStatus = 0xFFu;
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
        lastTransactionTimedOut = true;
        APP_STAGE_ERROR("M02",
                        "CH585 IAP link timeout: cmd=%u seq=%u offset=%lu timeout=%lu",
                        static_cast<unsigned int>(command),
                        static_cast<unsigned int>(packet.sequence),
                        static_cast<unsigned long>(offset),
                        static_cast<unsigned long>(timeoutMs));
        return false;
    }
    if (response.magic != CH585_IAP_RESPONSE_MAGIC ||
        response.version != CH585_IAP_PROTOCOL_VERSION ||
        response.command != command || response.sequence != packet.sequence ||
        crc8Sum(reinterpret_cast<const uint8_t*>(&response),
                sizeof(response) - 1u) != response.crc8) {
        currentStatus = Ch585IapClientStatus::ProtocolError;
        APP_STAGE_ERROR("M03",
                        "CH585 IAP response invalid: cmd=%u seq=%u magic=%04x version=%u response_cmd=%u response_seq=%u",
                        static_cast<unsigned int>(command),
                        static_cast<unsigned int>(packet.sequence),
                        static_cast<unsigned int>(response.magic),
                        static_cast<unsigned int>(response.version),
                        static_cast<unsigned int>(response.command),
                        static_cast<unsigned int>(response.sequence));
        return false;
    }
    lastDeviceStatus = response.status;
    if (response.status != CH585_IAP_STATUS_OK) {
        currentStatus = Ch585IapClientStatus::DeviceError;
        APP_STAGE_ERROR("M04",
                        "CH585 IAP device rejected command: cmd=%u seq=%u status=%u offset=%lu",
                        static_cast<unsigned int>(command),
                        static_cast<unsigned int>(packet.sequence),
                        static_cast<unsigned int>(response.status),
                        static_cast<unsigned long>(offset));
        return false;
    }
    return true;
}

bool Ch585IapClient::probe()
{
    currentStatus = Ch585IapClientStatus::Idle;
    currentProgress = 0u;
    lastDeviceStatus = 0u;
    currentStage = CH585_STAGING_STAGE_PROBE;
    currentOffset = 0u;
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
            APP_STAGE("M05", "CH585 SPI IAP probe accepted: seq=%u",
                      static_cast<unsigned int>(sequence));
            return true;
        }
        USBBoardLinkPort_Shutdown();
        HAL_Delay(2u);
        (void)USBBoardLinkPort_InitIap();
    } while ((HAL_GetTick() - started) < kProbeWindowMs);

    currentStatus = Ch585IapClientStatus::LinkError;
    APP_STAGE_ERROR("M06", "CH585 SPI IAP probe window expired");
    return false;
}

bool Ch585IapClient::programCombinedImage(uint32_t mappedAddress,
                                          uint32_t totalSize)
{
    APP_STAGE("M00", "CH585 combined image programming begin: mapped=%08lx total=%lu",
              static_cast<unsigned long>(mappedAddress),
              static_cast<unsigned long>(totalSize));
    endResponseConfirmed = false;
    currentOffset = 0u;
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
    APP_STAGE("M00C", "CH585 application source CRC ready: size=%lu crc=%08lx",
              static_cast<unsigned long>(appSize),
              static_cast<unsigned long>(imageCrc));

    APP_STAGE("M00P", "CH585 starting IAP probe");
    if (!probe()) return false;

    currentStage = CH585_STAGING_STAGE_BEGIN;
    bool beginAccepted = false;
    for (uint8_t attempt = 0u; attempt < kBeginAttempts; ++attempt) {
        if (transact(CH585_IAP_CMD_BEGIN, appSize, imageCrc, nullptr, 0u,
                     kEraseResponseTimeoutMs)) {
            beginAccepted = true;
            break;
        }
        if (!lastTransactionTimedOut) return false;
        (void)USBBoardLinkPort_RawDiscardPendingResponse(
            sizeof(ch585_iap_response_t), 20u);
        APP_STAGE("M07R", "CH585 BEGIN response timeout; safe erase retry=%u",
                  static_cast<unsigned int>(attempt + 1u));
    }
    if (!beginAccepted) return false;
    APP_STAGE("M07", "CH585 SPI IAP erase accepted: app_size=%lu crc=%08lx",
              static_cast<unsigned long>(appSize),
              static_cast<unsigned long>(imageCrc));

    uint32_t offset = 0u;
    uint8_t lastLoggedProgress = 0u;
    currentStage = CH585_STAGING_STAGE_WRITE;
    while (offset < appSize) {
        currentOffset = offset;
        const uint16_t chunk = static_cast<uint16_t>(
            ((appSize - offset) > CH585_IAP_DATA_SIZE)
                ? CH585_IAP_DATA_SIZE
                : (appSize - offset));
        bool accepted = false;
        bool previousTimedOut = false;
        for (uint8_t attempt = 0u; attempt < kWriteAttempts; ++attempt) {
            if (transact(CH585_IAP_CMD_WRITE, offset, 0u, app + offset, chunk,
                         kWriteResponseTimeoutMs)) {
                accepted = true;
                break;
            }
            if (previousTimedOut &&
                lastDeviceStatus == CH585_IAP_STATUS_BAD_ADDRESS) {
                /* The only tolerated ambiguity: the timed-out request may
                 * have committed and advanced the loader offset. END's full
                 * image CRC remains the authority. */
                accepted = true;
                APP_STAGE("M08R", "CH585 WRITE likely committed before timeout: offset=%lu",
                          static_cast<unsigned long>(offset));
                break;
            }
            if (!lastTransactionTimedOut) return false;
            previousTimedOut = true;
            (void)USBBoardLinkPort_RawDiscardPendingResponse(
                sizeof(ch585_iap_response_t), 10u);
            APP_STAGE("M08R", "CH585 WRITE timeout; retrying same offset=%lu",
                      static_cast<unsigned long>(offset));
        }
        if (!accepted) return false;
        offset += chunk;
        currentOffset = offset;
        currentProgress = static_cast<uint8_t>((offset * 100u) / appSize);
        if (currentProgress >= static_cast<uint8_t>(lastLoggedProgress + 10u) ||
            currentProgress == 100u) {
            lastLoggedProgress = currentProgress;
            APP_STAGE("M08", "CH585 SPI IAP write progress=%u offset=%lu",
                      static_cast<unsigned int>(currentProgress),
                      static_cast<unsigned long>(offset));
        }
    }

    currentStage = CH585_STAGING_STAGE_END;
    if (transact(CH585_IAP_CMD_END, appSize, imageCrc, nullptr, 0u,
                 kEraseResponseTimeoutMs)) {
        endResponseConfirmed = true;
    } else if (!lastTransactionTimedOut) {
        return false;
    } else {
        /* END commits VALID metadata before replying.  A lost reply is only
         * accepted later if the real application passes role/CAPS checks. */
        APP_STAGE("M09", "CH585 END response lost; deferring result to app CAPS");
    }
    currentProgress = 100u;
    currentStatus = Ch585IapClientStatus::Completed;
    APP_STAGE("M10", "CH585 SPI IAP completed: app_size=%lu crc=%08lx",
              static_cast<unsigned long>(appSize),
              static_cast<unsigned long>(imageCrc));
    return true;
}

bool Ch585IapClient::validateApplication()
{
    currentStage = CH585_STAGING_STAGE_VERIFY_APP;
    USB_BOARD_LINK.shutdown();
    CH585_ROLE_BOOTSTRAP.shutdown();
    USBBoardLinkPort_Shutdown();
    CH585_ROLE_BOOTSTRAP.setSelector(UsbBoardLink_SelectRoleCallback);

    if (!CH585_ROLE_BOOTSTRAP.start(Ch585Role::Maintenance)) {
        currentStatus = Ch585IapClientStatus::DeviceError;
        APP_STAGE_ERROR("M09R", "CH585 application maintenance role selection failed: state=%u",
                        static_cast<unsigned int>(CH585_ROLE_BOOTSTRAP.state()));
        CH585_ROLE_BOOTSTRAP.shutdown();
        USB_BOARD_LINK.shutdown();
        return false;
    }
    APP_STAGE("M09R", "CH585 application maintenance role selected");
    /* ROLE_SELECTED is emitted immediately before CH585 tears down the
     * polling selector and initializes the DMA board-link plus USB host.
     * That acknowledgement is deliberately early, so a one-shot GET_CAPS
     * can land before usb_board_link_process() exists.  Retry within a fixed
     * boot window; transact() drains a late CAPS first and the operation is
     * read-only/idempotent. */
    bool capsReady = false;
    const uint32_t capsStarted = HAL_GetTick();
    uint32_t capsAttempts = 0u;
    do {
        ++capsAttempts;
        if (USB_BOARD_LINK.getCapabilities()) {
            capsReady = true;
            break;
        }
        HAL_Delay(kApplicationCapsRetryMs);
    } while ((HAL_GetTick() - capsStarted) < kApplicationCapsWindowMs);
    if (!capsReady) {
        currentStatus = Ch585IapClientStatus::DeviceError;
        APP_STAGE_ERROR("M09C", "CH585 application GET_CAPS failed after role selection: attempts=%lu window=%lu ms",
                        static_cast<unsigned long>(capsAttempts),
                        static_cast<unsigned long>(kApplicationCapsWindowMs));
        CH585_ROLE_BOOTSTRAP.shutdown();
        USB_BOARD_LINK.shutdown();
        return false;
    }
    APP_STAGE("M09C", "CH585 application GET_CAPS accepted: attempts=%lu elapsed=%lu ms",
              static_cast<unsigned long>(capsAttempts),
              static_cast<unsigned long>(HAL_GetTick() - capsStarted));
    const usb_board_caps_v1_t& caps = USB_BOARD_LINK.capabilities();
    const bool valid =
        (caps.role_flags & USB_BOARD_CAP_ROLE_MAINTENANCE) != 0u &&
        (caps.profile_flags & USB_BOARD_CAP_PROFILE_WEB_CONFIG) != 0u &&
        (caps.feature_flags & USB_BOARD_CAP_FEATURE_WEBHID_V1) != 0u;
    APP_STAGE(valid ? "M09V" : "M09E",
              "CH585 application CAPS: valid=%u fw=%u.%u.%u roles=%02x profiles=%04x features=%02x end_ack=%u",
              valid ? 1u : 0u,
              static_cast<unsigned int>(caps.firmware_major),
              static_cast<unsigned int>(caps.firmware_minor),
              static_cast<unsigned int>(caps.firmware_patch),
              static_cast<unsigned int>(caps.role_flags),
              static_cast<unsigned int>(caps.profile_flags),
              static_cast<unsigned int>(caps.feature_flags),
              endResponseConfirmed ? 1u : 0u);
    CH585_ROLE_BOOTSTRAP.shutdown();
    USB_BOARD_LINK.shutdown();
    if (!valid) {
        currentStatus = Ch585IapClientStatus::DeviceError;
        return false;
    }
    currentStage = CH585_STAGING_STAGE_COMPLETE;
    currentStatus = Ch585IapClientStatus::Completed;
    return true;
}
