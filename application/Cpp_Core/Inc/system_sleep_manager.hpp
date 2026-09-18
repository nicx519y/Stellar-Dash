#ifndef SYSTEM_SLEEP_MANAGER_HPP
#define SYSTEM_SLEEP_MANAGER_HPP

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void SystemSleep_CaptureBootFlags(void);
void SystemSleep_ConfirmWakeHoldOrReturnStandby(void);
void SystemSleep_HandleWakeRecovery(void);
void SystemSleep_RequestStandby(void);
void SystemSleep_UpdateRotaryHold(uint32_t nowMs);
void SystemSleep_NotifyButtonActivity(uint32_t nowMs, uint32_t inputMask);
void SystemSleep_NotifyScreenActivity(uint32_t nowMs);
void SystemSleep_UpdateAutoStandby(uint32_t nowMs);
bool SystemSleep_ShouldSuppressRotaryLongAction(void);

#ifdef __cplusplus
}
#endif

#endif
