#ifndef MAIN_RUNTIME_CONTROL_HPP
#define MAIN_RUNTIME_CONTROL_HPP

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void MainRuntime_RequestReset(void);
bool MainRuntime_RequestTxIsp(bool enabled);
bool MainRuntime_IsTxIspActive(void);
bool MainRuntime_TxIspTransitionFailed(void);
void MainRuntime_RequestBatteryRecovery(void);
bool MainRuntime_IsInputActive(void);

#ifdef __cplusplus
}
#endif

#endif
