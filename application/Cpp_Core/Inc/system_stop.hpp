#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// Board-level CPU stop only. Call after DMA/peripheral owners have quiesced.
// Returns false on a recoverable preparation failure; clock recovery failure
// requests an ordinary reset with a one-boot RAM inhibitor.
bool SystemStop_Enter(uint32_t intervalMs, uint32_t* wakePins);
bool SystemStop_ConsumeRecoveryFault(bool coldReset, bool softwareReset);
bool SystemStop_HandleKeyIRQ(void);
#ifdef __cplusplus
}
#endif
