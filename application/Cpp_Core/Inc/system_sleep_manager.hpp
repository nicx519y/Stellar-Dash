#ifndef SYSTEM_SLEEP_MANAGER_HPP
#define SYSTEM_SLEEP_MANAGER_HPP

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void SystemSleep_CaptureBootFlags(void);
void SystemSleep_InitializeWakeKeys(void);
void SystemSleep_Tick1msFromISR(void);
void SystemSleep_Service(bool inputMode, bool resetPending);
void SystemSleep_Idle(void);
void SystemSleep_CancelForModeChange(void);
bool SystemSleep_IsBusy(void);
uint32_t SystemSleep_FilterInput(uint32_t mask);
void SystemSleep_DisableForBoot(void);
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
