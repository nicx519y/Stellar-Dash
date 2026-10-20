#pragma once

#include <cstdint>

// Calibration-only redundancy: WS2812B has no receiver acknowledgement.
// Submit the latest full frame three times, then leave the strip idle.
class CalibrationLedRefresh {
public:
    void request(uint32_t now) {
        remaining = 3u;
        lastSubmissionMs = now - intervalMs;
    }

    void cancel() { remaining = 0u; }

    template <typename SubmitLatestFrame>
    void service(uint32_t now, SubmitLatestFrame submit) {
        if (remaining == 0u ||
            static_cast<uint32_t>(now - lastSubmissionMs) < intervalMs) {
            return;
        }
        // Busy/error is not a submission. Keep the budget for a later pass;
        // the caller owns lifecycle checks and must never restart a faulted DMA.
        if (submit()) {
            --remaining;
            lastSubmissionMs = now;
        }
    }

private:
    static constexpr uint32_t intervalMs = 30u;
    uint32_t lastSubmissionMs = 0u;
    uint8_t remaining = 0u;
};
