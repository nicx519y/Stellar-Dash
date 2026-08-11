#include "ch585_firmware_update.hpp"

#include <string.h>

#include "ch585_iap_client.hpp"
#include "ch585_iap_protocol.h"
#include "firmware_metadata.h"
#include "qspi-w25q64.h"
#include "sha256_simple.h"
#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"
#include "storagemanager.hpp"
#include "system_logger.h"

namespace {

static constexpr uint32_t kStagingMagic = 0x55353843u; /* "C85U" */
static constexpr uint16_t kStagingVersion = 1u;
static constexpr uint8_t kStagingStateReceiving = 1u;
static constexpr uint8_t kStagingStateReady = 2u;

struct __attribute__((packed)) StagingHeader {
    uint32_t magic;
    uint16_t version;
    uint8_t state;
    uint8_t reserved;
    uint32_t imageSize;
    uint8_t sha256[32];
};

static_assert(sizeof(StagingHeader) <= 256u, "staging header must fit one page");

static bool writeWithoutErase(uint32_t address,
                              const uint8_t* data,
                              uint32_t length)
{
    bool ok = true;
    const bool wasMapped = QSPI_W25Qxx_IsMemoryMappedMode();
    if (wasMapped && QSPI_W25Qxx_ExitMemoryMappedMode() != QSPI_W25Qxx_OK) {
        return false;
    }
    while (length != 0u) {
        const uint32_t pageRemaining = 256u - (address & 0xFFu);
        const uint16_t chunk = static_cast<uint16_t>(
            length < pageRemaining ? length : pageRemaining);
        if (QSPI_W25Qxx_WritePage(const_cast<uint8_t*>(data),
                                  address,
                                  chunk) != QSPI_W25Qxx_OK) {
            ok = false;
            break;
        }
        address += chunk;
        data += chunk;
        length -= chunk;
    }
    if (wasMapped && QSPI_W25Qxx_EnterMemoryMappedMode() != QSPI_W25Qxx_OK) {
        ok = false;
    }
    return ok;
}

static bool writeHeader(const StagingHeader& header, bool eraseSector)
{
    const bool wasMapped = QSPI_W25Qxx_IsMemoryMappedMode();
    if (wasMapped && QSPI_W25Qxx_ExitMemoryMappedMode() != QSPI_W25Qxx_OK) {
        return false;
    }
    bool ok = true;
    if (eraseSector &&
        QSPI_W25Qxx_SectorErase(CH585_FIRMWARE_STAGING_ADDR) != QSPI_W25Qxx_OK) {
        ok = false;
    }
    if (ok && QSPI_W25Qxx_WritePage(
                  reinterpret_cast<uint8_t*>(const_cast<StagingHeader*>(&header)),
                  CH585_FIRMWARE_STAGING_ADDR,
                  sizeof(header)) != QSPI_W25Qxx_OK) {
        ok = false;
    }
    if (wasMapped && QSPI_W25Qxx_EnterMemoryMappedMode() != QSPI_W25Qxx_OK) {
        ok = false;
    }
    return ok;
}

} // namespace

bool Ch585FirmwareUpdate::isPending() const
{
    return (STORAGE_MANAGER.config.screenControl.serviceFlags &
            SCREEN_SERVICE_CH585_UPDATE_PENDING) != 0u;
}

bool Ch585FirmwareUpdate::hasFailed() const
{
    return (STORAGE_MANAGER.config.screenControl.serviceFlags &
            SCREEN_SERVICE_CH585_UPDATE_FAILED) != 0u;
}

uint8_t Ch585FirmwareUpdate::progress() const
{
    if (currentStatus == Ch585FirmwareUpdateStatus::Programming) {
        return CH585_IAP_CLIENT.progress();
    }
    if (imageSize == 0u) return 0u;
    return static_cast<uint8_t>((received * 100u) / imageSize);
}

bool Ch585FirmwareUpdate::persistServiceState(bool pending,
                                              bool failed,
                                              bool confirmed)
{
    uint8_t& flags = STORAGE_MANAGER.config.screenControl.serviceFlags;
    const uint8_t previous = flags;
    flags = pending ? static_cast<uint8_t>(flags | SCREEN_SERVICE_CH585_UPDATE_PENDING)
                    : static_cast<uint8_t>(flags & ~SCREEN_SERVICE_CH585_UPDATE_PENDING);
    flags = failed ? static_cast<uint8_t>(flags | SCREEN_SERVICE_CH585_UPDATE_FAILED)
                   : static_cast<uint8_t>(flags & ~SCREEN_SERVICE_CH585_UPDATE_FAILED);
    if (confirmed) flags = static_cast<uint8_t>(flags | SCREEN_SERVICE_CH585_IAP_CONFIRMED);
    if (STORAGE_MANAGER.saveConfig()) return true;
    flags = previous;
    return false;
}

bool Ch585FirmwareUpdate::begin(uint32_t totalSize,
                                const uint8_t expectedSha256[32])
{
    if (expectedSha256 == nullptr || totalSize <= CH585_IAP_APP_START ||
        totalSize > CH585_FIRMWARE_STAGING_DATA_SIZE || (totalSize & 3u) != 0u) {
        currentStatus = Ch585FirmwareUpdateStatus::Failed;
        return false;
    }
    if (QSPI_W25Qxx_ExitMemoryMappedMode() != QSPI_W25Qxx_OK ||
        QSPI_W25Qxx_BufferErase(CH585_FIRMWARE_STAGING_ADDR,
                               CH585_FIRMWARE_STAGING_SIZE) != QSPI_W25Qxx_OK ||
        QSPI_W25Qxx_EnterMemoryMappedMode() != QSPI_W25Qxx_OK) {
        currentStatus = Ch585FirmwareUpdateStatus::Failed;
        return false;
    }

    StagingHeader header = {};
    header.magic = kStagingMagic;
    header.version = kStagingVersion;
    header.state = kStagingStateReceiving;
    header.imageSize = totalSize;
    memcpy(header.sha256, expectedSha256, sizeof(header.sha256));
    if (!writeHeader(header, false)) {
        currentStatus = Ch585FirmwareUpdateStatus::Failed;
        return false;
    }
    imageSize = totalSize;
    received = 0u;
    memcpy(expectedSha, expectedSha256, sizeof(expectedSha));
    currentStatus = Ch585FirmwareUpdateStatus::Receiving;
    return true;
}

bool Ch585FirmwareUpdate::write(uint32_t offset,
                                const uint8_t* data,
                                uint32_t length)
{
    if (currentStatus != Ch585FirmwareUpdateStatus::Receiving ||
        data == nullptr || length == 0u || offset != received ||
        offset + length > imageSize) {
        return false;
    }
    if (!writeWithoutErase(CH585_FIRMWARE_STAGING_DATA_ADDR + offset,
                           data,
                           length)) {
        currentStatus = Ch585FirmwareUpdateStatus::Failed;
        return false;
    }
    received += length;
    return true;
}

bool Ch585FirmwareUpdate::loadAndVerifyStagedImage()
{
    if (QSPI_W25Qxx_EnterMemoryMappedMode() != QSPI_W25Qxx_OK) return false;
    const StagingHeader* header = reinterpret_cast<const StagingHeader*>(
        CH585_FIRMWARE_STAGING_ADDR);
    if (header->magic != kStagingMagic || header->version != kStagingVersion ||
        header->state != kStagingStateReady ||
        header->imageSize <= CH585_IAP_APP_START ||
        header->imageSize > CH585_FIRMWARE_STAGING_DATA_SIZE ||
        (header->imageSize & 3u) != 0u) {
        return false;
    }
    uint8_t actual[32];
    if (sha256_calculate_raw(
            reinterpret_cast<const uint8_t*>(CH585_FIRMWARE_STAGING_DATA_ADDR),
            header->imageSize,
            actual) != 0 || memcmp(actual, header->sha256, sizeof(actual)) != 0) {
        return false;
    }
    imageSize = header->imageSize;
    received = imageSize;
    memcpy(expectedSha, header->sha256, sizeof(expectedSha));
    return true;
}

bool Ch585FirmwareUpdate::finalizeAndSchedule()
{
    if (currentStatus != Ch585FirmwareUpdateStatus::Receiving ||
        received != imageSize) return false;
    uint8_t actual[32];
    if (QSPI_W25Qxx_EnterMemoryMappedMode() != QSPI_W25Qxx_OK ||
        sha256_calculate_raw(
            reinterpret_cast<const uint8_t*>(CH585_FIRMWARE_STAGING_DATA_ADDR),
            imageSize,
            actual) != 0 || memcmp(actual, expectedSha, sizeof(actual)) != 0) {
        currentStatus = Ch585FirmwareUpdateStatus::Failed;
        return false;
    }

    StagingHeader header = {};
    header.magic = kStagingMagic;
    header.version = kStagingVersion;
    header.state = kStagingStateReady;
    header.imageSize = imageSize;
    memcpy(header.sha256, expectedSha, sizeof(header.sha256));
    if (!writeHeader(header, true) || !persistServiceState(true, false, false)) {
        currentStatus = Ch585FirmwareUpdateStatus::Failed;
        return false;
    }
    currentStatus = Ch585FirmwareUpdateStatus::Scheduled;
    rebootAtMs = HAL_GetTick() + 500u;
    return true;
}

void Ch585FirmwareUpdate::process()
{
    if (currentStatus == Ch585FirmwareUpdateStatus::Scheduled &&
        static_cast<int32_t>(HAL_GetTick() - rebootAtMs) >= 0) {
        NVIC_SystemReset();
    }
}

bool Ch585FirmwareUpdate::performPendingUpdate()
{
    currentStatus = Ch585FirmwareUpdateStatus::Programming;
    if (!loadAndVerifyStagedImage() ||
        !CH585_IAP_CLIENT.programCombinedImage(CH585_FIRMWARE_STAGING_DATA_ADDR,
                                               imageSize)) {
        currentStatus = Ch585FirmwareUpdateStatus::Failed;
        (void)persistServiceState(false, true, false);
        APP_STAGE_ERROR("M11", "CH585 staged SPI IAP update failed");
        return false;
    }
    currentStatus = Ch585FirmwareUpdateStatus::Completed;
    if (!persistServiceState(false, false, true)) {
        currentStatus = Ch585FirmwareUpdateStatus::Failed;
        return false;
    }
    return true;
}

bool Ch585FirmwareUpdate::requestRetry()
{
    if (!hasFailed() || !loadAndVerifyStagedImage()) return false;
    currentStatus = Ch585FirmwareUpdateStatus::Scheduled;
    if (!persistServiceState(true, false, false)) return false;
    NVIC_SystemReset();
    return true;
}
