#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

/* Inspect runtime-only session state while executing the real implementation. */
#define private public
#include "firmware/firmware_manager.hpp"
#undef private

#include "firmware_signature.h"
#include "qspi-w25q64.h"
#include "sha256_simple.h"

namespace {

std::vector<uint8_t> flash(EXTERNAL_FLASH_SIZE, 0xFFu);
uint32_t tickMs = 100u;
size_t readCalls = 0u;
size_t payloadWriteCalls = 0u;
size_t metadataWriteCalls = 0u;

constexpr uint32_t metadataPhysicalAddress =
    METADATA_ADDR - EXTERNAL_FLASH_BASE;

void resetFlashAccounting()
{
    std::fill(flash.begin(), flash.end(), 0xFFu);
    readCalls = 0u;
    payloadWriteCalls = 0u;
    metadataWriteCalls = 0u;
    tickMs = 100u;
}

std::string sha256Hex(const uint8_t *data, size_t length)
{
    std::array<char, 65u> encoded = {};
    assert(sha256_calculate(data, length, encoded.data()) == 1);
    return std::string(encoded.data());
}

FirmwareMetadata makeManifest(const std::vector<uint8_t> &application)
{
    FirmwareMetadata manifest = {};
    manifest.magic = FIRMWARE_MAGIC;
    manifest.metadata_version_major = METADATA_VERSION_MAJOR;
    manifest.metadata_version_minor = METADATA_VERSION_MINOR;
    manifest.metadata_size = METADATA_STRUCT_SIZE;
    std::strcpy(manifest.firmware_version, "host-reliability-test");
    manifest.target_slot = FIRMWARE_SLOT_B;
    std::strcpy(manifest.build_date, "2026-08-14 00:00:00");
    manifest.build_timestamp = 1u;
    std::strcpy(manifest.device_model, DEVICE_MODEL_STRING);
    manifest.hardware_version = HARDWARE_VERSION;
    manifest.bootloader_min_version = BOOTLOADER_VERSION;
    manifest.component_count = FIRMWARE_COMPONENT_COUNT;
    manifest.signature_algorithm = FIRMWARE_SIGNATURE_ECDSA_P256_SHA256;
    manifest.security_version = FIRMWARE_SECURITY_VERSION;

    FirmwareComponent &app = manifest.components[0];
    std::strcpy(app.name, "application");
    std::strcpy(app.file, "application.bin");
    app.address = SLOT_B_APPLICATION_ADDR;
    app.size = static_cast<uint32_t>(application.size());
    const std::string appHash = sha256Hex(application.data(), application.size());
    std::memcpy(app.sha256, appHash.c_str(), appHash.size() + 1u);
    app.active = true;

    FirmwareComponent &web = manifest.components[1];
    std::strcpy(web.name, "webresources");
    std::strcpy(web.file, "webresources.bin");
    web.address = SLOT_B_WEBRESOURCES_ADDR;
    web.size = 1u;
    web.active = true;

    FirmwareComponent &adc = manifest.components[2];
    std::strcpy(adc.name, "adc_mapping");
    std::strcpy(adc.file, "adc_mapping.bin");
    adc.address = SLOT_B_ADC_MAPPING_ADDR;
    adc.size = 1u;
    adc.active = true;
    return manifest;
}

FirmwareManager *manager()
{
    FirmwareManager *value = FirmwareManager::GetInstance();
    value->metadata_loaded = true;
    std::memset(&value->current_metadata, 0, sizeof(value->current_metadata));
    value->current_metadata.target_slot = FIRMWARE_SLOT_A;
    value->current_metadata.security_version = FIRMWARE_SECURITY_VERSION;
    return value;
}

void clearSession(FirmwareManager *value)
{
    delete value->current_session;
    value->current_session = nullptr;
    value->session_active = false;
}

void installSession(
    FirmwareManager *value,
    const std::vector<uint8_t> &application)
{
    clearSession(value);
    value->current_session = new UpgradeSession{};
    value->session_active = true;
    UpgradeSession &session = *value->current_session;
    std::strcpy(session.session_id, "ota-session");
    session.status = UPGRADE_STATUS_ACTIVE;
    session.manifest = makeManifest(application);
    session.created_at = tickMs;
    session.component_count = FIRMWARE_COMPONENT_COUNT;

    ComponentUpgradeData &app = session.components[0];
    std::strcpy(app.component_name, "application");
    app.total_chunks = 0u;
    app.received_chunks = 0u;
    app.total_size = static_cast<uint32_t>(application.size());
    app.received_size = 0u;
    app.base_address = SLOT_B_APPLICATION_ADDR;
    app.last_chunk_checksum[0] = '\0';
    app.completed = false;

    for (size_t index = 1u; index < FIRMWARE_COMPONENT_COUNT; ++index) {
        ComponentUpgradeData &component = session.components[index];
        std::strcpy(
            component.component_name,
            session.manifest.components[index].name);
        component.total_size = session.manifest.components[index].size;
        component.base_address = session.manifest.components[index].address;
        component.completed = true;
    }
}

ChunkData makeChunk(
    const std::vector<uint8_t> &bytes,
    uint32_t index,
    uint32_t totalChunks,
    uint32_t offset)
{
    ChunkData chunk = {};
    chunk.chunk_index = index;
    chunk.total_chunks = totalChunks;
    chunk.chunk_size = static_cast<uint32_t>(bytes.size());
    chunk.chunk_offset = offset;
    chunk.target_address = SLOT_B_APPLICATION_ADDR + offset;
    const std::string hash = sha256Hex(bytes.data(), bytes.size());
    std::memcpy(chunk.checksum, hash.c_str(), hash.size() + 1u);
    chunk.data = bytes.data();
    return chunk;
}

bool process(FirmwareManager *value, ChunkData &chunk)
{
    return value->ProcessFirmwareChunk(
        "ota-session", "application", &chunk);
}

void testImmediateReplayIsReadOnlyAndDoesNotAdvanceProgress()
{
    resetFlashAccounting();
    FirmwareManager *value = manager();
    const std::vector<uint8_t> image = {0x10u, 0x20u, 0x30u, 0x40u};
    installSession(value, image);
    ChunkData chunk = makeChunk(image, 0u, 1u, 0u);

    assert(process(value, chunk));
    assert(payloadWriteCalls == 1u);
    assert(value->current_session->components[0].received_chunks == 1u);
    assert(value->current_session->components[0].received_size == image.size());
    const uint32_t progress = value->current_session->total_progress;
    UpgradeStatus observedStatus = UPGRADE_STATUS_IDLE;
    uint32_t observedProgress = 0u;
    assert(value->GetUpgradeStatus(
        "ota-session", &observedStatus, &observedProgress));
    assert(observedStatus == UPGRADE_STATUS_ACTIVE);
    assert(observedProgress == progress);
    const size_t readsAfterFirstWrite = readCalls;

    assert(process(value, chunk));
    assert(payloadWriteCalls == 1u);
    assert(readCalls == readsAfterFirstWrite + 1u);
    assert(value->current_session->components[0].received_chunks == 1u);
    assert(value->current_session->components[0].received_size == image.size());
    assert(value->current_session->total_progress == progress);
    assert(value->GetUpgradeStatus(
        "ota-session", &observedStatus, &observedProgress));
    assert(observedStatus == UPGRADE_STATUS_ACTIVE);
    assert(observedProgress == progress);
}

void testAlteredReplayFailsClosed()
{
    resetFlashAccounting();
    FirmwareManager *value = manager();
    const std::vector<uint8_t> original = {1u, 2u, 3u, 4u};
    installSession(value, original);
    ChunkData first = makeChunk(original, 0u, 1u, 0u);
    assert(process(value, first));

    std::vector<uint8_t> altered = original;
    altered[2] ^= 0x5Au;
    ChunkData replay = makeChunk(altered, 0u, 1u, 0u);
    assert(!process(value, replay));
    assert(payloadWriteCalls == 1u);
    assert(value->current_session->status == UPGRADE_STATUS_FAILED);
}

void testNextChunkRequiresExactOffset()
{
    resetFlashAccounting();
    FirmwareManager *value = manager();
    const std::vector<uint8_t> image = {
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    installSession(value, image);
    std::vector<uint8_t> firstBytes(image.begin(), image.begin() + 4);
    std::vector<uint8_t> secondBytes(image.begin() + 4, image.end());
    ChunkData first = makeChunk(firstBytes, 0u, 2u, 0u);
    ChunkData discontinuous = makeChunk(secondBytes, 1u, 2u, 5u);
    assert(process(value, first));
    assert(!process(value, discontinuous));
    assert(payloadWriteCalls == 1u);
    assert(value->current_session->status == UPGRADE_STATUS_FAILED);
}

void testOnlyTheImmediatelyPreviousChunkCanReplay()
{
    resetFlashAccounting();
    FirmwareManager *value = manager();
    const std::vector<uint8_t> image = {
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u};
    installSession(value, image);
    std::vector<uint8_t> firstBytes(image.begin(), image.begin() + 4);
    std::vector<uint8_t> secondBytes(image.begin() + 4, image.begin() + 8);
    ChunkData first = makeChunk(firstBytes, 0u, 3u, 0u);
    ChunkData second = makeChunk(secondBytes, 1u, 3u, 4u);
    assert(process(value, first));
    assert(process(value, second));
    assert(!process(value, first));
    assert(payloadWriteCalls == 2u);
    assert(value->current_session->status == UPGRADE_STATUS_FAILED);
}

void testCompleteIsIdempotentAndAbortCannotEraseCommitState()
{
    resetFlashAccounting();
    FirmwareManager *value = manager();
    const std::vector<uint8_t> image = {
        0xA1u, 0xB2u, 0xC3u, 0xD4u, 0xE5u, 0xF6u};
    installSession(value, image);
    ChunkData chunk = makeChunk(image, 0u, 1u, 0u);
    assert(process(value, chunk));
    assert(value->current_session->components[0].completed);

    assert(value->CompleteUpgradeSession("ota-session"));
    assert(value->current_session->status == UPGRADE_STATUS_COMPLETED);
    assert(metadataWriteCalls == 1u);
    UpgradeStatus observedStatus = UPGRADE_STATUS_IDLE;
    uint32_t observedProgress = 0u;
    assert(value->GetUpgradeStatus(
        "ota-session", &observedStatus, &observedProgress));
    assert(observedStatus == UPGRADE_STATUS_COMPLETED);
    assert(observedProgress == 100u);
    const size_t readsAfterCommit = readCalls;

    assert(value->CompleteUpgradeSession("ota-session"));
    assert(metadataWriteCalls == 1u);
    assert(readCalls == readsAfterCommit);
    assert(value->current_session->status == UPGRADE_STATUS_COMPLETED);

    assert(!value->AbortUpgradeSession("ota-session"));
    assert(value->current_session != nullptr);
    assert(value->current_session->status == UPGRADE_STATUS_COMPLETED);
    assert(metadataWriteCalls == 1u);
    assert(value->GetUpgradeStatus(
        "ota-session", &observedStatus, &observedProgress));
    assert(observedStatus == UPGRADE_STATUS_COMPLETED);
    assert(observedProgress == 100u);
}

} // namespace

extern "C" uint32_t HAL_GetTick(void)
{
    return tickMs;
}

extern "C" void HAL_Delay(uint32_t delay)
{
    tickMs += delay;
}

extern "C" void NVIC_SystemReset(void)
{
}

extern "C" int8_t QSPI_W25Qxx_WriteBuffer_WithXIPOrNot(
    uint8_t *data,
    uint32_t address,
    uint32_t length)
{
    if (data == nullptr || address > flash.size() ||
        length > flash.size() - address) {
        return -1;
    }
    std::memcpy(flash.data() + address, data, length);
    if (address == metadataPhysicalAddress &&
        length == sizeof(FirmwareMetadata)) {
        ++metadataWriteCalls;
    } else {
        ++payloadWriteCalls;
    }
    return QSPI_W25Qxx_OK;
}

extern "C" int8_t QSPI_W25Qxx_ReadBuffer_WithXIPOrNot(
    uint8_t *data,
    uint32_t address,
    uint32_t length)
{
    if (data == nullptr || address > flash.size() ||
        length > flash.size() - address) {
        return -1;
    }
    std::memcpy(data, flash.data() + address, length);
    ++readCalls;
    return QSPI_W25Qxx_OK;
}

extern "C" int8_t QSPI_W25Qxx_BufferErase(
    uint32_t address,
    uint32_t length)
{
    if (address > flash.size() || length > flash.size() - address) {
        return -1;
    }
    std::fill(flash.begin() + address, flash.begin() + address + length, 0xFFu);
    return QSPI_W25Qxx_OK;
}

extern "C" bool firmware_metadata_calculate_hash(
    const FirmwareMetadata *,
    uint8_t hash[32])
{
    if (hash != nullptr) {
        std::memset(hash, 0x5Au, 32u);
    }
    return hash != nullptr;
}

extern "C" bool firmware_release_key_is_provisioned(void)
{
    return true;
}

extern "C" FirmwareValidationResult firmware_metadata_verify_signature(
    const FirmwareMetadata *)
{
    return FIRMWARE_VALID;
}

int main()
{
    /* Constructor metadata probing is deliberately served by blank fake Flash. */
    testImmediateReplayIsReadOnlyAndDoesNotAdvanceProgress();
    testAlteredReplayFailsClosed();
    testNextChunkRequiresExactOffset();
    testOnlyTheImmediatelyPreviousChunkCanReplay();
    testCompleteIsIdempotentAndAbortCannotEraseCommitState();
    FirmwareManager::DestroyInstance();
    std::puts("firmware manager reliability tests passed");
    return 0;
}
