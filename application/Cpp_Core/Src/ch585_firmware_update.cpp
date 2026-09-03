#include "ch585_firmware_update.hpp"

#include <string.h>
#include <stddef.h>

#include "ch585_iap_client.hpp"
#include "ch585_iap_protocol.h"
#include "ch585_staging.h"
#include "firmware_metadata.h"
#include "qspi-w25q64.h"
#include "sha256_simple.h"
#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"
#include "system_logger.h"
#include "main_runtime_control.hpp"

namespace {

static constexpr uint32_t kRecordCount =
    CH585_FIRMWARE_STAGING_HEADER_SIZE / CH585_STAGING_RECORD_BYTES;

static constexpr uint32_t flashOffset(uint32_t mappedAddress)
{
    return mappedAddress & 0x00FFFFFFu;
}

static void invalidateMappedRange(uint32_t mappedAddress, uint32_t length)
{
    const uint32_t start = mappedAddress & ~31u;
    const uint32_t end = (mappedAddress + length + 31u) & ~31u;
    SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(start), end - start);
    __DSB();
    __ISB();
}

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

static uint32_t crc32(const uint8_t* data, uint32_t length)
{
    return crc32Update(0xFFFFFFFFu, data, length) ^ 0xFFFFFFFFu;
}

static bool recordValid(const ch585_staging_record_t& record)
{
    return record.magic == CH585_STAGING_RECORD_MAGIC &&
           record.version == CH585_STAGING_RECORD_VERSION &&
           record.record_bytes == CH585_STAGING_RECORD_BYTES &&
           record.commit == CH585_STAGING_RECORD_COMMIT &&
           record.record_crc32 ==
               crc32(reinterpret_cast<const uint8_t*>(&record),
                     CH585_STAGING_RECORD_CRC_BYTES) &&
           record.state >= CH585_STAGING_STATE_READY &&
           record.state <= CH585_STAGING_STATE_FAILED &&
           record.image_size > CH585_IAP_APP_START &&
           record.image_size <= CH585_FIRMWARE_STAGING_DATA_SIZE &&
           (record.image_size & 3u) == 0u;
}

static bool ensureMapped()
{
    return QSPI_W25Qxx_IsMemoryMappedMode() ||
           QSPI_W25Qxx_EnterMemoryMappedMode() == QSPI_W25Qxx_OK;
}

static bool loadLatestRecord(ch585_staging_record_t& latest,
                             uint32_t* latestIndex = nullptr)
{
    if (!ensureMapped()) return false;
    const auto* records = reinterpret_cast<const ch585_staging_record_t*>(
        CH585_FIRMWARE_STAGING_ADDR);
    bool found = false;
    for (uint32_t index = 0u; index < kRecordCount; ++index) {
        if (!recordValid(records[index])) continue;
        latest = records[index];
        if (latestIndex != nullptr) *latestIndex = index;
        found = true;
    }
    return found;
}

static bool pageIsErased(const ch585_staging_record_t& record)
{
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
    for (uint32_t index = 0u; index < sizeof(record); ++index) {
        if (bytes[index] != 0xFFu) return false;
    }
    return true;
}

static bool appendRecord(ch585_staging_record_t record)
{
    if (!ensureMapped()) return false;
    const auto* records = reinterpret_cast<const ch585_staging_record_t*>(
        CH585_FIRMWARE_STAGING_ADDR);
    uint32_t targetIndex = kRecordCount;
    for (uint32_t index = 0u; index < kRecordCount; ++index) {
        if (pageIsErased(records[index])) {
            targetIndex = index;
            break;
        }
    }
    if (targetIndex == kRecordCount) return false;

    record.magic = CH585_STAGING_RECORD_MAGIC;
    record.version = CH585_STAGING_RECORD_VERSION;
    record.record_bytes = CH585_STAGING_RECORD_BYTES;
    record.commit = 0xFFFFFFFFu;
    record.record_crc32 =
        crc32(reinterpret_cast<const uint8_t*>(&record),
              CH585_STAGING_RECORD_CRC_BYTES);

    const uint32_t mappedTarget = CH585_FIRMWARE_STAGING_ADDR +
                                  targetIndex * CH585_STAGING_RECORD_BYTES;
    const uint32_t target = flashOffset(mappedTarget);
    if (QSPI_W25Qxx_ExitMemoryMappedMode() != QSPI_W25Qxx_OK) return false;
    bool ok = QSPI_W25Qxx_WritePage(
                  reinterpret_cast<uint8_t*>(&record), target,
                  static_cast<uint16_t>(offsetof(ch585_staging_record_t, commit))) ==
              QSPI_W25Qxx_OK;
    const uint32_t commit = CH585_STAGING_RECORD_COMMIT;
    if (ok) {
        ok = QSPI_W25Qxx_WritePage(
                 reinterpret_cast<uint8_t*>(const_cast<uint32_t*>(&commit)),
                 target + offsetof(ch585_staging_record_t, commit),
                 sizeof(commit)) == QSPI_W25Qxx_OK;
    }
    if (QSPI_W25Qxx_EnterMemoryMappedMode() != QSPI_W25Qxx_OK) return false;
    if (!ok) return false;

    invalidateMappedRange(mappedTarget, CH585_STAGING_RECORD_BYTES);

    const auto* written =
        reinterpret_cast<const ch585_staging_record_t*>(mappedTarget);
    return recordValid(*written) && written->state == record.state &&
           written->generation == record.generation;
}

static bool eraseStaging()
{
    if (QSPI_W25Qxx_IsMemoryMappedMode() &&
        QSPI_W25Qxx_ExitMemoryMappedMode() != QSPI_W25Qxx_OK) {
        return false;
    }
    const bool erased =
        QSPI_W25Qxx_BufferErase(flashOffset(CH585_FIRMWARE_STAGING_ADDR),
                               CH585_FIRMWARE_STAGING_SIZE) == QSPI_W25Qxx_OK;
    const bool mapped =
        QSPI_W25Qxx_EnterMemoryMappedMode() == QSPI_W25Qxx_OK;
    if (mapped) {
        invalidateMappedRange(CH585_FIRMWARE_STAGING_ADDR,
                              CH585_FIRMWARE_STAGING_SIZE);
    }
    return erased && mapped;
}

static bool eraseHeaderJournal()
{
    if (QSPI_W25Qxx_IsMemoryMappedMode() &&
        QSPI_W25Qxx_ExitMemoryMappedMode() != QSPI_W25Qxx_OK) {
        return false;
    }
    const bool erased =
        QSPI_W25Qxx_SectorErase(flashOffset(CH585_FIRMWARE_STAGING_ADDR)) ==
        QSPI_W25Qxx_OK;
    const bool mapped =
        QSPI_W25Qxx_EnterMemoryMappedMode() == QSPI_W25Qxx_OK;
    if (mapped) {
        invalidateMappedRange(CH585_FIRMWARE_STAGING_ADDR,
                              CH585_FIRMWARE_STAGING_HEADER_SIZE);
    }
    return erased && mapped;
}

static bool writeWithoutErase(uint32_t address,
                              const uint8_t* data,
                              uint32_t length)
{
    const uint32_t mappedAddress = 0x90000000u | flashOffset(address);
    const uint32_t originalLength = length;
    address = flashOffset(address);
    const bool wasMapped = QSPI_W25Qxx_IsMemoryMappedMode();
    if (wasMapped && QSPI_W25Qxx_ExitMemoryMappedMode() != QSPI_W25Qxx_OK) {
        return false;
    }
    bool ok = true;
    while (length != 0u) {
        const uint32_t pageRemaining = 256u - (address & 0xFFu);
        const uint16_t chunk = static_cast<uint16_t>(
            length < pageRemaining ? length : pageRemaining);
        if (QSPI_W25Qxx_WritePage(const_cast<uint8_t*>(data), address, chunk) !=
            QSPI_W25Qxx_OK) {
            ok = false;
            break;
        }
        address += chunk;
        data += chunk;
        length -= chunk;
    }
    if (wasMapped && QSPI_W25Qxx_EnterMemoryMappedMode() != QSPI_W25Qxx_OK) {
        ok = false;
    } else if (wasMapped) {
        invalidateMappedRange(mappedAddress, originalLength);
    }
    return ok;
}

static ch585_staging_record_t makeRecord(uint8_t state,
                                         uint8_t stage,
                                         uint32_t generation,
                                         uint32_t imageSize,
                                         const uint8_t sha[32])
{
    ch585_staging_record_t record;
    memset(&record, 0xFF, sizeof(record));
    record.state = state;
    record.stage = stage;
    record.client_status = 0u;
    record.device_status = 0u;
    record.generation = generation;
    record.image_size = imageSize;
    record.error_offset = 0u;
    record.progress = 0u;
    memcpy(record.sha256, sha, sizeof(record.sha256));
    record.image_crc32 = 0u;
    return record;
}

} // namespace

bool Ch585FirmwareUpdate::isPending() const
{
    ch585_staging_record_t record;
    return loadLatestRecord(record) &&
           record.state == CH585_STAGING_STATE_READY;
}

bool Ch585FirmwareUpdate::hasFailed() const
{
    ch585_staging_record_t record;
    return loadLatestRecord(record) &&
           (record.state == CH585_STAGING_STATE_FAILED ||
            record.state == CH585_STAGING_STATE_CLAIMED);
}

bool Ch585FirmwareUpdate::hasAppliedImage() const
{
    ch585_staging_record_t record;
    return loadLatestRecord(record) &&
           record.state == CH585_STAGING_STATE_APPLIED;
}

bool Ch585FirmwareUpdate::hasReadyStagedImage()
{
    return isPending();
}

uint8_t Ch585FirmwareUpdate::progress() const
{
    if (currentStatus == Ch585FirmwareUpdateStatus::Programming) {
        return CH585_IAP_CLIENT.progress();
    }
    if (imageSize == 0u) return 0u;
    return static_cast<uint8_t>((received * 100u) / imageSize);
}

bool Ch585FirmwareUpdate::begin(uint32_t totalSize,
                                const uint8_t expectedSha256[32])
{
    if (expectedSha256 == nullptr || totalSize <= CH585_IAP_APP_START ||
        totalSize > CH585_FIRMWARE_STAGING_DATA_SIZE || (totalSize & 3u) != 0u ||
        !eraseStaging()) {
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
        offset + length > imageSize ||
        !writeWithoutErase(CH585_FIRMWARE_STAGING_DATA_ADDR + offset,
                           data, length)) {
        currentStatus = Ch585FirmwareUpdateStatus::Failed;
        return false;
    }
    received += length;
    return true;
}

bool Ch585FirmwareUpdate::loadAndVerifyStagedImage()
{
    ch585_staging_record_t record;
    if (!loadLatestRecord(record) ||
        record.state != CH585_STAGING_STATE_READY) {
        return false;
    }
    uint8_t actual[32];
    if (sha256_calculate_raw(
            reinterpret_cast<const uint8_t*>(CH585_FIRMWARE_STAGING_DATA_ADDR),
            record.image_size, actual) == 0 ||
        memcmp(actual, record.sha256, sizeof(actual)) != 0) {
        return false;
    }
    imageSize = record.image_size;
    received = imageSize;
    memcpy(expectedSha, record.sha256, sizeof(expectedSha));
    return true;
}

bool Ch585FirmwareUpdate::finalizeAndSchedule()
{
    if (currentStatus != Ch585FirmwareUpdateStatus::Receiving ||
        received != imageSize || !ensureMapped()) return false;
    uint8_t actual[32];
    if (sha256_calculate_raw(
            reinterpret_cast<const uint8_t*>(CH585_FIRMWARE_STAGING_DATA_ADDR),
            imageSize, actual) == 0 ||
        memcmp(actual, expectedSha, sizeof(actual)) != 0) {
        currentStatus = Ch585FirmwareUpdateStatus::Failed;
        return false;
    }

    auto ready = makeRecord(CH585_STAGING_STATE_READY,
                            CH585_STAGING_STAGE_STAGING,
                            HAL_GetTick(), imageSize, expectedSha);
    ready.image_crc32 = crc32(
        reinterpret_cast<const uint8_t*>(CH585_FIRMWARE_STAGING_DATA_ADDR +
                                         CH585_IAP_APP_START),
        imageSize - CH585_IAP_APP_START);
    if (!appendRecord(ready)) {
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
        MainRuntime_RequestReset();
    }
}

bool Ch585FirmwareUpdate::performPendingUpdate()
{
    updateClaimed = false;
    ch585_staging_record_t ready;
    if (!loadLatestRecord(ready) ||
        ready.state != CH585_STAGING_STATE_READY ||
        !loadAndVerifyStagedImage()) {
        return false;
    }

    auto claimed = ready;
    claimed.state = CH585_STAGING_STATE_CLAIMED;
    claimed.stage = CH585_STAGING_STAGE_PROBE;
    claimed.progress = 0u;
    if (!appendRecord(claimed)) {
        APP_STAGE_ERROR("M11", "CH585 READY could not be claimed; update not started");
        return false;
    }
    updateClaimed = true;

    currentStatus = Ch585FirmwareUpdateStatus::Programming;
    const bool programmed =
        CH585_IAP_CLIENT.programCombinedImage(CH585_FIRMWARE_STAGING_DATA_ADDR,
                                              imageSize);
    const bool appVerified = programmed && CH585_IAP_CLIENT.validateApplication();

    auto terminal = claimed;
    terminal.state = appVerified ? CH585_STAGING_STATE_APPLIED
                                 : CH585_STAGING_STATE_FAILED;
    terminal.stage = appVerified ? CH585_STAGING_STAGE_COMPLETE
                                 : CH585_IAP_CLIENT.stage();
    terminal.client_status = static_cast<uint8_t>(CH585_IAP_CLIENT.status());
    terminal.device_status = CH585_IAP_CLIENT.deviceStatus();
    terminal.error_offset = CH585_IAP_CLIENT.offset();
    terminal.progress = CH585_IAP_CLIENT.progress();
    (void)appendRecord(terminal);

    currentStatus = appVerified ? Ch585FirmwareUpdateStatus::Completed
                                : Ch585FirmwareUpdateStatus::Failed;
    APP_STAGE(appVerified ? "M12" : "M11",
              "CH585 update terminal: applied=%u client=%u device=%u offset=%lu progress=%u",
              appVerified ? 1u : 0u,
              static_cast<unsigned int>(terminal.client_status),
              static_cast<unsigned int>(terminal.device_status),
              static_cast<unsigned long>(terminal.error_offset),
              static_cast<unsigned int>(terminal.progress));
    return appVerified;
}

bool Ch585FirmwareUpdate::requestRetry()
{
    ch585_staging_record_t terminal;
    if (!loadLatestRecord(terminal) ||
        (terminal.state != CH585_STAGING_STATE_FAILED &&
         terminal.state != CH585_STAGING_STATE_CLAIMED)) {
        return false;
    }
    uint8_t actual[32];
    if (sha256_calculate_raw(
            reinterpret_cast<const uint8_t*>(CH585_FIRMWARE_STAGING_DATA_ADDR),
            terminal.image_size, actual) == 0 ||
        memcmp(actual, terminal.sha256, sizeof(actual)) != 0) {
        return false;
    }

    if (!eraseHeaderJournal()) return false;
    terminal.state = CH585_STAGING_STATE_READY;
    terminal.stage = CH585_STAGING_STAGE_STAGING;
    terminal.client_status = 0u;
    terminal.device_status = 0u;
    terminal.error_offset = 0u;
    terminal.progress = 0u;
    if (!appendRecord(terminal)) return false;
    MainRuntime_RequestReset();
    return true;
}

bool Ch585FirmwareUpdate::acknowledgeManualRecovery()
{
    ch585_staging_record_t terminal;
    if (!loadLatestRecord(terminal)) return true;
    if (terminal.state != CH585_STAGING_STATE_FAILED &&
        terminal.state != CH585_STAGING_STATE_CLAIMED) {
        return false;
    }

    /* A manual USB-ROM recovery replaces the combined CH585 image outside
     * this journal.  Erase only the dedicated 64-KiB status sector after the
     * live IAP and Application/CAPS checks have both succeeded.  The payload,
     * STM32 slots and firmware metadata remain untouched. */
    if (!eraseHeaderJournal()) return false;
    currentStatus = Ch585FirmwareUpdateStatus::Idle;
    imageSize = 0u;
    received = 0u;
    return true;
}
