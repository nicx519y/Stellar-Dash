#pragma once

#include <cstdint>

struct DeviceCommandContractRecording {
    uint32_t storageSaves = 0;
    uint32_t calibrationStarts = 0;
    uint32_t calibrationStops = 0;
    uint32_t calibrationResets = 0;
    uint32_t monitorStarts = 0;
    uint32_t monitorStops = 0;
    uint32_t ledPreviews = 0;
    uint32_t ledClears = 0;
    uint32_t screenBrightnessPreviews = 0;
    uint32_t firmwareCreates = 0;
    uint32_t firmwareChunks = 0;
    const uint8_t *firmwareChunkData = nullptr;
    uint32_t firmwareChunkSize = 0;
    uint32_t firmwareCompletes = 0;
    uint32_t firmwareAborts = 0;
    uint32_t ch585Begins = 0;
    uint32_t ch585Writes = 0;
    uint32_t ch585Completes = 0;
};

extern DeviceCommandContractRecording g_deviceCommandContractRecording;
