#pragma once

#include <cstdint>

#include "firmware_metadata.h"

struct ChunkData {
    uint32_t chunk_index = 0u;
    uint32_t total_chunks = 0u;
    uint32_t chunk_size = 0u;
    uint32_t chunk_offset = 0u;
    uint32_t target_address = 0u;
    char checksum[65] = {};
    const uint8_t *data = nullptr;
};

enum UpgradeStatus {
    UPGRADE_STATUS_IDLE = 0,
    UPGRADE_STATUS_ACTIVE,
    UPGRADE_STATUS_COMPLETED,
    UPGRADE_STATUS_ABORTED,
    UPGRADE_STATUS_FAILED,
};

class FirmwareManager {
public:
    static FirmwareManager *GetInstance();
    const FirmwareMetadata *GetCurrentMetadata();
    bool CreateUpgradeSession(const char *sessionId,
                              const FirmwareMetadata *manifest);
    bool ProcessFirmwareChunk(const char *sessionId,
                              const char *componentName,
                              const ChunkData *chunk);
    bool CompleteUpgradeSession(const char *sessionId);
    bool AbortUpgradeSession(const char *sessionId);
    uint32_t GetUpgradeProgress(const char *sessionId);
    bool GetUpgradeStatus(const char *sessionId,
                          UpgradeStatus *status,
                          uint32_t *progress) const;
    void resetForContractTest() { active_ = false; }

private:
    FirmwareMetadata metadata_ = {};
    bool active_ = false;
};
