#include "sleep_diagnostics.hpp"
#include "board_cfg.h"
#include "stm32h7xx_hal.h"
#include "system_logger.h"
volatile SleepDiagnostics g_sleepDiagnostics = {};
void SleepDiagnostics_Record(SleepStage stage, uint32_t error)
{
    g_sleepDiagnostics.stage = static_cast<uint32_t>(stage);
    g_sleepDiagnostics.changedAt = HAL_GetTick();
    if (error) g_sleepDiagnostics.lastError = error;
    // STOP counters are deliberately not printed at the 10-ms wake cadence.
    if (stage != SleepStage::StopEnter && stage != SleepStage::StopReturn)
        APP_STAGE("S97", "sleep stage=%lu error=%lu", (unsigned long)stage, (unsigned long)error);
}
